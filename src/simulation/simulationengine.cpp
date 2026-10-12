#include "simulationengine.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
CalendarResult denied(const QString &text) { return {CalendarError::InvalidState, text, 0, {}}; }
}

const QVector<ScenarioDefinition> &SimulationEngine::eventCatalog()
{
    static const QVector<ScenarioDefinition> definitions{
        {ScenarioKind::ClientMeeting, QStringLiteral("Встреча с клиентом"), 16, 60},
        {ScenarioKind::Planning, QStringLiteral("Оперативная планерка"), 14, 30},
        {ScenarioKind::ManagementMeeting, QStringLiteral("Совещание руководителей"), 8, 60},
        {ScenarioKind::Seminar, QStringLiteral("Обучающий семинар"), 6, 90},
        {ScenarioKind::BusinessTrip, QStringLiteral("Командировка"), 4, 480},
        {ScenarioKind::Interview, QStringLiteral("Собеседование с кандидатом"), 7, 60},
        {ScenarioKind::Presentation, QStringLiteral("Презентация проекта"), 6, 60},
        {ScenarioKind::DocumentSigning, QStringLiteral("Согласование документов"), 10, 30},
        {ScenarioKind::UrgentTask, QStringLiteral("Срочное поручение"), 10, 60},
        {ScenarioKind::SickLeave, QStringLiteral("Болезнь участника"), 5, 0},
        {ScenarioKind::Recovery, QStringLiteral("Выздоровление"), 8, 0},
        {ScenarioKind::ConflictStarted, QStringLiteral("Конфликт в отделе"), 5, 0},
        {ScenarioKind::ConflictResolved, QStringLiteral("Разрешение конфликта"), 8, 0},
        {ScenarioKind::RoomFailure, QStringLiteral("Поломка оборудования помещения"), 4, 0},
        {ScenarioKind::RoomRestored, QStringLiteral("Завершение ремонта"), 6, 0},
        {ScenarioKind::WorkloadSurge, QStringLiteral("Наплыв заявок в отдел"), 6, 0},
        {ScenarioKind::WorkloadNormalized, QStringLiteral("Нормализация нагрузки"), 7, 0},
        {ScenarioKind::EmployeeTransfer, QStringLiteral("Перевод участника"), 3, 0},
        {ScenarioKind::ScheduleChange, QStringLiteral("Перенос запланированной встречи"), 0, 0}
    };
    return definitions;
}

SimulationEngine::SimulationEngine(CalendarSetup setup)
    : setup_(std::move(setup)), calendar_(makeCompanyCatalog(setup_), modelDateTime(setup_.startDate),
          modelDateTime(setup_.startDate.addDays(setup_.dayCount))), random_(setup_.seed)
{
    const int count = setup_.departmentCount * setup_.employeesPerDepartment;
    if (!std::isfinite(setup_.requestRatePerWorkday) || setup_.requestRatePerWorkday < 0 || setup_.requestRatePerWorkday > 100000 ||
        !std::isfinite(setup_.changeRatePerWorkday) || setup_.changeRatePerWorkday < 0 || setup_.changeRatePerWorkday > 100000 ||
        setup_.participantsMin < 1 || setup_.participantsMax < setup_.participantsMin || setup_.participantsMax > count ||
        setup_.leadTimeMinWorkdays < 0 || setup_.leadTimeMaxWorkdays < setup_.leadTimeMinWorkdays || setup_.leadTimeMaxWorkdays > 365 ||
        setup_.animationTickMs < 1 || setup_.importanceWeights.size() != 3 ||
        (setup_.initialMeetings && setup_.workStart.secsTo(setup_.workEnd) < 3600))
        throw std::invalid_argument("Invalid simulation configuration");
    double total = 0;
    for (double weight : setup_.importanceWeights) {
        if (!std::isfinite(weight) || weight < 0) throw std::invalid_argument("Invalid importance weight");
        total += weight;
    }
    if (!(total > 0) || !std::isfinite(total)) throw std::invalid_argument("Invalid importance distribution");
    reset();
}

