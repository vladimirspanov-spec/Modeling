#include "calendarsetup.h"
#include <stdexcept>

QDateTime modelDateTime(const QDate &date, const QTime &time)
{
    return QDateTime(date, time, Qt::OffsetFromUTC, 3 * 3600);
}

CompanyCatalog makeCompanyCatalog(const CalendarSetup &setup)
{
    if (!setup.startDate.isValid() || setup.dayCount < 7 || setup.dayCount > 30 ||
        !setup.startDate.addDays(setup.dayCount).isValid() ||
        (setup.stepMinutes != 30 && setup.stepMinutes != 60) ||
        setup.departmentCount < 5 || setup.departmentCount > 9 ||
        setup.employeesPerDepartment < 1 || setup.employeesPerDepartment > 100000 ||
        setup.workingDays.size() != 7 || !setup.workingDays.contains(true) ||
        !setup.workStart.isValid() || !setup.workEnd.isValid() || setup.workStart >= setup.workEnd ||
        setup.role < UserRole::Employee || setup.role > UserRole::Secretary)
        throw std::invalid_argument("Invalid calendar setup");
    CompanyCatalog catalog;
    for (int department = 1; department <= setup.departmentCount; ++department) {
        catalog.departments.insert(department, {department, QStringLiteral("Отдел %1").arg(department)});
        catalog.rooms.insert(department, {department, QStringLiteral("Комната отдела %1").arg(department), department});
        for (int index = 1; index <= setup.employeesPerDepartment; ++index) {
            int id = (department - 1) * setup.employeesPerDepartment + index;
            catalog.employees.insert(id, {id, QStringLiteral("Сотрудник %1").arg(id), department});
        }
    }
    int hall = setup.departmentCount + 1;
    catalog.rooms.insert(hall, {hall, QStringLiteral("Конференц-зал"), std::nullopt});
    return catalog;
}

CalendarResult loadInitialCalendar(CalendarService &service, const CalendarSetup &setup)
{
    if (!setup.initialMeetings) return {};
    if (setup.workStart.secsTo(setup.workEnd) < 3600)
        return {CalendarError::InvalidData, QStringLiteral("Для пресета планерок и совещаний нужен рабочий день не короче часа. Выберите пустой календарь или увеличьте рабочее время."), 0, {}};
    // Load atomically: an invalid preset must not leave half a calendar behind.
    auto candidate = service;
    const UserContext secretary{1, UserRole::Secretary, {}};
    QSet<QDate> weeks;
    for (int day = 0; day < setup.dayCount; ++day) {
        const auto date = setup.startDate.addDays(day);
        if (!setup.workingDays.value(date.dayOfWeek() - 1)) continue;
        for (int department = 1; department <= setup.departmentCount; ++department) {
            EventData data;
            data.title = QStringLiteral("Планерка отдела %1").arg(department);
            data.type = EventType::Planning;
            data.start = modelDateTime(date, setup.workStart);
            data.end = data.start.addSecs(30 * 60);
            data.roomId = department;
            data.organizerId = (department - 1) * setup.employeesPerDepartment + 1;
            for (int index = 0; index < setup.employeesPerDepartment; ++index)
                data.participantIds.append(data.organizerId + index);
            data.recurrenceTemplateId = quint64(department);
            const auto result = candidate.createEvent(data, secretary);
            if (!result.ok()) return result;
        }
        const auto week = date.addDays(1 - date.dayOfWeek());
        if (!weeks.contains(week)) {
            weeks.insert(week);
            EventData data;
            data.title = QStringLiteral("Недельное совещание руководителей");
            data.type = EventType::ManagementMeeting;
            data.start = modelDateTime(date, setup.workStart).addSecs(30 * 60);
            data.end = data.start.addSecs(30 * 60);
            data.roomId = setup.departmentCount + 1;
            data.organizerId = 1;
            data.importance = Importance::High;
            data.recurrenceTemplateId = quint64(setup.departmentCount + 1);
            for (int department = 0; department < setup.departmentCount; ++department)
                data.participantIds.append(department * setup.employeesPerDepartment + 1);
            const auto result = candidate.createEvent(data, secretary);
            if (!result.ok()) return result;
        }
    }
    service = std::move(candidate);
    return {};
}
