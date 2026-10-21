#include "simulationwindow.h"
#include "officescene.h"

#include <QComboBox>
#include <QCheckBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFontMetrics>
#include <QPainter>
#include <QTextLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGraphicsOpacityEffect>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsTextItem>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
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
QString participantText(const QVector<int> &participants, const QMap<int, QString> &codes) {
    QStringList ids;
    for (int id : participants) ids.append(codes.value(id));
    return ids.join(", ");
}
// Keep QPushButton semantics (keyboard activation and accessible text), while
// drawing a card whose text is measured against its actual available width.
class CalendarEventCard : public QPushButton {
public:
    CalendarEventCard(const QString &time, const QString &title, EventStatus status, QWidget *parent)
        : QPushButton(parent), time_(time), title_(title.simplified()), status_(status) {
        setText(time + '\n' + title + '\n' + statusName(status));
        setAccessibleName(text());
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        setFixedHeight(106);
        setAttribute(Qt::WA_Hover);
    }
    QSize sizeHint() const override { return {160, 106}; }
    QSize minimumSizeHint() const override { return {0, 106}; }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool active = status_ == EventStatus::InProgress;
        const bool archived = status_ == EventStatus::Completed || status_ == EventStatus::Cancelled;
        const QColor accent = active ? QColor("#258454") : archived ? QColor("#8290a3") : QColor("#5277c3");
        const QColor background = active ? QColor("#edf8f1") : archived ? QColor("#f3f4f6") : QColor("#eff4fc");
        painter.setBrush(isDown() ? background.darker(108) : underMouse() ? background.darker(103) : background);
        painter.setPen(QPen(hasFocus() ? accent : QColor("#dce3ed"), hasFocus() ? 2 : 1));
        painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 7, 7);
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        painter.drawRoundedRect(QRectF(5, 10, 3, height() - 20), 1.5, 1.5);
        const int left = 16, available = qMax(0, width() - left - 10);
        QFont heading = font();
        heading.setBold(true);
        painter.setFont(heading);
        painter.setPen(QColor("#23344d"));
        painter.drawText(QRect(left, 9, available, 20), Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(heading).elidedText(time_, Qt::ElideRight, available));
        painter.setFont(font());
        QTextLayout titleLayout(title_, font());
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        titleLayout.setTextOption(option);
        titleLayout.beginLayout();
        for (int row = 0; row < 2; ++row) {
            auto line = titleLayout.createLine();
            if (!line.isValid()) break;
            line.setLineWidth(available);
            const auto fragment = row == 1 ? title_.mid(line.textStart())
                                          : title_.mid(line.textStart(), line.textLength());
            painter.drawText(QRect(left, 33 + row * 21, available, 21), Qt::AlignLeft | Qt::AlignVCenter,
                             fontMetrics().elidedText(fragment, Qt::ElideRight, available));
        }
        titleLayout.endLayout();
        painter.setPen(accent);
        painter.drawText(QRect(left, 79, available, 20), Qt::AlignLeft | Qt::AlignVCenter,
                         fontMetrics().elidedText(statusName(status_), Qt::ElideRight, available));
    }