void SimulationEngine::reset()
{
    calendar_ = CalendarService(makeCompanyCatalog(setup_), modelDateTime(setup_.startDate), modelDateTime(setup_.startDate.addDays(setup_.dayCount)));
    random_.seed(setup_.seed);
    log_.clear(); historyOffset_ = 0;
    participants_.clear(); organizations_.clear(); brokenRooms_.clear(); effects_.clear(); eventOrganizations_.clear(); meetingWeeks_.clear();
    for (const auto &employee : calendar_.catalog().employees) participants_.insert(employee.id, {});
    for (const auto &department : calendar_.catalog().departments) organizations_.insert(department.id, {});
    nextMeetingCreation_ = {};
    nextMeetingDepartment_ = 1;
    if (setup_.initialMeetings) scheduleMeetingCreation(setup_.startDate);
    initialized_ = false;
}

int SimulationEngine::randomInt(int first, int last) { return std::uniform_int_distribution<int>(first, last)(random_); }
int SimulationEngine::pick(const QVector<int> &ids) { return ids[randomInt(0, int(ids.size()) - 1)]; }
void SimulationEngine::append(const QString &description, std::optional<ScenarioKind> kind, std::optional<EventId> event)
{
    log_.append({quint64(log_.size()) + 1, calendar_.modelTime(), description, kind, event});
}

void SimulationEngine::publishCalendar()
{
    const auto records = calendar_.historySince(historyOffset_);
    historyOffset_ += records.size();
    for (const auto &record : records) {
        const auto &event = record.snapshot;
        QString action;
        switch (record.action) {
        case CalendarAction::Created:
            action = QStringLiteral("Ожидает исполнения %1").arg(event.data.start.toString("dd.MM.yyyy HH:mm")); break;
        case CalendarAction::Updated: action = QStringLiteral("Изменено"); break;
        case CalendarAction::Cancelled: action = QStringLiteral("Отменено"); break;
        case CalendarAction::Started: action = QStringLiteral("Началось"); break;
        case CalendarAction::Completed: action = QStringLiteral("Завершено"); break;
        }
        QStringList people;
        for (int id : event.data.participantIds) people.append(event.participantCodes.value(id));
        log_.append({quint64(log_.size()) + 1, record.timestamp,
            QStringLiteral("%1: %2 · %3").arg(action, event.data.title, people.join(", ")), {}, event.id});
        if (record.action == CalendarAction::Completed && effects_.contains(event.id)) {
            const auto kind = effects_.take(event.id);
            auto &organization = organizations_[eventOrganizations_.take(event.id)];
            if (kind == ScenarioKind::Seminar)
                for (int id : event.data.participantIds) participants_[id].skill = std::min(100, participants_[id].skill + 10);
            if (kind == ScenarioKind::ClientMeeting || kind == ScenarioKind::Presentation) organization.reputation = std::min(100, organization.reputation + 2);
            organization.workload = std::max(0, organization.workload - 5);
            for (int id : event.data.participantIds) participants_[id].stress = std::max(0, participants_[id].stress - 3);
        }
        if (record.action == CalendarAction::Cancelled && effects_.contains(event.id)) {
            effects_.remove(event.id);
            auto &organization = organizations_[eventOrganizations_.take(event.id)];
            organization.workload = std::max(0, organization.workload - 5);
        }
    }
}

CalendarResult SimulationEngine::createEvent(const EventData &data, const UserContext &actor)
{
    auto result = data.start < calendar_.modelTime().addSecs(5 * 3600)
        ? CalendarResult{CalendarError::InvalidTime, QStringLiteral("До исполнения плана должно оставаться не менее 5 модельных часов.")}
        : calendar_.createEvent(data, actor);
    if (!result.ok()) append(QStringLiteral("Заявка «%1» отклонена: %2").arg(data.title, result.message));
    publishCalendar();
    return result;
}
CalendarResult SimulationEngine::updateEvent(EventId id, const EventData &data, const UserContext &actor)
{
    const auto history = calendar_.history(id);
    // Keep the original creation time: editing a title near execution remains valid.
    auto result = !history.isEmpty() && data.start < history.first().timestamp.addSecs(5 * 3600)
        ? CalendarResult{CalendarError::InvalidTime, QStringLiteral("Между созданием и исполнением плана должно быть не менее 5 модельных часов.")}
        : calendar_.updateEvent(id, data, actor);
    if (!result.ok()) append(QStringLiteral("Изменение события №%1 отклонено: %2").arg(id).arg(result.message));
    publishCalendar(); return result;
}
CalendarResult SimulationEngine::cancelEvent(EventId id, const UserContext &actor)
{
    auto result = calendar_.cancelEvent(id, actor);
    if (!result.ok()) append(QStringLiteral("Отмена события №%1 отклонена: %2").arg(id).arg(result.message));
    publishCalendar(); return result;
}
CalendarResult SimulationEngine::transferEmployee(int id, int organization, const UserContext &actor)
{
    const auto oldCode = calendar_.participantCode(id);
    auto result = calendar_.transferEmployee(id, organization, actor);
    if (result.ok()) append(QStringLiteral("Перевод участника: %1 → %2").arg(oldCode, calendar_.participantCode(id)), ScenarioKind::EmployeeTransfer);
    else append(QStringLiteral("Перевод отклонен: %1").arg(result.message));
    publishCalendar(); return result;
}

