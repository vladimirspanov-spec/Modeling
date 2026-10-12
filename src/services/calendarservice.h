#pragma once

#include "src/domain/calendarevent.h"

class CalendarService
{
public:
    // Invalid catalog/time configuration is a programming error (invalid_argument).
    CalendarService(CompanyCatalog catalog, QDateTime start, QDateTime horizonEnd);

    CalendarResult createEvent(const EventData &data, const UserContext &actor);
    CalendarResult updateEvent(EventId id, const EventData &data, const UserContext &actor);
    CalendarResult cancelEvent(EventId id, const UserContext &actor);
    std::optional<CalendarEvent> event(EventId id) const;
    CalendarQueryResult calendar(const CalendarFilter &filter = {}) const;
    QVector<CalendarRecord> history(std::optional<EventId> id = std::nullopt) const;

    // Processes every start/end inside the interval; never uses the wall clock.
    CalendarResult advanceTo(const QDateTime &time);
    QDateTime modelTime() const { return time_; }
    QDateTime horizonEnd() const { return horizonEnd_; }
    const CompanyCatalog &catalog() const { return catalog_; }

private:
    CalendarResult validate(const EventData &data, EventId ignoredId = 0) const;
    bool validActor(const UserContext &actor) const;
    bool canModify(const EventData &data, const UserContext &actor) const;
    bool matchesDepartment(const EventData &data, int departmentId) const;
    void record(const CalendarEvent &event, CalendarAction action, const QDateTime &at, int actorId);
    void startIfDue(EventId id);

    CompanyCatalog catalog_;
    QDateTime time_;
    QDateTime horizonEnd_;
    EventId nextId_ = 1;
    QMap<EventId, CalendarEvent> active_;
    QMap<EventId, CalendarEvent> archive_;
    QVector<CalendarRecord> history_;
};
