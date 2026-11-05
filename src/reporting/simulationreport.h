#pragma once
#include "src/simulation/simulationengine.h"

class SimulationReport {
public:
    static QJsonObject create(const SimulationEngine &engine);
    static QJsonObject metrics(const SimulationEngine &engine);
    static QByteArray csv(const QJsonObject &report);
    static QString summary(const QJsonObject &metrics);
};
