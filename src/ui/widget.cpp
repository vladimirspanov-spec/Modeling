#include "src/ui/widget.h"
#include "src/ui/simulationwindow.h"
#include "src/core/simulationstore.h"
#include "src/ui/experimentdialog.h"
#include <QFileDialog>

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimeEdit>
#include <QVBoxLayout>
#include <limits>

Widget::Widget(QWidget *parent) : QWidget(parent)
{
    setWindowTitle(tr("Параметры моделирования — календарь фирмы"));
    resize(900, 760);
    auto *layout = new QVBoxLayout(this);
    auto *title = new QLabel(tr("Параметры моделирования"), this);
    QFont titleFont = title->font();
    titleFont.setPointSize(20);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);
    auto *intro = new QLabel(tr("Настройте компанию и сценарий работы секретаря. Начало модельного времени — 00:00, UTC+03:00."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto *tabs = new QTabWidget(this);
    layout->addWidget(tabs, 1);
    auto page = [tabs](const QString &name) {
        auto *content = new QWidget;
        auto *form = new QFormLayout(content);
        form->setContentsMargins(20, 20, 20, 20);
        form->setSpacing(12);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        auto *scroll = new QScrollArea(tabs);
        scroll->setWidgetResizable(true);
        scroll->setWidget(content); // Scroll area owns its content and controls.
        tabs->addTab(scroll, name);
        return form;
    };
    auto *company = page(tr("Компания и время"));
    startDate = new QDateEdit(QDate::currentDate(), this);
    startDate->setCalendarPopup(true);
    startDate->setDisplayFormat("dd.MM.yyyy");
    company->addRow(tr("Дата начала"), startDate);
    addInteger(company, "departmentCount", tr("Количество отделов"), 5, 9, 6);
    addInteger(company, "employeesPerDepartment", tr("Сотрудников в каждом отделе"), 1, 1000, 8);
    addInteger(company, "simulationDays", tr("Период, календарных дней"), 7, 30, 14);
    addChoice(company, "stepMinutes", tr("Шаг модельного времени"), {tr("30 минут"), tr("60 минут")}, {"30", "60"}, 0);
    auto *days = new QWidget(this);
    auto *daysLayout = new QHBoxLayout(days);
    daysLayout->setContentsMargins(0, 0, 0, 0);
    for (const auto &name : QStringList{tr("Пн"), tr("Вт"), tr("Ср"), tr("Чт"), tr("Пт"), tr("Сб"), tr("Вс")}) {
        auto *check = new QCheckBox(name, days);
        workingDays.append(check);
        daysLayout->addWidget(check);
        connect(check, &QCheckBox::toggled, this, &Widget::updateSummary);
    }
    company->addRow(tr("Рабочие дни"), days);
    workStart = new QTimeEdit(QTime(9, 0), this);
    workEnd = new QTimeEdit(QTime(18, 0), this);
    workStart->setDisplayFormat("HH:mm");
    workEnd->setDisplayFormat("HH:mm");
    company->addRow(tr("Начало рабочего дня"), workStart);
    company->addRow(tr("Конец рабочего дня"), workEnd);
    auto *rooms = new QLabel(tr("Помещения: по одной комнате на отдел и один конференц-зал."), this);
    rooms->setWordWrap(true);
    company->addRow(rooms);

    auto *scenario = page(tr("Сценарий"));
    addChoice(scenario, "initialCalendar", tr("Начальный календарь"), {tr("Ежедневные планерки всех отделов и недельные совещания"), tr("Пустой календарь")}, {"planningAndWeeklyMeetings", "empty"}, 0);
    addDecimal(scenario, "requestRatePerWorkday", tr("Случайных событий за рабочий день, на фирму"), 6);
    addDecimal(scenario, "changeRatePerWorkday", tr("Изменений за рабочий день, на фирму"), 1);
    addInteger(scenario, "leadTimeMinWorkdays", tr("Дальний срок: минимум рабочих дней"), 0, 365, 1);
    addInteger(scenario, "leadTimeMaxWorkdays", tr("Дальний срок: максимум рабочих дней"), 0, 365, 5);
    auto *duration = new QLabel(tr("Планерки, совещания руководителей, семинары, командировки и другие встречи. Длительность задаётся типом мероприятия."), this);
    duration->setWordWrap(true);
    scenario->addRow(duration);
    addInteger(scenario, "participantsMin", tr("Минимум участников"), 1, 9000, 2);
    addInteger(scenario, "participantsMax", tr("Максимум участников"), 1, 9000, 8);
    addDecimal(scenario, "importanceLowWeight", tr("Вес низкой важности"), .5);
    addDecimal(scenario, "importanceMediumWeight", tr("Вес средней важности"), .35);
    addDecimal(scenario, "importanceHighWeight", tr("Вес высокой важности"), .15);

    auto *seminars = page(tr("Периодические семинары"));
    addChoice(seminars, "periodicSeminars", tr("Периодические семинары"), {tr("Включены"), tr("Отключены")}, {"true", "false"}, 0);
    addInteger(seminars, "seminarPeriodDays", tr("Период, календарных дней"), 1, 365, 7);
    addInteger(seminars, "seminarLeadDays", tr("Создавать за календарных дней"), 1, 365, 2);
    addInteger(seminars, "seminarDurationMinutes", tr("Длительность, минут"), 15, 480, 90);
    seminarTime = new QTimeEdit(QTime(14, 0), this); seminarTime->setObjectName("seminarTime");
    seminarTime->setDisplayFormat("HH:mm"); seminars->addRow(tr("Время начала"), seminarTime);
    auto *seminarNote = new QLabel(tr("Будущие семинары создаются постепенно. Выходные сдвигают дату на следующий рабочий день. Участвуют сотрудники минимум двух отделов. Случайные семинары сохраняются отдельно."), this);
    seminarNote->setWordWrap(true); seminars->addRow(seminarNote);
    auto updateSeminarControls = [this] {
        const bool enabled = choices.value("periodicSeminars")->currentIndex() == 0;
        for (const auto *key : {"seminarPeriodDays", "seminarLeadDays", "seminarDurationMinutes"}) integers.value(key)->setEnabled(enabled);
        seminarTime->setEnabled(enabled);
    };
    connect(choices.value("periodicSeminars"), qOverload<int>(&QComboBox::currentIndexChanged), this, updateSeminarControls);
    connect(seminarTime, &QTimeEdit::timeChanged, this, &Widget::updateSummary);
    updateSeminarControls();

    auto *reminders = page(tr("Напоминания"));
    const QStringList keys{"Low", "Medium", "High"};
    const QStringList names{tr("Низкая важность"), tr("Средняя важность"), tr("Высокая важность")};
    for (int i = 0; i < 3; ++i) {
        auto *group = new QGroupBox(names[i], this);
        auto *form = new QFormLayout(group);
        addInteger(form, "reminder" + keys[i] + "LeadDays", tr("Первое напоминание за календарных дней"), 0, 365, i == 2 ? 4 : i + 1);
        addInteger(form, "reminder" + keys[i] + "PeriodHours", tr("Период повторения, часов"), 1, 8760, 24);
        reminders->addRow(group);
    }
    auto *note = new QLabel(tr("Доставка имитируется журналом. Использование имитации необходимо согласовать с преподавателем. Напоминания допускаются круглосуточно."), this);
    note->setWordWrap(true);
    reminders->addRow(note);

    auto *experiment = page(tr("Конфликты и эксперимент"));
    addChoice(experiment, "conflictPolicy", tr("Политика конфликтов"), {tr("Отклонять заявку"), tr("Предлагать варианты пользователю"), tr("Автоматически переносить")}, {"Reject", "Suggest", "AutoReschedule"}, 1);
    addInteger(experiment, "rescheduleWindowWorkdays", tr("Горизонт поиска, рабочих дней"), 0, 365, 5);
    addInteger(experiment, "maxSuggestions", tr("Максимум предложений"), 1, 1000, 3);
    addChoice(experiment, "actorRole", tr("Роль пользователя"), {tr("Сотрудник"), tr("Руководитель"), tr("Секретарь")}, {"Employee", "Manager", "Secretary"}, 2);
    seed = new QLineEdit("42", this);
    seed->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]{1,10}"), seed));
    experiment->addRow(tr("Seed генератора (0–4294967295)"), seed);
    addInteger(experiment, "repetitions", tr("Количество независимых прогонов"), 1, 1000, 20);
    addInteger(experiment, "animationTickMs", tr("Интервал анимации, мс"), 1, 60000, 250);
    auto *hint = new QLabel(tr("Политика «Предлагать варианты» требует выбора пользователя; в пакетном сравнении применяется явное правило выбора первого варианта. Интервал анимации не влияет на модельное время."), this);
    hint->setWordWrap(true);
    experiment->addRow(hint);

    summary = new QLabel(this);
    summary->setWordWrap(true);
    layout->addWidget(summary);
    status = new QLabel(this);
    status->setWordWrap(true);
    layout->addWidget(status);
    auto *buttons = new QHBoxLayout;
    auto *reset = new QPushButton(tr("По умолчанию"), this);
    auto *check = new QPushButton(tr("Проверить параметры"), this);
    auto *openSimulation = new QPushButton(tr("Открыть моделирование"), this);
    buttons->addWidget(reset);
    buttons->addStretch();
    buttons->addWidget(check);
    buttons->addWidget(openSimulation);
    layout->addLayout(buttons);
    auto *files = new QHBoxLayout;
    auto *saveConfig = new QPushButton(tr("Сохранить настройки…"));
    auto *loadConfig = new QPushButton(tr("Загрузить настройки…"));
    auto *loadState = new QPushButton(tr("Открыть состояние…"));
    auto *batch = new QPushButton(tr("Провести серию экспериментов…"));
    for (auto *button : {saveConfig, loadConfig, loadState, batch}) files->addWidget(button);
    layout->addLayout(files);
    connect(saveConfig, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getSaveFileName(this, tr("Настройки"), "configuration.json", "JSON (*.json)"); if (path.isEmpty()) return;
        try { SimulationStore::saveConfiguration(path, currentSetup()); }
        catch (const std::exception &e) { QMessageBox::warning(this, tr("Ошибка"), e.what()); }
    });
    connect(loadConfig, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Настройки"), {}, "JSON (*.json)"); if (path.isEmpty()) return;
        try { applySetup(SimulationStore::loadConfiguration(path)); }
        catch (const std::exception &e) { QMessageBox::warning(this, tr("Ошибка загрузки"), e.what()); }
    });
    connect(loadState, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Состояние моделирования"), {}, "JSON (*.json)"); if (path.isEmpty()) return;
        try {
            auto engine = SimulationStore::load(path);
            auto *window = new SimulationWindow(engine->setup(), this);
            window->restoreState(std::move(*engine)); window->show();
        } catch (const std::exception &e) { QMessageBox::warning(this, tr("Ошибка загрузки"), e.what()); }
    });
    connect(batch, &QPushButton::clicked, this, [this] {
        const auto errors = validationErrors();
        if (!errors.isEmpty()) { QMessageBox::warning(this, tr("Параметры"), errors.join('\n')); return; }
        try { ExperimentDialog dialog(currentSetup(), this); dialog.exec(); }
        catch (const std::exception &e) { QMessageBox::warning(this, tr("Ошибка эксперимента"), e.what()); }
    });
    connect(reset, &QPushButton::clicked, this, &Widget::resetDefaults);
    connect(openSimulation, &QPushButton::clicked, this, [this] {
        const auto errors = validationErrors();
        if (!errors.isEmpty()) {
            QMessageBox::warning(this, tr("Проверьте параметры"), errors.join('\n'));
            return;
        }
        const auto setup = currentSetup();
        auto *window = new SimulationWindow(setup, this);
        window->show();
    });
    connect(check, &QPushButton::clicked, this, [this] {
        const auto errors = validationErrors();
        if (errors.isEmpty())
            status->setText(tr("Параметры корректны. Можно запускать моделирование."));
        else
            QMessageBox::warning(this, tr("Проверьте параметры"), errors.join('\n'));
    });
    connect(workStart, &QTimeEdit::timeChanged, this, &Widget::updateSummary);
    connect(workEnd, &QTimeEdit::timeChanged, this, &Widget::updateSummary);
    connect(startDate, &QDateEdit::dateChanged, this, &Widget::updateSummary);
    connect(seed, &QLineEdit::textChanged, this, &Widget::updateSummary);
    auto updateConflictControls = [this] {
        const bool search = choices.value("conflictPolicy")->currentIndex() != 0;
        integers.value("rescheduleWindowWorkdays")->setEnabled(search);
        integers.value("maxSuggestions")->setEnabled(choices.value("conflictPolicy")->currentIndex() == 1);
    };
    connect(choices.value("conflictPolicy"), qOverload<int>(&QComboBox::currentIndexChanged), this, updateConflictControls);
    resetDefaults(); updateConflictControls();
}

