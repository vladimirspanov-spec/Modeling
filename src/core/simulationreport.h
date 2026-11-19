#pragma once
#include "src/core/simulationengine.h"

class SimulationReport {
public:
    // Compact JSON for users; create() remains the internal replay snapshot.
    static QJsonObject userReport(const SimulationEngine &engine);
    static QJsonObject create(const SimulationEngine &engine);
    static QJsonObject metrics(const SimulationEngine &engine);
    static QString summary(const QJsonObject &metrics);
};