bool SimulationEngine::workingMinute(const QDateTime &time) const
{
    const auto local = time.toOffsetFromUtc(10800);
    return setup_.workingDays.value(local.date().dayOfWeek() - 1) && local.time() >= setup_.workStart && local.time() < setup_.workEnd;
}

CalendarResult SimulationEngine::step()
{
    if (finished()) return denied(QStringLiteral("Период моделирования завершен."));
    if (!initialized_) { initialized_ = true; minute(calendar_.modelTime()); }
    const auto next = std::min(calendar_.modelTime().addSecs(setup_.stepMinutes * 60), calendar_.horizonEnd());
    while (calendar_.modelTime() < next) minute(std::min(calendar_.modelTime().addSecs(60), next));
    if (finished()) append(QStringLiteral("Моделирование завершено. Архив и журнал доступны для просмотра."));
    return {};
}

void SimulationEngine::minute(const QDateTime &time)
{
    calendar_.advanceTo(time, false);
    publishCalendar();
    if (!finished() && workingMinute(time)) {
        if (setup_.initialMeetings && time >= nextMeetingCreation_) {
            dailyMeetings();
            scheduleMeetingCreation(time.date().addDays(1));
        }
        const double minutes = setup_.workStart.secsTo(setup_.workEnd) / 60.0;
        const int requests = setup_.requestRatePerWorkday > 0 ? std::poisson_distribution<int>(setup_.requestRatePerWorkday / minutes)(random_) : 0;
        for (int request = 0; request < requests; ++request) {
            QVector<double> weights;
            for (const auto &definition : eventCatalog()) {
                double weight = definition.weight;
                if (definition.durationMinutes == 0 && candidates(definition.kind).isEmpty()) weight = 0;
                if (definition.kind == ScenarioKind::ConflictStarted) {
                    int stressed = 0;
                    for (const auto &organization : organizations_) stressed += organization.overloaded;
                    weight *= 1.0 + double(stressed) / organizations_.size();
                }
                weights.append(weight);
            }
            const int index = std::discrete_distribution<int>(weights.cbegin(), weights.cend())(random_);
            trigger(eventCatalog()[index].kind);
        }
        const int changes = setup_.changeRatePerWorkday > 0 ? std::poisson_distribution<int>(setup_.changeRatePerWorkday / minutes)(random_) : 0;
        for (int change = 0; change < changes; ++change) trigger(ScenarioKind::ScheduleChange);
    }
    calendar_.advanceTo(time);
    publishCalendar();
}

