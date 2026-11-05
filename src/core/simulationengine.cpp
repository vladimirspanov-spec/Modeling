#include "simulationengine.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <sstream>
#include <QScopedValueRollback>
#include "src/core/conflictresolver.h"
#include "src/core/jsoncodec.h"

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
    if (setup_.seminarPeriodDays < 1 || setup_.seminarPeriodDays > 365 ||
        setup_.seminarLeadDays < 1 || setup_.seminarLeadDays > 365 ||
        setup_.seminarDurationMinutes < 15 || setup_.seminarDurationMinutes > 480 ||
        !setup_.seminarTime.isValid() || setup_.seminarTime.second() || setup_.seminarTime.msec() ||
        (setup_.periodicSeminars && (setup_.seminarTime < setup_.workStart ||
            setup_.seminarTime.secsTo(setup_.workEnd) < setup_.seminarDurationMinutes * 60 ||
            setup_.participantsMax < 2 ||
            !setup_.startDate.addDays(setup_.dayCount + setup_.seminarLeadDays + setup_.seminarPeriodDays + 7).isValid())))
        throw std::invalid_argument("Invalid periodic seminar configuration");
    if (setup_.conflictPolicy < ConflictPolicy::Reject || setup_.conflictPolicy > ConflictPolicy::AutoReschedule ||
        setup_.rescheduleWindowWorkdays < 0 || setup_.rescheduleWindowWorkdays > 365 || setup_.maxSuggestions < 1 || setup_.maxSuggestions > 1000 ||
        setup_.repetitions < 1 || setup_.repetitions > 1000) throw std::invalid_argument("Invalid experiment configuration");
    for (const auto &rule : setup_.reminders)
        if (rule.leadDays < 0 || rule.leadDays > 365 || rule.periodHours < 1 || rule.periodHours > 8760)
            throw std::invalid_argument("Invalid reminder configuration");
    if (setup_.workStart.second() || setup_.workEnd.second() ||
        !setup_.startDate.addDays(setup_.dayCount + 7 * (setup_.leadTimeMaxWorkdays + setup_.rescheduleWindowWorkdays + 2)).isValid())
        throw std::invalid_argument("Model times require minute precision and a valid planning horizon");
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
    commands_ = {}; requests_.clear(); reminders_.clear(); email_.clear(); internalCommand_ = false;
    meetingWeeks_.clear();
    initialized_ = false;
    nextSeminarDate_ = setup_.startDate.addDays(setup_.seminarLeadDays);
    if (setup_.initialMeetings) initializeCalendar();
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
    reminders_.observe(records, setup_.reminders);
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

    }
}

CalendarResult SimulationEngine::createEvent(const EventData &data, const UserContext &actor)
{
    recordCommand({{"op", "create"}, {"data", JsonCodec::event(data)}, {"actor", JsonCodec::actor(actor)}});
    return submit(data, actor);
}
CalendarResult SimulationEngine::updateEvent(EventId id, const EventData &data, const UserContext &actor)
{
    recordCommand({{"op", "update"}, {"id", QString::number(id)}, {"data", JsonCodec::event(data)}, {"actor", JsonCodec::actor(actor)}});
    return submit(data, actor, id);
}

CalendarResult SimulationEngine::submit(const EventData &data, const UserContext &actor, EventId id, std::optional<ScenarioKind> kind)
{
    RequestRecord request;
    request.id = quint64(requests_.size()) + 1; request.time = calendar_.modelTime();
    request.requested = data; request.actor = actor; request.editedEvent = id; request.kind = kind;
    auto result = calendar_.checkEvent(data, actor, id);
    if ((result.ok() || result.error == CalendarError::Conflict) &&
        (data.start.time().second() || data.end.time().second() || data.start.time().msec() || data.end.time().msec()))
        result = {CalendarError::InvalidData, QStringLiteral("Укажите время с точностью до минуты.")};
    for (auto conflict : result.conflicts) {
        const auto existing = calendar_.event(conflict).value();
        request.roomConflict |= data.roomId && data.roomId == existing.data.roomId;
        for (int person : data.participantIds) request.participantConflict |= existing.data.participantIds.contains(person);
    }
    auto accepted = data;
    if (result.error == CalendarError::Conflict && setup_.conflictPolicy != ConflictPolicy::Reject) {
        request.alternatives = ConflictResolver::alternatives(calendar_, setup_, data, actor, id);
        if (!request.alternatives.isEmpty()) {
            if (setup_.conflictPolicy == ConflictPolicy::AutoReschedule) {
                accepted = request.alternatives.first(); result = {}; request.automatic = true;
            } else {
                request.pending = true;
                result.message = QStringLiteral("Найдено вариантов: %1. Заявка ожидает выбора на вкладке «Заявки».").arg(request.alternatives.size());
            }
        }
    }
    if (result.ok()) {
        result = id ? calendar_.updateEvent(id, accepted, actor) : calendar_.createEvent(accepted, actor);
        request.shiftSeconds = data.start.secsTo(accepted.start);
    }
    result.requestId = request.id; request.result = result; requests_.append(request);
    if (!result.ok()) append(QStringLiteral("Заявка «%1»: %2").arg(data.title, result.message));
    publishCalendar(); return result;
}

