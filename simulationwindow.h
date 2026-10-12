#ifndef SIMULATIONWINDOW_H
#define SIMULATIONWINDOW_H

#include "src/simulation/simulationengine.h"
#include <QWidget>

class QLabel;
class QScrollArea;
class QComboBox;
class QSpinBox;
class QDateEdit;
class QPushButton;
class QTimer;
class QTableWidget;
class QTabWidget;
class QCheckBox;

class SimulationWindow : public QWidget
{
public:
    explicit SimulationWindow(const CalendarSetup &setup, QWidget *parent = nullptr);
    const CalendarService &calendarService() const { return engine_.calendar(); }
    const SimulationEngine &simulationEngine() const { return engine_; }

private:
    void refreshCalendar();
    void editEvent(std::optional<EventId> id = std::nullopt);
    void step();
    void start();
    void pause();
    void reset();
    void updateSpeed();
    void refreshJournalAndState();
    void transferParticipant();
    CalendarSetup setup_;
    SimulationEngine engine_;
    UserContext actor_;
    // QObject children are owned exclusively by the QWidget parent hierarchy.
    QLabel *summary_;
    QScrollArea *scroll_;
    QComboBox *scope_;
    QComboBox *department_;
    QComboBox *employee_;
    QComboBox *status_;
    QDateEdit *from_;
    QDateEdit *to_;
    QPushButton *stepButton_;
    QPushButton *addButton_;
    QPushButton *startButton_;
    QPushButton *pauseButton_;
    QComboBox *speed_;
    QTimer *timer_;
    QTableWidget *journal_;
    QTableWidget *participants_;
    QTableWidget *organizations_;
    QTabWidget *tabs_;
    QCheckBox *showPlanned_;
    QLabel *actorLabel_;
    int displayedLogCount_ = 0;
};

#endif