QVector<int> SimulationEngine::candidates(ScenarioKind kind) const
{
    QVector<int> ids;
    const auto now = calendar_.modelTime();
    if (kind == ScenarioKind::SickLeave || kind == ScenarioKind::Recovery || kind == ScenarioKind::EmployeeTransfer) {
        for (const auto &person : calendar_.catalog().employees) {
            const auto since = participants_.value(person.id).sickSince;
            if (kind == ScenarioKind::Recovery ? (since && since->addDays(1) <= now) :
                (person.available && !calendar_.employeeBusy(person.id))) ids.append(person.id);
        }
    } else if (kind == ScenarioKind::ConflictStarted || kind == ScenarioKind::ConflictResolved || kind == ScenarioKind::WorkloadSurge || kind == ScenarioKind::WorkloadNormalized) {
        for (auto it = organizations_.cbegin(); it != organizations_.cend(); ++it) {
            bool eligible = false;
            if (kind == ScenarioKind::ConflictStarted) eligible = !it->conflictSince;
            if (kind == ScenarioKind::ConflictResolved) eligible = it->conflictSince && it->conflictSince->addSecs(3600) <= now;
            if (kind == ScenarioKind::WorkloadSurge) eligible = !it->overloaded;
            if (kind == ScenarioKind::WorkloadNormalized) eligible = it->overloaded;
            if (eligible) ids.append(it.key());
        }
    } else if (kind == ScenarioKind::RoomFailure || kind == ScenarioKind::RoomRestored) {
        for (const auto &room : calendar_.catalog().rooms) {
            bool eligible = kind == ScenarioKind::RoomFailure ? calendar_.roomAvailable(room.id) :
                (brokenRooms_.contains(room.id) && brokenRooms_.value(room.id).addSecs(3600) <= now);
            if (kind == ScenarioKind::RoomFailure)
                for (const auto &event : calendar_.calendar().events)
                    if (event.status == EventStatus::InProgress && event.data.roomId == room.id) eligible = false;
            if (eligible) ids.append(room.id);
        }
    } else if (kind == ScenarioKind::ScheduleChange) {
        // Only existence is needed here; actual EventIds are never narrowed to int.
        for (const auto &event : calendar_.calendar().events)
            if (event.status == EventStatus::Planned) { ids.append(1); break; }
    }
    return ids;
}

CalendarResult SimulationEngine::trigger(ScenarioKind kind)
{
    if (finished()) return denied(QStringLiteral("Прогон завершен."));
    if (int(kind) < 0 || int(kind) >= eventCatalog().size()) return denied(QStringLiteral("Неизвестный тип события."));
    const auto &definition = eventCatalog()[int(kind)];
    if (definition.durationMinutes > 0) return schedule(kind);
    const auto eligible = candidates(kind);
    if (eligible.isEmpty()) {
        const auto result = denied(QStringLiteral("Нет подходящего состояния для события «%1».").arg(definition.title));
        append(result.message, kind); return result;
    }
    const int id = pick(eligible);
    CalendarResult result;
    QString detail;
    switch (kind) {
    case ScenarioKind::SickLeave:
        result = calendar_.setEmployeeAvailable(id, false, systemActor());
        if (result.ok()) { participants_[id].sickSince = calendar_.modelTime(); detail = calendar_.participantCode(id) + QStringLiteral(" временно недоступен; будущие встречи отменены"); }
        break;
    case ScenarioKind::Recovery:
        result = calendar_.setEmployeeAvailable(id, true, systemActor());
        if (result.ok()) { participants_[id].sickSince.reset(); participants_[id].stress = 0; detail = calendar_.participantCode(id) + QStringLiteral(" снова доступен"); }
        break;
    case ScenarioKind::ConflictStarted:
        organizations_[id].conflictSince = calendar_.modelTime();
        organizations_[id].reputation = std::max(0, organizations_[id].reputation - 5);
        for (const auto &employee : calendar_.catalog().employees)
            if (employee.departmentId == id) participants_[employee.id].stress = std::min(100, participants_[employee.id].stress + 20);
        detail = QStringLiteral("отдел %1; напряжение выросло, встречи занимают больше времени").arg(id);
        break;
    case ScenarioKind::ConflictResolved:
        organizations_[id].conflictSince.reset();
        organizations_[id].reputation = std::min(100, organizations_[id].reputation + 3);
        for (const auto &employee : calendar_.catalog().employees)
            if (employee.departmentId == id) participants_[employee.id].stress = std::max(0, participants_[employee.id].stress - 20);
        detail = QStringLiteral("отдел %1; обычная длительность встреч восстановлена").arg(id);
        break;
    case ScenarioKind::RoomFailure:
        result = calendar_.setRoomAvailable(id, false, systemActor());
        if (result.ok()) { brokenRooms_.insert(id, calendar_.modelTime()); detail = calendar_.catalog().rooms.value(id).name + QStringLiteral(" закрыто; будущие встречи отменены"); }
        break;
    case ScenarioKind::RoomRestored:
        result = calendar_.setRoomAvailable(id, true, systemActor());
        if (result.ok()) { brokenRooms_.remove(id); detail = calendar_.catalog().rooms.value(id).name + QStringLiteral(" снова доступно"); }
        break;
    case ScenarioKind::WorkloadSurge:
        organizations_[id].overloaded = true; organizations_[id].workload = std::min(100, organizations_[id].workload + 40);
        detail = QStringLiteral("отдел %1; нагрузка повышена, растет вероятность конфликта").arg(id);
        break;
    case ScenarioKind::WorkloadNormalized:
        organizations_[id].overloaded = false; organizations_[id].workload = 20;
        detail = QStringLiteral("отдел %1; нагрузка вернулась к обычной").arg(id);
        break;
    case ScenarioKind::EmployeeTransfer: {
        QVector<int> destinations;
        const int source = calendar_.catalog().employees.value(id).departmentId;
        for (int department : calendar_.catalog().departments.keys()) if (department != source) destinations.append(department);
        return transferEmployee(id, pick(destinations), systemActor());
    }
    case ScenarioKind::ScheduleChange: {
        QVector<CalendarEvent> planned;
        for (const auto &event : calendar_.calendar().events) if (event.status == EventStatus::Planned) planned.append(event);
        const auto event = planned[randomInt(0, int(planned.size()) - 1)];
        auto data = event.data;
        const int shift = randomInt(1, 4) * 30 * 60;
        data.start = data.start.addSecs(shift); data.end = data.end.addSecs(shift);
        result = calendar_.updateEvent(event.id, data, systemActor());
        detail = QStringLiteral("событие №%1, перенос на %2 мин").arg(event.id).arg(shift / 60);
        break;
    }
    default: return denied(QStringLiteral("Неизвестное действие."));
    }
    append(result.ok() ? definition.title + ": " + detail : definition.title + ": " + result.message, kind);
    publishCalendar(); return result;
}

