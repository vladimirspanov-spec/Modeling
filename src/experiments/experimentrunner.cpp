#include "experimentrunner.h"
#include "src/reporting/simulationreport.h"
#include "src/storage/jsoncodec.h"
#include <cmath>
#include <limits>
#include <stdexcept>

ExperimentRunner::ExperimentRunner(QVector<ExperimentCase> cases, int repetitions)
    : cases_(std::move(cases)), repetitions_(repetitions)
{
    if (cases_.isEmpty() || repetitions < 1 || repetitions > 1000) throw std::invalid_argument("Invalid experiment size");
    for (const auto &c : cases_) {
        SimulationEngine validation(c.setup);
        if (quint64(c.setup.seed) + repetitions - 1 > std::numeric_limits<quint32>::max())
            throw std::invalid_argument("Experiment seeds overflow uint32");
    }
}
QVector<ExperimentCase> ExperimentRunner::comparison(const CalendarSetup &base)
{
    QVector<ExperimentCase> cases;
    const QStringList policies{"Reject", "Suggest (first)", "AutoReschedule"};
    for (double factor : {1.0, 2.0}) for (int policy = 0; policy < 3; ++policy) {
        auto setup = base;
        setup.requestRatePerWorkday = std::min(100000.0, base.requestRatePerWorkday * factor);
        setup.conflictPolicy = ConflictPolicy(policy);
        cases.append({QStringLiteral("%1; rate=%2").arg(policies[policy]).arg(setup.requestRatePerWorkday), setup});
    }
    return cases;
}
bool ExperimentRunner::advance()
{
    if (finished()) return false;
    if (!current_) { auto setup = cases_[caseIndex_].setup; setup.seed += repetition_; current_ = std::make_unique<SimulationEngine>(setup); }
    current_->step();
    if (!current_->finished()) {
        // Explicit, reproducible decision rule for batch Suggest; interactive mode still asks a user.
        QVector<quint64> pending;
        for (const auto &request : current_->requests()) if (request.pending) pending.append(request.id);
        for (auto id : pending) current_->acceptSuggestion(id, 0);
    }
    if (current_->finished()) {
        runs_.append(QJsonObject{{"case", cases_[caseIndex_].name}, {"seed", double(current_->setup().seed)},
            {"configuration", JsonCodec::setup(current_->setup())}, {"metrics", SimulationReport::metrics(*current_)}});
        current_.reset();
        if (++repetition_ == repetitions_) { repetition_ = 0; ++caseIndex_; }
    }
    return !finished();
}
QJsonObject ExperimentRunner::report() const
{
    QJsonArray aggregates;
    for (const auto &c : cases_) {
        QVector<QJsonObject> measurements;
        for (const auto &run : runs_) if (run.toObject()["case"] == c.name) measurements.append(run.toObject()["metrics"].toObject());
        QJsonObject means, deviations;
        if (!measurements.isEmpty()) {
            for (const auto &key : measurements.first().keys()) {
                if (!measurements.first()[key].isDouble()) continue;
                double sum = 0; for (const auto &m : measurements) sum += m[key].toDouble();
                const double mean = sum / measurements.size();
                double squares = 0; for (const auto &m : measurements) squares += std::pow(m[key].toDouble() - mean, 2);
                means[key] = mean; deviations[key] = measurements.size() > 1 ? std::sqrt(squares / (measurements.size() - 1)) : 0;
            }
        }
        aggregates.append(QJsonObject{{"case", c.name}, {"count", measurements.size()}, {"mean", means}, {"sampleStdDev", deviations}});
    }
    return {{"schemaVersion", 1}, {"generator", SimulationEngine::generatorVersion()}, {"complete", finished()},
        {"suggestDecision", "first alternative after each model step"}, {"requestedRuns", totalRuns()}, {"runs", runs_}, {"aggregates", aggregates}};
}
QByteArray ExperimentRunner::csv() const
{
    QString text = "case,seed,created,completed,cancelled,requests,conflicts,rejected,pending,autoRescheduled,emailsDelivered,roomUtilization\n";
    for (const auto &entry : runs_) {
        const auto run = entry.toObject(), metrics = run["metrics"].toObject();
        QString name = run["case"].toString(); name.replace('"', "\"\"");
        text += '"' + name + "\"," + QString::number(run["seed"].toDouble(), 'f', 0);
        for (const auto &key : {"created", "completed", "cancelled", "requests", "conflicts", "rejected", "pending", "autoRescheduled", "emailsDelivered", "roomUtilization"})
            text += ',' + QString::number(metrics[key].toDouble(), 'g', 15);
        text += '\n';
    }
    return text.toUtf8();
}
