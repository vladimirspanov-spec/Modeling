#pragma once
#include "src/core/simulationengine.h"
#include <memory>

struct ExperimentCase { QString name; CalendarSetup setup; };
class ExperimentRunner {
public:
    ExperimentRunner(QVector<ExperimentCase> cases, int repetitions);
    static QVector<ExperimentCase> comparison(const CalendarSetup &base);
    bool advance(); // One model step; caller controls UI responsiveness/cancellation.
    bool finished() const { return caseIndex_ >= cases_.size(); }
    int completedRuns() const { return runs_.size(); }
    int totalRuns() const { return cases_.size() * repetitions_; }
    QJsonObject report() const;
    QByteArray csv() const;
private:
    QVector<ExperimentCase> cases_;
    int repetitions_;
    int caseIndex_ = 0;
    int repetition_ = 0;
    std::unique_ptr<SimulationEngine> current_;
    QJsonArray runs_;
};