QSpinBox *Widget::addInteger(QFormLayout *form, const QString &key, const QString &label, int minimum, int maximum, int value)
{
    auto *input = new QSpinBox(this);
    input->setObjectName(key);
    input->setRange(minimum, maximum);
    input->setValue(value);
    input->setProperty("defaultValue", value);
    integers.insert(key, input);
    form->addRow(label, input);
    connect(input, qOverload<int>(&QSpinBox::valueChanged), this, &Widget::updateSummary);
    return input;
}

QDoubleSpinBox *Widget::addDecimal(QFormLayout *form, const QString &key, const QString &label, double value)
{
    auto *input = new QDoubleSpinBox(this);
    input->setObjectName(key);
    input->setRange(0, 100000);
    input->setDecimals(3);
    input->setSingleStep(.05);
    input->setValue(value);
    input->setProperty("defaultValue", value);
    decimals.insert(key, input);
    form->addRow(label, input);
    connect(input, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &Widget::updateSummary);
    return input;
}

QComboBox *Widget::addChoice(QFormLayout *form, const QString &key, const QString &label, const QStringList &labels, const QStringList &values, int index)
{
    auto *input = new QComboBox(this);
    input->setObjectName(key);
    for (int i = 0; i < labels.size(); ++i) input->addItem(labels[i], values[i]);
    input->setCurrentIndex(index);
    input->setProperty("defaultValue", index);
    choices.insert(key, input);
    form->addRow(label, input);
    connect(input, qOverload<int>(&QComboBox::currentIndexChanged), this, &Widget::updateSummary);
    return input;
}

