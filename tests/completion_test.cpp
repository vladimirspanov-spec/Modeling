#include "src/simulation/simulationengine.h"
#include "src/storage/simulationstore.h"
#include "src/storage/jsoncodec.h"
#include "src/reporting/simulationreport.h"
#include "src/experiments/experimentrunner.h"
#include <QtTest>
#include <QTemporaryDir>

namespace {
CalendarSetup config() {
    CalendarSetup s; s.startDate = QDate(2026,10,12); s.initialMeetings = false; s.periodicSeminars = false;
    s.requestRatePerWorkday = s.changeRatePerWorkday = 0;
    s.reminders = {{{1,1}, {2,2}, {4,1}}}; return s;
}
EventData meeting(const CalendarSetup &s, int hour = 10) {
    EventData e; e.title = "Meeting, \"quoted\""; e.start = modelDateTime(s.startDate, QTime(hour,0)); e.end = e.start.addSecs(3600);
    e.roomId = 1; e.participantIds = {1,2}; e.organizerId = 1; e.importance = Importance::Low; return e;
}
UserContext actor() { return {1, UserRole::Secretary, {}}; }
void until(SimulationEngine &e, QDateTime time) { while (!e.finished() && e.calendar().modelTime() < time) QVERIFY(e.step().ok()); }
}
class CompletionTest : public QObject {
    Q_OBJECT
private slots:
    void seminarsAndAppointmentsSurviveSaveAndContinue() {
        auto s=config(); s.periodicSeminars=true; s.dayCount=21;
        s.seminarPeriodDays=3; s.seminarLeadDays=1; s.seminarTime=QTime(15,0); s.seminarDurationMinutes=60;
        SimulationEngine e(s);
        QVERIFY(e.assignLeader(1,2,actor()).ok());
        until(e,modelDateTime(s.startDate.addDays(4)));
        auto restored=SimulationStore::restore(SimulationStore::snapshot(e));
        QCOMPARE(restored->calendar().catalog().departments.value(1).leaderId,2);
        QCOMPARE(JsonCodec::setup(restored->setup()),JsonCodec::setup(s));
        until(e,e.calendar().horizonEnd()); until(*restored,restored->calendar().horizonEnd());
        QCOMPARE(SimulationReport::create(e),SimulationReport::create(*restored));
        auto hourly=s; hourly.stepMinutes=60;
        SimulationEngine h(hourly); QVERIFY(h.assignLeader(1,2,actor()).ok()); until(h,h.calendar().horizonEnd());
        auto a=SimulationReport::create(e), b=SimulationReport::create(h);
        a.remove("configuration"); b.remove("configuration"); QCOMPARE(a,b);
        auto invalid=s; invalid.seminarTime=QTime(17,30);
        QVERIFY_EXCEPTION_THROWN(SimulationEngine{invalid},std::invalid_argument);
        invalid=s; invalid.seminarPeriodDays=0;
        QVERIFY_EXCEPTION_THROWN(SimulationEngine{invalid},std::invalid_argument);
        auto json=JsonCodec::setup(s); json["seminarPeriodDays"]=1.5;
        QVERIFY_EXCEPTION_THROWN(JsonCodec::setup(json),std::runtime_error);
    }
    void initialCalendarAndPreviousGeneratorRoundTrip() {
        auto s = config(); s.initialMeetings = true;
        for (bool previous : {false, true}) {
            SimulationEngine engine(s, false, previous);
            QCOMPARE(engine.calendar().calendar().events.isEmpty(), previous);
            auto restored = SimulationStore::restore(SimulationStore::snapshot(engine));
            QCOMPARE(SimulationReport::create(*restored), SimulationReport::create(engine));
            until(engine, modelDateTime(s.startDate.addDays(1)));
            until(*restored, modelDateTime(s.startDate.addDays(1)));
            QCOMPARE(SimulationReport::create(*restored), SimulationReport::create(engine));
            auto resumed = SimulationStore::restore(SimulationStore::snapshot(engine));
            QCOMPARE(SimulationReport::create(*resumed), SimulationReport::create(engine));
        }
        // Previously planned meetings may start less than five hours after t0.
        s.workStart = QTime(1, 0); s.workEnd = QTime(3, 0);
        SimulationEngine early(s);
        QVERIFY(!early.calendar().calendar().events.isEmpty());
        QCOMPARE(early.calendar().calendar().events.first().data.start.time(), QTime(1, 0));
    }
    void remindersRepeatWithoutDuplicates() {
        auto s = config(); SimulationEngine e(s); auto data = meeting(s); QVERIFY(e.createEvent(data, actor()).ok());
        until(e, modelDateTime(s.startDate, QTime(2,0)));
        QCOMPARE(e.emails().size(), 6); // catch-up at 00:00, fixed grid at 01:00 and 02:00; two recipients.
        QSet<QString> keys;
        for (const auto &mail : e.emails()) {
            auto key = QString::number(mail.eventId) + '/' + QString::number(mail.scheduleVersion) + '/' + QString::number(mail.recipientId) + '/' + mail.dueTime.toString(Qt::ISODate);
            QVERIFY(!keys.contains(key)); keys.insert(key);
            QCOMPARE(mail.sentAt, mail.deliveredAt); QVERIFY(mail.sentAt < data.start);
            QCOMPARE(mail.recipient, QString("employee%1@work").arg(mail.recipientId));
            QVERIFY(mail.body.contains(data.title));
            QVERIFY(mail.body.contains(data.start.toString("dd.MM.yyyy HH:mm")));
        }
        QVERIFY(e.step().ok()); QCOMPARE(e.emails().size(), 6);
    }
    void sentMailRetainsOriginalDetails() {
        auto s = config(); SimulationEngine e(s); auto data = meeting(s);
        const auto created = e.createEvent(data, actor()); QVERIFY(created.ok());
        QVERIFY(e.step().ok());
        const auto original = e.emails().first().body;
        QVERIFY(original.contains(e.calendar().catalog().rooms.value(1).name));
        QVERIFY(original.contains("1_01"));
        data.title = "Changed title";
        data.start = data.start.addSecs(3600); data.end = data.end.addSecs(3600);
        QVERIFY(e.updateEvent(created.eventId, data, actor()).ok());
        QVERIFY(e.step().ok());
        QCOMPARE(e.emails().first().body, original);
        QVERIFY(e.emails().last().body.contains(data.title));
        QVERIFY(e.emails().last().body.contains(data.start.toString("dd.MM.yyyy HH:mm")));
        QCOMPARE(SimulationReport::create(e)["emails"].toArray().first().toObject()["body"].toString(), original);
        QVERIFY(e.cancelEvent(created.eventId, actor()).ok());
        auto restored = SimulationStore::restore(SimulationStore::snapshot(e));
        QCOMPARE(restored->emails().first().body, original);
    }
    void legacyMailStateUpgradesWithoutLosingHistory() {
        const auto path = QFINDTESTDATA("fixtures/legacy-email-state.json");
        QVERIFY(!path.isEmpty());
        const auto legacy = JsonCodec::read(path);
        QCOMPARE(legacy["schemaVersion"].toInt(), 1);
        auto restored = SimulationStore::restore(legacy);
        QCOMPARE(restored->emails().size(), 2);
        QCOMPARE(restored->emails().first().recipient, QString("employee1@work"));
        QVERIFY(restored->emails().first().body.contains("Legacy reminder"));
        const auto upgraded = SimulationStore::snapshot(*restored);
        QCOMPARE(upgraded["schemaVersion"].toInt(), 3);
        auto reloaded = SimulationStore::restore(upgraded);
        QCOMPARE(SimulationReport::create(*restored), SimulationReport::create(*reloaded));
        auto broken = legacy; broken["fingerprint"] = "wrong";
        QVERIFY_EXCEPTION_THROWN(SimulationStore::restore(broken), std::runtime_error);
    }
    void importanceChangesAndCancellation() {
        auto s = config(); s.reminders[0] = {0,1}; SimulationEngine e(s); auto data = meeting(s);
        auto result = e.createEvent(data, actor()); QVERIFY(result.ok()); e.step(); QVERIFY(e.emails().isEmpty());
        data.importance = Importance::High; QVERIFY(e.updateEvent(result.eventId, data, actor()).ok()); e.step();
        QCOMPARE(e.emails().size(), 4); const auto version = e.emails().last().scheduleVersion;
        data.title = "Title only"; QVERIFY(e.updateEvent(result.eventId, data, actor()).ok()); e.step();
        QCOMPARE(e.emails().last().scheduleVersion, version);
        const int before = e.emails().size(); QVERIFY(e.cancelEvent(result.eventId, actor()).ok());
        until(e, data.end); QCOMPARE(e.emails().size(), before);
    }
    void rescheduleReplacesReminderSchedule() {
        auto s = config(); SimulationEngine e(s); auto data = meeting(s); auto result = e.createEvent(data, actor());
        e.step(); QCOMPARE(e.emails().size(), 2);
        data.start = data.start.addDays(2); data.end = data.end.addDays(2);
        QVERIFY(e.updateEvent(result.eventId, data, actor()).ok());
        until(e, modelDateTime(s.startDate.addDays(1), QTime(9,30))); QCOMPARE(e.emails().size(), 2);
        e.step(); QCOMPARE(e.emails().size(), 4); QCOMPARE(e.emails().last().scheduleVersion, quint64(2));
        QCOMPARE(e.emails().last().dueTime, data.start.addDays(-1));
    }
    void rejectSuggestAndAutomatic() {
        for (auto policy : {ConflictPolicy::Reject, ConflictPolicy::Suggest, ConflictPolicy::AutoReschedule}) {
            auto s = config(); s.conflictPolicy = policy; SimulationEngine e(s); auto data = meeting(s);
            QVERIFY(e.createEvent(data, actor()).ok()); auto second = e.createEvent(data, actor());
            QVERIFY(e.requests().last().participantConflict); QVERIFY(e.requests().last().roomConflict);
            if (policy == ConflictPolicy::Reject) { QCOMPARE(second.error, CalendarError::Conflict); QCOMPARE(e.calendar().calendar().events.size(), 1); }
            else if (policy == ConflictPolicy::Suggest) {
                QVERIFY(e.requests().last().pending); QCOMPARE(e.calendar().calendar().events.size(), 1);
                QVERIFY(e.acceptSuggestion(second.requestId, 0).ok()); QCOMPARE(e.calendar().calendar().events.size(), 2);
                QVERIFY(!e.acceptSuggestion(second.requestId, 0).ok());
            } else {
                QVERIFY(second.ok()); QVERIFY(e.calendar().event(second.eventId)->data.start >= data.end);
                QVERIFY(e.requests().last().automatic);
            }
        }
    }
    void staleSuggestionAndNoSlot() {
        auto s = config(); s.conflictPolicy = ConflictPolicy::Suggest; s.rescheduleWindowWorkdays = 0;
        SimulationEngine e(s); auto data = meeting(s); QVERIFY(e.createEvent(data, actor()).ok());
        const auto pending = e.createEvent(data, actor()); const auto candidate = e.requests().last().alternatives.first();
        QVERIFY(e.createEvent(candidate, actor()).ok()); QVERIFY(!e.acceptSuggestion(pending.requestId,0).ok());
        auto full = meeting(s,9); full.end = modelDateTime(s.startDate,QTime(18,0));
        SimulationEngine occupied(s); QVERIFY(occupied.createEvent(full,actor()).ok());
        auto noSlot = occupied.createEvent(full,actor()); QVERIFY(!noSlot.ok()); QVERIFY(!occupied.requests().last().pending);
        // Permissions are validated before any automatic resolution.
        QCOMPARE(occupied.createEvent(full, {2,UserRole::Employee,{1}}).error, CalendarError::PermissionDenied);
    }
    void reportClipsOccupancyAndSeparatesOutcomes() {
        auto s = config(); SimulationEngine e(s); auto data = meeting(s); QVERIFY(e.createEvent(data,actor()).ok());
        QVERIFY(!e.createEvent(data,actor()).ok()); until(e,data.start.addSecs(1800));
        auto metrics = SimulationReport::metrics(e);
        QCOMPARE(metrics["created"].toInt(),1); QCOMPARE(metrics["rejected"].toInt(),1); QCOMPARE(metrics["conflicts"].toInt(),1);
        QCOMPARE(metrics["rooms"].toArray().first().toObject()["busySeconds"].toDouble(),1800.0);
        until(e,data.end); QCOMPARE(SimulationReport::metrics(e)["completed"].toInt(),1);
        const auto csv = SimulationReport::csv(SimulationReport::create(e)); QVERIFY(csv.contains("\"\"title\"\""));
        // CSV encloses JSON in a quoted cell; quotes inside that cell must be doubled.
        QVERIFY(csv.contains(QByteArray("\\") + "\"\"quoted" + "\\" + "\"\""));
    }
    void saveLoadContinuationAndCorruption() {
        auto s = config(); s.requestRatePerWorkday = 12; s.changeRatePerWorkday = 2;
        s.conflictPolicy = ConflictPolicy::Suggest; SimulationEngine e(s);
        auto data = meeting(s); QVERIFY(e.createEvent(data,actor()).ok()); e.createEvent(data,actor());
        QVERIFY(e.acceptSuggestion(2,0).ok()); QVERIFY(e.transferEmployee(1,2,actor()).ok());
        until(e, modelDateTime(s.startDate.addDays(2)));
        QTemporaryDir dir; QVERIFY(dir.isValid()); SimulationStore::save(dir.filePath("state.json"),e);
        auto restored = SimulationStore::load(dir.filePath("state.json"));
        QCOMPARE(SimulationReport::create(*restored),SimulationReport::create(e));
        until(e,e.calendar().horizonEnd()); until(*restored,restored->calendar().horizonEnd());
        QCOMPARE(SimulationReport::create(*restored),SimulationReport::create(e)); QCOMPARE(restored->randomState(),e.randomState());
        auto broken = SimulationStore::snapshot(e); broken["fingerprint"] = "wrong";
        QVERIFY_EXCEPTION_THROWN(SimulationStore::restore(broken),std::runtime_error);
        broken["generator"] = "old"; QVERIFY_EXCEPTION_THROWN(SimulationStore::restore(broken),std::runtime_error);
        SimulationStore::saveConfiguration(dir.filePath("config.json"),s);
        QCOMPARE(JsonCodec::setup(SimulationStore::loadConfiguration(dir.filePath("config.json"))),JsonCodec::setup(s));
    }
    void horizonAndFuturePlans() {
        auto s = config(); s.dayCount = 7; SimulationEngine e(s); auto data = meeting(s); data.start = data.start.addDays(20); data.end = data.end.addDays(20);
        QVERIFY(e.createEvent(data,actor()).ok()); until(e,e.calendar().horizonEnd());
        QCOMPARE(e.calendar().event(1)->status,EventStatus::Planned); QCOMPARE(SimulationReport::metrics(e)["planned"].toInt(),1);
        QVERIFY(e.emails().isEmpty());
    }
    void reproducibilityAcrossStepsAndReset() {
        auto s = config(); s.requestRatePerWorkday = 14; s.changeRatePerWorkday = 2; s.initialMeetings = true; s.conflictPolicy = ConflictPolicy::AutoReschedule;
        SimulationEngine a(s); s.stepMinutes = 60; SimulationEngine b(s);
        until(a,a.calendar().horizonEnd()); until(b,b.calendar().horizonEnd());
        auto ar = SimulationReport::create(a), br = SimulationReport::create(b); ar.remove("configuration"); br.remove("configuration"); QCOMPARE(ar,br);
        b.reset(); until(b,b.calendar().horizonEnd()); auto again = SimulationReport::create(b); again.remove("configuration"); QCOMPARE(again,br);
    }
    void batchAggregationAndDeterminism() {
        auto s = config(); s.requestRatePerWorkday = 6; s.dayCount = 7;
        QVector<ExperimentCase> cases{{"baseline",s}}; ExperimentRunner a(cases,2), b(cases,2);
        while (!a.finished()) a.advance(); while (!b.finished()) b.advance(); QCOMPARE(a.report(),b.report());
        auto report = a.report(); QCOMPARE(report["runs"].toArray().size(),2);
        const auto runs = report["runs"].toArray(); double mean = 0;
        for (auto r : runs) mean += r.toObject()["metrics"].toObject()["created"].toDouble() / 2;
        QCOMPARE(report["aggregates"].toArray().first().toObject()["mean"].toObject()["created"].toDouble(),mean);
        QVERIFY(!a.csv().isEmpty());
    }
    void invalidConfigurationAndState() {
        auto s = config(); s.reminders[0].periodHours = 0; QVERIFY_EXCEPTION_THROWN(SimulationEngine{s},std::invalid_argument);
        s = config(); auto json = JsonCodec::setup(s); json["departmentCount"] = 6.5; QVERIFY_EXCEPTION_THROWN(JsonCodec::setup(json),std::runtime_error);
        json = JsonCodec::setup(s); json["workingDays"] = QJsonArray{true}; QVERIFY_EXCEPTION_THROWN(JsonCodec::setup(json),std::runtime_error);
    }
};
QTEST_APPLESS_MAIN(CompletionTest)
#include "completion_test.moc"