private:
    QString time_, title_;
    EventStatus status_;
};
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
    : QWidget(parent, Qt::Window), setup_(setup), engine_(setup), actor_{1, setup.role, {1}}
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Моделирование — календарь фирмы"));
    resize(1120, 760);
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(8);
    timer_ = new QTimer(this);
    timer_->setObjectName("simulationTimer");
    timer_->setTimerType(Qt::PreciseTimer);
    auto *actions = new QHBoxLayout;
    startButton_ = new QPushButton(tr("▶ Запустить"), this);
    startButton_->setObjectName("startSimulation");
    startButton_->setMinimumWidth(125);
    pauseButton_ = new QPushButton(tr("⏸ Пауза"), this);
    pauseButton_->setObjectName("pauseSimulation");
    stepButton_ = new QPushButton(tr("Следующий шаг · %1 мин").arg(setup.stepMinutes), this);
    stepButton_->setObjectName("stepCalendar");
    auto *resetButton = new QPushButton(tr("↺ Сброс"), this);
    resetButton->setObjectName("resetSimulation");
    speed_ = new QComboBox(this);
    speed_->setObjectName("simulationSpeed");
    for (double speed : {0.5, 1.0, 2.0, 5.0}) speed_->addItem(QString::number(speed) + QStringLiteral("×"), speed);
    speed_->setCurrentIndex(1);
    addButton_ = new QPushButton(tr("+ Событие"), this);
    addButton_->setObjectName("addEvent");
    for (auto *button : {startButton_, pauseButton_, stepButton_, resetButton}) actions->addWidget(button);
    actions->addWidget(new QLabel(tr("Скорость"), this));
    actions->addWidget(speed_);
    actions->addStretch();
    actions->addWidget(addButton_);
    layout->addLayout(actions);
    summary_ = new QLabel(this);
    summary_->setObjectName("calendarSummary");
    summary_->setWordWrap(true);
    layout->addWidget(summary_);
    actorLabel_ = new QLabel(this);
    actorLabel_->setWordWrap(true);
    layout->addWidget(actorLabel_);

    tabs_ = new QTabWidget(this);
    layout->addWidget(tabs_, 1);
    auto table = [this](const QStringList &headers, const QString &name) {
        auto *view = new QTableWidget(0, headers.size(), this);
        view->setObjectName(name);
        view->setHorizontalHeaderLabels(headers);
        view->setEditTriggers(QAbstractItemView::NoEditTriggers);
        view->setSelectionBehavior(QAbstractItemView::SelectRows);
        view->setSelectionMode(QAbstractItemView::SingleSelection);
        view->verticalHeader()->hide();
        view->setAlternatingRowColors(true);
        view->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        view->horizontalHeader()->setStretchLastSection(true);
        return view;
    };
    journal_ = table({tr("Модельное время"), tr("Что произошло")}, "simulationLog");
    journal_->setWordWrap(true);
    tabs_->addTab(journal_, tr("Журнал"));
    connect(journal_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const auto id = journal_->item(row, 0)->data(Qt::UserRole).toULongLong();
        if (id) editEvent(id);
    });

    auto *calendarPage = new QWidget(this);
    auto *calendarLayout = new QVBoxLayout(calendarPage);
    auto *filters = new QGridLayout;
    scope_ = new QComboBox(this);
    scope_->setObjectName("calendarScope");
    scope_->addItems({tr("Активные"), tr("Архив"), tr("Все события")});
    department_ = new QComboBox(this);
    department_->setObjectName("departmentFilter");
    department_->addItem(tr("Все организации / отделы"), 0);
    for (const auto &department : engine_.calendar().catalog().departments) department_->addItem(department.name, department.id);
    employee_ = new QComboBox(this);
    employee_->setObjectName("employeeFilter");
    status_ = new QComboBox(this);
    status_->setObjectName("statusFilter");
    status_->addItem(tr("Все статусы"), -1);
    for (auto status : {EventStatus::Planned, EventStatus::InProgress, EventStatus::Completed, EventStatus::Cancelled})
        status_->addItem(statusName(status), int(status));
    from_ = new QDateEdit(setup.startDate, this);
    to_ = new QDateEdit(setup.startDate.addDays(setup.dayCount - 1), this);
    from_->setObjectName("filterFrom"); to_->setObjectName("filterTo");
    for (auto *date : {from_, to_}) {
        date->setDisplayFormat("dd.MM.yyyy"); date->setCalendarPopup(true);
        date->setDateRange(setup.startDate, setup.startDate.addDays(setup.dayCount - 1));
    }
    showPlanned_ = new QCheckBox(tr("Показывать запланированные"), this);
    showPlanned_->setObjectName("showPlanned");
    showPlanned_->setChecked(true);
    filters->addWidget(scope_, 0, 0); filters->addWidget(department_, 0, 1); filters->addWidget(employee_, 0, 2);
    filters->addWidget(status_, 1, 0); filters->addWidget(from_, 1, 1); filters->addWidget(to_, 1, 2);
    filters->addWidget(showPlanned_, 2, 0, 1, 3);
    calendarLayout->addLayout(filters);
    scroll_ = new QScrollArea(this); scroll_->setWidgetResizable(true);
    calendarLayout->addWidget(scroll_, 1);
    tabs_->addTab(calendarPage, tr("Календарь и архив"));

    auto *officePage = new QWidget(this);
    auto *officeLayout = new QVBoxLayout(officePage);
    auto *officeHelp = new QLabel(tr("План офиса · Сотрудники работают в своих отделах, уходят на встречи и в командировки. Заболевшие показаны отдельно."), officePage);
    officeHelp->setWordWrap(true);
    officeLayout->addWidget(officeHelp);
    officeScene_ = new QGraphicsScene(officePage);
    officeScene_->setObjectName("officeScene");
    officeAnimator_ = new OfficeAnimator(*officeScene_);
    auto *officeView = new QGraphicsView(officeScene_, officePage);
    officeView->setObjectName("officeView");
    officeView->setRenderHint(QPainter::Antialiasing);
    officeView->setBackgroundBrush(QColor("#f1f5f9"));
    officeView->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    officeView->setDragMode(QGraphicsView::ScrollHandDrag);
    auto *officeTools = new QHBoxLayout;
    auto *fitOffice = new QPushButton(tr("Вписать схему"), officePage);
    fitOffice->setObjectName("fitOffice");
    auto *zoomOutOffice = new QPushButton(tr("−"), officePage);
    auto *zoomInOffice = new QPushButton(tr("+"), officePage);
    zoomOutOffice->setToolTip(tr("Уменьшить схему"));
    zoomInOffice->setToolTip(tr("Увеличить схему"));
    zoomOutOffice->setFixedWidth(36);
    zoomInOffice->setFixedWidth(36);
    officeTools->addStretch();
    officeTools->addWidget(zoomOutOffice);
    officeTools->addWidget(zoomInOffice);
    officeTools->addWidget(fitOffice);
    officeLayout->addLayout(officeTools);
    connect(fitOffice, &QPushButton::clicked, officeView, [officeView] {
        officeView->fitInView(officeView->scene()->sceneRect(), Qt::KeepAspectRatio);
    });
    connect(zoomOutOffice, &QPushButton::clicked, officeView, [officeView] { officeView->scale(0.8, 0.8); });
    connect(zoomInOffice, &QPushButton::clicked, officeView, [officeView] { officeView->scale(1.25, 1.25); });
    officeLayout->addWidget(officeView, 1);
    tabs_->addTab(officePage, tr("Офис"));
    connect(tabs_, &QTabWidget::currentChanged, officeView, [this, officePage, officeView] {
        if (tabs_->currentWidget() != officePage) return;
        QTimer::singleShot(0, officeView, [officeView] {
            officeView->fitInView(officeView->scene()->sceneRect(), Qt::KeepAspectRatio);
        });
    });

    auto *peoplePage = new QWidget(this);
    auto *peopleLayout = new QVBoxLayout(peoplePage);
    participants_ = table({tr("ID"), tr("Участник"), tr("Организация / отдел"), tr("Состояние"), tr("Напряжение"), tr("Навык")}, "participantsTable");
    peopleLayout->addWidget(participants_);
    auto *transfer = new QPushButton(tr("Перевести выбранного участника…"), peoplePage);
    transfer->setObjectName("transferParticipant");
    transfer->setEnabled(setup.role == UserRole::Secretary);
    peopleLayout->addWidget(transfer);
    tabs_->addTab(peoplePage, tr("Участники"));
    connect(transfer, &QPushButton::clicked, this, [this] { transferParticipant(); });
    organizations_ = table({tr("Организация / отдел"), tr("Участников"), tr("Нагрузка"), tr("Репутация"), tr("Состояние")}, "organizationsTable");
    tabs_->addTab(organizations_, tr("Организации"));
    auto *catalog = table({tr("Событие"), tr("Относительный вес"), tr("Базовая длительность")}, "eventCatalog");
    for (const auto &definition : SimulationEngine::eventCatalog()) {
        int row = catalog->rowCount(); catalog->insertRow(row);
        catalog->setItem(row, 0, new QTableWidgetItem(definition.title));
        catalog->setItem(row, 1, new QTableWidgetItem(definition.weight ? QString::number(definition.weight) : tr("Частота изменений")));
        catalog->setItem(row, 2, new QTableWidgetItem(definition.durationMinutes ? tr("%1 мин").arg(definition.durationMinutes) : tr("Изменение состояния")));
    }
    catalog->setToolTip(tr("Вероятности пропорциональны весам доступных событий. Восстановление возможно только после происшествия."));
    tabs_->addTab(catalog, tr("Типы событий"));

    connect(startButton_, &QPushButton::clicked, this, [this] { start(); });
    connect(pauseButton_, &QPushButton::clicked, this, [this] { pause(); refreshCalendar(); });
    connect(resetButton, &QPushButton::clicked, this, [this] { reset(); });
    connect(speed_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateSpeed(); });
    connect(timer_, &QTimer::timeout, this, [this] { step(); });
    connect(addButton_, &QPushButton::clicked, this, [this] { editEvent(); });
    connect(stepButton_, &QPushButton::clicked, this, [this] { step(); });
    for (auto *combo : {scope_, department_, status_, employee_})
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { refreshCalendar(); });
    for (auto *date : {from_, to_}) connect(date, &QDateEdit::dateChanged, this, [this] { refreshCalendar(); });
    connect(showPlanned_, &QCheckBox::toggled, this, [this] { refreshCalendar(); });
    updateSpeed();
    refreshCalendar();
}

