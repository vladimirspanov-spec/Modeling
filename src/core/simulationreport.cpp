#include "simulationreport.h"
#include "src/core/jsoncodec.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>

namespace {
QJsonObject eventSnapshot(const CalendarEvent &event) {
    auto o = JsonCodec::event(event.data);
    o["id"] = QString::number(event.id); o["version"] = QString::number(event.version);
    o["scheduleVersion"] = QString::number(event.scheduleVersion); o["status"] = int(event.status);
    QJsonObject codes; for (auto it = event.participantCodes.cbegin(); it != event.participantCodes.cend(); ++it) codes[QString::number(it.key())] = it.value();
    o["participantCodes"] = codes; return o;
}
qint64 workingSeconds(const CalendarSetup &setup, QDateTime start, QDateTime end) {
    qint64 total = 0;
    for (auto day = start.date(); day <= end.date(); day = day.addDays(1)) {
        if (!setup.workingDays.value(day.dayOfWeek() - 1)) continue;
        const auto a = std::max(start, modelDateTime(day, setup.workStart));
        const auto b = std::min(end, modelDateTime(day, setup.workEnd));
        total += std::max(qint64(0), a.secsTo(b));
    }
    return total;
}

}
QJsonObject SimulationReport::metrics(const SimulationEngine &engine)
{
    int accepted = 0, rejected = 0, pending = 0, selected = 0, conflicts = 0, people = 0, rooms = 0, automatic = 0;
    qint64 shift = 0;
    QJsonObject reasons;
    for (const auto &request : engine.requests()) {
        if (request.result.ok()) ++accepted;
        else if (request.pending) ++pending;
        else if (request.selected) ++selected;
        else { ++rejected; const auto key = QString::number(int(request.result.error)); reasons[key] = reasons[key].toInt() + 1; }
        people += request.participantConflict; rooms += request.roomConflict;
        conflicts += request.participantConflict || request.roomConflict;
        automatic += request.automatic && request.result.ok(); shift += request.shiftSeconds;
    }
    int created = 0, completed = 0, cancelled = 0, started = 0, rescheduled = 0;
    QMap<EventId, EventData> previous, executed;
    for (const auto &record : engine.calendar().history()) {
        const auto &event = record.snapshot;
        if (record.action == CalendarAction::Created) ++created;
        if (record.action == CalendarAction::Started) { ++started; executed[event.id] = event.data; }
        if (record.action == CalendarAction::Completed) ++completed;
        if (record.action == CalendarAction::Cancelled) ++cancelled;
        if (record.action == CalendarAction::Updated && previous.contains(event.id)) {
            const auto old = previous.value(event.id);
            if (old.start != event.data.start || old.end != event.data.end || old.roomId != event.data.roomId) ++rescheduled;
        }
        previous[event.id] = event.data;
    }
    int future = 0, ongoing = 0;
    for (const auto &event : engine.calendar().calendar().events) {
        future += event.status == EventStatus::Planned; ongoing += event.status == EventStatus::InProgress;
    }
    const auto now = engine.calendar().modelTime();
    const auto start = modelDateTime(engine.setup().startDate);
    const qint64 available = workingSeconds(engine.setup(), start, now);
    QJsonArray roomStats;
    qint64 totalBusy = 0;
    for (const auto &room : engine.calendar().catalog().rooms) {
        qint64 busy = 0, elapsedBusy = 0;
        for (const auto &data : executed) if (data.roomId == room.id) {
            const auto a = std::max(data.start, start), b = std::min(data.end, now);
            if (a < b) { busy += workingSeconds(engine.setup(), a, b); elapsedBusy += a.secsTo(b); }
        }
        totalBusy += busy;
        roomStats.append(QJsonObject{{"roomId", room.id}, {"name", room.name}, {"busySeconds", double(busy)},
            {"elapsedBusySeconds", double(elapsedBusy)}, {"availableWorkingSeconds", double(available)},
            {"utilization", available ? double(busy) / available : 0.0}});
    }
    return {{"requests", engine.requests().size()}, {"accepted", accepted}, {"rejected", rejected}, {"pending", pending}, {"selected", selected},
        {"conflicts", conflicts}, {"participantConflicts", people}, {"roomConflicts", rooms}, {"rejectionReasons", reasons},
        {"created", created}, {"started", started}, {"completed", completed}, {"cancelled", cancelled}, {"planned", future}, {"inProgress", ongoing},
        {"rescheduled", rescheduled}, {"autoRescheduled", automatic}, {"automaticShiftSeconds", double(shift)},
        {"emailsSent", engine.emails().size()}, {"emailsDelivered", engine.emails().size()}, {"rooms", roomStats},
        {"roomUtilization", available ? double(totalBusy) / (available * engine.calendar().catalog().rooms.size()) : 0.0}};
}

