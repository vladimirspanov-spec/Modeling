#pragma once
#include "src/services/calendarsetup.h"
#include <random>
#include <QJsonArray>

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

struct RequestRecord {
    quint64 id = 0;
    QDateTime time;
    EventId editedEvent = 0;
    EventData requested;
    UserContext actor;
    CalendarResult result;
    bool participantConflict = false;
    bool roomConflict = false;
    bool pending = false;
    bool selected = false;
    bool automatic = false;
    qint64 shiftSeconds = 0;
    QVector<EventData> alternatives;
    std::optional<ScenarioKind> kind;
};

// No widgets or wall-clock dependency. All modes call this same step().
class SimulationEngine {
public:
    explicit SimulationEngine(CalendarSetup setup, bool legacySchedule = false, bool previousSchedule = false);
    CalendarResult step();
    void reset();
    const CalendarSetup &setup() const { return setup_; }
    const QVector<RequestRecord> &requests() const { return requests_; }
    const QVector<EmailMessage> &emails() const { return email_.messages(); }
    const QJsonArray &commands() const { return commands_; }
    CalendarResult acceptSuggestion(quint64 requestId, int index);
    CalendarResult rejectSuggestion(quint64 requestId);
    void replayCommand(const QJsonObject &command);
    QString randomState() const;

    bool finished() const { return calendar_.modelTime() >= calendar_.horizonEnd(); }
    const CalendarService &calendar() const { return calendar_; }
    const QVector<SimulationLogEntry> &log() const { return log_; }
    const QMap<int, ParticipantState> &participants() const { return participants_; }
    const QMap<int, OrganizationState> &organizations() const { return organizations_; }
    static const QVector<ScenarioDefinition> &eventCatalog();
    static QString generatorVersion() { return QStringLiteral("daily-calendar-v6"); }
    QString modelVersion() const { return legacySchedule_ ? QStringLiteral("live-minute-v4") : previousSchedule_ ? QStringLiteral("live-minute-v5") : generatorVersion(); }
    CalendarResult createEvent(const EventData &data, const UserContext &actor);
    CalendarResult updateEvent(EventId id, const EventData &data, const UserContext &actor);
    CalendarResult cancelEvent(EventId id, const UserContext &actor);
    CalendarResult transferEmployee(int id, int organization, const UserContext &actor);
    CalendarResult assignLeader(int department, int employee, const UserContext &actor);
    // A model command, also used to test incident preconditions without random waiting.
    CalendarResult trigger(ScenarioKind kind);

private:
    CalendarResult submit(const EventData &data, const UserContext &actor, EventId id = 0,
        std::optional<ScenarioKind> kind = {});
    void applyEffects(EventId id, ScenarioKind kind);
    void recordCommand(QJsonObject command);
    void minute(const QDateTime &time);
    void publishCalendar();
    void append(const QString &description, std::optional<ScenarioKind> kind = {}, std::optional<EventId> event = {});
    void dailyMeetings();
    void initializeCalendar();
    void periodicSeminar();
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
    bool previousSchedule_ = false;
    bool initializingCalendar_ = false;
    bool legacySchedule_ = false; // Preserve meeting rules when replaying v4 saves.
    QDate nextSeminarDate_;
    bool internalCommand_ = false;
    QJsonArray commands_;
    QVector<RequestRecord> requests_;
    ReminderService reminders_;
    SimulatedEmailChannel email_;

};