void SimulationWindow::start()
{
    if (engine_.finished()) return;
    officeAnimator_->resume();
    timer_->start();
    refreshCalendar();
}
void SimulationWindow::pause() { timer_->stop(); officeAnimator_->pause(); }
void SimulationWindow::updateSpeed()
{
    timer_->setInterval(std::max(1, qRound(setup_.animationTickMs / speed_->currentData().toDouble())));
    officeAnimator_->retime(timer_->interval() * 4 / 5);
}
void SimulationWindow::reset()
{
    pause(); officeAnimator_->finish(); officeCatalogSignature_.clear(); engine_.reset(); displayedLogCount_ = 0; journal_->setRowCount(0);
    {
        const QSignalBlocker a(scope_), b(department_), c(employee_), d(status_), e(from_), f(to_), g(showPlanned_);
        scope_->setCurrentIndex(0); department_->setCurrentIndex(0); employee_->setCurrentIndex(0); status_->setCurrentIndex(0);
        from_->setDate(setup_.startDate); to_->setDate(setup_.startDate.addDays(setup_.dayCount - 1)); showPlanned_->setChecked(true);
    }
    refreshCalendar();
}
void SimulationWindow::step()
{
    officeAnimator_->finish();
    const auto result = engine_.step();
    if (!result.ok()) { pause(); QMessageBox::warning(this, tr("Модельное время"), errorText(result)); }
    if (engine_.finished()) pause();
    animateOfficeStep_ = true;
    refreshCalendar();
    animateOfficeStep_ = false;
}