CalendarResult SimulationEngine::acceptSuggestion(quint64 requestId, int index)
{
    recordCommand({{"op", "accept"}, {"id", QString::number(requestId)}, {"index", index}});
    if (!requestId || requestId > quint64(requests_.size())) return denied(QStringLiteral("Заявка не найдена."));
    auto &request = requests_[int(requestId - 1)];
    if (!request.pending || index < 0 || index >= request.alternatives.size()) return denied(QStringLiteral("Вариант недоступен."));
    request.pending = false; request.selected = true;
    const auto copy = request; // submit may reallocate requests_.
    return submit(copy.alternatives[index], copy.actor, copy.editedEvent, copy.kind);
}
CalendarResult SimulationEngine::rejectSuggestion(quint64 requestId)
{
    recordCommand({{"op", "reject"}, {"id", QString::number(requestId)}});
    if (!requestId || requestId > quint64(requests_.size()) || !requests_[int(requestId - 1)].pending)
        return denied(QStringLiteral("Ожидающая заявка не найдена."));
    auto &request = requests_[int(requestId - 1)]; request.pending = false;
    request.result.message = QStringLiteral("Предложения отклонены пользователем.");
    append(request.result.message); return {};
}

CalendarResult SimulationEngine::cancelEvent(EventId id, const UserContext &actor)
{
    recordCommand({{"op", "cancel"}, {"id", QString::number(id)}, {"actor", JsonCodec::actor(actor)}});
    auto result = calendar_.cancelEvent(id, actor);
    if (!result.ok()) append(QStringLiteral("Отмена события №%1 отклонена: %2").arg(id).arg(result.message));
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
    recordCommand({{"op", "step"}});
    QScopedValueRollback<bool> internal(internalCommand_, true);
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
        if (setup_.periodicSeminars && time.date().addDays(setup_.seminarLeadDays) >= nextSeminarDate_)
            periodicSeminar();
        const double minutes = setup_.workStart.secsTo(setup_.workEnd) / 60.0;
        const int requests = setup_.requestRatePerWorkday > 0 ? std::poisson_distribution<int>(setup_.requestRatePerWorkday / minutes)(random_) : 0;
        for (int request = 0; request < requests; ++request) {
            QVector<double> weights;
            for (const auto &definition : eventCatalog()) {
                double weight = definition.weight;
                weights.append(weight);
            }
            const int index = std::discrete_distribution<int>(weights.cbegin(), weights.cend())(random_);
            trigger(eventCatalog()[index].kind);
        }
        const int changes = setup_.changeRatePerWorkday > 0 ? std::poisson_distribution<int>(setup_.changeRatePerWorkday / minutes)(random_) : 0;
        for (int change = 0; change < changes; ++change) trigger(ScenarioKind::ScheduleChange);
    }
    reminders_.deliver(calendar_, email_);
    calendar_.advanceTo(time);
    publishCalendar();
}

