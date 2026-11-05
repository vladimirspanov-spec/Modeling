#include "simulationstore.h"
#include "jsoncodec.h"
#include "src/core/simulationreport.h"
#include <QJsonDocument>
#include <QCryptographicHash>
#include <stdexcept>

namespace {
QString fingerprint(const SimulationEngine &engine) {
    auto report = SimulationReport::create(engine);
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
    check(document["format"] == "SecretarySimulation" && schemaVersion == 3, "Unknown state format/version");
    check(document["generator"] == SimulationEngine::generatorVersion(),
        "Сохранение создано другой версией модели. Начните новый прогон; параметры можно загрузить отдельно.");
    check(document["commands"].isArray(), "Missing command journal");
    auto engine = std::make_unique<SimulationEngine>(JsonCodec::setup(document["configuration"].toObject()));
    const auto commands = document["commands"].toArray();
    check(commands.size() <= 1000000, "Command journal too large");
    for (const auto &command : commands) { check(command.isObject(), "Invalid command"); engine->replayCommand(command.toObject()); }
    check(engine->commands() == commands, "Command replay mismatch");
    check(fingerprint(*engine) == document["fingerprint"].toString(), "State replay differs: modified file or incompatible C++ random library");
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