void SimulationWindow::refreshCalendar()
{
    refreshJournalAndState();
    CalendarFilter filter;
    filter.scope = static_cast<CalendarScope>(scope_->currentIndex());
    if (department_->currentData().toInt()) filter.departmentId = department_->currentData().toInt();
    if (employee_->currentData().toInt()) filter.employeeId = employee_->currentData().toInt();
    if (status_->currentData().toInt() >= 0) filter.status = static_cast<EventStatus>(status_->currentData().toInt());
    filter.from = modelDateTime(from_->date());
    filter.to = modelDateTime(to_->date().addDays(1));
    auto result = engine_.calendar().calendar(filter);
    if (!showPlanned_->isChecked()) {
        auto &events = result.events;
        events.erase(std::remove_if(events.begin(), events.end(), [](const CalendarEvent &event) { return event.status == EventStatus::Planned; }), events.end());
    }
    CalendarFilter archiveFilter;
    archiveFilter.scope = CalendarScope::Archive;
    summary_->setText(tr("Модельное время: %1 (UTC+03:00) · Активных: %2 · В архиве: %3 · По фильтру: %4%5")
        .arg(engine_.calendar().modelTime().toOffsetFromUtc(10800).toString("dd.MM.yyyy HH:mm"))
        .arg(engine_.calendar().calendar().events.size()).arg(engine_.calendar().calendar(archiveFilter).events.size())
        .arg(result.events.size()).arg(result.result.ok() ? QString() : "\n" + result.result.message));
    const bool finished = engine_.calendar().modelTime() == engine_.calendar().horizonEnd();
    stepButton_->setEnabled(!finished && !timer_->isActive());
    startButton_->setEnabled(!finished && !timer_->isActive());
    pauseButton_->setEnabled(timer_->isActive());
    startButton_->setText(engine_.calendar().modelTime() == modelDateTime(setup_.startDate) ? tr("▶ Запустить") : tr("▶ Продолжить"));
    summary_->setText((timer_->isActive() ? tr("Идет · ") : finished ? tr("Завершено · ") : tr("Пауза · ")) + summary_->text());
    addButton_->setEnabled(!finished);
    if (finished) summary_->setText(summary_->text() + tr("\nПериод завершен. Незавершенные и будущие события остаются в активном календаре."));

    auto *calendar = new QWidget;
    calendar->setObjectName("simulationCalendar");
    auto *grid = new QGridLayout(calendar);
    grid->setSpacing(8);
    grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    const QStringList weekdays{tr("пн"), tr("вт"), tr("ср"), tr("чт"), tr("пт"), tr("сб"), tr("вс")};
    for (int column = 0; column < 7; ++column) {
        auto *header = new QLabel(weekdays[column], calendar);
        header->setFixedHeight(24);
        header->setAlignment(Qt::AlignCenter);
        grid->addWidget(header, 0, column);
    }
    const auto first = from_->date();
    const int dayCount = int(first.daysTo(to_->date())) + 1;
    const int offset = first.dayOfWeek() - 1;
    for (int index = 0; index < dayCount && result.result.ok(); ++index) {
        const auto date = first.addDays(index);
        const auto begin = modelDateTime(date), end = begin.addDays(1);
        const int weekday = date.dayOfWeek() - 1;
        auto *day = new QFrame(calendar);
        day->setObjectName("calendarDay");
        day->setProperty("date", date);
        // A day is past only after its end in the simulation's UTC+03:00 clock.
        if (end <= engine_.calendar().modelTime()) {
            auto *opacity = new QGraphicsOpacityEffect(day);
            opacity->setOpacity(0.55);
            day->setGraphicsEffect(opacity);
        }
        day->setFixedSize(160, 210);
        day->setStyleSheet("QFrame#calendarDay { background: #ffffff; border: 1px solid #dce3ed; border-radius: 9px; }");
        auto *dayLayout = new QVBoxLayout(day);
        dayLayout->setContentsMargins(7, 7, 7, 7);
        dayLayout->setSpacing(6);
        auto *header = new QLabel(date.toString("dd.MM.yyyy"), day);
        header->setObjectName("dayHeader");
        header->setAlignment(Qt::AlignCenter);
        header->setStyleSheet(QString("background: %1; color: white; font-weight: bold; padding: 6px; border-radius: 5px;")
            .arg(setup_.workingDays.value(weekday) ? "#34445c" : "#ad4650"));
        dayLayout->addWidget(header);
        auto *eventsScroll = new QScrollArea(day);
        eventsScroll->setObjectName("dayEventsScroll");
        eventsScroll->setWidgetResizable(true);
        eventsScroll->setFrameShape(QFrame::NoFrame);
        eventsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        eventsScroll->setStyleSheet("QScrollArea#dayEventsScroll, QScrollArea#dayEventsScroll > QWidget > QWidget { background: transparent; }");
        auto *events = new QWidget;
        auto *eventsLayout = new QVBoxLayout(events);
        eventsLayout->setContentsMargins(0, 0, 0, 0);
        eventsLayout->setSpacing(6);
        eventsLayout->setSizeConstraint(QLayout::SetMinimumSize);
        eventsScroll->setWidget(events);
        dayLayout->addWidget(eventsScroll, 1);
        for (const auto &event : result.events) {
            if (event.data.start >= end || event.data.end <= begin) continue;
            const auto time = tr("%1–%2")
                .arg(std::max(event.data.start, begin).toOffsetFromUtc(10800).toString("HH:mm"))
                .arg(event.data.end >= end ? QStringLiteral("24:00") : event.data.end.toOffsetFromUtc(10800).toString("HH:mm"));
            auto *button = new CalendarEventCard(time, event.data.title, event.status, events);
            button->setObjectName("calendarEvent");
            button->setProperty("eventId", QVariant::fromValue<qulonglong>(event.id));
            button->setToolTip(tr("ID: %1\n%2 — %3\nУчастники: %4\n%5\n%6")
                .arg(event.id).arg(event.data.start.toString(Qt::ISODate), event.data.end.toString(Qt::ISODate))
                .arg(participantText(event.data.participantIds, event.participantCodes))
                .arg(event.data.roomId ? engine_.calendar().catalog().rooms.value(*event.data.roomId).name : tr("Без помещения"))
                .arg(event.data.title));
            connect(button, &QPushButton::clicked, this, [this, id = event.id] { editEvent(id); });
            eventsLayout->addWidget(button);
        }
        eventsLayout->addStretch();
        grid->addWidget(day, (offset + index) / 7 + 1, weekday);
    }
    scroll_->setWidget(calendar); // Scroll area deletes the previous presentation, never the events.
}

