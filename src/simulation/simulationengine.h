#pragma once
#include "src/services/calendarsetup.h"
#include <random>

enum class ScenarioKind {
    ClientMeeting, Planning, ManagementMeeting, Seminar, BusinessTrip,
    Interview, Presentation, DocumentSigning, UrgentTask, SickLeave,
    Recovery, ConflictStarted, ConflictResolved, RoomFailure, RoomRestored,
    WorkloadSurge, WorkloadNormalized, EmployeeTransfer, ScheduleChange
};
struct ScenarioDefinition {
    ScenarioKind kind;
    QString title;
    int weight;
    int durationMinutes;
};
struct ParticipantState {
    int stress = 0;
    int skill = 0;
    std::optional<QDateTime> sickSince;
};
struct OrganizationState {
    int workload = 20;
    int reputation = 50;
    std::optional<QDateTime> conflictSince;
    bool overloaded = false;
};
struct SimulationLogEntry {
    quint64 sequence;
    QDateTime time;
    QString description;
    std::optional<ScenarioKind> kind;
    std::optional<EventId> eventId;
};

// No widgets or wall-clock dependency. All modes call this same step().
class SimulationEngine {
public:
    explicit SimulationEngine(CalendarSetup setup);
    CalendarResult step();
    void reset();
    bool finished() const { return calendar_.modelTime() >= calendar_.horizonEnd(); }
    const CalendarService &calendar() const { return calendar_; }
    const QVector<SimulationLogEntry> &log() const { return log_; }
    const QMap<int, ParticipantState> &participants() const { return participants_; }
    const QMap<int, OrganizationState> &organizations() const { return organizations_; }
    static const QVector<ScenarioDefinition> &eventCatalog();
    static QString generatorVersion() { return QStringLiteral("live-minute-v3"); }
    CalendarResult createEvent(const EventData &data, const UserContext &actor);
    CalendarResult updateEvent(EventId id, const EventData &data, const UserContext &actor);
    CalendarResult cancelEvent(EventId id, const UserContext &actor);
    CalendarResult transferEmployee(int id, int organization, const UserContext &actor);
    // A model command, also used to test incident preconditions without random waiting.
    CalendarResult trigger(ScenarioKind kind);

private:
    void minute(const QDateTime &time);
    void publishCalendar();
    void append(const QString &description, std::optional<ScenarioKind> kind = {}, std::optional<EventId> event = {});
    void dailyMeetings();
    void scheduleMeetingCreation(QDate date);
    bool workingMinute(const QDateTime &time) const;
    QVector<int> candidates(ScenarioKind kind) const;
    int pick(const QVector<int> &ids);
    int randomInt(int first, int last);
    CalendarResult schedule(ScenarioKind kind);
    UserContext systemActor() const { return {1, UserRole::Secretary, {}}; }
    CalendarSetup setup_;
    CalendarService calendar_;
    std::mt19937 random_;
    QVector<SimulationLogEntry> log_;
    int historyOffset_ = 0;
    QMap<int, ParticipantState> participants_;
    QMap<int, OrganizationState> organizations_;
    QMap<int, QDateTime> brokenRooms_;
    QMap<EventId, ScenarioKind> effects_;
    QMap<EventId, int> eventOrganizations_;
    QSet<QDate> meetingWeeks_;
    QDateTime nextMeetingCreation_;
    int nextMeetingDepartment_ = 1;
    bool initialized_ = false;
};
