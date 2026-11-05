#include "reminderservice.h"
#include <QStringList>
#include <algorithm>

namespace {
QString reminderText(const CalendarEvent &event, const CalendarService &calendar)
{
    const auto &data = event.data;
    const auto organizer = calendar.catalog().employees.value(data.organizerId);
    const QStringList importance{QStringLiteral("Низкая"), QStringLiteral("Средняя"), QStringLiteral("Высокая")};
    const auto room = data.roomId ? calendar.catalog().rooms.value(*data.roomId).name : QStringLiteral("Не требуется");
    return QStringLiteral("Напоминаем о предстоящем мероприятии.\n\n"
                          "Мероприятие: %1\nНачало: %2\nОкончание: %3\n"
                          "Помещение: %4\nОрганизатор: %5 (%6)\nВажность: %7\n\n"
                          "Время указано по модельным часам (UTC+03:00).")
        .arg(data.title, data.start.toString("dd.MM.yyyy HH:mm"), data.end.toString("dd.MM.yyyy HH:mm"),
             room, organizer.name, organizer.publicId, importance.at(int(data.importance)));
}
}

void SimulatedEmailChannel::send(EmailMessage message)
{
    message.deliveredAt = message.sentAt; // Simulation: immediate successful delivery, no network.
    messages_.append(std::move(message));
}

void ReminderService::observe(const QVector<CalendarRecord> &records, const std::array<ReminderRule, 3> &rules)
{
    for (const auto &record : records) {
        const auto &event = record.snapshot;
        if (event.status != EventStatus::Planned) { pending_.remove(event.id); continue; }
        if (pending_.contains(event.id) && pending_[event.id].version == event.scheduleVersion) continue;
        const auto rule = rules.at(int(event.data.importance));
        const auto first = event.data.start.addDays(-rule.leadDays);
        // A single catch-up for late creation, never a batch of expired reminders.
        pending_[event.id] = {event.scheduleVersion, first, std::max(first, record.timestamp), rule.periodHours * 3600};
    }
}

void ReminderService::deliver(const CalendarService &calendar, NotificationChannel &channel)
{
    const auto now = calendar.modelTime();
    if (now >= calendar.horizonEnd()) return;
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        const auto event = calendar.event(it.key());
        if (!event || event->status != EventStatus::Planned) continue;
        while (it->next <= now && it->next < event->data.start) {
            const auto body = reminderText(*event, calendar);
            for (int recipient : event->data.participantIds) {
                channel.send({event->id, event->scheduleVersion, recipient,
                    QStringLiteral("employee%1@work").arg(recipient),
                    QStringLiteral("Напоминание: %1 — %2").arg(event->data.title, event->data.start.toString(Qt::ISODate)),
                    it->next, now, {}, body});
            }
            // Advancing next is the deduplication cursor for this event/version.
            const qint64 periods = it->first.secsTo(it->next) / it->periodSeconds + 1;
            it->next = it->first.addSecs(periods * it->periodSeconds);
        }
    }
}