void SimulationWindow::editEvent(std::optional<EventId> id)
{
    pause();
    const auto existing = id ? engine_.calendar().event(*id) : std::nullopt;
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
    for (const auto &item : engine_.calendar().catalog().rooms) room->addItem(item.name, item.id);
    auto *participants = new QLineEdit(&dialog);
    participants->setObjectName("eventParticipants");
    participants->setPlaceholderText(tr("ID через запятую: 1_01, 1_02, 2_01"));
    auto *importance = new QComboBox(&dialog);
    importance->setObjectName("eventImportance");
    importance->addItems({tr("Низкая"), tr("Средняя"), tr("Высокая")});
    auto *organizer = new QLineEdit(&dialog);
    organizer->setObjectName("eventOrganizer");
    EventData initial;
    initial.title = tr("Новое событие");
    initial.start = engine_.calendar().modelTime().addSecs(5 * 3600);
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
    QMap<int, QString> codes;
    for (int person : initial.participantIds) codes.insert(person, engine_.calendar().participantCode(person));
    participants->setText(participantText(initial.participantIds, existing ? existing->participantCodes : codes));
    importance->setCurrentIndex(int(initial.importance));
    organizer->setText(existing ? existing->organizerCode : engine_.calendar().participantCode(initial.organizerId));
    form->addRow(tr("Название"), title);
    form->addRow(tr("Тип"), type);
    form->addRow(tr("Начало (UTC+03:00)"), start);
    form->addRow(tr("Конец (UTC+03:00)"), end);
    form->addRow(tr("Помещение"), room);
    form->addRow(tr("Участники"), participants);
    form->addRow(tr("Важность"), importance);
    form->addRow(tr("ID организатора"), organizer);
    auto *help = new QLabel(tr("ID: организация_номер, например 1_01. Актуальные ID указаны на вкладке «Участники». При переводе ID меняется автоматически."), &dialog);
    help->setWordWrap(true);
    form->addRow(help);
    const bool editable = (!existing || existing->status == EventStatus::Planned) && engine_.calendar().modelTime() < engine_.calendar().horizonEnd();
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
                const auto participant = engine_.calendar().participantKey(token.trimmed());
                if (!participant) { error->setText(tr("Участник %1 не найден. Используйте ID вида 1_01.").arg(token.trimmed())); return; }
                data.participantIds.append(*participant);
            }
            data.importance = static_cast<Importance>(importance->currentIndex());
            const auto organizerId = engine_.calendar().participantKey(organizer->text());
            if (!organizerId) { error->setText(tr("Организатор не найден.")); return; }
            data.organizerId = *organizerId;
            const auto result = id ? engine_.updateEvent(*id, data, actor_) : engine_.createEvent(data, actor_);
            if (!result.ok()) { error->setText(errorText(result)); return; }
            dialog.accept();
        });
        if (existing) {
            auto *cancel = buttons->addButton(tr("Отменить событие"), QDialogButtonBox::ActionRole);
            cancel->setObjectName("cancelEvent");
            connect(cancel, &QPushButton::clicked, &dialog, [&] {
                const auto result = engine_.cancelEvent(*id, actor_);
                if (!result.ok()) { error->setText(errorText(result)); return; }
                dialog.accept();
            });
        }
    }
    if (existing) {
        form->addRow(new QLabel(tr("Версия: %1 · Записей в истории: %2")
            .arg(existing->version).arg(engine_.calendar().history(*id).size()), &dialog));
    }
    form->addRow(buttons);
    dialog.exec();
    // Defer rebuilding: the button whose click opened this dialog may still be emitting clicked().
    QMetaObject::invokeMethod(this, [this] { refreshCalendar(); }, Qt::QueuedConnection);
}

