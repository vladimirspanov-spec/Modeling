#include "simulationwindow.h"
#include "widget.h"
#include <QtTest>
#include <QComboBox>
#include <QCheckBox>
#include <QTableWidget>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QTabWidget>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGraphicsScene>
#include <QGraphicsEllipseItem>
#include <QScrollArea>
#include <QScrollBar>
#include <QWheelEvent>

namespace {
CalendarSetup setup(bool preset = false) {
    CalendarSetup value;
    value.startDate = QDate(2026, 10, 12);
    value.initialMeetings = preset;
    value.requestRatePerWorkday = 0;
    value.changeRatePerWorkday = 0;
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
    void finishStopsTimerAndTransferUpdatesUi() {
        auto config = setup(); config.stepMinutes = 60;
        SimulationWindow window(config);
        window.show();
        auto *people = window.findChild<QTableWidget *>("participantsTable");
        people->selectRow(0);
        auto *filter = window.findChild<QComboBox *>("employeeFilter");
        filter->setCurrentIndex(1);
        inDialog([](QDialog *dialog) { dialog->accept(); });
        window.findChild<QPushButton *>("transferParticipant")->click();
        QCOMPARE(window.calendarService().participantCode(1), QString("2_09"));
        QCOMPARE(people->item(0, 0)->text(), QString("2_09"));
        QCOMPARE(filter->currentText(), QString("2_09"));
        QCOMPARE(filter->currentData().toInt(), 1);
        auto *timer = window.findChild<QTimer *>("simulationTimer");
        auto *start = window.findChild<QPushButton *>("startSimulation");
        start->click();
        QVERIFY(timer->isActive());
        for (int step = 0; step < config.dayCount * 24 * 60 / config.stepMinutes; ++step)
            QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
        QVERIFY(window.simulationEngine().finished());
        QVERIFY(!timer->isActive());
        QVERIFY(!start->isEnabled());
        QVERIFY(!window.findChild<QPushButton *>("stepCalendar")->isEnabled());
        QCOMPARE(window.calendarService().modelTime(), window.calendarService().horizonEnd());
        window.findChild<QPushButton *>("resetSimulation")->click();
        QCOMPARE(window.calendarService().participantCode(1), QString("1_01"));
        QVERIFY(start->isEnabled());
    }
    void automaticPauseSpeedResetAndManualEquivalence() {
        auto config = setup(true);
        config.workStart = QTime(0, 0);
        config.animationTickMs = 100;
        SimulationWindow automatic(config), manual(config);
        automatic.show(); manual.show();
        auto *timer = automatic.findChild<QTimer *>("simulationTimer");
        auto *speed = automatic.findChild<QComboBox *>("simulationSpeed");
        auto *start = automatic.findChild<QPushButton *>("startSimulation");
        auto *pause = automatic.findChild<QPushButton *>("pauseSimulation");
        auto *step = automatic.findChild<QPushButton *>("stepCalendar");
        QCOMPARE(timer->interval(), 100);
        for (int index = 0; index < 4; ++index) {
            speed->setCurrentIndex(index);
            QCOMPARE(timer->interval(), QVector<int>({200, 100, 50, 20})[index]);
        }
        int ticks = 0;
        auto connection = connect(timer, &QTimer::timeout, &automatic, [&] {
            if (++ticks == 3) pause->click();
        });
        start->click();
        QVERIFY(timer->isActive());
        QVERIFY(!step->isEnabled());
        QTRY_VERIFY(!timer->isActive());
        QCOMPARE(ticks, 3);
        const auto pausedTime = automatic.calendarService().modelTime();
        QTest::qWait(60);
        QCOMPARE(automatic.calendarService().modelTime(), pausedTime);
        for (int i = 0; i < 3; ++i) manual.findChild<QPushButton *>("stepCalendar")->click();
        QCOMPARE(automatic.simulationEngine().log().size(), manual.simulationEngine().log().size());
        for (int i = 0; i < automatic.simulationEngine().log().size(); ++i) {
            QCOMPARE(automatic.simulationEngine().log()[i].time, manual.simulationEngine().log()[i].time);
            QCOMPARE(automatic.simulationEngine().log()[i].description, manual.simulationEngine().log()[i].description);
        }
        disconnect(connection);
        connection = connect(timer, &QTimer::timeout, &automatic, [&] { pause->click(); });
        start->click();
        QTRY_VERIFY(!timer->isActive());
        QVERIFY(automatic.calendarService().modelTime() > pausedTime);
        automatic.findChild<QPushButton *>("resetSimulation")->click();
        QCOMPARE(automatic.calendarService().modelTime(), modelDateTime(config.startDate));
        QVERIFY(automatic.simulationEngine().log().isEmpty());
        QCOMPARE(automatic.findChild<QTableWidget *>("simulationLog")->rowCount(), 0);
        QCOMPARE(automatic.calendarService().participantCode(1), QString("1_01"));
        QVERIFY(step->isEnabled());
        QVERIFY(!timer->isActive());
    }
    void dialogPausesAndParticipantIds() {
        SimulationWindow window(setup());
        window.show();
        window.findChild<QPushButton *>("startSimulation")->click();
        inDialog([&](QDialog *dialog) {
            QVERIFY(!window.findChild<QTimer *>("simulationTimer")->isActive());
            QCOMPARE(dialog->findChild<QLineEdit *>("eventParticipants")->text(), QString("1_01"));
            QCOMPARE(dialog->findChild<QLineEdit *>("eventOrganizer")->text(), QString("1_01"));
            dialog->findChild<QLineEdit *>("eventParticipants")->setText("1_01, 2_01");
            fillEvent(dialog, "Cross department", QTime(10, 0), QTime(11, 0));
        });
        window.findChild<QPushButton *>("addEvent")->click();
        QTRY_COMPARE(window.calendarService().calendar().events.size(), 1);
        QCOMPARE(window.calendarService().event(1)->data.participantIds, QVector<int>({1, 9}));
        QVERIFY(!window.findChild<QTimer *>("simulationTimer")->isActive());
    }
    void createEditCancelAndFilter() {
        SimulationWindow window(setup());
        window.show();
        window.findChild<QCheckBox *>("showPlanned")->setChecked(true);
        inDialog([](QDialog *dialog) { fillEvent(dialog, "Test event", QTime(9, 10), QTime(9, 40)); });
        window.findChild<QPushButton *>("addEvent")->click();
        QTRY_COMPARE(window.findChildren<QPushButton *>("calendarEvent").size(), 1);
        QCOMPARE(window.calendarService().event(1)->data.title, QString("Test event"));
        inDialog([](QDialog *dialog) { fillEvent(dialog, "Renamed", QTime(10, 0), QTime(11, 0)); });
        window.findChild<QPushButton *>("calendarEvent")->click();
        QTRY_VERIFY(window.findChild<QPushButton *>("calendarEvent")->text().contains("Renamed"));
        auto *employee = window.findChild<QComboBox *>("employeeFilter");
        employee->setCurrentIndex(2);
        QCOMPARE(window.findChildren<QPushButton *>("calendarEvent").size(), 0);
        employee->setCurrentIndex(0);
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
    void officeShowsOnlyCurrentEventParticipants() {
        SimulationWindow window(setup());
        window.show();
        auto *scene = window.findChild<QGraphicsScene *>("officeScene");
        QVERIFY(scene);
        auto circles = [scene] {
            QList<QGraphicsEllipseItem *> result;
            for (auto *item : scene->items())
                if (auto *circle = qgraphicsitem_cast<QGraphicsEllipseItem *>(item)) result.append(circle);
            return result;
        };
        QVERIFY(circles().isEmpty());
        const int hallId = window.calendarService().catalog().rooms.lastKey();
        inDialog([hallId](QDialog *dialog) {
            auto *room = dialog->findChild<QComboBox *>("eventRoom");
            QVERIFY(room);
            room->setCurrentIndex(room->findData(hallId));
            fillEvent(dialog, "Conference", QTime(9, 10), QTime(9, 40));
        });
        window.findChild<QPushButton *>("addEvent")->click();
        QTRY_COMPARE(window.calendarService().calendar().events.size(), 1);
        QVERIFY(circles().isEmpty());
        for (int i = 0; i < 19; ++i) window.findChild<QPushButton *>("stepCalendar")->click();
        QCOMPARE(window.calendarService().event(1)->status, EventStatus::InProgress);
        QCOMPARE(circles().size(), window.calendarService().event(1)->data.participantIds.size());
        QVERIFY(!circles().isEmpty());
        for (auto *circle : circles()) QCOMPARE(circle->data(1).toInt(), hallId);
        // Calendar filters must not hide the current occupants of the office.
        window.findChild<QComboBox *>("departmentFilter")->setCurrentIndex(2);
        QVERIFY(!circles().isEmpty());
        window.findChild<QPushButton *>("stepCalendar")->click();
        QCOMPARE(window.calendarService().event(1)->status, EventStatus::Completed);
        QVERIFY(circles().isEmpty());
        window.findChild<QPushButton *>("resetSimulation")->click();
        QVERIFY(circles().isEmpty());
    }
    void stepStartsCompletesAndArchives() {
        SimulationWindow window(setup());
        window.show();
        inDialog([](QDialog *dialog) { fillEvent(dialog, "Short", QTime(9, 10), QTime(9, 40)); });
        window.findChild<QPushButton *>("addEvent")->click();
        QTRY_COMPARE(window.calendarService().calendar().events.size(), 1);
        QTRY_COMPARE(window.findChildren<QPushButton *>("calendarEvent").size(), 1);
        for (int i = 0; i < 18; ++i) window.findChild<QPushButton *>("stepCalendar")->click();
        QCOMPARE(window.calendarService().event(1)->status, EventStatus::Planned);
        window.findChild<QPushButton *>("stepCalendar")->click();
        QCOMPARE(window.calendarService().event(1)->status, EventStatus::InProgress);
        QVERIFY(window.findChild<QPushButton *>("calendarEvent")->text().contains(QString::fromUtf8("Идет")));
        window.findChild<QPushButton *>("stepCalendar")->click();
        QCOMPARE(window.calendarService().event(1)->status, EventStatus::Completed);
        QCOMPARE(window.findChildren<QPushButton *>("calendarEvent").size(), 0);
        window.findChild<QComboBox *>("calendarScope")->setCurrentIndex(1);
        QCOMPARE(window.findChildren<QPushButton *>("calendarEvent").size(), 1);
        QCOMPARE(window.calendarService().history(1).last().timestamp.time(), QTime(9, 40));
    }
    void pastDaysDimAtModelMidnightAndReset() {
        auto config = setup();
        config.stepMinutes = 60;
        SimulationWindow window(config);
        auto dayFor = [&](QDate date) -> QFrame * {
            for (auto *day : window.findChildren<QFrame *>("calendarDay"))
                if (day->property("date").toDate() == date) return day;
            return nullptr;
        };
        auto *step = window.findChild<QPushButton *>("stepCalendar");
        for (int hour = 0; hour < 23; ++hour) step->click();
        QCOMPARE(window.calendarService().modelTime(), modelDateTime(config.startDate, QTime(23, 0)));
        QVERIFY(dayFor(config.startDate));
        QVERIFY(!dayFor(config.startDate)->graphicsEffect());
        step->click();
        QCOMPARE(window.calendarService().modelTime(), modelDateTime(config.startDate.addDays(1)));
        auto *opacity = qobject_cast<QGraphicsOpacityEffect *>(dayFor(config.startDate)->graphicsEffect());
        QVERIFY(opacity);
        QCOMPARE(opacity->opacity(), 0.55);
        QVERIFY(dayFor(config.startDate)->isEnabled());
        QVERIFY(!dayFor(config.startDate.addDays(1))->graphicsEffect());
        QVERIFY(!dayFor(config.startDate.addDays(2))->graphicsEffect());
        window.findChild<QPushButton *>("resetSimulation")->click();
        QVERIFY(!dayFor(config.startDate)->graphicsEffect());
    }
    void fixedDaysScrollWithLongTitles() {
        SimulationWindow window(setup());
        window.show();
        auto *tabs = window.findChild<QTabWidget *>();
        for (int index = 0; index < tabs->count(); ++index)
            if (tabs->tabText(index).startsWith(QString::fromUtf8("Календарь"))) tabs->setCurrentIndex(index);
        const QString title = QString::fromUtf8("Очень длинное название события для проверки переноса и размещения карточки ")
            + QString(120, QLatin1Char('W'));
        for (int hour = 6; hour < 11; ++hour) {
            inDialog([title, hour](QDialog *dialog) {
                fillEvent(dialog, title, QTime(hour, 0), QTime(hour + 1, 0));
            });
            window.findChild<QPushButton *>("addEvent")->click();
            QTRY_COMPARE(window.findChildren<QPushButton *>("calendarEvent").size(), hour - 5);
        }
        for (int width : {1120, 1500}) {
            window.resize(width, 760);
            QCoreApplication::processEvents();
            for (auto *card : window.findChildren<QPushButton *>("calendarEvent")) {
                QTRY_VERIFY(card->parentWidget()->rect().contains(card->geometry()));
                QVERIFY(card->width() < 250);
                QVERIFY(card->toolTip().contains(title));
            }
        }
        for (auto *day : window.findChildren<QFrame *>("calendarDay"))
            QCOMPARE(day->size(), QSize(160, 210));
        auto *card = window.findChild<QPushButton *>("calendarEvent");
        auto *eventsScroll = card->parentWidget()->parentWidget()->parentWidget();
        auto *area = qobject_cast<QScrollArea *>(eventsScroll);
        QVERIFY(area);
        QTRY_VERIFY(area->verticalScrollBar()->maximum() > 0);
        const auto position = area->viewport()->rect().center();
        QWheelEvent wheel(position, area->viewport()->mapToGlobal(position), QPoint(), QPoint(0, -120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(area->viewport(), &wheel);
        QVERIFY(area->verticalScrollBar()->value() > 0);
        QCOMPARE(area->parentWidget()->size(), QSize(160, 210));
        QVERIFY(window.grab().save(QDir::temp().filePath("modeling-calendar-cards-preview.png")));
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
        QCOMPARE(window->calendarService().calendar().events.size(), 0);
        QCOMPARE(window->findChildren<QPushButton *>("calendarEvent").size(), 0);
        window->findChild<QPushButton *>("stepCalendar")->click();
        QCOMPARE(window->calendarService().modelTime().time(), QTime(1, 0));
        for (int step = 0; step < 17; ++step) window->findChild<QPushButton *>("stepCalendar")->click();
        QVERIFY(!window->simulationEngine().log().isEmpty());
        QVERIFY(window->grab().save(QDir::temp().filePath("modeling-calendar-preview.png")));
    }
};
QTEST_MAIN(CalendarWindowTest)
#include "calendarwindow_test.moc"