QJsonObject SimulationReport::create(const SimulationEngine &engine)
{
    QJsonArray events, history, requests, emails, log, employees, departments;
    CalendarFilter all; all.scope = CalendarScope::All;
    for (const auto &e : engine.calendar().calendar(all).events) events.append(eventSnapshot(e));
    for (const auto &record : engine.calendar().history()) history.append(QJsonObject{{"sequence", QString::number(record.sequence)},
        {"time", record.timestamp.toString(Qt::ISODate)}, {"action", int(record.action)}, {"actor", record.actorCode}, {"event", eventSnapshot(record.snapshot)}});
    for (const auto &r : engine.requests()) {
        QJsonArray alternatives, conflicts;
        for (const auto &a : r.alternatives) alternatives.append(JsonCodec::event(a));
        for (auto id : r.result.conflicts) conflicts.append(QString::number(id));
        requests.append(QJsonObject{{"id", QString::number(r.id)}, {"time", r.time.toString(Qt::ISODate)}, {"editedEvent", QString::number(r.editedEvent)},
            {"data", JsonCodec::event(r.requested)}, {"error", int(r.result.error)}, {"message", r.result.message}, {"eventId", QString::number(r.result.eventId)},
            {"conflicts", conflicts}, {"participantConflict", r.participantConflict}, {"roomConflict", r.roomConflict}, {"pending", r.pending},
            {"selected", r.selected}, {"automatic", r.automatic}, {"shiftSeconds", double(r.shiftSeconds)}, {"alternatives", alternatives}});
    }
    for (const auto &e : engine.emails()) emails.append(QJsonObject{{"eventId", QString::number(e.eventId)}, {"scheduleVersion", QString::number(e.scheduleVersion)},
        {"recipientId", e.recipientId}, {"recipient", e.recipient}, {"subject", e.subject}, {"body", e.body}, {"dueTime", e.dueTime.toString(Qt::ISODate)},
        {"sentAt", e.sentAt.toString(Qt::ISODate)}, {"deliveredAt", e.deliveredAt.toString(Qt::ISODate)}, {"status", "Delivered (simulated)"}});
    for (const auto &e : engine.log()) log.append(QJsonObject{{"time", e.time.toString(Qt::ISODate)}, {"description", e.description},
        {"eventId", e.eventId ? QString::number(*e.eventId) : QString()}});
    for (const auto &e : engine.calendar().catalog().employees) employees.append(QJsonObject{{"id", e.id}, {"publicId", e.publicId}, {"departmentId", e.departmentId}});
    for (const auto &department : engine.calendar().catalog().departments)
        departments.append(QJsonObject{{"id", department.id}, {"name", department.name}, {"leaderId", department.leaderId},
            {"leaderCode", engine.calendar().participantCode(department.leaderId)}});
    return {{"schemaVersion", 1}, {"generator", engine.modelVersion()}, {"configuration", JsonCodec::setup(engine.setup())},
        {"modelTime", engine.calendar().modelTime().toString(Qt::ISODate)}, {"finished", engine.finished()}, {"metrics", metrics(engine)},
        {"events", events}, {"history", history}, {"requests", requests}, {"emails", emails}, {"log", log}, {"employees", employees},
        {"departments", departments}};
}