void SimulationWindow::refreshJournalAndState()
{
    refreshOffice();
    const auto &entries = engine_.log();
    const bool follow = journal_->verticalScrollBar()->value() == journal_->verticalScrollBar()->maximum();
    const bool added = displayedLogCount_ < entries.size();
    for (; displayedLogCount_ < entries.size(); ++displayedLogCount_) {
        const auto &entry = entries[displayedLogCount_];
        int row = journal_->rowCount(); journal_->insertRow(row);
        auto *time = new QTableWidgetItem(entry.time.toOffsetFromUtc(10800).toString("dd.MM HH:mm"));
        time->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(entry.eventId.value_or(0)));
        journal_->setItem(row, 0, time);
        auto *description = new QTableWidgetItem(entry.description);
        description->setToolTip(entry.description);
        journal_->setItem(row, 1, description);
        journal_->resizeRowToContents(row);
    }
    if (added && follow) journal_->scrollToBottom();
    const auto &catalog = engine_.calendar().catalog();
    {
        QSignalBlocker blocker(employee_);
        const int selected = employee_->currentData().toInt();
        if (employee_->count() != catalog.employees.size() + 1) {
            employee_->clear(); employee_->addItem(tr("Все участники"), 0);
            for (const auto &person : catalog.employees) employee_->addItem(person.publicId, person.id);
        } else {
            int index = 1;
            for (const auto &person : catalog.employees) employee_->setItemText(index++, person.publicId);
        }
        employee_->setCurrentIndex(std::max(0, employee_->findData(selected)));
    }
    auto put = [](QTableWidget *table, int row, int column, const QString &text) {
        if (auto *item = table->item(row, column)) item->setText(text);
        else table->setItem(row, column, new QTableWidgetItem(text));
    };
    participants_->setRowCount(catalog.employees.size());
    QMap<int, int> counts;
    int row = 0;
    for (const auto &person : catalog.employees) {
        const auto state = engine_.participants().value(person.id);
        ++counts[person.departmentId];
        put(participants_, row, 0, person.publicId);
        participants_->item(row, 0)->setData(Qt::UserRole, person.id);
        put(participants_, row, 1, person.name);
        put(participants_, row, 2, catalog.departments.value(person.departmentId).name);
        put(participants_, row, 3, !person.available ? tr("Болеет") : engine_.calendar().employeeBusy(person.id) ? tr("На мероприятии") : tr("Доступен"));
        put(participants_, row, 4, QString::number(state.stress));
        put(participants_, row, 5, QString::number(state.skill));
        ++row;
    }
    organizations_->setRowCount(catalog.departments.size());
    row = 0;
    for (const auto &department : catalog.departments) {
        const auto state = engine_.organizations().value(department.id);
        put(organizations_, row, 0, department.name);
        put(organizations_, row, 1, QString::number(counts.value(department.id)));
        put(organizations_, row, 2, QString::number(state.workload));
        put(organizations_, row, 3, QString::number(state.reputation));
        QStringList states;
        if (state.conflictSince) states.append(tr("Конфликт"));
        if (state.overloaded) states.append(tr("Повышенная нагрузка"));
        if (!engine_.calendar().roomAvailable(department.id)) states.append(tr("Помещение на ремонте"));
        put(organizations_, row, 4, states.isEmpty() ? tr("Штатная работа") : states.join(" · "));
        ++row;
    }
    const auto actor = catalog.employees.value(actor_.employeeId);
    actor_.permittedDepartmentIds = {actor.departmentId};
    actorLabel_->setText(tr("Пользователь %1 · %2 · Seed %3 · %4")
        .arg(actor.publicId)
        .arg(actor_.role == UserRole::Secretary ? tr("Секретарь") : actor_.role == UserRole::Manager ? tr("Руководитель") : tr("Сотрудник"))
        .arg(setup_.seed).arg(engine_.log().isEmpty() ? tr("Нажмите «Запустить» или выполните шаг.") : tr("Записи появляются по мере моделирования.")));
}

