#pragma once
#include "calendarservice.h"
#include <QList>
#include "reminderservice.h"

enum class ConflictPolicy { Reject, Suggest, AutoReschedule };

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
    bool periodicSeminars = true;
    int seminarPeriodDays = 7;
    int seminarLeadDays = 2;
    QTime seminarTime{14, 0};
    int seminarDurationMinutes = 90;
    UserRole role = UserRole::Secretary;
    quint32 seed = 42;
    double requestRatePerWorkday = 6;
    double changeRatePerWorkday = 1;
    int animationTickMs = 250;
    int participantsMin = 2;
    int participantsMax = 8;
    int leadTimeMinWorkdays = 1;
    int leadTimeMaxWorkdays = 5;
    std::array<ReminderRule, 3> reminders{{{1, 24}, {2, 24}, {4, 24}}};
    ConflictPolicy conflictPolicy = ConflictPolicy::Reject;
    int rescheduleWindowWorkdays = 5;
    int maxSuggestions = 3;
    int repetitions = 20;
    QVector<double> importanceWeights{0.5, 0.35, 0.15};
};

QDateTime modelDateTime(const QDate &date, const QTime &time = QTime(0, 0));
CompanyCatalog makeCompanyCatalog(const CalendarSetup &setup);
