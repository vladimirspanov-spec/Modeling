#include "simulationwindow.h"

<<<<<<< HEAD
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
=======
#include <QComboBox>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QString statusName(EventStatus status) {
    switch (status) {
    case EventStatus::Planned: return QStringLiteral("Запланировано");
    case EventStatus::InProgress: return QStringLiteral("Идет");
    case EventStatus::Completed: return QStringLiteral("Завершено");
    case EventStatus::Cancelled: return QStringLiteral("Отменено");
    }
    return {};
}
QString participantText(const QVector<int> &participants) {
    QStringList ids;
    for (int id : participants) ids.append(QString::number(id));
    return ids.join(", ");
}
QString cardTitle(const QString &title, const QFontMetrics &metrics) {
    QStringList lines;
    QString line;
    for (const auto &word : title.simplified().split(' ', Qt::SkipEmptyParts)) {
        const auto next = line.isEmpty() ? word : line + ' ' + word;
        if (!line.isEmpty() && metrics.horizontalAdvance(next) > 135) {
            lines.append(line);
            line.clear();
        }
        line = line.isEmpty() ? metrics.elidedText(word, Qt::ElideRight, 135) : line + ' ' + word;
    }
    if (!line.isEmpty()) lines.append(line);
    return lines.join('\n');
}
QString errorText(const CalendarResult &result) {
    QString message = result.message;
    if (!result.conflicts.isEmpty()) {
        QStringList ids;
        for (auto id : result.conflicts) ids.append(QString::number(id));
        message += QStringLiteral("\nКонфликтующие события: %1").arg(ids.join(", "));
    }
    return message;
}
}

SimulationWindow::SimulationWindow(const CalendarSetup &setup, QWidget *parent)
    : QWidget(parent, Qt::Window), setup_(setup),
      service_(makeCompanyCatalog(setup), modelDateTime(setup.startDate), modelDateTime(setup.startDate.addDays(setup.dayCount))),
      actor_{1, setup.role, {1}}
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Моделирование — календарь фирмы"));
    resize(1200, 850);
    auto *layout = new QVBoxLayout(this);
    auto *title = new QLabel(tr("Календарь моделирования"), this);
    auto font = title->font();
    font.setPointSize(20);
    font.setBold(true);
    title->setFont(font);
    layout->addWidget(title);

    auto *actions = new QHBoxLayout;
    addButton_ = new QPushButton(tr("Добавить событие"), this);
    addButton_->setObjectName("addEvent");
    stepButton_ = new QPushButton(tr("Шаг +%1 мин").arg(setup.stepMinutes), this);
    stepButton_->setObjectName("stepCalendar");
    actions->addWidget(addButton_);
    actions->addWidget(stepButton_);
    actions->addStretch();
    layout->addLayout(actions);
    summary_ = new QLabel(this);
    summary_->setObjectName("calendarSummary");
    summary_->setWordWrap(true);
    layout->addWidget(summary_);
    auto *actorLabel = new QLabel(tr("Пользователь: сотрудник 1, отдел 1. Роль: %1. При конфликте заявка отклоняется; подбор переноса пока не подключен.")
        .arg(setup.role == UserRole::Secretary ? tr("Секретарь") : setup.role == UserRole::Manager ? tr("Руководитель") : tr("Сотрудник")), this);
    actorLabel->setWordWrap(true);
    layout->addWidget(actorLabel);

    auto *filters = new QGridLayout;
    scope_ = new QComboBox(this);
    scope_->setObjectName("calendarScope");
    scope_->addItems({tr("Активный календарь"), tr("Архив"), tr("Все события")});
    department_ = new QComboBox(this);
    department_->setObjectName("departmentFilter");
    department_->addItem(tr("Вся фирма"), 0);
    for (const auto &department : service_.catalog().departments) department_->addItem(department.name, department.id);
    employee_ = new QSpinBox(this);
    employee_->setObjectName("employeeFilter");
    employee_->setRange(0, setup.departmentCount * setup.employeesPerDepartment);
    employee_->setSpecialValueText(tr("Все сотрудники"));
    status_ = new QComboBox(this);
    status_->setObjectName("statusFilter");
    status_->addItem(tr("Все статусы"), -1);
    for (auto status : {EventStatus::Planned, EventStatus::InProgress, EventStatus::Completed, EventStatus::Cancelled})
        status_->addItem(statusName(status), int(status));
    from_ = new QDateEdit(setup.startDate, this);
    to_ = new QDateEdit(setup.startDate.addDays(setup.dayCount - 1), this);
    from_->setObjectName("filterFrom");
    to_->setObjectName("filterTo");
    for (auto *date : {from_, to_}) {
        date->setDisplayFormat("dd.MM.yyyy");
        date->setCalendarPopup(true);
        date->setDateRange(setup.startDate, setup.startDate.addDays(setup.dayCount - 1));
    }
    filters->addWidget(scope_, 0, 0);
    filters->addWidget(department_, 0, 1);
    filters->addWidget(new QLabel(tr("ID сотрудника:"), this), 0, 2);
    filters->addWidget(employee_, 0, 3);
    filters->addWidget(status_, 1, 0);
    filters->addWidget(from_, 1, 1);
    filters->addWidget(new QLabel(tr("По дату включительно:"), this), 1, 2);
    filters->addWidget(to_, 1, 3);
    layout->addLayout(filters);
    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    layout->addWidget(scroll_, 1);

    connect(addButton_, &QPushButton::clicked, this, [this] { editEvent(); });
    connect(stepButton_, &QPushButton::clicked, this, [this] { step(); });
    for (auto *combo : {scope_, department_, status_})
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { refreshCalendar(); });
    connect(employee_, qOverload<int>(&QSpinBox::valueChanged), this, [this] { refreshCalendar(); });
    for (auto *date : {from_, to_})
        connect(date, &QDateEdit::dateChanged, this, [this] { refreshCalendar(); });

    const auto loaded = loadInitialCalendar(service_, setup);
    refreshCalendar();
    if (!loaded.ok()) {
        auto *warning = new QLabel(tr("Начальный календарь не загружен: %1").arg(errorText(loaded)), this);
        warning->setWordWrap(true);
        layout->insertWidget(3, warning);
    }
}

