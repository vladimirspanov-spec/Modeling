#include "conflictresolver.h"
#include <algorithm>

QVector<EventData> ConflictResolver::alternatives(const CalendarService &calendar, const CalendarSetup &setup,
    const EventData &requested, const UserContext &actor, EventId id, bool separatePlanningDays)
{
    QVector<EventData> result;
    const qint64 duration = requested.start.secsTo(requested.end);
    if (duration <= 0 || duration > setup.workStart.secsTo(setup.workEnd)) return result;
    auto last = requested.start.toOffsetFromUtc(10800).date();
    for (int days = 0; days < setup.rescheduleWindowWorkdays; ) {
        last = last.addDays(1);
        if (setup.workingDays.value(last.dayOfWeek() - 1)) ++days;
    }
    QVector<std::optional<int>> rooms{requested.roomId};
    if (requested.roomId)
        for (int room : calendar.catalog().rooms.keys()) if (room != *requested.roomId) rooms.append(room);
    for (auto day = requested.start.toOffsetFromUtc(10800).date(); day <= last; day = day.addDays(1)) {
        if (!setup.workingDays.value(day.dayOfWeek() - 1)) continue;
        for (auto start = modelDateTime(day, setup.workStart); start.addSecs(duration) <= modelDateTime(day, setup.workEnd); start = start.addSecs(1800)) {
            if (start < requested.start || start < calendar.modelTime()) continue;
            for (auto room : rooms) {
                auto candidate = requested;
                candidate.start = start; candidate.end = start.addSecs(duration); candidate.roomId = room;
                bool occupiedDay = false;
                if (separatePlanningDays && candidate.type == EventType::Planning && candidate.recurrenceTemplateId)
                    for (const auto &other : calendar.calendar().events)
                        if (other.id != id && other.data.type == EventType::Planning && other.data.recurrenceTemplateId &&
                            other.data.start.date() == candidate.start.date()) occupiedDay = true;
                if (!occupiedDay && calendar.checkEvent(candidate, actor, id).ok()) {
                    result.append(candidate);
                    if (result.size() >= setup.maxSuggestions) return result;
                }
            }
        }
    }
    return result;
}