void Widget::resetDefaults()
{
    for (auto *input : integers) input->setValue(input->property("defaultValue").toInt());
    for (auto *input : decimals) input->setValue(input->property("defaultValue").toDouble());
    for (auto *input : choices) input->setCurrentIndex(input->property("defaultValue").toInt());
    for (int i = 0; i < workingDays.size(); ++i) workingDays[i]->setChecked(i < 5);
    startDate->setDate(QDate::currentDate());
    workStart->setTime(QTime(9, 0));
    workEnd->setTime(QTime(18, 0));
    seed->setText("42");
    seminarTime->setTime(QTime(14, 0));
    updateSummary();
}

QStringList Widget::validationErrors() const
{
    QStringList errors;
    auto value = [this](const QString &key) { return integers.value(key)->value(); };
    if (workStart->time() >= workEnd->time()) errors << tr("Начало рабочего дня должно быть раньше конца.");
    if (choices.value("periodicSeminars")->currentIndex() == 0 &&
        (seminarTime->time() < workStart->time() || seminarTime->time().secsTo(workEnd->time()) < value("seminarDurationMinutes") * 60))
        errors << tr("Периодический семинар должен полностью помещаться в рабочее время.");
    if (choices.value("periodicSeminars")->currentIndex() == 0 && value("participantsMax") < 2)
        errors << tr("Для периодического семинара нужны участники минимум двух отделов.");
    if (choices.value("initialCalendar")->currentData().toString() == "planningAndWeeklyMeetings" &&
        workStart->time().secsTo(workEnd->time()) < 3600)
        errors << tr("Для начальных планерок и совещаний нужен рабочий день не короче часа; иначе выберите пустой календарь.");
    bool hasDay = false;
    for (auto *day : workingDays) hasDay |= day->isChecked();
    if (!hasDay) errors << tr("Выберите хотя бы один рабочий день.");
    if (value("leadTimeMinWorkdays") > value("leadTimeMaxWorkdays")) errors << tr("Минимальный срок до события превышает максимальный.");
    if (value("participantsMin") > value("participantsMax")) errors << tr("Минимум участников превышает максимум.");
    if (value("participantsMax") > value("departmentCount") * value("employeesPerDepartment")) errors << tr("Количество участников превышает штат компании.");
    double weights = 0;
    for (const auto &key : {"importanceLowWeight", "importanceMediumWeight", "importanceHighWeight"}) weights += decimals.value(key)->value();
    if (weights <= 0) errors << tr("Хотя бы один вес важности должен быть положительным.");
    bool ok = false;
    const auto seedValue = seed->text().toULongLong(&ok);
    if (!ok || seedValue > std::numeric_limits<quint32>::max()) errors << tr("Seed должен быть целым числом от 0 до 4294967295.");
    if (!startDate->date().addDays(value("simulationDays")).isValid()) errors << tr("Дата конца моделирования выходит за допустимый диапазон.");
    return errors;
}

