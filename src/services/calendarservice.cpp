#include "calendarservice.h"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <QRegularExpression>

namespace {
CalendarResult failure(CalendarError error, const QString &message, EventId id = 0) {
    return {error, message, id, {}};
}
bool validStatus(EventStatus status) {
    return status >= EventStatus::Planned && status <= EventStatus::Cancelled;
}
}

CalendarService::CalendarService(CompanyCatalog catalog, QDateTime start, QDateTime horizonEnd)
    : catalog_(std::move(catalog)), time_(start), horizonEnd_(horizonEnd)
{
    if (!start.isValid() || !horizonEnd.isValid() || start >= horizonEnd)
        throw std::invalid_argument("Invalid calendar horizon");
    for (auto it = catalog_.departments.cbegin(); it != catalog_.departments.cend(); ++it)
        if (it.key() <= 0 || it->id != it.key()) throw std::invalid_argument("Invalid department");
    for (auto it = catalog_.employees.cbegin(); it != catalog_.employees.cend(); ++it)
        if (it.key() <= 0 || it->id != it.key() || !catalog_.departments.contains(it->departmentId))
            throw std::invalid_argument("Invalid employee");
    for (auto it = catalog_.rooms.cbegin(); it != catalog_.rooms.cend(); ++it)
        if (it.key() <= 0 || it->id != it.key() || (it->departmentId && !catalog_.departments.contains(*it->departmentId)))
            throw std::invalid_argument("Invalid room");
    QSet<QString> codes;
    const QRegularExpression format(QStringLiteral("^([1-9][0-9]*)_([0-9]{2,})$"));
    for (auto &employee : catalog_.employees) {
        int &next = nextParticipantNumber_[employee.departmentId];
        if (employee.publicId.isEmpty())
            employee.publicId = QStringLiteral("%1_%2").arg(employee.departmentId).arg(++next, 2, 10, QChar('0'));
        const auto match = format.match(employee.publicId);
        bool ok = false;
        int number = match.captured(2).toInt(&ok);
        if (!match.hasMatch() || !ok || number < 1 || match.captured(1).toInt() != employee.departmentId || codes.contains(employee.publicId))
            throw std::invalid_argument("Invalid or duplicate participant ID");
        next = std::max(next, number);
        codes.insert(employee.publicId);
    }
}

bool CalendarService::validActor(const UserContext &actor) const
{
    if (!catalog_.employees.contains(actor.employeeId) || actor.role < UserRole::Employee || actor.role > UserRole::Secretary)
        return false;
    for (int id : actor.permittedDepartmentIds)
        if (!catalog_.departments.contains(id)) return false;
    return true;
}

bool CalendarService::canModify(const EventData &data, const UserContext &actor) const
{
    if (!validActor(actor)) return false;
    if (actor.role == UserRole::Secretary) return true;
    if (actor.role != UserRole::Manager) return false;
    QSet<int> departments{catalog_.employees.value(data.organizerId).departmentId};
    for (int id : data.participantIds) departments.insert(catalog_.employees.value(id).departmentId);
    if (data.roomId) {
        const auto room = catalog_.rooms.value(*data.roomId);
        if (room.departmentId) departments.insert(*room.departmentId);
    }
    for (int id : departments)
        if (!actor.permittedDepartmentIds.contains(id)) return false;
    return true;
}

