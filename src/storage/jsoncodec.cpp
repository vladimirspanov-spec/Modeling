#include "jsoncodec.h"
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <stdexcept>
#include <cmath>
#include <algorithm>

namespace {
void require(bool condition, const QString &message) { if (!condition) throw std::runtime_error(message.toStdString()); }
int integer(const QJsonObject &o, const QString &key, int low, int high) {
    const auto v = o.value(key);
    double n = v.toDouble(-1e100);
    require(v.isDouble() && std::isfinite(n) && n == std::floor(n) && n >= low && n <= high, "Invalid field: " + key);
    return int(n);
}
double number(const QJsonObject &o, const QString &key) {
    double n = o.value(key).toDouble(-1);
    require(std::isfinite(n) && n >= 0 && n <= 100000, "Invalid field: " + key); return n;
}
}
namespace JsonCodec {
QDateTime time(const QJsonValue &value) {
    auto t = QDateTime::fromString(value.toString(), Qt::ISODate);
    require(t.isValid(), "Invalid model timestamp"); return t.toOffsetFromUtc(10800);
}
QJsonObject setup(const CalendarSetup &s) {
    QJsonArray days, weights, reminders;
    for (bool day : s.workingDays) days.append(day);
    for (double weight : s.importanceWeights) weights.append(weight);
    for (const auto &rule : s.reminders) reminders.append(QJsonObject{{"leadDays", rule.leadDays}, {"periodHours", rule.periodHours}});
    return {{"startDate", s.startDate.toString(Qt::ISODate)}, {"dayCount", s.dayCount}, {"stepMinutes", s.stepMinutes},
        {"departmentCount", s.departmentCount}, {"employeesPerDepartment", s.employeesPerDepartment}, {"workingDays", days},
        {"workStart", s.workStart.toString(Qt::ISODate)}, {"workEnd", s.workEnd.toString(Qt::ISODate)}, {"initialMeetings", s.initialMeetings},
        {"role", int(s.role)}, {"seed", double(s.seed)}, {"requestRatePerWorkday", s.requestRatePerWorkday},
        {"changeRatePerWorkday", s.changeRatePerWorkday}, {"animationTickMs", s.animationTickMs}, {"participantsMin", s.participantsMin},
        {"participantsMax", s.participantsMax}, {"leadTimeMinWorkdays", s.leadTimeMinWorkdays}, {"leadTimeMaxWorkdays", s.leadTimeMaxWorkdays},
        {"importanceWeights", weights}, {"reminders", reminders}, {"conflictPolicy", int(s.conflictPolicy)},
        {"rescheduleWindowWorkdays", s.rescheduleWindowWorkdays}, {"maxSuggestions", s.maxSuggestions}, {"repetitions", s.repetitions},
        {"periodicSeminars", s.periodicSeminars}, {"seminarPeriodDays", s.seminarPeriodDays},
        {"seminarLeadDays", s.seminarLeadDays}, {"seminarTime", s.seminarTime.toString(Qt::ISODate)},
        {"seminarDurationMinutes", s.seminarDurationMinutes}};
}
CalendarSetup setup(const QJsonObject &o) {
    CalendarSetup s;
    s.startDate = QDate::fromString(o["startDate"].toString(), Qt::ISODate);
    s.dayCount = integer(o,"dayCount",7,30); s.stepMinutes = integer(o,"stepMinutes",30,60);
    s.departmentCount = integer(o,"departmentCount",5,9); s.employeesPerDepartment = integer(o,"employeesPerDepartment",1,1000);
    require(o["workingDays"].isArray() && o["workingDays"].toArray().size() == 7, "Invalid working days");
    s.workingDays.clear(); for (auto day : o["workingDays"].toArray()) { require(day.isBool(), "Invalid weekday"); s.workingDays.append(day.toBool()); }
    s.workStart = QTime::fromString(o["workStart"].toString(), Qt::ISODate); s.workEnd = QTime::fromString(o["workEnd"].toString(), Qt::ISODate);
    require(o["initialMeetings"].isBool(), "Missing initialMeetings"); s.initialMeetings = o["initialMeetings"].toBool();
    s.role = UserRole(integer(o,"role",0,2));
    double seed = o["seed"].toDouble(-1); require(seed >= 0 && seed <= 4294967295.0 && std::floor(seed) == seed, "Invalid seed"); s.seed = quint32(seed);
    s.requestRatePerWorkday = number(o,"requestRatePerWorkday"); s.changeRatePerWorkday = number(o,"changeRatePerWorkday");
    s.animationTickMs = integer(o,"animationTickMs",1,60000); s.participantsMin = integer(o,"participantsMin",1,9000); s.participantsMax = integer(o,"participantsMax",1,9000);
    s.leadTimeMinWorkdays = integer(o,"leadTimeMinWorkdays",0,365); s.leadTimeMaxWorkdays = integer(o,"leadTimeMaxWorkdays",0,365);
    require(o["importanceWeights"].toArray().size() == 3, "Invalid importance weights");
    s.importanceWeights.clear(); for (auto v : o["importanceWeights"].toArray()) { require(v.isDouble(), "Invalid weight"); s.importanceWeights.append(v.toDouble()); }
    const auto rules = o["reminders"].toArray(); require(rules.size() == 3, "Invalid reminder rules");
    for (int i = 0; i < 3; ++i) s.reminders[i] = {integer(rules[i].toObject(),"leadDays",0,365), integer(rules[i].toObject(),"periodHours",1,8760)};
    s.conflictPolicy = ConflictPolicy(integer(o,"conflictPolicy",0,2)); s.rescheduleWindowWorkdays = integer(o,"rescheduleWindowWorkdays",0,365);
    s.maxSuggestions = integer(o,"maxSuggestions",1,1000); s.repetitions = integer(o,"repetitions",1,1000);
    // Older configurations did not generate periodic seminars.
    s.periodicSeminars = false;
    if (o.contains("periodicSeminars")) {
        require(o["periodicSeminars"].isBool(), "Invalid periodicSeminars");
        s.periodicSeminars = o["periodicSeminars"].toBool();
        s.seminarPeriodDays = integer(o,"seminarPeriodDays",1,365);
        s.seminarLeadDays = integer(o,"seminarLeadDays",1,365);
        s.seminarTime = QTime::fromString(o["seminarTime"].toString(), Qt::ISODate);
        s.seminarDurationMinutes = integer(o,"seminarDurationMinutes",15,480);
    }
    return s;
}
QJsonObject event(const EventData &e) {
    QJsonArray people; for (int id : e.participantIds) people.append(id);
    QJsonObject o{{"title", e.title}, {"type", int(e.type)}, {"start", e.start.toString(Qt::ISODate)}, {"end", e.end.toString(Qt::ISODate)},
        {"participants", people}, {"importance", int(e.importance)}, {"organizer", e.organizerId}};
    if (e.roomId) o["room"] = *e.roomId;
    if (e.recurrenceTemplateId) o["template"] = QString::number(*e.recurrenceTemplateId);
    return o;
}
EventData event(const QJsonObject &o) {
    EventData e; e.title = o["title"].toString(); e.type = EventType(integer(o,"type",0,4));
    e.start = time(o["start"]); e.end = time(o["end"]); e.importance = Importance(integer(o,"importance",0,2)); e.organizerId = integer(o,"organizer",1,9000);
    require(o["participants"].isArray(), "Missing participants");
    for (auto v : o["participants"].toArray()) { QJsonObject item{{"id", v}}; e.participantIds.append(integer(item,"id",1,9000)); }
    if (o.contains("room")) e.roomId = integer(o,"room",1,10);
    if (o.contains("template")) { bool ok; auto id = o["template"].toString().toULongLong(&ok); require(ok, "Invalid template"); e.recurrenceTemplateId = id; }
    return e;
}
QJsonObject actor(const UserContext &a) {
    QJsonArray departments; auto ids = a.permittedDepartmentIds.values(); std::sort(ids.begin(), ids.end());
    for (int id : ids) departments.append(id);
    return {{"employee", a.employeeId}, {"role", int(a.role)}, {"departments", departments}};
}
UserContext actor(const QJsonObject &o) {
    UserContext a; a.employeeId = integer(o,"employee",0,9000); a.role = UserRole(integer(o,"role",0,2));
    require(o["departments"].isArray(), "Missing actor departments");
    for (auto v : o["departments"].toArray()) { QJsonObject item{{"id", v}}; a.permittedDepartmentIds.insert(integer(item,"id",1,9)); }
    return a;
}
QJsonObject read(const QString &path) {
    QFile file(path); require(file.open(QIODevice::ReadOnly), file.errorString());
    require(file.size() <= 100 * 1024 * 1024, "File exceeds 100 MiB");
    QJsonParseError error; const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    require(error.error == QJsonParseError::NoError && doc.isObject(), "Invalid JSON: " + error.errorString()); return doc.object();
}
void writeBytes(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path); require(file.open(QIODevice::WriteOnly), file.errorString());
    require(file.write(bytes) == bytes.size(), file.errorString()); require(file.commit(), file.errorString());
}
void write(const QString &path, const QJsonObject &object) { writeBytes(path, QJsonDocument(object).toJson(QJsonDocument::Indented)); }
}
