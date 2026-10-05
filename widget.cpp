#include "widget.h"
#include "ui_widget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSaveFile>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimeEdit>
#include <QVBoxLayout>
#include <limits>

Widget::Widget(QWidget *parent) : QWidget(parent), ui(new Ui::Widget)
{
    ui->setupUi(this);
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
    addInteger(company, "employeesPerDepartment", tr("Сотрудников в каждом отделе"), 1, 100000, 8);
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
    addChoice(scenario, "initialCalendar", tr("Начальный календарь"), {tr("Планерки и недельные совещания"), tr("Пустой календарь")}, {"planningAndWeeklyMeetings", "empty"}, 0);
    addDecimal(scenario, "requestRatePerWorkday", tr("Новых заявок за рабочий день, на фирму"), 6);
    addDecimal(scenario, "changeRatePerWorkday", tr("Изменений за рабочий день, на фирму"), 1);
    addInteger(scenario, "leadTimeMinWorkdays", tr("Минимум рабочих дней до события"), 0, 365, 1);
    addInteger(scenario, "leadTimeMaxWorkdays", tr("Максимум рабочих дней до события"), 0, 365, 5);
    auto *duration = new QLabel(tr("Длительности: 30, 60, 90, 120 минут с равными весами."), this);
    duration->setWordWrap(true);
    scenario->addRow(duration);
    addInteger(scenario, "participantsMin", tr("Минимум участников"), 1, 900000, 2);
    addInteger(scenario, "participantsMax", tr("Максимум участников"), 1, 900000, 8);
    addDecimal(scenario, "importanceLowWeight", tr("Вес низкой важности"), .5);
    addDecimal(scenario, "importanceMediumWeight", tr("Вес средней важности"), .35);
    addDecimal(scenario, "importanceHighWeight", tr("Вес высокой важности"), .15);

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
    auto *note = new QLabel(tr("Доставка будет имитироваться журналом. Напоминания допускаются круглосуточно."), this);
    note->setWordWrap(true);
    reminders->addRow(note);

    auto *experiment = page(tr("Конфликты и эксперимент"));
    addChoice(experiment, "conflictPolicy", tr("Политика конфликтов"), {tr("Отклонять заявку"), tr("Предлагать варианты"), tr("Автоматически переносить")}, {"Reject", "Suggest", "AutoReschedule"}, 1);
    addInteger(experiment, "rescheduleWindowWorkdays", tr("Горизонт поиска, рабочих дней"), 0, 365, 5);
    addInteger(experiment, "maxSuggestions", tr("Максимум предложений"), 1, 1000, 3);
    addChoice(experiment, "actorRole", tr("Роль пользователя"), {tr("Сотрудник"), tr("Руководитель"), tr("Секретарь")}, {"Employee", "Manager", "Secretary"}, 2);
    seed = new QLineEdit("42", this);
    seed->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]{1,10}"), seed));
    experiment->addRow(tr("Seed генератора (0–4294967295)"), seed);
    addInteger(experiment, "repetitions", tr("Количество независимых прогонов"), 1, 100000, 20);
    addInteger(experiment, "animationTickMs", tr("Интервал анимации, мс"), 1, 60000, 250);
    auto *hint = new QLabel(tr("Политика «Предлагать варианты» требует выбора пользователя; для пакетных прогонов понадобятся заранее записанные решения. Интервал анимации не влияет на модельное время."), this);
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
    auto *save = new QPushButton(tr("Сохранить параметры…"), this);
    auto *check = new QPushButton(tr("Проверить параметры"), this);
    buttons->addWidget(reset);
    buttons->addStretch();
    buttons->addWidget(check);
    buttons->addWidget(save);
    layout->addLayout(buttons);
    connect(reset, &QPushButton::clicked, this, &Widget::resetDefaults);
    connect(save, &QPushButton::clicked, this, &Widget::saveConfiguration);
    connect(check, &QPushButton::clicked, this, [this] {
        const auto errors = validationErrors();
        if (errors.isEmpty())
            status->setText(tr("Параметры корректны. Конфигурация готова; движок моделирования пока не подключен."));
        else
            QMessageBox::warning(this, tr("Проверьте параметры"), errors.join('\n'));
    });
    connect(workStart, &QTimeEdit::timeChanged, this, &Widget::updateSummary);
    connect(workEnd, &QTimeEdit::timeChanged, this, &Widget::updateSummary);
    connect(startDate, &QDateEdit::dateChanged, this, &Widget::updateSummary);
    connect(seed, &QLineEdit::textChanged, this, &Widget::updateSummary);
    resetDefaults();
}

Widget::~Widget() { delete ui; }

QSpinBox *Widget::addInteger(QFormLayout *form, const QString &key, const QString &label, int minimum, int maximum, int value)
{
    auto *input = new QSpinBox(this);
    input->setObjectName(key);
    input->setRange(minimum, maximum);
    input->setValue(value);
    input->setProperty("defaultValue", value);
    integers.insert(key, input);
    form->addRow(label, input);
    connect(input, &QSpinBox::valueChanged, this, &Widget::updateSummary);
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
    connect(input, &QDoubleSpinBox::valueChanged, this, &Widget::updateSummary);
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
    connect(input, &QComboBox::currentIndexChanged, this, &Widget::updateSummary);
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
    updateSummary();
}

QStringList Widget::validationErrors() const
{
    QStringList errors;
    auto value = [this](const QString &key) { return integers.value(key)->value(); };
    if (workStart->time() >= workEnd->time()) errors << tr("Начало рабочего дня должно быть раньше конца.");
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
    status->setText(errors.isEmpty() ? tr("Параметры корректны. Можно сохранить конфигурацию в JSON.") : errors.join('\n'));
}

QJsonObject Widget::configuration() const
{
    QJsonObject result{{"schemaVersion", 1}, {"startDate", startDate->date().toString(Qt::ISODate)},
                       {"utcOffsetMinutes", 180}, {"workStart", workStart->time().toString("HH:mm")},
                       {"workEnd", workEnd->time().toString("HH:mm")},
                       {"seed", static_cast<double>(seed->text().toULongLong())}};
    for (auto it = integers.cbegin(); it != integers.cend(); ++it) result.insert(it.key(), it.value()->value());
    for (auto it = decimals.cbegin(); it != decimals.cend(); ++it) result.insert(it.key(), it.value()->value());
    for (auto it = choices.cbegin(); it != choices.cend(); ++it) result.insert(it.key(), it.value()->currentData().toString());
    result.insert("stepMinutes", choices.value("stepMinutes")->currentData().toInt());
    QJsonArray days;
    for (int i = 0; i < workingDays.size(); ++i) if (workingDays[i]->isChecked()) days.append(i + 1);
    result.insert("workingDays", days);
    result.insert("durationMinutes", QJsonArray{30, 60, 90, 120});
    result.insert("durationWeights", QJsonArray{1, 1, 1, 1});
    result.insert("roomPreset", "onePerDepartmentAndConferenceHall");
    return result;
}

void Widget::saveConfiguration()
{
    const auto errors = validationErrors();
    if (!errors.isEmpty()) {
        QMessageBox::warning(this, tr("Проверьте параметры"), errors.join('\n'));
        return;
    }
    QString path = QFileDialog::getSaveFileName(this, tr("Сохранить параметры"), "simulation-config.json", tr("JSON (*.json)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".json", Qt::CaseInsensitive)) path += ".json";
    QSaveFile file(path);
    const auto data = QJsonDocument(configuration()).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        QMessageBox::critical(this, tr("Ошибка сохранения"), file.errorString());
        return;
    }
    status->setText(tr("Параметры сохранены: %1").arg(path));
}