CalendarResult CalendarService::validate(const EventData &data, EventId ignoredId) const
{
    if (data.title.trimmed().isEmpty() || !data.start.isValid() || !data.end.isValid() || data.end <= data.start)
        return failure(CalendarError::InvalidData, QStringLiteral("Укажите название и корректный интервал начала и конца."));
    if (data.start < time_ || time_ >= horizonEnd_)
        return failure(CalendarError::InvalidTime, QStringLiteral("Нельзя создавать или переносить событие в прошлое либо после окончания прогона."));
    if (data.type < EventType::Planning || data.type > EventType::Other || data.importance < Importance::Low || data.importance > Importance::High)
        return failure(CalendarError::InvalidData, QStringLiteral("Некорректный тип или важность события."));
    if (!catalog_.employees.contains(data.organizerId) || data.participantIds.isEmpty())
        return failure(CalendarError::InvalidData, QStringLiteral("Выберите существующего организатора и хотя бы одного участника."));
    QSet<int> participants;
    for (int id : data.participantIds) {
        if (!catalog_.employees.contains(id) || participants.contains(id))
            return failure(CalendarError::InvalidData, QStringLiteral("Участники должны существовать и не повторяться."));
        participants.insert(id);
        if (!catalog_.employees.value(id).available)
            return failure(CalendarError::InvalidState, QStringLiteral("Участник %1 временно недоступен.").arg(participantCode(id)));
    }
    if (data.roomId && !catalog_.rooms.contains(*data.roomId))
        return failure(CalendarError::InvalidData, QStringLiteral("Помещение не найдено."));
    if (data.roomId && !roomAvailable(*data.roomId))
        return failure(CalendarError::InvalidState, QStringLiteral("Помещение временно закрыто на ремонт."));
    if (data.type == EventType::BusinessTrip && data.roomId)
        return failure(CalendarError::InvalidData, QStringLiteral("Для командировки помещение не указывается."));
    CalendarResult result;
    for (const auto &existing : active_) {
        if (existing.id == ignoredId || data.start >= existing.data.end || existing.data.start >= data.end) continue;
        bool collision = data.roomId && existing.data.roomId == data.roomId;
        for (int id : existing.data.participantIds) collision |= participants.contains(id);
        if (collision) result.conflicts.append(existing.id);
    }
    if (!result.conflicts.isEmpty()) {
        result.error = CalendarError::Conflict;
        result.message = QStringLiteral("Участники или помещение заняты другим событием.");
    }
    return result;
}

void CalendarService::record(const CalendarEvent &event, CalendarAction action, const QDateTime &at, int actorId)
{
    history_.append({quint64(history_.size()) + 1, at, action, actorId, event, participantCode(actorId)});
}

void CalendarService::startIfDue(EventId id)
{
    auto &event = active_[id];
    if (!deferStarts_ && event.status == EventStatus::Planned && event.data.start <= time_ && time_ < horizonEnd_) {
        event.status = EventStatus::InProgress;
        record(event, CalendarAction::Started, time_, 0);
    }
}

CalendarResult CalendarService::createEvent(const EventData &data, const UserContext &actor)
{
    if (!validActor(actor) || (actor.role != UserRole::Secretary && data.organizerId != actor.employeeId))
        return failure(CalendarError::PermissionDenied, QStringLiteral("Недостаточно прав для создания события от имени организатора."));
    auto result = validate(data);
    if (!result.ok()) return result;
    CalendarEvent event;
    event.id = nextId_++;
    event.data = data;
    captureParticipants(event);
    active_.insert(event.id, event);
    record(event, CalendarAction::Created, time_, actor.employeeId);
    startIfDue(event.id);
    result.eventId = event.id;
    return result;
}

CalendarResult CalendarService::updateEvent(EventId id, const EventData &data, const UserContext &actor)
{
    const auto existing = event(id);
    if (!existing) return failure(CalendarError::NotFound, QStringLiteral("Событие не найдено."), id);
    if (!canModify(existing->data, actor)) return failure(CalendarError::PermissionDenied, QStringLiteral("Недостаточно прав для изменения события."), id);
    if (existing->status != EventStatus::Planned)
        return failure(CalendarError::InvalidState, QStringLiteral("Изменять можно только запланированные события."), id);
    auto result = validate(data, id);
    if (!result.ok()) return result;
    if (!canModify(data, actor)) return failure(CalendarError::PermissionDenied, QStringLiteral("Нет полномочий на все затронутые отделы."), id);
    auto changed = *existing;
    ++changed.version;
    QSet<int> before, after;
    for (int participant : changed.data.participantIds) before.insert(participant);
    for (int participant : data.participantIds) after.insert(participant);
    if (changed.data.start != data.start || changed.data.end != data.end || before != after || changed.data.importance != data.importance)
        ++changed.scheduleVersion;
    changed.data = data;
    captureParticipants(changed);
    active_[id] = changed;
    record(changed, CalendarAction::Updated, time_, actor.employeeId);
    startIfDue(id);
    result.eventId = id;
    return result;
}