CalendarResult SimulationEngine::schedule(ScenarioKind kind)
{
    const auto &definition = eventCatalog()[int(kind)];
    QVector<int> available;
    for (const auto &employee : calendar_.catalog().employees) if (employee.available) available.append(employee.id);
    if (available.size() < setup_.participantsMin) {
        append(definition.title + QStringLiteral(": недостаточно доступных участников"), kind);
        return denied(QStringLiteral("Недостаточно участников."));
    }
    EventData data;
    data.title = definition.title;
    data.type = kind == ScenarioKind::BusinessTrip ? EventType::BusinessTrip : kind == ScenarioKind::Seminar ? EventType::Seminar :
                kind == ScenarioKind::Planning ? EventType::Planning : kind == ScenarioKind::ManagementMeeting ? EventType::ManagementMeeting : EventType::Other;
    const int count = randomInt(setup_.participantsMin, std::min(setup_.participantsMax, int(available.size())));
    for (int index = 0; index < count; ++index) {
        const int chosen = randomInt(index, int(available.size()) - 1);
        std::swap(available[index], available[chosen]);
        data.participantIds.append(available[index]);
    }
    data.organizerId = data.participantIds.first();
    const int organization = calendar_.catalog().employees.value(data.organizerId).departmentId;
    auto date = calendar_.modelTime().date();
    // 30% short notice, 70% the configured multi-day range. No urgent bypass.
    const int lead = randomInt(1, 100) <= 30 ? 0 : randomInt(setup_.leadTimeMinWorkdays, setup_.leadTimeMaxWorkdays);
    for (int day = 0; day < lead; ) { date = date.addDays(1); if (setup_.workingDays.value(date.dayOfWeek() - 1)) ++day; }
    int duration = definition.durationMinutes;
    if (organizations_.value(organization).conflictSince) duration += duration / 2;
    const int skill = participants_.value(data.organizerId).skill;
    duration = std::max(15, duration - duration * skill / 500);
    const auto minimumStart = calendar_.modelTime().addSecs(5 * 3600);
    if (duration * 60 > setup_.workStart.secsTo(setup_.workEnd)) {
        append(definition.title + QStringLiteral(": нет времени в рабочем дне"), kind);
        return denied(QStringLiteral("Нет допустимого времени."));
    }
    QDateTime earliest, latest;
    // Move to the next working day if the short-notice window has already closed.
    do {
        if (setup_.workingDays.value(date.dayOfWeek() - 1)) {
            earliest = std::max(modelDateTime(date, setup_.workStart), minimumStart);
            latest = modelDateTime(date, setup_.workEnd).addSecs(-duration * 60);
            if (earliest <= latest) break;
        }
        date = date.addDays(1);
    } while (true);
    data.start = earliest.addSecs(randomInt(0, int(earliest.secsTo(latest) / 60)) * 60);
    data.end = data.start.addSecs(duration * 60);
    data.importance = static_cast<Importance>(std::discrete_distribution<int>(setup_.importanceWeights.cbegin(), setup_.importanceWeights.cend())(random_));
    if (kind != ScenarioKind::BusinessTrip) {
        QVector<int> rooms;
        for (int id : calendar_.catalog().rooms.keys()) if (calendar_.roomAvailable(id)) rooms.append(id);
        if (rooms.isEmpty()) { append(definition.title + QStringLiteral(": нет доступных помещений"), kind); return denied(QStringLiteral("Нет помещений.")); }
        data.roomId = pick(rooms);
    }
    auto result = calendar_.createEvent(data, systemActor());
    if (result.ok()) {
        effects_.insert(result.eventId, kind); eventOrganizations_.insert(result.eventId, organization);
        organizations_[organization].workload = std::min(100, organizations_[organization].workload + 5);
        for (int id : data.participantIds) participants_[id].stress = std::min(100, participants_[id].stress + 3);
    } else append(QStringLiteral("%1: заявка отклонена — %2").arg(definition.title, result.message), kind);
    publishCalendar(); return result;
}