CalendarResult SimulationEngine::trigger(ScenarioKind kind)
{
    recordCommand({{"op", "trigger"}, {"kind", int(kind)}});
    QScopedValueRollback<bool> internal(internalCommand_, true);
    if (finished()) return denied(QStringLiteral("Прогон завершен."));
    if (int(kind) < 0 || int(kind) >= eventCatalog().size()) return denied(QStringLiteral("Неизвестный тип события."));
    const auto &definition = eventCatalog()[int(kind)];
    if (definition.durationMinutes > 0) return schedule(kind);
    CalendarResult result;
    QString detail;
    if (kind == ScenarioKind::ScheduleChange) {
        QVector<CalendarEvent> planned;
        for (const auto &event : calendar_.calendar().events) if (event.status == EventStatus::Planned) planned.append(event);
        if (planned.isEmpty()) return denied(QStringLiteral("Нет запланированных мероприятий для переноса."));
        const auto event = planned[randomInt(0, int(planned.size()) - 1)];
        auto data = event.data;
        const int shift = randomInt(1, 4) * 30 * 60;
        data.start = data.start.addSecs(shift); data.end = data.end.addSecs(shift);
        result = submit(data, systemActor(), event.id);
        detail = QStringLiteral("событие №%1, перенос на %2 мин").arg(event.id).arg(shift / 60);
    } else return denied(QStringLiteral("Неизвестное действие."));
    append(result.ok() ? definition.title + ": " + detail : definition.title + ": " + result.message, kind);
    publishCalendar(); return result;
}

