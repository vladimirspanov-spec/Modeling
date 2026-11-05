#include "experimentdialog.h"
#include "src/core/jsoncodec.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QElapsedTimer>
#include <QTableWidget>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>

ExperimentDialog::ExperimentDialog(const CalendarSetup &setup, QWidget *parent)
    : QDialog(parent), runner_(ExperimentRunner::comparison(setup), setup.repetitions)
{
    setWindowTitle(tr("Пакетные эксперименты")); resize(950, 500);
    auto *layout = new QVBoxLayout(this);
    auto *label = new QLabel(tr("Сравниваются три стратегии и две интенсивности заявок (исходная и удвоенная).\nДля Suggest пакетный сценарий выбирает первый вариант после каждого шага."));
    label->setWordWrap(true); layout->addWidget(label);
    auto *progress = new QLabel; layout->addWidget(progress);
    auto *table = new QTableWidget(0, 6, this); table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setHorizontalHeaderLabels({tr("Сценарий"), tr("Прогонов"), tr("Завершено, среднее"), tr("Отказы, среднее"), tr("Конфликты, среднее"), tr("Занятость, %")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); layout->addWidget(table);
    auto *stop = new QPushButton(tr("Остановить")); layout->addWidget(stop);
    auto *save = new QPushButton(tr("Сохранить JSON и CSV…")); layout->addWidget(save);
    auto *timer = new QTimer(this);
    auto refresh = [this, table, progress] {
        progress->setText(tr("Завершено %1 из %2 прогонов. %3").arg(runner_.completedRuns()).arg(runner_.totalRuns())
            .arg(runner_.finished() ? tr("Эксперимент завершен.") : tr("Данные пока неполные.")));
        table->setRowCount(0);
        for (const auto &entry : runner_.report()["aggregates"].toArray()) {
            const auto a = entry.toObject(), m = a["mean"].toObject(); int r = table->rowCount(); table->insertRow(r);
            const QStringList values{a["case"].toString(), QString::number(a["count"].toInt()), QString::number(m["completed"].toDouble(), 'f', 2),
                QString::number(m["rejected"].toDouble(), 'f', 2), QString::number(m["conflicts"].toDouble(), 'f', 2), QString::number(100 * m["roomUtilization"].toDouble(), 'f', 2)};
            for (int c = 0; c < values.size(); ++c) table->setItem(r, c, new QTableWidgetItem(values[c]));
        }
    };
    connect(timer, &QTimer::timeout, this, [this, timer, refresh, stop] {
        const int before = runner_.completedRuns(); QElapsedTimer budget; budget.start();
        do { runner_.advance(); } while (!runner_.finished() && budget.elapsed() < 12);
        if (before != runner_.completedRuns() || runner_.finished()) refresh();
        if (runner_.finished()) { timer->stop(); stop->setEnabled(false); }
    });
    connect(stop, &QPushButton::clicked, this, [timer, refresh, stop] { timer->stop(); stop->setEnabled(false); refresh(); });
    connect(save, &QPushButton::clicked, this, [this] {
        const auto file = QFileDialog::getSaveFileName(this, tr("Результаты эксперимента"), "comparison.json", "JSON (*.json)");
        if (file.isEmpty()) return;
        try { JsonCodec::write(file, runner_.report()); JsonCodec::writeBytes(file + ".csv", runner_.csv()); }
        catch (const std::exception &e) { QMessageBox::warning(this, tr("Ошибка"), e.what()); }
    });
    refresh(); timer->start(0);
}
