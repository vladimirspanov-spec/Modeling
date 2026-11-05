#include "src/core/simulationengine.h"
#include <QtTest>

namespace {
CalendarSetup setup(int step = 30) {
    CalendarSetup value;
    value.startDate = QDate(2026, 10, 12);
    value.dayCount = 7;
    value.stepMinutes = step;
    value.initialMeetings = false; value.periodicSeminars = false;
    value.requestRatePerWorkday = 0;
    value.changeRatePerWorkday = 0;
    return value;
}
void until(SimulationEngine &engine, const QDateTime &time) {
    while (!engine.finished() && engine.calendar().modelTime() < time) QVERIFY(engine.step().ok());
}
QStringList transcript(const SimulationEngine &engine) {
    QStringList result;
    for (const auto &entry : engine.log()) result.append(entry.time.toString(Qt::ISODate) + ' ' + entry.description);
    return result;
}
}

class SimulationEngineTest : public QObject {
    Q_OBJECT
private slots:
    void recurringSeminarsAreCreatedGraduallyAndRepeat() {
        auto config = setup(); config.periodicSeminars = true; config.dayCount = 21;
        SimulationEngine engine(config);
        QVERIFY(engine.calendar().calendar().events.isEmpty());
        until(engine,modelDateTime(config.startDate,QTime(8,30)));
        QVERIFY(engine.calendar().calendar().events.isEmpty());
        until(engine,modelDateTime(config.startDate,QTime(9,0)));
        QCOMPARE(engine.calendar().calendar().events.size(),1);
        const auto first = engine.calendar().calendar().events.first();
        QCOMPARE(first.data.start,modelDateTime(config.startDate.addDays(2),QTime(14,0)));
        QCOMPARE(first.data.end,first.data.start.addSecs(90*60));
        QCOMPARE(first.status,EventStatus::Planned);
        QSet<int> departments;
        for (int id:first.data.participantIds) departments.insert(engine.calendar().catalog().employees.value(id).departmentId);
        QVERIFY(departments.size() >= 2);
        until(engine,engine.calendar().horizonEnd());
        QVector<QDateTime> starts;
        for (const auto &r:engine.calendar().history()) {
            if (r.action != CalendarAction::Created || r.snapshot.data.recurrenceTemplateId != 1000) continue;
            starts.append(r.snapshot.data.start);
            QVERIFY(r.timestamp.secsTo(r.snapshot.data.start) >= 5*3600);
        }
        QCOMPARE(starts.size(),3);
        for (int i=1;i<starts.size();++i) QCOMPARE(starts[i-1].daysTo(starts[i]),qint64(7));
        QCOMPARE(engine.calendar().event(first.id)->status,EventStatus::Completed);
    }
    void seminarWeekendsAndConflictsAreHandled() {
        auto config = setup(); config.startDate = QDate(2026,10,16); // Friday.
        config.periodicSeminars = true; config.seminarPeriodDays = 1;
        config.conflictPolicy = ConflictPolicy::Reject;
        SimulationEngine engine(config);
        EventData occupied; occupied.title="Hall reserved"; occupied.organizerId=1; occupied.participantIds={1};
        occupied.start=modelDateTime(config.startDate.addDays(3),QTime(14,0)); occupied.end=occupied.start.addSecs(5400);
        occupied.roomId=config.departmentCount+1;
        QVERIFY(engine.createEvent(occupied,{1,UserRole::Secretary,{}}).ok());
        until(engine,modelDateTime(config.startDate,QTime(9,0)));
        QCOMPARE(engine.requests().last().requested.type,EventType::Seminar);
        QCOMPARE(engine.requests().last().requested.start,occupied.start);
        QCOMPARE(engine.requests().last().result.error,CalendarError::Conflict);
        const int count = engine.requests().size(); engine.step(); QCOMPARE(engine.requests().size(),count);
        until(engine,engine.calendar().horizonEnd());
        QSet<QDate> days;
        for (const auto &r:engine.requests()) if (r.requested.recurrenceTemplateId == 1000) {
            QVERIFY(r.requested.start.date().dayOfWeek() <= 5);
            QVERIFY(!days.contains(r.requested.start.date())); days.insert(r.requested.start.date());
        }
        QVERIFY(days.size()>1);
    }
    void randomManagementMeetingUsesAppointedLeaders() {
        auto config = setup(); config.participantsMin = config.departmentCount;
        SimulationEngine engine(config);
        QVERIFY(engine.trigger(ScenarioKind::ManagementMeeting).ok());
        const auto event = engine.calendar().calendar().events.first();
        QVERIFY(event.data.participantIds.contains(1));
        QVERIFY(!event.data.participantIds.contains(2));
        for (int id : event.data.participantIds) {
            const auto &catalog = engine.calendar().catalog();
            QCOMPARE(catalog.departments.value(catalog.employees.value(id).departmentId).leaderId,id);
        }
    }
    void initialCalendarIsReadyBeforeFirstStep() {
        auto config = setup(); config.initialMeetings = true;
        SimulationEngine engine(config);
        QCOMPARE(engine.calendar().modelTime(), modelDateTime(config.startDate));
        QVERIFY(!engine.calendar().calendar().events.isEmpty());
        QVERIFY(!engine.log().isEmpty());
        const auto initialCount = engine.calendar().calendar().events.size();
        for (const auto &event : engine.calendar().calendar().events) {
            QCOMPARE(event.status, EventStatus::Planned);
            QCOMPARE(engine.calendar().history(event.id).first().timestamp, modelDateTime(config.startDate));
        }
        until(engine, modelDateTime(config.startDate, QTime(8, 30)));
        QCOMPARE(engine.calendar().calendar().events.size(), initialCount);
        engine.reset();
        QCOMPARE(engine.calendar().calendar().events.size(), initialCount);
        config.initialMeetings = false;
        SimulationEngine empty(config);
        QVERIFY(empty.calendar().calendar().events.isEmpty());
    }
    void everyDepartmentMeetsEveryWorkday() {
        for (int count : {5, 9}) {
            auto config = setup(); config.initialMeetings = true; config.dayCount = 14;
            config.departmentCount = count;
            // Include a weekend start and a partial first week.
            config.startDate = QDate(2026, 10, 10);
            SimulationEngine engine(config);
            QMap<QDate, QSet<int>> departments;
            QMap<QDate, int> weekly;
            for (const auto &event : engine.calendar().calendar().events) {
                const auto &data = event.data;
                QVERIFY(data.start.date().dayOfWeek() <= 5);
                QVERIFY(data.start < engine.calendar().horizonEnd());
                QVERIFY(engine.calendar().checkEvent(data, {1, UserRole::Secretary, {}}, event.id).ok());
                if (data.type == EventType::Planning) {
                    const int department = int(*data.recurrenceTemplateId);
                    QVERIFY(!departments[data.start.date()].contains(department));
                    departments[data.start.date()].insert(department);
                    QCOMPARE(data.roomId.value(), department);
                    QCOMPARE(data.start.time(), config.workStart);
                    QCOMPARE(data.start.secsTo(data.end), qint64(1800));
                    QCOMPARE(data.participantIds.size(), config.employeesPerDepartment);
                } else if (data.type == EventType::ManagementMeeting) {
                    ++weekly[data.start.date().addDays(1-data.start.date().dayOfWeek())];
                    QCOMPARE(data.participantIds.size(), count);
                    QCOMPARE(data.start.time(), config.workStart.addSecs(1800));
                }
            }
            QCOMPARE(departments.size(), 10);
            for (const auto &ids : departments) QCOMPARE(ids.size(), count);
            QCOMPARE(weekly.size(), 2);
            for (int meetings : weekly) QCOMPARE(meetings, 1);
            until(engine, engine.calendar().horizonEnd());
            int created = 0;
            for (const auto &record : engine.calendar().history())
                if (record.action == CalendarAction::Created) ++created;
            QCOMPARE(created, 10 * count + 2); // No duplicate generation during the run.
        }
    }
    void shortNoticeAndLifecycle() {
        SimulationEngine engine(setup());
        const auto now = engine.calendar().modelTime();
        const UserContext actor{1, UserRole::Secretary, {}};
        EventData data; data.title="Short notice"; data.organizerId=1; data.participantIds={1};
        data.start=now.addSecs(60); data.end=now.addSecs(1200);
        const auto result=engine.createEvent(data,actor); QVERIFY(result.ok());
        data.start=now.addSecs(120); data.end=now.addSecs(1500);
        QVERIFY(engine.updateEvent(result.eventId,data,actor).ok());
        auto past=data; past.start=now.addSecs(-60);
        QCOMPARE(engine.updateEvent(result.eventId,past,actor).error,CalendarError::InvalidTime);
        QVERIFY(engine.step().ok());
        QCOMPARE(engine.calendar().event(result.eventId)->status,EventStatus::Completed);
        QCOMPARE(engine.calendar().history(result.eventId).last().timestamp,data.end);
    }
    void generatedNoticeAndDiversity() {
        bool hours = false, days = false;
        QSet<qint64> intervals;
        QSet<QTime> creationTimes;
        for (int seed = 1; seed <= 40; ++seed) {
            auto config = setup(); config.seed = seed;
            SimulationEngine engine(config);
            until(engine, modelDateTime(config.startDate, QTime(9, 0)));
            for (const auto &definition : SimulationEngine::eventCatalog()) {
                if (!definition.durationMinutes) continue;
                const auto result = engine.trigger(definition.kind);
                if (!result.ok()) continue; // Resource conflicts remain possible.
                const auto event = engine.calendar().event(result.eventId).value();
                const auto created = engine.calendar().history(result.eventId).first().timestamp;
                const auto notice = created.secsTo(event.data.start);
                QVERIFY(notice >= 0);
                QCOMPARE(event.status, EventStatus::Planned);
                intervals.insert(notice);
                hours |= notice < 24 * 3600; days |= notice >= 2 * 24 * 3600;
            }
            config.initialMeetings = true;
            SimulationEngine preset(config);
            until(preset, modelDateTime(config.startDate, QTime(18, 0)));
            for (const auto &event : preset.calendar().calendar().events) {
                const auto created = preset.calendar().history(event.id).first().timestamp;
                QVERIFY(created.secsTo(event.data.start) >= 0);
                QCOMPARE(event.status, EventStatus::Planned);
                creationTimes.insert(created.time());
            }
        }
        QVERIFY(hours); QVERIFY(days);
        QVERIFY(intervals.size() > 10);
        QCOMPARE(creationTimes.size(), 1); // Initial plans are created at t0.
    }
    void deterministicAcrossStepsAndReset() {
        auto config = setup(); config.requestRatePerWorkday = 25; config.changeRatePerWorkday = 2;
        SimulationEngine half(config);
        config.stepMinutes = 60;
        SimulationEngine hour(config);
        until(half, half.calendar().horizonEnd());
        until(hour, hour.calendar().horizonEnd());
        QVERIFY(half.log().size() > 20);
        QCOMPARE(transcript(half), transcript(hour));
        const auto expected = transcript(hour);
        hour.reset();
        QVERIFY(hour.log().isEmpty());
        QCOMPARE(hour.calendar().modelTime(), modelDateTime(config.startDate));
        until(hour, hour.calendar().horizonEnd());
        QCOMPARE(transcript(hour), expected);
        for (int i = 1; i < hour.log().size(); ++i) QVERIFY(hour.log()[i - 1].time <= hour.log()[i].time);
        QVERIFY(!hour.step().ok());
        QCOMPARE(transcript(hour), expected);
        config.seed = 43;
        SimulationEngine different(config);
        until(different, different.calendar().horizonEnd());
        QVERIFY(transcript(different) != expected);
    }
    void calendarEventsCompleteWithoutPersonnelEffects() {
        QCOMPARE(SimulationEngine::eventCatalog().size(), 6);
        auto config = setup(); config.leadTimeMinWorkdays = config.leadTimeMaxWorkdays = 0;
        SimulationEngine engine(config);
        QVERIFY(engine.trigger(ScenarioKind::Seminar).ok());
        const auto event = engine.calendar().calendar().events.first();
        until(engine, event.data.end);
        QCOMPARE(engine.calendar().event(event.id)->status, EventStatus::Completed);
    }
};
QTEST_APPLESS_MAIN(SimulationEngineTest)
#include "simulationengine_test.moc"
