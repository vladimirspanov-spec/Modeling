#pragma once
#include "calendarservice.h"
#include <QList>

struct CalendarSetup {
    QDate startDate;
    int dayCount = 14;
    int stepMinutes = 30;
    int departmentCount = 6;
    int employeesPerDepartment = 8;
    QList<bool> workingDays{true, true, true, true, true, false, false};
    QTime workStart{9, 0};
    QTime workEnd{18, 0};
    bool initialMeetings = true;
    UserRole role = UserRole::Secretary;
    quint32 seed = 42;
    double requestRatePerWorkday = 6;
    double changeRatePerWorkday = 1;
    int animationTickMs = 250;
    int participantsMin = 2;
    int participantsMax = 8;
    int leadTimeMinWorkdays = 1;
    int leadTimeMaxWorkdays = 5;
    QVector<double> importanceWeights{0.5, 0.35, 0.15};
};

QDateTime modelDateTime(const QDate &date, const QTime &time = QTime(0, 0));
CompanyCatalog makeCompanyCatalog(const CalendarSetup &setup);
CalendarResult loadInitialCalendar(CalendarService &service, const CalendarSetup &setup);