void Widget::updateSummary()
{
    if (!summary || !status) return;
    const int days = integers.value("simulationDays")->value();
    const int step = choices.value("stepMinutes")->currentData().toInt();
    summary->setText(tr("Сотрудников: %1 · Помещений: %2 · Шагов за прогон: %3\nПериод: %4 00:00 — %5 00:00 (конец не включается)")
                         .arg(integers.value("departmentCount")->value() * integers.value("employeesPerDepartment")->value())
                         .arg(integers.value("departmentCount")->value() + 1).arg(days * 24 * 60 / step)
                         .arg(startDate->date().toString("dd.MM.yyyy"), startDate->date().addDays(days).toString("dd.MM.yyyy")));
    const auto errors = validationErrors();
    status->setText(errors.isEmpty() ? tr("Параметры корректны.") : errors.join('\n'));
}

CalendarSetup Widget::currentSetup() const
{
    QList<bool> workdays;
    for (auto *day : workingDays)
        workdays.append(day->isChecked());
    CalendarSetup setup;
    setup.startDate = startDate->date();
    setup.dayCount = integers.value("simulationDays")->value();
    setup.stepMinutes = choices.value("stepMinutes")->currentData().toInt();
    setup.departmentCount = integers.value("departmentCount")->value();
    setup.employeesPerDepartment = integers.value("employeesPerDepartment")->value();
    setup.workingDays = workdays;
    setup.workStart = workStart->time();
    setup.workEnd = workEnd->time();
    setup.initialMeetings = choices.value("initialCalendar")->currentData().toString() == "planningAndWeeklyMeetings";
    setup.role = UserRole(choices.value("actorRole")->currentIndex());
    setup.seed = seed->text().toUInt();
    setup.requestRatePerWorkday = decimals.value("requestRatePerWorkday")->value();
    setup.changeRatePerWorkday = decimals.value("changeRatePerWorkday")->value();
    setup.animationTickMs = integers.value("animationTickMs")->value();
    setup.participantsMin = integers.value("participantsMin")->value();
    setup.participantsMax = integers.value("participantsMax")->value();
    setup.leadTimeMinWorkdays = integers.value("leadTimeMinWorkdays")->value();
    setup.leadTimeMaxWorkdays = integers.value("leadTimeMaxWorkdays")->value();
    setup.importanceWeights = {decimals.value("importanceLowWeight")->value(), decimals.value("importanceMediumWeight")->value(), decimals.value("importanceHighWeight")->value()};
    const QStringList levels{"Low", "Medium", "High"};
    for (int i = 0; i < 3; ++i) setup.reminders[i] = {integers.value("reminder" + levels[i] + "LeadDays")->value(), integers.value("reminder" + levels[i] + "PeriodHours")->value()};
    setup.conflictPolicy = ConflictPolicy(choices.value("conflictPolicy")->currentIndex());
    setup.rescheduleWindowWorkdays = integers.value("rescheduleWindowWorkdays")->value();
    setup.maxSuggestions = integers.value("maxSuggestions")->value();
    setup.repetitions = integers.value("repetitions")->value();
    setup.periodicSeminars = choices.value("periodicSeminars")->currentIndex() == 0;
    setup.seminarPeriodDays = integers.value("seminarPeriodDays")->value();
    setup.seminarLeadDays = integers.value("seminarLeadDays")->value();
    setup.seminarDurationMinutes = integers.value("seminarDurationMinutes")->value();
    setup.seminarTime = seminarTime->time();
    return setup;
}

