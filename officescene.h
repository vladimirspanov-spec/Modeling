#pragma once
#include "src/services/calendarservice.h"
#include <QObject>
class QGraphicsScene;

// Scene item metadata: role 0 = employee/room ID, role 1 = location ID,
// role 2 = "employee", "room", "workZone" or "meetingZone".
// Employee role 3 = "work", "meeting" or "other"; positive locations are room IDs.
// Employee roles 4 and 5 hold the aisle point and corridor connection (scene centers).
namespace OfficeLocation {
constexpr int Outside = -1;
constexpr int Sick = -2;
constexpr int Corridor = -3;
}

// Presentation only: does not advance time or change the calendar.
// The scene owns all created items and their text children.
void populateOfficeScene(QGraphicsScene &scene, const CalendarService &calendar);

class OfficeAnimator : public QObject
{
public:
    explicit OfficeAnimator(QGraphicsScene &scene);
    void update(const CalendarService &calendar, int durationMs);
    void finish();
    void pause();
    void resume();
    void retime(int remainingMs);
private:
    QGraphicsScene &scene_;
};
