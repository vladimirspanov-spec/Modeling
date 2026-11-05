#pragma once
#include "src/core/simulationengine.h"
#include <memory>

class SimulationStore {
public:
    static QJsonObject snapshot(const SimulationEngine &engine);
    static std::unique_ptr<SimulationEngine> restore(const QJsonObject &document);
    static void save(const QString &path, const SimulationEngine &engine);
    static std::unique_ptr<SimulationEngine> load(const QString &path);
    static void saveConfiguration(const QString &path, const CalendarSetup &setup);
    static CalendarSetup loadConfiguration(const QString &path);
};