CalendarResult CalendarService::cancelEvent(EventId id, const UserContext &actor)
{
    const auto existing = event(id);
    if (!existing) return failure(CalendarError::NotFound, QStringLiteral("Событие не найдено."), id);
    if (!canModify(existing->data, actor)) return failure(CalendarError::PermissionDenied, QStringLiteral("Недостаточно прав для отмены события."), id);
    if (existing->status != EventStatus::Planned || time_ >= horizonEnd_)
        return failure(CalendarError::InvalidState, QStringLiteral("Отменять можно только запланированные события до конца прогона."), id);
    auto cancelled = *existing;
    cancelled.status = EventStatus::Cancelled;
    ++cancelled.version;
    active_.remove(id);
    archive_.insert(id, cancelled);
    record(cancelled, CalendarAction::Cancelled, time_, actor.employeeId);
    return {CalendarError::None, {}, id, {}};
}

std::optional<CalendarEvent> CalendarService::event(EventId id) const
{
    auto it = active_.constFind(id);
    if (it != active_.cend()) return it.value();
    it = archive_.constFind(id);
    if (it != archive_.cend()) return it.value();
    return std::nullopt;
}

bool CalendarService::matchesDepartment(const EventData &data, int departmentId) const
{
    for (int id : data.participantIds)
        if (catalog_.employees.value(id).departmentId == departmentId) return true;
    return false;
}

CalendarQueryResult CalendarService::calendar(const CalendarFilter &filter) const
{
    CalendarQueryResult result;
    if ((filter.from && !filter.from->isValid()) || (filter.to && !filter.to->isValid()) ||
        (filter.from && filter.to && *filter.from >= *filter.to) ||
        (filter.departmentId && !catalog_.departments.contains(*filter.departmentId)) ||
        (filter.employeeId && !catalog_.employees.contains(*filter.employeeId)) ||
        (filter.roomId && !catalog_.rooms.contains(*filter.roomId)) ||
        (filter.status && !validStatus(*filter.status)) ||
        filter.scope < CalendarScope::Active || filter.scope > CalendarScope::All) {
        result.result = failure(CalendarError::InvalidData, QStringLiteral("Некорректный фильтр календаря."));
        return result;
    }
    auto append = [&](const QMap<EventId, CalendarEvent> &events) {
        for (const auto &event : events) {
            const auto &data = event.data;
            if (filter.from && data.end <= *filter.from) continue;
            if (filter.to && data.start >= *filter.to) continue;
            if (filter.departmentId && !event.participantDepartments.values().contains(*filter.departmentId)) continue;
            if (filter.employeeId && !data.participantIds.contains(*filter.employeeId)) continue;
            if (filter.roomId && data.roomId != filter.roomId) continue;
            if (filter.status && event.status != *filter.status) continue;
            result.events.append(event);
        }
    };
    if (filter.scope != CalendarScope::Archive) append(active_);
    if (filter.scope != CalendarScope::Active) append(archive_);
    std::sort(result.events.begin(), result.events.end(), [](const CalendarEvent &a, const CalendarEvent &b) {
        return a.data.start == b.data.start ? a.id < b.id : a.data.start < b.data.start;
    });
    return result;
}

QVector<CalendarRecord> CalendarService::history(std::optional<EventId> id) const
{
    QVector<CalendarRecord> result;
    for (const auto &entry : history_)
        if (!id || entry.snapshot.id == *id) result.append(entry);
    return result;
}

CalendarResult CalendarService::advanceTo(const QDateTime &time, bool startAtBoundary)
{
    if (!time.isValid() || time < time_ || time > horizonEnd_)
        return failure(CalendarError::InvalidTime, QStringLiteral("Время должно двигаться вперед в пределах прогона."));
    struct Transition { QDateTime at; EventId id; bool completion; };
    QVector<Transition> transitions;
    for (const auto &event : active_) {
        if (event.status == EventStatus::Planned && (event.data.start < time || (startAtBoundary && event.data.start == time)) && event.data.start < horizonEnd_)
            transitions.append({event.data.start, event.id, false});
        if (event.data.end <= time) transitions.append({event.data.end, event.id, true});
    }
    std::sort(transitions.begin(), transitions.end(), [](const Transition &a, const Transition &b) {
        if (a.at != b.at) return a.at < b.at;
        if (a.completion != b.completion) return a.completion; // Finish before start at equal timestamps.
        return a.id < b.id;
    });
    for (const auto &transition : transitions) {
        auto &event = active_[transition.id];
        event.status = transition.completion ? EventStatus::Completed : EventStatus::InProgress;
        record(event, transition.completion ? CalendarAction::Completed : CalendarAction::Started, transition.at, 0);
        if (transition.completion) {
            archive_.insert(event.id, event);
            active_.remove(transition.id);
        }
    }
    time_ = time;
    deferStarts_ = !startAtBoundary;
    return {};
}

