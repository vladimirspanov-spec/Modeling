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
        setup.employeesPerDepartment < 1 || setup.employeesPerDepartment > 1000 ||
        setup.workingDays.size() != 7 || !setup.workingDays.contains(true) ||
        !setup.workStart.isValid() || !setup.workEnd.isValid() || setup.workStart >= setup.workEnd ||
        setup.role < UserRole::Employee || setup.role > UserRole::Secretary)
        throw std::invalid_argument("Invalid calendar setup");
    CompanyCatalog catalog;
    for (int department = 1; department <= setup.departmentCount; ++department) {
        const int leader = (department - 1) * setup.employeesPerDepartment + 1;
        catalog.departments.insert(department, {department, QStringLiteral("Отдел %1").arg(department), leader});
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