CalendarResult SimulationEngine::schedule(ScenarioKind kind)
{
    const auto &definition = eventCatalog()[int(kind)];
    auto rejected = [&](const QString &reason) {
        RequestRecord request;
        request.id = quint64(requests_.size()) + 1; request.time = calendar_.modelTime();
        request.requested.title = definition.title; request.actor = systemActor(); request.kind = kind;
        request.result = denied(reason); request.result.requestId = request.id;
        requests_.append(request); append(definition.title + ": " + reason, kind);
        return request.result;
    };
    QVector<int> available;
    for (const auto &employee : calendar_.catalog().employees) {
        if (kind == ScenarioKind::ManagementMeeting &&
            calendar_.catalog().departments.value(employee.departmentId).leaderId != employee.id) continue;
        available.append(employee.id);
    }
    if (available.size() < setup_.participantsMin) {
        return rejected(QStringLiteral("Недостаточно участников."));
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
    auto date = calendar_.modelTime().date();
    // 30% same-day requests, 70% the configured multi-day range.
    const int lead = randomInt(1, 100) <= 30 ? 0 : randomInt(setup_.leadTimeMinWorkdays, setup_.leadTimeMaxWorkdays);
    for (int day = 0; day < lead; ) { date = date.addDays(1); if (setup_.workingDays.value(date.dayOfWeek() - 1)) ++day; }
    const int duration = definition.durationMinutes;
    const auto minimumStart = calendar_.modelTime();
    if (duration * 60 > setup_.workStart.secsTo(setup_.workEnd)) {
        return rejected(QStringLiteral("Нет допустимого времени в рабочем дне."));
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
        for (int id : calendar_.catalog().rooms.keys()) rooms.append(id);
        if (rooms.isEmpty()) return rejected(QStringLiteral("Нет доступных помещений."));
        data.roomId = pick(rooms);
    }
    return submit(data, systemActor(), 0, kind);
}

void SimulationEngine::initializeCalendar()
{
    // Initial plans pass the same rights and resource checks as new requests.
    QScopedValueRollback<bool> internal(internalCommand_, true);
    const auto &catalog = calendar_.catalog();
    for (auto date = setup_.startDate; date < calendar_.horizonEnd().date(); date = date.addDays(1)) {
        if (!setup_.workingDays.value(date.dayOfWeek() - 1)) continue;
        const auto start = modelDateTime(date, setup_.workStart);
        QVector<int> leaders;
        for (const auto &department : catalog.departments) {
            EventData data;
            data.title = QStringLiteral("Планерка отдела %1").arg(department.id);
            data.type = EventType::Planning;
            data.start = start; data.end = start.addSecs(1800);
            data.roomId = department.id;
            data.recurrenceTemplateId = quint64(department.id);
            for (const auto &employee : catalog.employees)
                if (employee.departmentId == department.id)
                    data.participantIds.append(employee.id);
            if (data.participantIds.isEmpty()) continue;
            data.organizerId = department.leaderId ? department.leaderId : data.participantIds.first();
            submit(data, systemActor());
            if (department.leaderId)
                leaders.append(department.leaderId);
        }
        const auto week = date.addDays(1 - date.dayOfWeek());
        if (!meetingWeeks_.contains(week) && !leaders.isEmpty()) {
            meetingWeeks_.insert(week);
            EventData data;
            data.title = QStringLiteral("Недельное совещание руководителей");
            data.type = EventType::ManagementMeeting;
            data.start = start.addSecs(1800); data.end = start.addSecs(3600);
            data.roomId = setup_.departmentCount + 1;
            data.participantIds = leaders; data.organizerId = leaders.first();
            data.recurrenceTemplateId = quint64(setup_.departmentCount + 1);
            submit(data, systemActor());
        }
    }
    publishCalendar();
}

void SimulationEngine::periodicSeminar()
{
    // Create only the next occurrence as its advance-planning window opens.
    const auto now = calendar_.modelTime();
    auto date = nextSeminarDate_;
    while (!setup_.workingDays.value(date.dayOfWeek() - 1) ||
           modelDateTime(date, setup_.seminarTime) < now) date = date.addDays(1);
    nextSeminarDate_ = date.addDays(setup_.seminarPeriodDays);
    EventData data;
    data.title = QStringLiteral("Периодический тематический семинар");
    data.type = EventType::Seminar;
    data.start = modelDateTime(date, setup_.seminarTime);
    data.end = data.start.addSecs(setup_.seminarDurationMinutes * 60);
    data.roomId = setup_.departmentCount + 1;
    data.recurrenceTemplateId = 1000; // Separate from department and weekly meeting templates.
    QSet<int> departments;
    const auto &employees = calendar_.catalog().employees;
    for (const auto &person : employees) {
        if (!departments.contains(person.departmentId) && data.participantIds.size() < setup_.participantsMax) {
            data.participantIds.append(person.id); departments.insert(person.departmentId);
        }
    }
    for (const auto &person : employees) {
        if (data.participantIds.size() >= setup_.participantsMin) break;
        if (!data.participantIds.contains(person.id)) data.participantIds.append(person.id);
    }
    if (departments.size() < 2 || data.participantIds.size() < setup_.participantsMin) {
        RequestRecord request;
        request.id = quint64(requests_.size()) + 1; request.time = now; request.requested = data;
        request.actor = systemActor(); request.kind = ScenarioKind::Seminar;
        request.result = denied(QStringLiteral("Недостаточно доступных участников минимум из двух отделов для семинара."));
        request.result.requestId = request.id; requests_.append(request);
        append(data.title + ": " + request.result.message, ScenarioKind::Seminar);
        return;
    }
    data.organizerId = data.participantIds.first();
    submit(data, systemActor(), 0, ScenarioKind::Seminar);
}

void SimulationEngine::recordCommand(QJsonObject command)
{
    if (internalCommand_) return;
    command["time"] = calendar_.modelTime().toString(Qt::ISODate);
    commands_.append(command);
}
QString SimulationEngine::randomState() const
{
    std::ostringstream stream; stream << random_; return QString::fromStdString(stream.str());
}
void SimulationEngine::replayCommand(const QJsonObject &command)
{
    if (JsonCodec::time(command["time"]) != calendar_.modelTime()) throw std::runtime_error("Command time mismatch");
    const auto op = command["op"].toString();
    bool validId = false;
    const auto id = command["id"].toString().toULongLong(&validId);
    if ((op == "update" || op == "cancel" || op == "accept" || op == "reject") && (!validId || !id))
        throw std::runtime_error("Invalid command id");
    if (op == "step") { if (!step().ok()) throw std::runtime_error("Step beyond horizon"); }
    else if (op == "create") createEvent(JsonCodec::event(command["data"].toObject()), JsonCodec::actor(command["actor"].toObject()));
    else if (op == "update") updateEvent(id, JsonCodec::event(command["data"].toObject()), JsonCodec::actor(command["actor"].toObject()));
    else if (op == "cancel") cancelEvent(id, JsonCodec::actor(command["actor"].toObject()));
    else if (op == "trigger") trigger(ScenarioKind(command["kind"].toInt(-1)));
    else if (op == "accept") acceptSuggestion(id, command["index"].toInt(-1));
    else if (op == "reject") rejectSuggestion(id);
    else throw std::runtime_error("Unknown scenario command");
}