void SimulationWindow::step()
{
    const auto next = std::min(service_.modelTime().addSecs(setup_.stepMinutes * 60), service_.horizonEnd());
    const auto result = service_.advanceTo(next);
    if (!result.ok()) QMessageBox::warning(this, tr("Модельное время"), errorText(result));
    refreshCalendar();
}

void SimulationWindow::refreshCalendar()
{
    CalendarFilter filter;
    filter.scope = static_cast<CalendarScope>(scope_->currentIndex());
    if (department_->currentData().toInt()) filter.departmentId = department_->currentData().toInt();
    if (employee_->value()) filter.employeeId = employee_->value();
    if (status_->currentData().toInt() >= 0) filter.status = static_cast<EventStatus>(status_->currentData().toInt());
    filter.from = modelDateTime(from_->date());
    filter.to = modelDateTime(to_->date().addDays(1));
    const auto result = service_.calendar(filter);
    CalendarFilter archiveFilter;
    archiveFilter.scope = CalendarScope::Archive;
    summary_->setText(tr("Модельное время: %1 (UTC+03:00) · Активных: %2 · В архиве: %3 · По фильтру: %4%5")
        .arg(service_.modelTime().toOffsetFromUtc(10800).toString("dd.MM.yyyy HH:mm"))
        .arg(service_.calendar().events.size()).arg(service_.calendar(archiveFilter).events.size())
        .arg(result.events.size()).arg(result.result.ok() ? QString() : "\n" + result.result.message));
    const bool finished = service_.modelTime() == service_.horizonEnd();
    stepButton_->setEnabled(!finished);
    addButton_->setEnabled(!finished);
    if (finished) summary_->setText(summary_->text() + tr("\nПериод завершен. Незавершенные и будущие события остаются в активном календаре."));

>>>>>>> 19d33f3 (added calender system)
    auto *calendar = new QWidget;
    calendar->setObjectName("simulationCalendar");
    auto *grid = new QGridLayout(calendar);
    grid->setSpacing(8);
    const QStringList weekdays{tr("пн"), tr("вт"), tr("ср"), tr("чт"), tr("пт"), tr("сб"), tr("вс")};
    for (int column = 0; column < 7; ++column) {
        grid->setColumnStretch(column, 1);
<<<<<<< HEAD
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
=======
        auto *header = new QLabel(weekdays[column], calendar);
        header->setAlignment(Qt::AlignCenter);
        grid->addWidget(header, 0, column);
    }
    const auto first = from_->date();
    const int dayCount = int(first.daysTo(to_->date())) + 1;
    const int offset = first.dayOfWeek() - 1;
    for (int index = 0; index < dayCount && result.result.ok(); ++index) {
        const auto date = first.addDays(index);
        const auto begin = modelDateTime(date), end = begin.addDays(1);
>>>>>>> 19d33f3 (added calender system)
        const int weekday = date.dayOfWeek() - 1;
        auto *day = new QFrame(calendar);
        day->setObjectName("calendarDay");
        day->setProperty("date", date);
        day->setMinimumSize(130, 150);
        day->setStyleSheet("QFrame#calendarDay { background: white; border: 1px solid #c9c9c9; }");
        auto *dayLayout = new QVBoxLayout(day);
<<<<<<< HEAD
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
=======
        dayLayout->setContentsMargins(3, 3, 3, 3);
        auto *header = new QLabel(date.toString("dd.MM.yyyy"), day);
        header->setObjectName("dayHeader");
        header->setAlignment(Qt::AlignCenter);
        header->setStyleSheet(QString("background: %1; color: white; font-weight: bold; padding: 4px;")
            .arg(setup_.workingDays.value(weekday) ? "#000000" : "#c62828"));
        dayLayout->addWidget(header);
        for (const auto &event : result.events) {
            if (event.data.start >= end || event.data.end <= begin) continue;
            auto *button = new QPushButton(day);
            button->setObjectName("calendarEvent");
            button->setProperty("eventId", QVariant::fromValue<qulonglong>(event.id));
            button->setText(tr("%1–%2\n%3\n%4")
                .arg(std::max(event.data.start, begin).toOffsetFromUtc(10800).toString("HH:mm"))
                .arg(event.data.end >= end ? QStringLiteral("24:00") : event.data.end.toOffsetFromUtc(10800).toString("HH:mm"))
                .arg(cardTitle(event.data.title, button->fontMetrics())).arg(statusName(event.status)));
            button->setToolTip(tr("ID: %1\n%2 — %3\nУчастники: %4\n%5\n%6")
                .arg(event.id).arg(event.data.start.toString(Qt::ISODate), event.data.end.toString(Qt::ISODate))
                .arg(participantText(event.data.participantIds))
                .arg(event.data.roomId ? service_.catalog().rooms.value(*event.data.roomId).name : tr("Без помещения"))
                .arg(event.data.title));
            button->setStyleSheet(event.status == EventStatus::InProgress ? "background: #d2efdb; color: #102a18;" : "background: #e9eef7; color: #182438;");
            connect(button, &QPushButton::clicked, this, [this, id = event.id] { editEvent(id); });
            dayLayout->addWidget(button);
        }
        dayLayout->addStretch();
        grid->addWidget(day, (offset + index) / 7 + 1, weekday);
    }
    scroll_->setWidget(calendar); // Scroll area deletes the previous presentation, never the events.
}