void SimulationEngine::scheduleMeetingCreation(QDate date)
{
    while (!setup_.workingDays.value(date.dayOfWeek() - 1)) date = date.addDays(1);
    const int minutes = setup_.workStart.secsTo(setup_.workEnd) / 60;
    nextMeetingCreation_ = modelDateTime(date, setup_.workStart).addSecs(randomInt(0, minutes - 1) * 60);
}

void SimulationEngine::dailyMeetings()
{
    const auto now = calendar_.modelTime();
    QSet<QDate> occupiedDays;
    for (const auto &event : calendar_.calendar().events)
        if (event.data.type == EventType::Planning && event.data.recurrenceTemplateId)
            occupiedDays.insert(event.data.start.date());
    auto date = now.date().addDays(1);
    while (!setup_.workingDays.value(date.dayOfWeek() - 1) ||
           occupiedDays.contains(date) ||
           modelDateTime(date, setup_.workStart) < now.addSecs(5 * 3600)) date = date.addDays(1);
    const auto start = modelDateTime(date, setup_.workStart);
    QVector<int> leaders;
    const int scheduledDepartment = nextMeetingDepartment_;
    for (const auto &department : calendar_.catalog().departments) {
        EventData data;
        data.title = QStringLiteral("Планерка отдела %1").arg(department.id);
        data.type = EventType::Planning;
        data.start = start; data.end = start.addSecs(1800); data.roomId = department.id;
        data.recurrenceTemplateId = quint64(department.id);
        for (const auto &employee : calendar_.catalog().employees)
            if (employee.departmentId == department.id && employee.available) data.participantIds.append(employee.id);
        if (data.participantIds.isEmpty()) continue;
        data.organizerId = data.participantIds.first(); leaders.append(data.organizerId);
        if (department.id != scheduledDepartment) continue;
        createEvent(data, systemActor());
    }
    nextMeetingDepartment_ = nextMeetingDepartment_ % setup_.departmentCount + 1;
    const auto week = date.addDays(1 - date.dayOfWeek());
    if (!meetingWeeks_.contains(week) && !leaders.isEmpty()) {
        meetingWeeks_.insert(week);
        EventData data;
        data.title = QStringLiteral("Недельное совещание руководителей");
        data.type = EventType::ManagementMeeting;
        data.start = start.addSecs(1800); data.end = start.addSecs(3600);
        data.participantIds = leaders; data.organizerId = leaders.first(); data.roomId = setup_.departmentCount + 1;
        data.recurrenceTemplateId = quint64(setup_.departmentCount + 1);
        createEvent(data, systemActor());
    }
}
