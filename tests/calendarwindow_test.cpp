#include "simulationwindow.h"
#include "widget.h"
#include <QtTest>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>

namespace {
CalendarSetup setup(bool preset = false) {
    CalendarSetup value;
    value.startDate = QDate(2026, 10, 12);
    value.initialMeetings = preset;
    return value;
}
// Always close a modal dialog even if an assertion in the callback fails.
void inDialog(const std::function<void(QDialog *)> &action) {
    QTimer::singleShot(0, [action] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        QTimer::singleShot(1000, dialog, &QDialog::reject);
        action(dialog);
    });
}
void fillEvent(QDialog *dialog, const QString &title, QTime start, QTime end) {
    dialog->findChild<QLineEdit *>("eventTitle")->setText(title);
    auto *startEdit = dialog->findChild<QDateTimeEdit *>("eventStart");
    auto *endEdit = dialog->findChild<QDateTimeEdit *>("eventEnd");
    startEdit->setDate(QDate(2026, 10, 12)); startEdit->setTime(start);
    endEdit->setDate(QDate(2026, 10, 12)); endEdit->setTime(end);
    dialog->findChild<QPushButton *>("saveEvent")->click();
}
}

class CalendarWindowTest : public QObject {
    Q_OBJECT
private slots:
    void createEditCancelAndFilter() {
        SimulationWindow window(setup());
        window.show();
        inDialog([](QDialog *dialog) { fillEvent(dialog, "Test event", QTime(9, 10), QTime(9, 40)); });
        window.findChild<QPushButton *>("addEvent")->click();
        QTRY_COMPARE(window.findChildren<QPushButton *>("calendarEvent").size(), 1);
        QCOMPARE(window.calendarService().event(1)->data.title, QString("Test event"));
        inDialog([](QDialog *dialog) { fillEvent(dialog, "Renamed", QTime(10, 0), QTime(11, 0)); });
        window.findChild<QPushButton *>("calendarEvent")->click();
        QTRY_VERIFY(window.findChild<QPushButton *>("calendarEvent")->text().contains("Renamed"));
        auto *employee = window.findChild<QSpinBox *>("employeeFilter");
        employee->setValue(2);
        QCOMPARE(window.findChildren<QPushButton *>("calendarEvent").size(), 0);
        employee->setValue(0);
        auto *department = window.findChild<QComboBox *>("departmentFilter");
        department->setCurrentIndex(2);
        QCOMPARE(window.findChildren<QPushButton *>("calendarEvent").size(), 0);
        department->setCurrentIndex(0);
        inDialog([](QDialog *dialog) { dialog->findChild<QPushButton *>("cancelEvent")->click(); });
        window.findChild<QPushButton *>("calendarEvent")->click();
        QTRY_COMPARE(window.findChildren<QPushButton *>("calendarEvent").size(), 0);
        window.findChild<QComboBox *>("calendarScope")->setCurrentIndex(1);
        QCOMPARE(window.findChildren<QPushButton *>("calendarEvent").size(), 1);
        QCOMPARE(window.calendarService().event(1)->status, EventStatus::Cancelled);
        inDialog([](QDialog *dialog) {
            QVERIFY(!dialog->findChild<QPushButton *>("saveEvent"));
            QVERIFY(!dialog->findChild<QLineEdit *>("eventTitle")->isEnabled());
            dialog->reject();
        });
        window.findChild<QPushButton *>("calendarEvent")->click();
    }
    void stepStartsCompletesAndArchives() {
        SimulationWindow window(setup());
        window.show();
        inDialog([](QDialog *dialog) { fillEvent(dialog, "Short", QTime(0, 10), QTime(0, 40)); });
        window.findChild<QPushButton *>("addEvent")->click();
        QTRY_COMPARE(window.findChildren<QPushButton *>("calendarEvent").size(), 1);
        window.findChild<QPushButton *>("stepCalendar")->click();
        QCOMPARE(window.calendarService().event(1)->status, EventStatus::InProgress);
        QVERIFY(window.findChild<QPushButton *>("calendarEvent")->text().contains(QString::fromUtf8("Идет")));
        window.findChild<QPushButton *>("stepCalendar")->click();
        QCOMPARE(window.calendarService().event(1)->status, EventStatus::Completed);
        QCOMPARE(window.findChildren<QPushButton *>("calendarEvent").size(), 0);
        window.findChild<QComboBox *>("calendarScope")->setCurrentIndex(1);
        QCOMPARE(window.findChildren<QPushButton *>("calendarEvent").size(), 1);
        QCOMPARE(window.calendarService().history(1).last().timestamp.time(), QTime(0, 40));
    }
    void loadPresetAndParameterHandoff() {
        Widget parameters;
        parameters.show();
        parameters.findChild<QSpinBox *>("departmentCount")->setValue(5);
        parameters.findChild<QSpinBox *>("simulationDays")->setValue(7);
        parameters.findChild<QComboBox *>("stepMinutes")->setCurrentIndex(1);
        parameters.findChild<QDateEdit *>()->setDate(QDate(2026, 10, 12));
        QPushButton *open = nullptr;
        for (auto *button : parameters.findChildren<QPushButton *>())
            if (button->text() == QString::fromUtf8("Открыть моделирование")) open = button;
        QVERIFY(open);
        open->click();
        SimulationWindow *window = nullptr;
        for (auto *child : parameters.children())
            if (auto *candidate = dynamic_cast<SimulationWindow *>(child)) window = candidate;
        QVERIFY(window);
        QVERIFY(window->isVisible());
        QCOMPARE(window->calendarService().calendar().events.size(), 26);
        QCOMPARE(window->findChildren<QPushButton *>("calendarEvent").size(), 26);
        window->findChild<QPushButton *>("stepCalendar")->click();
        QCOMPARE(window->calendarService().modelTime().time(), QTime(1, 0));
        QVERIFY(window->grab().save(QDir::temp().filePath("modeling-calendar-preview.png")));
    }
};
QTEST_MAIN(CalendarWindowTest)
#include "calendarwindow_test.moc"