void SimulationWindow::transferParticipant()
{
    pause();
    const int row = participants_->currentRow();
    if (row < 0) { QMessageBox::information(this, tr("Перевод"), tr("Выберите участника в таблице.")); refreshCalendar(); return; }
    const int id = participants_->item(row, 0)->data(Qt::UserRole).toInt();
    QStringList names;
    QVector<int> ids;
    const auto person = engine_.calendar().catalog().employees.value(id);
    for (const auto &department : engine_.calendar().catalog().departments) {
        if (department.id == person.departmentId) continue;
        names.append(department.name); ids.append(department.id);
    }
    bool accepted = false;
    const QString chosen = QInputDialog::getItem(this, tr("Перевод %1").arg(person.publicId), tr("Принимающая организация / отдел"), names, 0, false, &accepted);
    if (accepted) {
        const auto result = engine_.transferEmployee(id, ids[names.indexOf(chosen)], actor_);
        if (!result.ok()) QMessageBox::warning(this, tr("Перевод"), errorText(result));
    }
    refreshCalendar();
}

void SimulationWindow::refreshOffice()
{
    const auto &calendar = engine_.calendar();
    QString signature = QStringLiteral("office:");
    for (const auto &room : calendar.catalog().rooms)
        signature += QStringLiteral("r%1:%2;").arg(room.id).arg(calendar.roomAvailable(room.id));
    for (const auto &person : calendar.catalog().employees)
        signature += QStringLiteral("p%1:%2:%3:%4;").arg(person.id).arg(person.departmentId).arg(person.publicId).arg(person.available);
    CalendarFilter filter; filter.status = EventStatus::InProgress;
    for (const auto &event : calendar.calendar(filter).events)
        signature += QStringLiteral("e%1:%2;").arg(event.id).arg(event.version);
    if (signature == officeCatalogSignature_) return;
    officeCatalogSignature_ = signature;
    const int budget = timer_->isActive() ? std::min(timer_->interval(), std::max(0, timer_->remainingTime())) : timer_->interval();
    officeAnimator_->update(calendar, animateOfficeStep_ ? budget * 4 / 5 : 0);
}