void Widget::applySetup(const CalendarSetup &s)
{
    const QMap<QString, int> values{{"simulationDays", s.dayCount}, {"departmentCount", s.departmentCount},
        {"employeesPerDepartment", s.employeesPerDepartment}, {"animationTickMs", s.animationTickMs},
        {"participantsMin", s.participantsMin}, {"participantsMax", s.participantsMax}, {"leadTimeMinWorkdays", s.leadTimeMinWorkdays},
        {"leadTimeMaxWorkdays", s.leadTimeMaxWorkdays}, {"rescheduleWindowWorkdays", s.rescheduleWindowWorkdays},
        {"maxSuggestions", s.maxSuggestions}, {"repetitions", s.repetitions},
        {"seminarPeriodDays", s.seminarPeriodDays}, {"seminarLeadDays", s.seminarLeadDays},
        {"seminarDurationMinutes", s.seminarDurationMinutes}};
    for (auto it = values.begin(); it != values.end(); ++it) integers.value(it.key())->setValue(it.value());
    const QStringList levels{"Low", "Medium", "High"};
    for (int i = 0; i < 3; ++i) {
        integers.value("reminder" + levels[i] + "LeadDays")->setValue(s.reminders[i].leadDays);
        integers.value("reminder" + levels[i] + "PeriodHours")->setValue(s.reminders[i].periodHours);
        decimals.value("importance" + levels[i] + "Weight")->setValue(s.importanceWeights[i]);
    }
    decimals.value("requestRatePerWorkday")->setValue(s.requestRatePerWorkday); decimals.value("changeRatePerWorkday")->setValue(s.changeRatePerWorkday);
    startDate->setDate(s.startDate); workStart->setTime(s.workStart); workEnd->setTime(s.workEnd); seed->setText(QString::number(s.seed));
    for (int i = 0; i < 7; ++i) workingDays[i]->setChecked(s.workingDays[i]);
    choices.value("stepMinutes")->setCurrentIndex(s.stepMinutes == 30 ? 0 : 1);
    choices.value("initialCalendar")->setCurrentIndex(s.initialMeetings ? 0 : 1);
    choices.value("actorRole")->setCurrentIndex(int(s.role));
    choices.value("conflictPolicy")->setCurrentIndex(int(s.conflictPolicy));
    choices.value("periodicSeminars")->setCurrentIndex(s.periodicSeminars ? 0 : 1);
    seminarTime->setTime(s.seminarTime);
    updateSummary();
}
