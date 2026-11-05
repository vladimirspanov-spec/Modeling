#include "runtools.h"
#include "src/reporting/simulationreport.h"
#include "src/storage/simulationstore.h"
#include "src/storage/jsoncodec.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPlainTextEdit>

namespace {
QTableWidget *table(const QStringList &headers, QWidget *parent) {
    auto *view = new QTableWidget(0, headers.size(), parent);
    view->setHorizontalHeaderLabels(headers); view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setSelectionBehavior(QAbstractItemView::SelectRows); view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); view->horizontalHeader()->setStretchLastSection(true);
    return view;
}
void row(QTableWidget *table, const QStringList &values, quint64 id = 0) {
    int r = table->rowCount(); table->insertRow(r);
    for (int c = 0; c < values.size(); ++c) table->setItem(r, c, new QTableWidgetItem(values[c]));
    table->item(r, 0)->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(id));
}
}
RunTools::RunTools(SimulationEngine &engine, std::function<void()> pause, std::function<void()> changed,
    std::function<void(EventId)> openEvent, QWidget *parent)
    : QWidget(parent), engine_(engine), pause_(std::move(pause)), changed_(std::move(changed))
{
    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this); layout->addWidget(tabs);
    auto *requestPage = new QWidget; auto *requestLayout = new QVBoxLayout(requestPage);
    requests_ = table({tr("№"), tr("Время заявки"), tr("Мероприятие"), tr("Результат")}, this); requests_->setObjectName("requestsTable");
    requestLayout->addWidget(requests_);
    auto *buttons = new QHBoxLayout; requestLayout->addLayout(buttons);
    auto *accept = new QPushButton(tr("Выбрать вариант…")); auto *reject = new QPushButton(tr("Отклонить предложения"));
    buttons->addWidget(accept); buttons->addWidget(reject);
    tabs->addTab(requestPage, tr("Заявки"));
    connect(accept, &QPushButton::clicked, this, [this] {
        const int selected = requests_->currentRow(); if (selected < 0) return;
        const auto id = requests_->item(selected, 0)->data(Qt::UserRole).toULongLong();
        const auto request = engine_.requests().value(int(id - 1)); if (!request.pending) return;
        pause_(); QStringList choices;
        for (int i = 0; i < request.alternatives.size(); ++i) {
            const auto &e = request.alternatives[i]; choices.append(tr("%1. %2 — %3; помещение %4").arg(i + 1)
                .arg(e.start.toString("dd.MM HH:mm"), e.end.toString("dd.MM HH:mm"), e.roomId ? QString::number(*e.roomId) : tr("нет")));
        }
        bool ok; const auto chosen = QInputDialog::getItem(this, tr("Варианты переноса"), tr("Доступность будет проверена повторно"), choices, 0, false, &ok);
        if (ok) {
            auto result = engine_.acceptSuggestion(id, choices.indexOf(chosen));
            if (!result.ok()) QMessageBox::information(this, tr("Результат"), result.message);
            changed_();
        }
    });
    connect(reject, &QPushButton::clicked, this, [this] {
        if (requests_->currentRow() < 0) return; pause_();
        engine_.rejectSuggestion(requests_->item(requests_->currentRow(), 0)->data(Qt::UserRole).toULongLong()); changed_();
    });
    auto *mailPage = new QWidget;
    auto *mailLayout = new QVBoxLayout(mailPage);
    emails_ = table({tr("Плановое время"), tr("Отправлено"), tr("Доставлено (имитация)"), tr("Адресаты"), tr("Тема")}, mailPage);
    emails_->setObjectName("emailsTable");
    mailLayout->addWidget(emails_);
    openEmail_ = new QPushButton(tr("Открыть сообщение…"), mailPage);
    openEmail_->setObjectName("openEmail"); openEmail_->setEnabled(false);
    mailLayout->addWidget(openEmail_, 0, Qt::AlignLeft);
    connect(openEmail_, &QPushButton::clicked, this, [this] { openEmail(emails_->currentRow()); });
    connect(emails_, &QTableWidget::cellDoubleClicked, this, [this](int r, int) { openEmail(r); });
    connect(emails_, &QTableWidget::itemSelectionChanged, this, [this] {
        openEmail_->setEnabled(!emails_->selectedItems().isEmpty());
    });
    tabs->addTab(mailPage, tr("Почта (имитация)"));
    events_ = table({tr("№"), tr("Начало"), tr("Конец"), tr("Мероприятие"), tr("Статус")}, this);
    events_->setObjectName("allEventsTable"); tabs->addTab(events_, tr("Все мероприятия, включая будущие"));
    connect(events_, &QTableWidget::cellDoubleClicked, this, [this, openEvent](int r, int) { openEvent(events_->item(r, 0)->data(Qt::UserRole).toULongLong()); });
    auto *reportPage = new QWidget; auto *reportLayout = new QVBoxLayout(reportPage);
    summary_ = new QLabel; summary_->setWordWrap(true); summary_->setTextInteractionFlags(Qt::TextSelectableByMouse); reportLayout->addWidget(summary_);
    auto *note = new QLabel(tr("Почта имитируется, внешние письма не отправляются. Использование имитации необходимо согласовать с преподавателем."));
    note->setWordWrap(true); reportLayout->addWidget(note);
    auto *files = new QHBoxLayout; reportLayout->addLayout(files); reportLayout->addStretch();
    auto *save = new QPushButton(tr("Сохранить состояние…")); files->addWidget(save); save->setObjectName("saveState");
    connect(save, &QPushButton::clicked, this, [this] {
        pause_(); const auto path = QFileDialog::getSaveFileName(this, tr("Состояние и сценарий"), "simulation.json", "JSON (*.json)");
        if (path.isEmpty()) return;
        try { SimulationStore::save(path, engine_); } catch (const std::exception &e) { QMessageBox::warning(this, tr("Ошибка сохранения"), e.what()); }
    });
    for (const auto &format : {QString("JSON"), QString("CSV")}) {
        auto *button = new QPushButton(tr("Отчёт %1…").arg(format)); files->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, format] {
            pause_(); const auto path = QFileDialog::getSaveFileName(this, tr("Экспорт отчёта"), "report." + format.toLower(), format + " (*." + format.toLower() + ')');
            if (path.isEmpty()) return;
            try { const auto report = SimulationReport::create(engine_);
                if (format == "JSON") JsonCodec::write(path, report); else JsonCodec::writeBytes(path, SimulationReport::csv(report));
            } catch (const std::exception &e) { QMessageBox::warning(this, tr("Ошибка экспорта"), e.what()); }
        });
    }
    tabs->addTab(reportPage, tr("Итоги и сохранение"));
}
void RunTools::openEmail(int row)
{
    if (row < 0 || row >= emailGroups_.size()) return;
    pause_();
    const auto &group = emailGroups_.at(row);
    const auto &mail = engine_.emails().at(group.firstMessage);
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Почтовое сообщение (имитация)"));
    dialog.resize(680, 480);
    auto *layout = new QVBoxLayout(&dialog);
    auto *text = new QPlainTextEdit(&dialog);
    text->setObjectName("emailMessageText"); text->setReadOnly(true);
    text->setPlainText(tr("Тема: %1\nАдресаты (%2): %3\n"
                         "Плановое время: %4\nОтправлено: %5\nДоставлено: %6 (имитация)\n\n%7")
        .arg(mail.subject, QString::number(group.recipients.size()), group.recipients.join(", "),
             mail.dueTime.toString("dd.MM.yyyy HH:mm"), mail.sentAt.toString("dd.MM.yyyy HH:mm"),
             mail.deliveredAt.toString("dd.MM.yyyy HH:mm"), mail.body));
    layout->addWidget(text);
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(close);
    dialog.exec();
}
void RunTools::refresh()
{
    const auto selected = requests_->currentRow() >= 0 ? requests_->item(requests_->currentRow(), 0)->data(Qt::UserRole).toULongLong() : 0;
    requests_->setRowCount(0);
    for (const auto &r : engine_.requests()) {
        QString status = r.pending ? tr("Ожидает выбора") : r.selected ? tr("Вариант выбран, создана новая попытка") : r.result.ok() ? tr("Принята") : r.result.message;
        row(requests_, {QString::number(r.id), r.time.toString("dd.MM HH:mm"), r.requested.title, status}, r.id);
        if (r.id == selected) requests_->selectRow(requests_->rowCount() - 1);
    }
    const auto selectedEmail = emails_->currentRow() >= 0 ? emails_->item(emails_->currentRow(), 0)->data(Qt::UserRole).toULongLong() : 0;
    emails_->setRowCount(0); emailGroups_.clear();
    QMap<QString, int> groups;
    const auto &messages = engine_.emails();
    for (int i = 0; i < messages.size(); ++i) {
        const auto &mail = messages.at(i);
        // Separate events, schedule revisions and repeated sends must remain separate rows.
        const auto key = QString::number(mail.eventId) + '/' + QString::number(mail.scheduleVersion) + '/'
            + mail.dueTime.toString(Qt::ISODate) + '/' + mail.sentAt.toString(Qt::ISODate);
        auto group = groups.constFind(key);
        if (group == groups.cend()) {
            groups.insert(key, emailGroups_.size());
            emailGroups_.append({i, {mail.recipient}});
        } else {
            emailGroups_[group.value()].recipients.append(mail.recipient);
        }
    }
    for (const auto &group : emailGroups_) {
        const auto &mail = messages.at(group.firstMessage);
        const auto recipients = group.recipients.size() == 1 ? group.recipients.first()
            : tr("%1 (+%2)").arg(group.recipients.first()).arg(group.recipients.size() - 1);
        row(emails_, {mail.dueTime.toString("dd.MM HH:mm"), mail.sentAt.toString("dd.MM HH:mm"),
            mail.deliveredAt.toString("dd.MM HH:mm"), recipients, mail.subject}, group.firstMessage + 1);
        emails_->item(emails_->rowCount() - 1, 3)->setToolTip(group.recipients.join("\n"));
        if (quint64(group.firstMessage + 1) == selectedEmail) emails_->selectRow(emails_->rowCount() - 1);
    }
    openEmail_->setEnabled(!emails_->selectedItems().isEmpty());
    events_->setRowCount(0); CalendarFilter all; all.scope = CalendarScope::All;
    const QStringList statuses{tr("Ожидает"), tr("Идет"), tr("Завершено"), tr("Отменено")};
    for (const auto &e : engine_.calendar().calendar(all).events) row(events_, {QString::number(e.id), e.data.start.toString("dd.MM.yyyy HH:mm"),
        e.data.end.toString("dd.MM.yyyy HH:mm"), e.data.title, statuses[int(e.status)]}, e.id);
    summary_->setText((engine_.finished() ? tr("Итоговый отчёт\n\n") : tr("Промежуточный отчёт\n\n")) + SimulationReport::summary(SimulationReport::metrics(engine_)));
}
