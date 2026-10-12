#ifndef SIMULATIONWINDOW_H
#define SIMULATIONWINDOW_H

#include "src/services/calendarsetup.h"
#include <QWidget>

class QLabel;
class QScrollArea;
class QComboBox;
class QSpinBox;
class QDateEdit;
class QPushButton;

class SimulationWindow : public QWidget
{
public:
    explicit SimulationWindow(const CalendarSetup &setup, QWidget *parent = nullptr);
    const CalendarService &calendarService() const { return service_; }

private:
    void refreshCalendar();
    void editEvent(std::optional<EventId> id = std::nullopt);
    void step();
    CalendarSetup setup_;
    CalendarService service_;
    UserContext actor_;
    // QObject children are owned exclusively by the QWidget parent hierarchy.
    QLabel *summary_;
    QScrollArea *scroll_;
    QComboBox *scope_;
    QComboBox *department_;
    QSpinBox *employee_;
    QComboBox *status_;
    QDateEdit *from_;
    QDateEdit *to_;
    QPushButton *stepButton_;
    QPushButton *addButton_;
};

#endif
