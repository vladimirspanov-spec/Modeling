#pragma once
#include "calendarsetup.h"

class ConflictResolver {
public:
    static QVector<EventData> alternatives(const CalendarService &calendar, const CalendarSetup &setup,
        const EventData &requested, const UserContext &actor, EventId id = 0, bool separatePlanningDays = false);
};
