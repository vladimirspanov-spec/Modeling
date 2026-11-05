#pragma once
#include "calendarservice.h"
#include <array>

struct ReminderRule { int leadDays = 1; int periodHours = 24; };
struct EmailMessage {
    EventId eventId;
    quint64 scheduleVersion;
    int recipientId;
    QString recipient;
    QString subject;
    QDateTime dueTime;
    QDateTime sentAt;
    QDateTime deliveredAt;
    QString body; // Snapshot at sending time; later calendar edits do not change sent mail.
};

// The model can use another channel without changing reminder scheduling.
class NotificationChannel {
public:
    virtual ~NotificationChannel() = default;
    virtual void send(EmailMessage message) = 0;
};
class SimulatedEmailChannel : public NotificationChannel {
public:
    void send(EmailMessage message) override;
    const QVector<EmailMessage> &messages() const { return messages_; }
    void clear() { messages_.clear(); }
private:
    QVector<EmailMessage> messages_;
};

class ReminderService {
public:
    void observe(const QVector<CalendarRecord> &records, const std::array<ReminderRule, 3> &rules);
    void deliver(const CalendarService &calendar, NotificationChannel &channel);
    void clear() { pending_.clear(); }
private:
    struct Schedule { quint64 version; QDateTime first; QDateTime next; int periodSeconds; };
    QMap<EventId, Schedule> pending_;
};
