#include "src/services/calendarservice.h"
#include "src/services/calendarsetup.h"
#include <QtTest>

namespace {
QDateTime at(int hour, int minute = 0) {
    return QDateTime(QDate(2026, 10, 12), QTime(hour, minute), Qt::OffsetFromUTC, 3 * 3600);
}
CompanyCatalog catalog() {
    return {{{1, {1, "A"}}, {2, {2, "B"}}},
            {{1, {1, "One", 1}}, {2, {2, "Two", 1}}, {3, {3, "Three", 2}}},
            {{1, {1, "Room A", 1}}, {2, {2, "Hall", std::nullopt}}}};
}
UserContext secretary() { return {1, UserRole::Secretary, {}}; }
EventData data(int hour = 10, int participant = 1, int room = 1) {
    EventData value;
    value.title = "Meeting";
    value.start = at(hour);
    value.end = at(hour + 1);
    value.participantIds = {participant};
    value.organizerId = 1;
    value.roomId = room;
    return value;
}
}

class CalendarServiceTest : public QObject {
    Q_OBJECT
private slots:
    void initialPresetAndAtomicLoading() {
        CalendarSetup setup;
        setup.startDate = QDate(2026, 10, 12);
        CalendarService service(makeCompanyCatalog(setup), modelDateTime(setup.startDate), modelDateTime(setup.startDate.addDays(14)));
        QVERIFY(loadInitialCalendar(service, setup).ok());
        QCOMPARE(service.calendar().events.size(), 62); // Ten workdays x six departments, two weekly meetings.
        const auto records = service.history().size();
        QCOMPARE(loadInitialCalendar(service, setup).error, CalendarError::Conflict);
        QCOMPARE(service.history().size(), records);
        QCOMPARE(service.calendar().events.size(), 62);
        setup.initialMeetings = false;
        CalendarService empty(makeCompanyCatalog(setup), modelDateTime(setup.startDate), modelDateTime(setup.startDate.addDays(14)));
        QVERIFY(loadInitialCalendar(empty, setup).ok());
        QVERIFY(empty.calendar().events.isEmpty());
        setup.initialMeetings = true;
        setup.workEnd = QTime(9, 30);
        QCOMPARE(loadInitialCalendar(empty, setup).error, CalendarError::InvalidData);
        QVERIFY(empty.calendar().events.isEmpty());
    }
    void createUpdateAndSnapshots() {
        CalendarService service(catalog(), at(0), at(23));
        auto value = data();
        const auto created = service.createEvent(value, secretary());
        QVERIFY(created.ok());
        QCOMPARE(created.eventId, EventId(1));
        auto copy = service.event(1);
        copy->data.title = "external edit";
        QCOMPARE(service.event(1)->data.title, QString("Meeting"));
        value.title = "Renamed";
        QVERIFY(service.updateEvent(1, value, secretary()).ok());
        QCOMPARE(service.event(1)->version, quint64(2));
        QCOMPARE(service.event(1)->scheduleVersion, quint64(1));
        value.start = at(12);
        value.end = at(13);
        QVERIFY(service.updateEvent(1, value, secretary()).ok());
        QCOMPARE(service.event(1)->scheduleVersion, quint64(2));
        QCOMPARE(service.history(1).size(), 3);
        QCOMPARE(service.history(1).first().snapshot.data.title, QString("Meeting"));
        QVERIFY(!service.event(999));
        QCOMPARE(service.updateEvent(999, value, secretary()).error, CalendarError::NotFound);
    }
    void conflictsAndAtomicUpdate() {
        CalendarService service(catalog(), at(0), at(23));
        QVERIFY(service.createEvent(data(), secretary()).ok());
        auto samePerson = data(10, 1, 2);
        QCOMPARE(service.createEvent(samePerson, secretary()).error, CalendarError::Conflict);
        auto sameRoom = data(10, 3, 1);
        QCOMPARE(service.createEvent(sameRoom, secretary()).conflicts, QVector<EventId>{1});
        auto adjacent = data(11);
        QVERIFY(service.createEvent(adjacent, secretary()).ok());
        adjacent.start = at(10, 59);
        QCOMPARE(service.updateEvent(2, adjacent, secretary()).error, CalendarError::Conflict);
        QCOMPARE(service.event(2)->data.start, at(11));
        QCOMPARE(service.event(2)->version, quint64(1));
        QCOMPARE(service.history().size(), 2);
        QVERIFY(service.updateEvent(1, data(), secretary()).ok()); // Must exclude itself.
        auto trip = data(10, 2);
        trip.type = EventType::BusinessTrip;
        trip.roomId.reset();
        QVERIFY(service.createEvent(trip, secretary()).ok());
        QCOMPARE(service.createEvent(data(10, 2, 2), secretary()).error, CalendarError::Conflict);
    }
    void validationAndPermissions() {
        CalendarService service(catalog(), at(0), at(23));
        auto value = data();
        value.end = value.start;
        QCOMPARE(service.createEvent(value, secretary()).error, CalendarError::InvalidData);
        value = data(); value.participantIds = {1, 1};
        QCOMPARE(service.createEvent(value, secretary()).error, CalendarError::InvalidData);
        value.participantIds = {999};
        QCOMPARE(service.createEvent(value, secretary()).error, CalendarError::InvalidData);
        value = data(); value.roomId = 999;
        QCOMPARE(service.createEvent(value, secretary()).error, CalendarError::InvalidData);
        value = data(); value.type = EventType::BusinessTrip;
        QCOMPARE(service.createEvent(value, secretary()).error, CalendarError::InvalidData);
        QVERIFY(service.createEvent(data(), secretary()).ok());
        UserContext employee{1, UserRole::Employee, {}};
        QCOMPARE(service.updateEvent(1, data(), employee).error, CalendarError::PermissionDenied);
        QCOMPARE(service.cancelEvent(1, employee).error, CalendarError::PermissionDenied);
        QVERIFY(service.createEvent(data(12), employee).ok());
        UserContext manager{1, UserRole::Manager, {1}};
        QVERIFY(service.updateEvent(1, data(), manager).ok());
        value = data(); value.participantIds.append(3);
        QCOMPARE(service.updateEvent(1, value, manager).error, CalendarError::PermissionDenied);
        QVERIFY(service.updateEvent(1, value, secretary()).ok());
        QCOMPARE(service.cancelEvent(1, manager).error, CalendarError::PermissionDenied);
        manager.permittedDepartmentIds.insert(2);
        QVERIFY(service.cancelEvent(1, manager).ok());
    }
    void lifecycleAndArchive() {
        CalendarService service(catalog(), at(0), at(23));
        auto value = data(); value.start = at(10, 10); value.end = at(10, 40);
        QVERIFY(service.createEvent(value, secretary()).ok());
        QVERIFY(service.advanceTo(at(10, 10)).ok());
        QCOMPARE(service.event(1)->status, EventStatus::InProgress);
        QCOMPARE(service.cancelEvent(1, secretary()).error, CalendarError::InvalidState);
        QCOMPARE(service.updateEvent(1, value, secretary()).error, CalendarError::InvalidState);
        QVERIFY(service.advanceTo(at(10, 40)).ok());
        QVERIFY(service.calendar().events.isEmpty());
        CalendarFilter archive; archive.scope = CalendarScope::Archive;
        QCOMPARE(service.calendar(archive).events.size(), 1);
        QCOMPARE(service.event(1)->status, EventStatus::Completed);
        QCOMPARE(service.history(1).last().timestamp, at(10, 40));
        QVERIFY(service.advanceTo(at(11)).ok());
        QCOMPARE(service.history(1).size(), 3);
        QCOMPARE(service.advanceTo(at(10)).error, CalendarError::InvalidTime);
        QVERIFY(service.createEvent(data(12), secretary()).ok());
        QVERIFY(service.cancelEvent(2, secretary()).ok());
        QCOMPARE(service.event(2)->status, EventStatus::Cancelled);
        QVERIFY(service.createEvent(data(12), secretary()).ok()); // Cancellation frees resources.
    }
    void filtersAndMultiday() {
        CalendarService service(catalog(), at(0), at(0).addDays(7));
        auto value = data(10, 3, 2); value.end = at(11).addDays(2);
        QVERIFY(service.createEvent(value, secretary()).ok());
        QVERIFY(service.createEvent(data(), secretary()).ok());
        CalendarFilter filter;
        filter.departmentId = 2;
        QCOMPARE(service.calendar(filter).events.size(), 1);
        filter.employeeId = 1;
        QVERIFY(service.calendar(filter).events.isEmpty());
        filter = {}; filter.from = at(0).addDays(1); filter.to = at(0).addDays(2);
        QCOMPARE(service.calendar(filter).events.size(), 1);
        filter.from = value.end; filter.to = value.end.addSecs(3600);
        QVERIFY(service.calendar(filter).events.isEmpty());
        filter.to = filter.from;
        QCOMPARE(service.calendar(filter).result.error, CalendarError::InvalidData);
        QVERIFY(service.cancelEvent(1, secretary()).ok());
        filter = {}; filter.scope = CalendarScope::All; filter.status = EventStatus::Cancelled;
        QCOMPARE(service.calendar(filter).events.size(), 1);
        filter = {}; filter.roomId = 1;
        QCOMPARE(service.calendar(filter).events.first().id, EventId(2));
    }
    void stepIndependenceAndHorizon() {
        auto run = [](int minutes) {
            CalendarService service(catalog(), at(0), at(12));
            auto value = data(); value.start = at(10, 10); value.end = at(10, 40);
            service.createEvent(value, secretary());
            service.createEvent(data(11), secretary());
            service.createEvent(data(12), secretary());
            for (auto time = at(0); time < at(12); ) {
                time = time.addSecs(minutes * 60);
                service.advanceTo(time);
            }
            return service;
        };
        auto halfHour = run(30), hour = run(60);
        const auto a = halfHour.history(), b = hour.history();
        QCOMPARE(a.size(), b.size());
        for (int i = 0; i < a.size(); ++i) {
            QCOMPARE(a[i].timestamp, b[i].timestamp);
            QCOMPARE(a[i].action, b[i].action);
            QCOMPARE(a[i].snapshot.id, b[i].snapshot.id);
            QCOMPARE(a[i].snapshot.status, b[i].snapshot.status);
        }
        QCOMPARE(hour.event(2)->status, EventStatus::Completed);
        QCOMPARE(hour.event(3)->status, EventStatus::Planned); // Starts at horizon are excluded.
        QCOMPARE(hour.createEvent(data(13), secretary()).error, CalendarError::InvalidTime);
        CalendarService boundary(catalog(), at(10), at(12));
        QVERIFY(boundary.createEvent(data(), secretary()).ok());
        QCOMPARE(boundary.event(1)->status, EventStatus::InProgress);
        QVERIFY(boundary.createEvent(data(11), secretary()).ok());
        QVERIFY(boundary.advanceTo(at(12)).ok());
        const auto entries = boundary.history();
        QCOMPARE(entries[3].action, CalendarAction::Completed);
        QCOMPARE(entries[4].action, CalendarAction::Started);
        QCOMPARE(boundary.advanceTo(at(13)).error, CalendarError::InvalidTime);
    }
};
QTEST_APPLESS_MAIN(CalendarServiceTest)
#include "calendarservice_test.moc"