QString CalendarService::participantCode(int id) const { return catalog_.employees.value(id).publicId; }

std::optional<int> CalendarService::participantKey(const QString &code) const
{
    for (const auto &employee : catalog_.employees)
        if (employee.publicId == code.trimmed()) return employee.id;
    return std::nullopt;
}

void CalendarService::captureParticipants(CalendarEvent &event) const
{
    event.organizerCode = participantCode(event.data.organizerId);
    event.participantCodes.clear();
    event.participantDepartments.clear();
    for (int id : event.data.participantIds) {
        event.participantCodes.insert(id, participantCode(id));
        event.participantDepartments.insert(id, catalog_.employees.value(id).departmentId);
    }
}

bool CalendarService::employeeBusy(int id) const
{
    for (const auto &event : active_)
        if (event.status == EventStatus::InProgress && event.data.participantIds.contains(id)) return true;
    return false;
}

CalendarResult CalendarService::transferEmployee(int id, int department, const UserContext &actor)
{
    if (!validActor(actor) || actor.role != UserRole::Secretary)
        return failure(CalendarError::PermissionDenied, QStringLiteral("Перевод доступен секретарю."));
    if (!catalog_.employees.contains(id) || !catalog_.departments.contains(department))
        return failure(CalendarError::InvalidData, QStringLiteral("Участник или организация не найдены."));
    if (time_ >= horizonEnd_ || employeeBusy(id) || catalog_.employees.value(id).departmentId == department)
        return failure(CalendarError::InvalidState, QStringLiteral("Перевод сейчас невозможен."));
    auto &employee = catalog_.employees[id];
    employee.departmentId = department;
    employee.publicId = QStringLiteral("%1_%2").arg(department).arg(++nextParticipantNumber_[department], 2, 10, QChar('0'));
    for (auto &event : active_) {
        if (event.data.participantIds.contains(id) || event.data.organizerId == id) {
            captureParticipants(event);
            ++event.version;
            record(event, CalendarAction::Updated, time_, actor.employeeId);
        }
    }
    return {};
}

CalendarResult CalendarService::setEmployeeAvailable(int id, bool available, const UserContext &actor)
{
    if (!validActor(actor) || actor.role != UserRole::Secretary)
        return failure(CalendarError::PermissionDenied, QStringLiteral("Изменение доступности требует роли секретаря."));
    if (!catalog_.employees.contains(id)) return failure(CalendarError::NotFound, QStringLiteral("Участник не найден."));
    if (time_ >= horizonEnd_ || (!available && employeeBusy(id)))
        return failure(CalendarError::InvalidState, QStringLiteral("Участник уже участвует в событии или прогон завершен."));
    catalog_.employees[id].available = available;
    if (!available) {
        const auto events = active_.values();
        for (const auto &event : events)
            if (event.data.participantIds.contains(id)) cancelEvent(event.id, actor);
    }
    return {};
}

CalendarResult CalendarService::setRoomAvailable(int id, bool available, const UserContext &actor)
{
    if (!validActor(actor) || actor.role != UserRole::Secretary)
        return failure(CalendarError::PermissionDenied, QStringLiteral("Изменение помещения требует роли секретаря."));
    if (!catalog_.rooms.contains(id)) return failure(CalendarError::NotFound, QStringLiteral("Помещение не найдено."));
    if (time_ >= horizonEnd_) return failure(CalendarError::InvalidState, QStringLiteral("Прогон завершен."));
    for (const auto &event : active_)
        if (!available && event.data.roomId == id && event.status == EventStatus::InProgress)
            return failure(CalendarError::InvalidState, QStringLiteral("Помещение занято."));
    if (available) unavailableRooms_.remove(id); else unavailableRooms_.insert(id);
    if (!available) {
        const auto events = active_.values();
        for (const auto &event : events)
            if (event.data.roomId == id) cancelEvent(event.id, actor);
    }
    return {};
}

QVector<CalendarRecord> CalendarService::historySince(int offset) const
{
    return history_.mid(std::max(0, offset));
}
