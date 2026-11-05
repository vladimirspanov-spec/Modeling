#include "simulationstore.h"
#include "jsoncodec.h"
#include "src/reporting/simulationreport.h"
#include <QJsonDocument>
#include <QCryptographicHash>
#include <stdexcept>

namespace {
QString fingerprint(const SimulationEngine &engine, int schemaVersion = 3) {
    auto report = SimulationReport::create(engine);
    if (schemaVersion < 3) {
        auto configuration = report["configuration"].toObject();
        for (const auto *key : {"periodicSeminars", "seminarPeriodDays", "seminarLeadDays", "seminarTime", "seminarDurationMinutes"})
            configuration.remove(key);
        report["configuration"] = configuration;
        report.remove("departments");
    }
    if (schemaVersion == 1) {
        // Validate old files against their original mail representation before upgrading it.
        QJsonArray emails;
        for (const auto &value : report["emails"].toArray()) {
            auto email = value.toObject();
            email.remove("body");
            email["recipient"] = QStringLiteral("employee%1@example.invalid").arg(email["recipientId"].toInt());
            emails.append(email);
        }
        report["emails"] = emails;
    }
    const auto data = QJsonDocument(report).toJson(QJsonDocument::Compact) + engine.randomState().toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}
void check(bool valid, const char *message) { if (!valid) throw std::runtime_error(message); }
}
QJsonObject SimulationStore::snapshot(const SimulationEngine &engine)
{
    return {{"format", "SecretarySimulation"}, {"schemaVersion", 3}, {"generator", engine.modelVersion()},
        {"configuration", JsonCodec::setup(engine.setup())}, {"commands", engine.commands()},
        {"calendar", SimulationReport::create(engine)["events"]}, {"fingerprint", fingerprint(engine)}};
}
std::unique_ptr<SimulationEngine> SimulationStore::restore(const QJsonObject &document)
{
    const int schemaVersion = document["schemaVersion"].toInt();
    check(document["format"] == "SecretarySimulation" && schemaVersion >= 1 && schemaVersion <= 3, "Unknown state format/version");
    const bool legacy = document["generator"] == "live-minute-v4";
    check(legacy || (schemaVersion == 3 && document["generator"] == SimulationEngine::generatorVersion()), "Incompatible generator version");
    check(document["commands"].isArray(), "Missing command journal");
    auto engine = std::make_unique<SimulationEngine>(JsonCodec::setup(document["configuration"].toObject()), legacy);
    const auto commands = document["commands"].toArray();
    check(commands.size() <= 1000000, "Command journal too large");
    for (const auto &command : commands) { check(command.isObject(), "Invalid command"); engine->replayCommand(command.toObject()); }
    check(engine->commands() == commands, "Command replay mismatch");
    check(fingerprint(*engine, schemaVersion) == document["fingerprint"].toString(), "State replay differs: modified file or incompatible C++ random library");
    check(SimulationReport::create(*engine)["events"] == document["calendar"], "Calendar snapshot mismatch");
    return engine; // Caller replaces the current engine only after all validation succeeds.
}
void SimulationStore::save(const QString &path, const SimulationEngine &engine) { JsonCodec::write(path, snapshot(engine)); }
std::unique_ptr<SimulationEngine> SimulationStore::load(const QString &path) { return restore(JsonCodec::read(path)); }
void SimulationStore::saveConfiguration(const QString &path, const CalendarSetup &setup) {
    SimulationEngine validation(setup);
    JsonCodec::write(path, {{"format", "SecretaryConfiguration"}, {"schemaVersion", 1}, {"configuration", JsonCodec::setup(setup)}});
}
CalendarSetup SimulationStore::loadConfiguration(const QString &path) {
    const auto doc = JsonCodec::read(path);
    check(doc["format"] == "SecretaryConfiguration" && doc["schemaVersion"].toInt() == 1, "Unknown configuration format");
    auto setup = JsonCodec::setup(doc["configuration"].toObject()); SimulationEngine validation(setup); return setup;
}