void SimulationWindow::editEvent(std::optional<EventId> id)
{
    const auto existing = id ? service_.event(*id) : std::nullopt;
    if (id && !existing) return;
    QDialog dialog(this);
    dialog.setObjectName("eventDialog");
    dialog.setWindowTitle(existing ? tr("Событие №%1 — %2").arg(*id).arg(statusName(existing->status)) : tr("Новое событие"));
    auto *form = new QFormLayout(&dialog);
    auto *title = new QLineEdit(&dialog);
    title->setObjectName("eventTitle");
    auto *type = new QComboBox(&dialog);
    type->setObjectName("eventType");
    type->addItems({tr("Планерка"), tr("Совещание"), tr("Семинар"), tr("Командировка"), tr("Другое")});
    auto *start = new QDateTimeEdit(&dialog);
    auto *end = new QDateTimeEdit(&dialog);
    start->setObjectName("eventStart");
    end->setObjectName("eventEnd");
    for (auto *date : {start, end}) {
        date->setDisplayFormat("dd.MM.yyyy HH:mm");
        date->setCalendarPopup(true);
    }
    auto *room = new QComboBox(&dialog);
    room->setObjectName("eventRoom");
    room->addItem(tr("Без помещения"), 0);
    for (const auto &item : service_.catalog().rooms) room->addItem(item.name, item.id);
    auto *participants = new QLineEdit(&dialog);
    participants->setObjectName("eventParticipants");
    participants->setPlaceholderText(tr("ID через запятую, например: 1, 2, 3"));
    auto *importance = new QComboBox(&dialog);
    importance->setObjectName("eventImportance");
    importance->addItems({tr("Низкая"), tr("Средняя"), tr("Высокая")});
    auto *organizer = new QSpinBox(&dialog);
    organizer->setObjectName("eventOrganizer");
    organizer->setRange(1, setup_.departmentCount * setup_.employeesPerDepartment);
    EventData initial;
    initial.title = tr("Новое событие");
    initial.start = service_.modelTime().addSecs(setup_.stepMinutes * 60);
    initial.end = initial.start.addSecs(3600);
    initial.participantIds = {actor_.employeeId};
    initial.organizerId = actor_.employeeId;
    if (existing) initial = existing->data;
    title->setText(initial.title);
    type->setCurrentIndex(int(initial.type));
    // Editors hold civil fields; conversion to the model's fixed UTC+03:00 is explicit on save.
    start->setDate(initial.start.toOffsetFromUtc(10800).date());
    start->setTime(initial.start.toOffsetFromUtc(10800).time());
    end->setDate(initial.end.toOffsetFromUtc(10800).date());
    end->setTime(initial.end.toOffsetFromUtc(10800).time());
    room->setCurrentIndex(room->findData(initial.roomId.value_or(0)));
    participants->setText(participantText(initial.participantIds));
    importance->setCurrentIndex(int(initial.importance));
    organizer->setValue(initial.organizerId);
    form->addRow(tr("Название"), title);
    form->addRow(tr("Тип"), type);
    form->addRow(tr("Начало (UTC+03:00)"), start);
    form->addRow(tr("Конец (UTC+03:00)"), end);
    form->addRow(tr("Помещение"), room);
    form->addRow(tr("Участники"), participants);
    form->addRow(tr("Важность"), importance);
    form->addRow(tr("ID организатора"), organizer);
    auto *help = new QLabel(tr("Сотрудники: ID 1–%1. В каждом отделе %2 сотрудников; ID идут подряд по отделам.")
        .arg(setup_.departmentCount * setup_.employeesPerDepartment).arg(setup_.employeesPerDepartment), &dialog);
    help->setWordWrap(true);
    form->addRow(help);
    const bool editable = (!existing || existing->status == EventStatus::Planned) && service_.modelTime() < service_.horizonEnd();
    for (QWidget *field : QVector<QWidget *>{title, type, start, end, room, participants, importance, organizer}) field->setEnabled(editable);
    auto *error = new QLabel(&dialog);
    error->setObjectName("eventError");
    error->setWordWrap(true);
    form->addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (editable) {
        auto *save = buttons->addButton(tr("Сохранить"), QDialogButtonBox::ActionRole);
        save->setObjectName("saveEvent");
        connect(save, &QPushButton::clicked, &dialog, [&] {
            EventData data = initial;
            data.title = title->text();
            data.type = static_cast<EventType>(type->currentIndex());
            data.start = modelDateTime(start->date(), start->time());
            data.end = modelDateTime(end->date(), end->time());
            data.roomId = room->currentData().toInt() ? std::optional<int>(room->currentData().toInt()) : std::nullopt;
            data.participantIds.clear();
            for (const auto &token : participants->text().split(',')) {
                bool ok = false;
                const int participant = token.trimmed().toInt(&ok);
                if (!ok) { error->setText(tr("Введите ID участников через запятую.")); return; }
                data.participantIds.append(participant);
            }
            data.importance = static_cast<Importance>(importance->currentIndex());
            data.organizerId = organizer->value();
            const auto result = id ? service_.updateEvent(*id, data, actor_) : service_.createEvent(data, actor_);
            if (!result.ok()) { error->setText(errorText(result)); return; }
            dialog.accept();
        });
        if (existing) {
            auto *cancel = buttons->addButton(tr("Отменить событие"), QDialogButtonBox::ActionRole);
            cancel->setObjectName("cancelEvent");
            connect(cancel, &QPushButton::clicked, &dialog, [&] {
                const auto result = service_.cancelEvent(*id, actor_);
                if (!result.ok()) { error->setText(errorText(result)); return; }
                dialog.accept();
            });
        }
    }
    if (existing) {
        form->addRow(new QLabel(tr("Версия: %1 · Записей в истории: %2")
            .arg(existing->version).arg(service_.history(*id).size()), &dialog));
    }
    form->addRow(buttons);
    dialog.exec();
    // Defer rebuilding: the button whose click opened this dialog may still be emitting clicked().
    QMetaObject::invokeMethod(this, [this] { refreshCalendar(); }, Qt::QueuedConnection);
>>>>>>> 19d33f3 (added calender system)
}
