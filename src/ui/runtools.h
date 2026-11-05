#pragma once
#include "src/simulation/simulationengine.h"
#include <QWidget>
#include <QStringList>
#include <functional>
class QTableWidget;
class QLabel;
class QPushButton;

// Run views, explicit conflict decisions and export dialogs.
class RunTools : public QWidget {
public:
    RunTools(SimulationEngine &engine, std::function<void()> pause, std::function<void()> changed,
             std::function<UserContext()> actor, std::function<void(EventId)> openEvent,
             std::function<void(const RequestRecord &)> editRequest, QWidget *parent = nullptr);
    void refresh();
private:
    void openEmail(int row);
    struct EmailGroup { int firstMessage; QStringList recipients; };
    QVector<EmailGroup> emailGroups_;
    SimulationEngine &engine_;
    std::function<void()> pause_, changed_;
    std::function<UserContext()> actor_;
    QTableWidget *requests_, *emails_, *events_;
    QLabel *summary_;
    QPushButton *openEmail_;
};
