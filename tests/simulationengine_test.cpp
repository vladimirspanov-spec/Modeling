#include "src/simulation/simulationengine.h"
#include <QtTest>

namespace {
CalendarSetup setup(int step = 30) {
    CalendarSetup value;
    value.startDate = QDate(2026, 10, 12);
    value.dayCount = 7;
    value.stepMinutes = step;
    value.initialMeetings = false;
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
    void noFutureGenerationAndLivePreset() {
        auto config = setup(); config.initialMeetings = true;
        SimulationEngine engine(config);
        QVERIFY(engine.calendar().calendar().events.isEmpty());
        QVERIFY(engine.log().isEmpty());
        until(engine, modelDateTime(config.startDate, QTime(8, 30)));
        QVERIFY(engine.log().isEmpty());
        until(engine, modelDateTime(config.startDate, QTime(18, 0)));
        QCOMPARE(engine.calendar().calendar().events.size(), 2);
        for (const auto &entry : engine.log()) QVERIFY(entry.time <= engine.calendar().modelTime());
        for (const auto &event : engine.calendar().calendar().events) QCOMPARE(event.data.start.date(), config.startDate.addDays(1));
    }
    void departmentMeetingsUseSeparateDays() {
        auto config = setup(); config.initialMeetings = true; config.dayCount = 14;
        SimulationEngine engine(config);
        until(engine, engine.calendar().horizonEnd());
        QSet<QDate> days;
        QSet<quint64> departments;
        for (const auto &record : engine.calendar().historySince(0)) {
            const auto &data = record.snapshot.data;
            if (record.action != CalendarAction::Created || data.type != EventType::Planning ||
                !data.recurrenceTemplateId) continue;
            QVERIFY(!days.contains(data.start.date()));
            days.insert(data.start.date());
            departments.insert(*data.recurrenceTemplateId);
            QVERIFY(record.timestamp.secsTo(data.start) >= 5 * 3600);
        }
        QCOMPARE(departments.size(), config.departmentCount);
        QVERIFY(days.size() >= config.departmentCount);
    }
    void planNoticeBoundaryAndLifecycle() {
        SimulationEngine engine(setup());
        const auto created = engine.calendar().modelTime();
        EventData data;
        data.title = "Notice"; data.organizerId = 1; data.participantIds = {1};
        data.start = created.addSecs(5 * 3600 - 1); data.end = data.start.addSecs(1800);
        const UserContext actor{1, UserRole::Secretary, {}};
        QCOMPARE(engine.createEvent(data, actor).error, CalendarError::InvalidTime);
        data.start = created.addSecs(5 * 3600); data.end = data.start.addSecs(1800);
        const auto result = engine.createEvent(data, actor);
        QVERIFY(result.ok());
        until(engine, created.addSecs(4 * 3600 + 1800));
        QCOMPARE(engine.calendar().event(result.eventId)->status, EventStatus::Planned);
        data.title = "Edited while waiting";
        QVERIFY(engine.updateEvent(result.eventId, data, actor).ok());
        auto tooEarly = data; tooEarly.start = created.addSecs(5 * 3600 - 1);
        QCOMPARE(engine.updateEvent(result.eventId, tooEarly, actor).error, CalendarError::InvalidTime);
        QVERIFY(engine.step().ok());
        QCOMPARE(engine.calendar().event(result.eventId)->status, EventStatus::InProgress);
        QVERIFY(engine.step().ok());
        QCOMPARE(engine.calendar().event(result.eventId)->status, EventStatus::Completed);
        QCOMPARE(engine.calendar().history(result.eventId).last().timestamp, data.end);
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
                QVERIFY(notice >= 5 * 3600);
                QCOMPARE(event.status, EventStatus::Planned);
                intervals.insert(notice);
                hours |= notice < 24 * 3600; days |= notice >= 2 * 24 * 3600;
            }
            config.initialMeetings = true;
            SimulationEngine preset(config);
            until(preset, modelDateTime(config.startDate, QTime(18, 0)));
            for (const auto &event : preset.calendar().calendar().events) {
                const auto created = preset.calendar().history(event.id).first().timestamp;
                QVERIFY(created.secsTo(event.data.start) >= 5 * 3600);
                QCOMPARE(event.status, EventStatus::Planned);
                creationTimes.insert(created.time());
            }
        }
        QVERIFY(hours); QVERIFY(days);
        QVERIFY(intervals.size() > 10);
        QVERIFY(creationTimes.size() > 10);
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
    void dependencies() {
        SimulationEngine engine(setup());
        QVERIFY(!engine.trigger(ScenarioKind::Recovery).ok());
        QVERIFY(!engine.trigger(ScenarioKind::ConflictResolved).ok());
        QVERIFY(!engine.trigger(ScenarioKind::RoomRestored).ok());
        QVERIFY(!engine.trigger(ScenarioKind::WorkloadNormalized).ok());
        QVERIFY(engine.trigger(ScenarioKind::ConflictStarted).ok());
        QVERIFY(!engine.trigger(ScenarioKind::ConflictResolved).ok());
        QVERIFY(engine.trigger(ScenarioKind::RoomFailure).ok());
        QVERIFY(!engine.trigger(ScenarioKind::RoomRestored).ok());
        QVERIFY(engine.trigger(ScenarioKind::SickLeave).ok());
        QVERIFY(!engine.trigger(ScenarioKind::Recovery).ok());
        until(engine, modelDateTime(QDate(2026, 10, 13)));
        QVERIFY(engine.trigger(ScenarioKind::Recovery).ok());
        QVERIFY(engine.trigger(ScenarioKind::ConflictResolved).ok());
        QVERIFY(engine.trigger(ScenarioKind::RoomRestored).ok());
        QVERIFY(!engine.trigger(ScenarioKind::RoomRestored).ok());
        QVERIFY(engine.trigger(ScenarioKind::WorkloadSurge).ok());
        QVERIFY(engine.trigger(ScenarioKind::WorkloadNormalized).ok());
        for (const auto &employee : engine.calendar().catalog().employees) QVERIFY(employee.available);
        for (const auto &state : engine.organizations()) QVERIFY(!state.conflictSince && !state.overloaded);
    }
    void stableReferencesAndUniqueTransfers() {
        SimulationEngine engine(setup());
        QCOMPARE(engine.calendar().participantCode(1), QString("1_01"));
        QCOMPARE(engine.calendar().participantCode(9), QString("2_01"));
        EventData data;
        data.title = "History"; data.organizerId = 1; data.participantIds = {1};
        data.start = modelDateTime(QDate(2026, 10, 12), QTime(6, 0));
        data.end = data.start.addSecs(1800);
        const UserContext actor{1, UserRole::Secretary, {}};
        QVERIFY(engine.createEvent(data, actor).ok());
        QVERIFY(engine.transferEmployee(1, 2, actor).ok());
        QCOMPARE(engine.calendar().participantCode(1), QString("2_09"));
        QCOMPARE(engine.calendar().event(1)->data.participantIds, QVector<int>{1});
        QCOMPARE(engine.calendar().event(1)->participantCodes.value(1), QString("2_09"));
        QCOMPARE(engine.calendar().history(1).first().snapshot.participantCodes.value(1), QString("1_01"));
        QVERIFY(!engine.calendar().participantKey("1_01"));
        until(engine, data.end);
        QVERIFY(engine.transferEmployee(1, 1, actor).ok());
        QCOMPARE(engine.calendar().participantCode(1), QString("1_09"));
        QCOMPARE(engine.calendar().event(1)->participantCodes.value(1), QString("2_09"));
        CalendarFilter filter; filter.scope = CalendarScope::Archive; filter.departmentId = 2;
        QCOMPARE(engine.calendar().calendar(filter).events.size(), 1);
        QVERIFY(engine.transferEmployee(1, 2, actor).ok());
        QCOMPARE(engine.calendar().participantCode(1), QString("2_10"));
        QSet<QString> ids;
        for (const auto &person : engine.calendar().catalog().employees) { QVERIFY(!ids.contains(person.publicId)); ids.insert(person.publicId); }
    }
    void absenceAndRoomClosureCancelFutureEvents() {
        auto config = setup();
        CalendarService calendar(makeCompanyCatalog(config), modelDateTime(config.startDate), modelDateTime(config.startDate.addDays(7)));
        EventData data; data.title = "Meeting"; data.organizerId = 1; data.participantIds = {1}; data.roomId = 1;
        data.start = modelDateTime(config.startDate, QTime(10, 0)); data.end = data.start.addSecs(3600);
        const UserContext actor{1, UserRole::Secretary, {}};
        QVERIFY(calendar.createEvent(data, actor).ok());
        QVERIFY(calendar.setEmployeeAvailable(1, false, actor).ok());
        QCOMPARE(calendar.event(1)->status, EventStatus::Cancelled);
        QVERIFY(!calendar.createEvent(data, actor).ok());
        QVERIFY(calendar.setEmployeeAvailable(1, true, actor).ok());
        QVERIFY(calendar.createEvent(data, actor).ok());
        QVERIFY(calendar.setRoomAvailable(1, false, actor).ok());
        QCOMPARE(calendar.event(2)->status, EventStatus::Cancelled);
        QVERIFY(!calendar.createEvent(data, actor).ok());
        QVERIFY(calendar.setRoomAvailable(1, true, actor).ok());
        QVERIFY(calendar.createEvent(data, actor).ok());
    }
    void catalogAndCompletionEffects() {
        QCOMPARE(SimulationEngine::eventCatalog().size(), 19);
        auto config = setup(); config.leadTimeMinWorkdays = config.leadTimeMaxWorkdays = 0;
        SimulationEngine engine(config);
        QVERIFY(engine.trigger(ScenarioKind::Seminar).ok());
        const auto event = engine.calendar().calendar().events.first();
        for (int id : event.data.participantIds) QCOMPARE(engine.participants().value(id).skill, 0);
        until(engine, event.data.end);
        for (int id : event.data.participantIds) QCOMPARE(engine.participants().value(id).skill, 10);
    }
};
QTEST_APPLESS_MAIN(SimulationEngineTest)
#include "simulationengine_test.moc"
