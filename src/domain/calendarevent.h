#pragma once

#include <QDateTime>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>
#include <optional>

using EventId = quint64;
enum class EventStatus { Planned, InProgress, Completed, Cancelled };
enum class EventType { Planning, ManagementMeeting, Seminar, BusinessTrip, Other };
enum class Importance { Low, Medium, High };
enum class UserRole { Employee, Manager, Secretary };

struct Department { int id; QString name; };
struct Employee {
    int id; // Stable internal key: calendar references survive transfers.
    QString name;
    int departmentId;
    QString publicId; // Organization_sequence, never reused during a run.
    bool available = true;
};
struct Room { int id; QString name; std::optional<int> departmentId; };
struct CompanyCatalog {
    QMap<int, Department> departments;
    QMap<int, Employee> employees;
    QMap<int, Room> rooms;
};
struct UserContext {
    int employeeId = 0;
    UserRole role = UserRole::Employee;
    QSet<int> permittedDepartmentIds;
};

// Editable data is separate from service-owned identity and lifecycle fields.
struct EventData {
    QString title;
    EventType type = EventType::Other;
    QDateTime start;
    QDateTime end;
    std::optional<int> roomId;
    QVector<int> participantIds;
    Importance importance = Importance::Medium;
    int organizerId = 0;
    std::optional<quint64> recurrenceTemplateId;
};
struct CalendarEvent {
    EventId id = 0;
    quint64 version = 1;
    quint64 scheduleVersion = 1;
    EventData data;
    EventStatus status = EventStatus::Planned;
    QMap<int, QString> participantCodes;
    QMap<int, int> participantDepartments;
    QString organizerCode;
};

enum class CalendarAction { Created, Updated, Cancelled, Started, Completed };
struct CalendarRecord {
    quint64 sequence;
    QDateTime timestamp;
    CalendarAction action;
    int actorId; // 0 for automatic lifecycle transitions.
    CalendarEvent snapshot;
    QString actorCode;
};

enum class CalendarError { None, InvalidData, NotFound, PermissionDenied, InvalidState, Conflict, InvalidTime };
struct CalendarResult {
    CalendarError error = CalendarError::None;
    QString message;
    EventId eventId = 0;
    QVector<EventId> conflicts;
    bool ok() const { return error == CalendarError::None; }
};

enum class CalendarScope { Active, Archive, All };
struct CalendarFilter {
    CalendarScope scope = CalendarScope::Active;
    std::optional<int> departmentId;
    std::optional<int> employeeId;
    std::optional<int> roomId;
    std::optional<EventStatus> status;
    std::optional<QDateTime> from;
    std::optional<QDateTime> to; // Intersection with [from, to), not just start dates.
};
struct CalendarQueryResult {
    CalendarResult result;
    QVector<CalendarEvent> events;
};