QJsonObject SimulationReport::userReport(const SimulationEngine &engine)
{
    const auto &calendar = engine.calendar();
    const auto &setup = engine.setup();
    const auto &catalog = calendar.catalog();
    const auto from = modelDateTime(setup.startDate);
    const auto to = calendar.modelTime();
    const QStringList types{QStringLiteral("Планёрка"), QStringLiteral("Совещание руководителей"),
        QStringLiteral("Семинар"), QStringLiteral("Командировка"), QStringLiteral("Другая встреча")};
    const QStringList importance{QStringLiteral("Низкая"), QStringLiteral("Средняя"), QStringLiteral("Высокая")};
    const QStringList policies{"Reject", "Suggest", "AutoReschedule"};
    auto person = [&](int id) {
        const auto employee = catalog.employees.value(id);
        return employee.name + QStringLiteral(" (%1)").arg(employee.publicId);
    };
    QJsonArray events;
    CalendarFilter filter; filter.scope = CalendarScope::All;
    for (const auto &event : calendar.calendar(filter).events) {
        if (event.status != EventStatus::Completed && event.status != EventStatus::InProgress) continue;
        const auto &data = event.data;
        if (data.end <= from || data.start > to) continue;
        QJsonArray participants;
        for (int id : data.participantIds) participants.append(person(id));
        const auto location = data.roomId ? catalog.rooms.value(*data.roomId).name :
            data.type == EventType::BusinessTrip ? QStringLiteral("Вне офиса") : QStringLiteral("Не указано");
        events.append(QJsonObject{{"id", QString::number(event.id)}, {"title", data.title},
            {"type", types.at(int(data.type))}, {"start", data.start.toString(Qt::ISODate)},
            {"end", data.end.toString(Qt::ISODate)}, {"location", location}, {"participants", participants},
            {"organizer", person(data.organizerId)}, {"importance", importance.at(int(data.importance))},
            {"status", event.status == EventStatus::Completed ? QStringLiteral("Завершено") : QStringLiteral("Идёт")}});
    }
    const auto m = metrics(engine);
    QJsonObject totals;
    for (const auto *key : {"created", "started", "completed", "inProgress", "planned", "cancelled",
                            "conflicts", "rescheduled", "autoRescheduled", "rejected", "emailsDelivered"})
        totals[key] = m[key];
    return {{"format", "SecretarySimulationReport"}, {"schemaVersion", 1}, {"finished", engine.finished()},
        {"period", QJsonObject{{"from", from.toString(Qt::ISODate)}, {"to", to.toString(Qt::ISODate)},
                              {"plannedEnd", calendar.horizonEnd().toString(Qt::ISODate)}}},
        {"parameters", QJsonObject{{"days", setup.dayCount}, {"stepMinutes", setup.stepMinutes},
            {"departments", setup.departmentCount}, {"employeesPerDepartment", setup.employeesPerDepartment},
            {"seed", double(setup.seed)}, {"requestRatePerWorkday", setup.requestRatePerWorkday},
            {"changeRatePerWorkday", setup.changeRatePerWorkday}, {"conflictPolicy", policies.at(int(setup.conflictPolicy))}}},
        {"summary", totals}, {"events", events}};
}

QString SimulationReport::summary(const QJsonObject &m)
{
    return QStringLiteral("Мероприятий: %1 · Началось: %2 · Завершено: %3 · Отменено: %4\nОжидают начала: %5 · Идут: %6\nЗаявок: %7 · Принято: %8 · Отказов: %9 · Ожидают решения: %10\nКонфликтных заявок: %11 · Переносов: %12 · Автопереносов: %13\nПисем отправлено / доставлено (имитация): %14 / %15\nЗанятость помещений в прошедшее рабочее время: %16%")
        .arg(m["created"].toInt()).arg(m["started"].toInt()).arg(m["completed"].toInt()).arg(m["cancelled"].toInt())
        .arg(m["planned"].toInt()).arg(m["inProgress"].toInt()).arg(m["requests"].toInt()).arg(m["accepted"].toInt())
        .arg(m["rejected"].toInt()).arg(m["pending"].toInt()).arg(m["conflicts"].toInt()).arg(m["rescheduled"].toInt())
        .arg(m["autoRescheduled"].toInt()).arg(m["emailsSent"].toInt()).arg(m["emailsDelivered"].toInt()).arg(m["roomUtilization"].toDouble() * 100, 0, 'f', 2);
}
