#include "simulationwindow.h"

#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

SimulationWindow::SimulationWindow(const QDate &startDate, int dayCount,
                                   const QList<bool> &workingDays, QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Моделирование — календарь фирмы"));
    resize(1100, 800);

    auto *layout = new QVBoxLayout(this);
    auto *title = new QLabel(tr("Календарь моделирования"), this);
    auto titleFont = title->font();
    titleFont.setPointSize(20);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);
    layout->addWidget(new QLabel(tr("Период: %1 — %2 · Дней: %3")
        .arg(startDate.toString("dd.MM.yyyy"), startDate.addDays(dayCount - 1).toString("dd.MM.yyyy"))
        .arg(dayCount), this));

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *calendar = new QWidget;
    calendar->setObjectName("simulationCalendar");
    auto *grid = new QGridLayout(calendar);
    grid->setSpacing(8);
    const QStringList weekdays{tr("пн"), tr("вт"), tr("ср"), tr("чт"), tr("пт"), tr("сб"), tr("вс")};
    for (int column = 0; column < 7; ++column) {
        grid->setColumnStretch(column, 1);
        auto *weekdayHeader = new QLabel(weekdays[column], calendar);
        weekdayHeader->setAlignment(Qt::AlignCenter);
        auto font = weekdayHeader->font();
        font.setBold(true);
        weekdayHeader->setFont(font);
        grid->addWidget(weekdayHeader, 0, column);
    }

    const int offset = startDate.dayOfWeek() - 1;
    for (int index = 0; index < dayCount; ++index) {
        const QDate date = startDate.addDays(index);
        const int weekday = date.dayOfWeek() - 1;
        auto *day = new QFrame(calendar);
        day->setObjectName("calendarDay");
        day->setProperty("date", date);
        day->setMinimumSize(130, 150);
        day->setStyleSheet("QFrame#calendarDay { background: white; border: 1px solid #c9c9c9; }");
        auto *dayLayout = new QVBoxLayout(day);
        dayLayout->setContentsMargins(0, 0, 0, 0);
        dayLayout->setSpacing(0);
        auto *header = new QLabel(date.toString("dd.MM.yyyy"), day);
        header->setObjectName("dayHeader");
        header->setAlignment(Qt::AlignCenter);
        header->setMinimumHeight(34);
        const bool isWorkingDay = workingDays.value(weekday, weekday < 5);
        header->setStyleSheet(QString("QLabel#dayHeader { background: %1; color: white; font-weight: bold; border: none; padding: 4px; }")
                                  .arg(isWorkingDay ? "#000000" : "#c62828"));
        dayLayout->addWidget(header);
        dayLayout->addStretch();
        grid->addWidget(day, (offset + index) / 7 + 1, weekday);
    }
    grid->setRowStretch((offset + dayCount - 1) / 7 + 2, 1);
    scroll->setWidget(calendar);
    layout->addWidget(scroll, 1);
}
