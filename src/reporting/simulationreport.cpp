#include "simulationreport.h"
#include "src/storage/jsoncodec.h"
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
QString quoted(QString text) { text.replace('"', "\"\""); return '"' + text + '"'; }
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
    QJsonArray events, history, requests, emails, log, employees, participantStates, departmentStates, departments;
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
    for (const auto &e : engine.calendar().catalog().employees) employees.append(QJsonObject{{"id", e.id}, {"publicId", e.publicId}, {"departmentId", e.departmentId}, {"available", e.available}});
    for (auto it = engine.participants().cbegin(); it != engine.participants().cend(); ++it) participantStates.append(QJsonObject{{"id", it.key()},
        {"stress", it->stress}, {"skill", it->skill}, {"sickSince", it->sickSince ? it->sickSince->toString(Qt::ISODate) : QString()}});
    for (auto it = engine.organizations().cbegin(); it != engine.organizations().cend(); ++it) departmentStates.append(QJsonObject{{"id", it.key()},
        {"workload", it->workload}, {"reputation", it->reputation}, {"overloaded", it->overloaded}, {"conflictSince", it->conflictSince ? it->conflictSince->toString(Qt::ISODate) : QString()}});
    for (const auto &department : engine.calendar().catalog().departments)
        departments.append(QJsonObject{{"id", department.id}, {"name", department.name}, {"leaderId", department.leaderId},
            {"leaderCode", engine.calendar().participantCode(department.leaderId)}});
    return {{"schemaVersion", 1}, {"generator", engine.modelVersion()}, {"configuration", JsonCodec::setup(engine.setup())},
        {"modelTime", engine.calendar().modelTime().toString(Qt::ISODate)}, {"finished", engine.finished()}, {"metrics", metrics(engine)},
        {"events", events}, {"history", history}, {"requests", requests}, {"emails", emails}, {"log", log}, {"employees", employees},
        {"participantStates", participantStates}, {"departmentStates", departmentStates}, {"departments", departments}};
}

QByteArray SimulationReport::csv(const QJsonObject &report)
{
    QString out = QStringLiteral("section,key,value\n");
    auto row = [&](const QString &section, const QString &key, const QString &value) { out += quoted(section) + ',' + quoted(key) + ',' + quoted(value) + '\n'; };
    const auto metrics = report["metrics"].toObject();
    for (auto it = metrics.begin(); it != metrics.end(); ++it) {
        if (it.value().isDouble()) row("metrics", it.key(), QString::number(it.value().toDouble(), 'g', 15));
        else if (it.value().isObject()) row("metrics", it.key(), QString::fromUtf8(QJsonDocument(it.value().toObject()).toJson(QJsonDocument::Compact)));
    }
    for (const auto &section : {"events", "history", "requests", "emails", "log", "departments"}) {
        int i = 0; for (const auto &entry : report[section].toArray()) row(section, QString::number(++i), QString::fromUtf8(QJsonDocument(entry.toObject()).toJson(QJsonDocument::Compact)));
    }
    for (const auto &entry : metrics["rooms"].toArray()) row("rooms", QString::number(entry.toObject()["roomId"].toInt()), QString::fromUtf8(QJsonDocument(entry.toObject()).toJson(QJsonDocument::Compact)));
    return out.toUtf8();
}
QString SimulationReport::summary(const QJsonObject &m)
{
    return QStringLiteral("Мероприятий: %1 · Началось: %2 · Завершено: %3 · Отменено: %4\nОжидают начала: %5 · Идут: %6\nЗаявок: %7 · Принято: %8 · Отказов: %9 · Ожидают решения: %10\nКонфликтных заявок: %11 · Переносов: %12 · Автопереносов: %13\nПисем отправлено / доставлено (имитация): %14 / %15\nЗанятость помещений в прошедшее рабочее время: %16%")
        .arg(m["created"].toInt()).arg(m["started"].toInt()).arg(m["completed"].toInt()).arg(m["cancelled"].toInt())
        .arg(m["planned"].toInt()).arg(m["inProgress"].toInt()).arg(m["requests"].toInt()).arg(m["accepted"].toInt())
        .arg(m["rejected"].toInt()).arg(m["pending"].toInt()).arg(m["conflicts"].toInt()).arg(m["rescheduled"].toInt())
        .arg(m["autoRescheduled"].toInt()).arg(m["emailsSent"].toInt()).arg(m["emailsDelivered"].toInt()).arg(m["roomUtilization"].toDouble() * 100, 0, 'f', 2);
}
