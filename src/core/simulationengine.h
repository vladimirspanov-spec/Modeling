#pragma once
#include "src/core/calendarsetup.h"
#include <random>
#include <QJsonArray>

enum class ScenarioKind { ClientMeeting, Planning, ManagementMeeting, Seminar, BusinessTrip, ScheduleChange };
struct ScenarioDefinition {
    ScenarioKind kind;
    QString title;
    int weight;
    int durationMinutes;
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
    explicit SimulationEngine(CalendarSetup setup);
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
    static const QVector<ScenarioDefinition> &eventCatalog();
    static QString generatorVersion() { return QStringLiteral("calendar-v7"); }
    QString modelVersion() const { return generatorVersion(); }
    CalendarResult createEvent(const EventData &data, const UserContext &actor);
    CalendarResult updateEvent(EventId id, const EventData &data, const UserContext &actor);
    CalendarResult cancelEvent(EventId id, const UserContext &actor);
    // A reproducible calendar scenario command.
    CalendarResult trigger(ScenarioKind kind);

private:
    CalendarResult submit(const EventData &data, const UserContext &actor, EventId id = 0,
        std::optional<ScenarioKind> kind = {});
    void recordCommand(QJsonObject command);
    void minute(const QDateTime &time);
    void publishCalendar();
    void append(const QString &description, std::optional<ScenarioKind> kind = {}, std::optional<EventId> event = {});
    void initializeCalendar();
    void periodicSeminar();
    bool workingMinute(const QDateTime &time) const;
    int pick(const QVector<int> &ids);
    int randomInt(int first, int last);
    CalendarResult schedule(ScenarioKind kind);
    UserContext systemActor() const { return {1, UserRole::Secretary, {}}; }
    CalendarSetup setup_;
    CalendarService calendar_;
    std::mt19937 random_;
    QVector<SimulationLogEntry> log_;
    int historyOffset_ = 0;
    QSet<QDate> meetingWeeks_;
    bool initialized_ = false;
    QDate nextSeminarDate_;
    bool internalCommand_ = false;
    QJsonArray commands_;
    QVector<RequestRecord> requests_;
    ReminderService reminders_;
    SimulatedEmailChannel email_;

};
