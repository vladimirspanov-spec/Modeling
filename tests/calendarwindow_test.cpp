#include "src/core/simulationreport.h"
#include "src/ui/simulationwindow.h"
#include "src/ui/officescene.h"
#include "src/ui/widget.h"
#include "src/core/simulationstore.h"
#include "src/ui/runtools.h"
#include <QPlainTextEdit>
#include <QtTest>
#include <QComboBox>
#include <QCheckBox>
#include <QTableWidget>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QTimeEdit>
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
#include <QVariantAnimation>

namespace {
CalendarSetup setup(bool preset = false) {
    CalendarSetup value;
    value.startDate = QDate(2026, 10, 12);
    value.initialMeetings = preset; value.periodicSeminars = false;
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
void seedEvent(SimulationWindow &window, const QString &title, QTime start, QTime end, int room = 0) {
    // Prepare a fixture through the model; the observation UI cannot create events.
    auto engine = window.simulationEngine();
    EventData data; data.title = title;
    data.start = modelDateTime(engine.setup().startDate, start);
    data.end = modelDateTime(engine.setup().startDate, end);
    data.participantIds = {1}; data.organizerId = 1;
    if (room) data.roomId = room;
    QVERIFY(engine.createEvent(data, {1, UserRole::Secretary, {}}).ok());
    window.restoreState(std::move(engine));
}

}

class CalendarWindowTest : public QObject {
    Q_OBJECT
private slots:
    void dailyHourFilterWorksInObservationMode() {
        auto config=setup(); SimulationEngine engine(config);
        EventData event; event.title="Overnight"; event.start=modelDateTime(config.startDate,QTime(20,0));
        event.end=modelDateTime(config.startDate.addDays(1),QTime(10,0)); event.participantIds={1}; event.organizerId=1;
        QVERIFY(engine.createEvent(event,{1,UserRole::Secretary,{}}).ok());
        SimulationWindow window(config); window.restoreState(std::move(engine)); window.show();
        auto *enabled=window.findChild<QCheckBox *>("filterHours");
        auto *from=window.findChild<QTimeEdit *>("filterHourFrom"); auto *to=window.findChild<QTimeEdit *>("filterHourTo");
        QVERIFY(enabled); QVERIFY(from); QVERIFY(to); QVERIFY(!from->isEnabled());
        QCOMPARE(window.findChildren<QPushButton *>("calendarEvent").size(),2);
        from->setTime(QTime(9,0)); to->setTime(QTime(11,0)); enabled->setChecked(true);
        QCOMPARE(window.findChildren<QPushButton *>("calendarEvent").size(),1);
        from->setTime(QTime(10,0)); QCOMPARE(window.findChildren<QPushButton *>("calendarEvent").size(),0);
        from->setTime(QTime(12,0));
        QVERIFY(window.findChild<QLabel *>("calendarSummary")->text().contains(QString::fromUtf8("Некорректный фильтр")));
        enabled->setChecked(false); QCOMPARE(window.findChildren<QPushButton *>("calendarEvent").size(),2);
        QVERIFY(!window.findChild<QPushButton *>("assignLeader"));
        QVERIFY(!window.findChild<QPushButton *>("transferParticipant"));
        QCOMPARE(window.calendarService().catalog().departments.value(1).leaderId,1);
    }
    void groupedMailCanBeOpenedAndKeepsRepeatsSeparate() {
        auto config = setup(); config.reminders[0] = {1,1};
        SimulationEngine engine(config);
        EventData event;
        event.title = "<b>Original meeting %2</b>";
        event.start = modelDateTime(config.startDate, QTime(10,0)); event.end = event.start.addSecs(3600);
        event.organizerId = 1; event.participantIds = {1,2}; event.importance = Importance::Low;
        QVERIFY(engine.createEvent(event, {1,UserRole::Secretary,{}}).ok());
        event.organizerId = 3; event.participantIds = {3};
        QVERIFY(engine.createEvent(event, {1,UserRole::Secretary,{}}).ok());
        QVERIFY(engine.step().ok());
        int pauses = 0;
        RunTools view(engine, [&] { ++pauses; }, [] {}, [] { return UserContext{1, UserRole::Secretary, {}}; }, [](EventId) {}, [](const RequestRecord &) {});
        view.refresh(); view.show();
        view.findChild<QTabWidget *>()->setCurrentIndex(1);
        auto *mail = view.findChild<QTableWidget *>("emailsTable");
        auto *open = view.findChild<QPushButton *>("openEmail");
        QVERIFY(mail); QVERIFY(open);
        QCOMPARE(engine.emails().size(), 3); QCOMPARE(mail->rowCount(), 2);
        QVERIFY(!open->isEnabled());
        QCOMPARE(mail->item(0,3)->text(), QString("employee1@work (+1)"));
        QVERIFY(mail->item(0,3)->toolTip().contains("employee2@work"));
        QCOMPARE(mail->item(1,3)->text(), QString("employee3@work"));
        mail->selectRow(0); QVERIFY(open->isEnabled());
        inDialog([&](QDialog *dialog) {
            auto *message = dialog->findChild<QPlainTextEdit *>("emailMessageText"); QVERIFY(message);
            QVERIFY(message->isReadOnly()); QCOMPARE(pauses, 1);
            const auto text = message->toPlainText();
            QVERIFY(text.contains("employee1@work, employee2@work"));
            QVERIFY(!text.contains("employee3@work"));
            QVERIFY(text.contains("<b>Original meeting %2</b>")); // Preserve literal HTML and percent signs.
            QVERIFY(text.contains("12.10.2026 10:00"));
            dialog->accept();
        });
        open->click();
        QCOMPARE(pauses, 1);
        QVERIFY(engine.step().ok()); view.refresh();
        QCOMPARE(engine.emails().size(), 6); QCOMPARE(mail->rowCount(), 4);
        QCOMPARE(mail->currentRow(), 0); QVERIFY(open->isEnabled());
        inDialog([&](QDialog *dialog) {
            auto *message = dialog->findChild<QPlainTextEdit *>("emailMessageText"); QVERIFY(message);
            QVERIFY(message->toPlainText().contains("employee3@work"));
            QVERIFY(!message->toPlainText().contains("employee1@work"));
            dialog->accept();
        });
        QVERIFY(QMetaObject::invokeMethod(mail, "cellDoubleClicked", Qt::DirectConnection, Q_ARG(int, 1), Q_ARG(int, 0)));
        QCOMPARE(pauses, 2);
        engine.reset(); view.refresh(); QCOMPARE(mail->rowCount(), 0); QVERIFY(!open->isEnabled());
    }
    void futureEventsAndRestoredStateAreVisible() {
        auto config = setup(); SimulationEngine engine(config);
        EventData event;
        event.title = "Beyond horizon"; event.start = modelDateTime(config.startDate.addDays(40),QTime(10,0));
        event.end = event.start.addSecs(3600); event.organizerId = 1; event.participantIds = {1};
        QVERIFY(engine.createEvent(event,{1,UserRole::Secretary,{}}).ok());
        auto saved = SimulationStore::snapshot(engine);
        auto restored = SimulationStore::restore(saved);
        SimulationWindow window(config); window.restoreState(std::move(*restored)); window.show();
        auto *events = window.findChild<QTableWidget *>("allEventsTable"); QVERIFY(events); QCOMPARE(events->rowCount(),1);
        QVERIFY(events->item(0,1)->text().contains(config.startDate.addDays(40).toString("dd.MM.yyyy")));
        QCOMPARE(window.findChild<QTableWidget *>("participantsTable")->columnCount(),4);
        QCOMPARE(window.findChild<QTableWidget *>("organizationsTable")->columnCount(),3);
        auto *from = window.findChild<QDateEdit *>("filterFrom"), *to = window.findChild<QDateEdit *>("filterTo");
        to->setDate(config.startDate.addDays(40)); from->setDate(config.startDate.addDays(40));
        QCOMPARE(window.findChildren<QPushButton *>("calendarEvent").size(),1);
    }
    void newSettingsReachEngine() {
        Widget parameters;
        parameters.findChild<QSpinBox *>("seminarPeriodDays")->setValue(3);
        parameters.findChild<QSpinBox *>("seminarLeadDays")->setValue(1);
        parameters.findChild<QSpinBox *>("seminarDurationMinutes")->setValue(60);
        parameters.findChild<QTimeEdit *>("seminarTime")->setTime(QTime(15,0));
        parameters.findChild<QSpinBox *>("reminderLowLeadDays")->setValue(7);
        parameters.findChild<QSpinBox *>("reminderHighPeriodHours")->setValue(3);
        parameters.findChild<QComboBox *>("conflictPolicy")->setCurrentIndex(2);
        parameters.findChild<QSpinBox *>("rescheduleWindowWorkdays")->setValue(8);
        parameters.findChild<QSpinBox *>("repetitions")->setValue(4);
        parameters.findChild<QComboBox *>("actorRole")->setCurrentIndex(1);
        for (auto *button : parameters.findChildren<QPushButton *>())
            if (button->text() == QString::fromUtf8("Открыть моделирование")) button->click();
        SimulationWindow *window = nullptr;
        for (auto *child : parameters.children()) if (auto *w = dynamic_cast<SimulationWindow *>(child)) window = w;
        QVERIFY(window);
        const auto &s = window->simulationEngine().setup();
        QCOMPARE(s.role, UserRole::Manager);
        QVERIFY(s.periodicSeminars); QCOMPARE(s.seminarPeriodDays,3); QCOMPARE(s.seminarLeadDays,1);
        QCOMPARE(s.seminarTime,QTime(15,0)); QCOMPARE(s.seminarDurationMinutes,60);
        QCOMPARE(s.reminders[0].leadDays,7); QCOMPARE(s.reminders[2].periodHours,3);
        QCOMPARE(s.conflictPolicy,ConflictPolicy::AutoReschedule); QCOMPARE(s.rescheduleWindowWorkdays,8); QCOMPARE(s.repetitions,4);
    }
    void finishStopsTimerInObservationMode() {
        auto config = setup(); config.stepMinutes = 60;
        SimulationWindow window(config);
        window.show();
        auto *people = window.findChild<QTableWidget *>("participantsTable");
        people->selectRow(0);
        auto *filter = window.findChild<QComboBox *>("employeeFilter");
        filter->setCurrentIndex(1);
        QVERIFY(!window.findChild<QPushButton *>("transferParticipant"));
        QCOMPARE(window.calendarService().participantCode(1), QString("1_01"));
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
        for (int index = 0; index < 5; ++index) {
            speed->setCurrentIndex(index);
            QCOMPARE(timer->interval(), QVector<int>({400, 200, 100, 50, 20})[index]);
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
        QCOMPARE(automatic.simulationEngine().log().size(), SimulationEngine(config).log().size());
        QCOMPARE(automatic.findChild<QTableWidget *>("simulationLog")->rowCount(), SimulationEngine(config).log().size());
        QCOMPARE(automatic.calendarService().participantCode(1), QString("1_01"));
        QVERIFY(step->isEnabled());
        QVERIFY(!timer->isActive());
    }
    void employeeCannotEditExistingEventsAndFiltersDoNotChangeModel() {
        auto config = setup(); config.role = UserRole::Employee;
        SimulationWindow window(config); window.show();
        seedEvent(window, "Read only", QTime(10,0), QTime(11,0));
        QVERIFY(window.findChild<QPushButton *>("addEvent"));
        const auto before = SimulationReport::create(window.simulationEngine());
        window.findChild<QPushButton *>("startSimulation")->click();
        inDialog([&](QDialog *dialog) {
            QVERIFY(!window.findChild<QTimer *>("simulationTimer")->isActive());
            QVERIFY(!dialog->findChild<QPushButton *>("saveEvent"));
            QVERIFY(!dialog->findChild<QPushButton *>("cancelEvent"));
            for (const auto *name : {"eventTitle", "eventParticipants", "eventOrganizer"})
                QVERIFY(!dialog->findChild<QLineEdit *>(name)->isEnabled());
            QVERIFY(!dialog->findChild<QDateTimeEdit *>("eventStart")->isEnabled());
            QVERIFY(!dialog->findChild<QComboBox *>("eventRoom")->isEnabled());
            dialog->reject();
        });
        window.findChild<QPushButton *>("calendarEvent")->click();
        auto *employee = window.findChild<QComboBox *>("employeeFilter");
        employee->setCurrentIndex(2);
        QCOMPARE(window.findChildren<QPushButton *>("calendarEvent").size(), 0);
        employee->setCurrentIndex(0);
        QCOMPARE(SimulationReport::create(window.simulationEngine()), before);
    }
    void suggestionsRequireExplicitChoiceOnPause() {
        auto config = setup(); config.conflictPolicy = ConflictPolicy::Suggest;
        SimulationEngine engine(config);
        EventData data; data.title = "Conflict";
        data.start = modelDateTime(config.startDate, QTime(10,0)); data.end = data.start.addSecs(3600);
        data.participantIds = {1}; data.organizerId = 1; data.roomId = 1;
        QVERIFY(engine.createEvent(data,{1,UserRole::Secretary,{}}).ok());
        QVERIFY(!engine.createEvent(data,{1,UserRole::Secretary,{}}).ok());
        SimulationWindow window(config); window.restoreState(std::move(engine));
        window.findChild<QPushButton *>("stepCalendar")->click();
        QVERIFY(window.simulationEngine().requests()[1].pending);
        QCOMPARE(window.calendarService().calendar().events.size(), 1);
        auto *requests = window.findChild<QTableWidget *>("requestsTable");
        QCOMPARE(requests->item(0, 0)->data(Qt::UserRole).toULongLong(), quint64(2));
        QCOMPARE(requests->item(1, 0)->data(Qt::UserRole).toULongLong(), quint64(1));
        requests->selectRow(0);
        auto *accept = window.findChild<QPushButton *>("acceptSuggestion");
        QVERIFY(accept->isEnabled());
        window.findChild<QPushButton *>("startSimulation")->click();
        inDialog([&](QDialog *dialog) {
            QVERIFY(!window.findChild<QTimer *>("simulationTimer")->isActive()); dialog->accept();
        });
        accept->click();
        QVERIFY(!window.simulationEngine().requests()[1].pending);
        QCOMPARE(window.calendarService().calendar().events.size(), 2);
        QVERIFY(!window.findChild<QTimer *>("simulationTimer")->isActive());
    }
    void createAndEditOnPauseWithoutPersonnelControls() {
        SimulationWindow window(setup()); window.show();
        QVERIFY(!window.findChild<QPushButton *>("transferParticipant"));
        QVERIFY(!window.findChild<QPushButton *>("assignLeader"));
        window.findChild<QPushButton *>("startSimulation")->click();
        inDialog([&](QDialog *dialog) {
            QVERIFY(!window.findChild<QTimer *>("simulationTimer")->isActive());
            QVERIFY(!dialog->findChild<QPushButton *>("cancelEvent"));
            dialog->findChild<QLineEdit *>("eventTitle")->setText("Created by user");
            dialog->findChild<QDateTimeEdit *>("eventStart")->setDateTime(modelDateTime(setup().startDate, QTime(10,0)));
            dialog->findChild<QDateTimeEdit *>("eventEnd")->setDateTime(modelDateTime(setup().startDate, QTime(11,0)));
            dialog->findChild<QPushButton *>("saveEvent")->click();
        });
        window.findChild<QPushButton *>("addEvent")->click();
        QCOMPARE(window.calendarService().calendar().events.size(), 1);
        QCOMPARE(window.calendarService().event(1)->data.title, QString("Created by user"));
        QTRY_VERIFY(window.findChild<QPushButton *>("calendarEvent"));
        inDialog([](QDialog *dialog) {
            dialog->findChild<QLineEdit *>("eventTitle")->setText("Edited by user");
            dialog->findChild<QPushButton *>("saveEvent")->click();
        });
        window.findChild<QPushButton *>("calendarEvent")->click();
        QCOMPARE(window.calendarService().event(1)->data.title, QString("Edited by user"));
        QVERIFY(!window.findChild<QTimer *>("simulationTimer")->isActive());
    }
    void pendingRequestCanReturnToEditing() {
        auto config = setup(); config.conflictPolicy = ConflictPolicy::Suggest;
        SimulationEngine engine(config);
        EventData data; data.title = "Draft";
        data.start = modelDateTime(config.startDate, QTime(10,0)); data.end = data.start.addSecs(3600);
        data.participantIds = {1}; data.organizerId = 1;
        QVERIFY(engine.createEvent(data, {1,UserRole::Secretary,{}}).ok());
        QVERIFY(!engine.createEvent(data, {1,UserRole::Secretary,{}}).ok());
        SimulationWindow window(config); window.restoreState(std::move(engine));
        window.findChild<QTableWidget *>("requestsTable")->selectRow(0);
        inDialog([](QDialog *dialog) {
            QCOMPARE(dialog->findChild<QLineEdit *>("eventTitle")->text(), QString("Draft"));
            dialog->findChild<QDateTimeEdit *>("eventStart")->setTime(QTime(12,0));
            dialog->findChild<QDateTimeEdit *>("eventEnd")->setTime(QTime(13,0));
            dialog->findChild<QPushButton *>("saveEvent")->click();
        });
        window.findChild<QPushButton *>("editRequest")->click();
        QVERIFY(!window.simulationEngine().requests()[1].pending);
        QCOMPARE(window.calendarService().calendar().events.size(), 2);
        QCOMPARE(window.calendarService().event(2)->data.start.time(), QTime(12,0));
    }
    void managerCannotEditAnotherDepartment() {
        auto config = setup(); config.role = UserRole::Manager;
        SimulationWindow window(config); seedEvent(window, "Own department", QTime(10,0), QTime(11,0), 1);
        inDialog([](QDialog *dialog) { QVERIFY(dialog->findChild<QPushButton *>("saveEvent")); dialog->reject(); });
        window.findChild<QPushButton *>("calendarEvent")->click();
        auto engine = window.simulationEngine();
        auto data = engine.calendar().event(1)->data;
        data.participantIds = {9}; data.organizerId = 9; data.roomId = 2;
        QVERIFY(engine.updateEvent(1, data, {1, UserRole::Secretary, {}}).ok());
        window.restoreState(std::move(engine));
        inDialog([](QDialog *dialog) { QVERIFY(!dialog->findChild<QPushButton *>("saveEvent")); dialog->reject(); });
        window.findChild<QPushButton *>("calendarEvent")->click();
    }
    void officeMovesParticipantsBetweenHomeAndMeeting() {
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
        QCOMPARE(circles().size(), window.calendarService().catalog().employees.size());
        for (auto *circle : circles())
            QCOMPARE(circle->data(1).toInt(), window.calendarService().catalog().employees.value(circle->data(0).toInt()).departmentId);
        const int hallId = window.calendarService().catalog().rooms.lastKey();
        seedEvent(window, "Conference", QTime(9,10), QTime(9,40), hallId);
        QTRY_COMPARE(window.calendarService().calendar().events.size(), 1);
        for (auto *circle : circles()) QVERIFY(circle->data(1).toInt() != hallId);
        for (int i = 0; i < 19; ++i) window.findChild<QPushButton *>("stepCalendar")->click();
        QCOMPARE(window.calendarService().event(1)->status, EventStatus::InProgress);
        QCOMPARE(circles().size(), window.calendarService().catalog().employees.size());
        int inHall = 0;
        for (auto *circle : circles()) {
            const bool attending = window.calendarService().event(1)->data.participantIds.contains(circle->data(0).toInt());
            QCOMPARE(circle->data(1).toInt() == hallId, attending);
            if (attending) ++inHall;
        }
        QCOMPARE(inHall, window.calendarService().event(1)->data.participantIds.size());
        // Calendar filters must not hide the current occupants of the office.
        window.findChild<QComboBox *>("departmentFilter")->setCurrentIndex(2);
        QVERIFY(!circles().isEmpty());
        window.findChild<QPushButton *>("stepCalendar")->click();
        QCOMPARE(window.calendarService().event(1)->status, EventStatus::Completed);
        for (auto *circle : circles()) QVERIFY(circle->data(1).toInt() != hallId);
        window.findChild<QPushButton *>("resetSimulation")->click();
        for (auto *circle : circles()) QVERIFY(circle->data(1).toInt() != hallId);
    }
    void officeAnimationUsesOrthogonalRoutesAndRespectsBudget() {
        auto config = setup();
        CalendarService calendar(makeCompanyCatalog(config), modelDateTime(config.startDate), modelDateTime(config.startDate.addDays(config.dayCount)));
        QGraphicsScene scene;
        OfficeAnimator animator(scene);
        animator.update(calendar, 0);
        const UserContext actor{1, UserRole::Secretary, {}};
        EventData meeting;
        meeting.title = "Animated meeting"; meeting.type = EventType::Planning;
        meeting.start = modelDateTime(config.startDate, QTime(9, 0)); meeting.end = meeting.start.addSecs(3600);
        meeting.roomId = 1; meeting.participantIds = {1, config.employeesPerDepartment + 1}; meeting.organizerId = 1;
        QVERIFY(calendar.createEvent(meeting, actor).ok());
        QVERIFY(calendar.advanceTo(meeting.start).ok());
        animator.update(calendar, 200);
        auto animations = animator.findChildren<QVariantAnimation *>();
        QVERIFY(!animations.isEmpty());
        bool hasInternalRoute = false, hasCorridorRoute = false;
        for (auto *animation : animations) {
            QVERIFY(animation->duration() <= 200);
            const auto points = animation->property("waypoints").toList();
            QVERIFY(points.size() >= 2);
            qreal travelled = 0;
            for (int i = 1; i < points.size(); ++i) {
                const auto before = points[i - 1].toPointF(), after = points[i].toPointF();
                QVERIFY(before.x() == after.x() || before.y() == after.y());
                travelled += std::abs(before.x() - after.x()) + std::abs(before.y() - after.y());
            }
            QVERIFY(travelled > 0);
            if (animation->property("employeeId").toInt() == 1) hasInternalRoute = true;
            if (animation->property("employeeId").toInt() == config.employeesPerDepartment + 1) {
                hasCorridorRoute = true;
                QVERIFY(points.size() >= 6);
            }
        }
        QVERIFY(hasInternalRoute); QVERIFY(hasCorridorRoute);
        animator.pause();
        auto *animation = animations.first();
        QCOMPARE(animation->state(), QAbstractAnimation::Paused);
        animation->setCurrentTime(animation->duration() / 3);
        const qreal progress = animation->currentValue().toReal();
        animator.retime(40);
        QCOMPARE(animation->duration(), 40);
        QCOMPARE(animation->state(), QAbstractAnimation::Paused);
        QCOMPARE(animation->currentValue().toReal(), progress);
        animator.resume();
        QCOMPARE(animation->state(), QAbstractAnimation::Running);
        QTest::qWait(70);
        for (auto *motion : animator.findChildren<QVariantAnimation *>())
            QCOMPARE(motion->state(), QAbstractAnimation::Stopped);
        for (auto *item : scene.items())
            if (item->data(2) == "employee") QVERIFY(QLineF(QPointF(), item->pos()).length() < 0.001);
        QVERIFY(calendar.advanceTo(meeting.end).ok());
        animator.update(calendar, 100);
        animator.finish(); // A rapid next step must finish all previous movement before rebuilding.
        QVERIFY(animator.findChildren<QVariantAnimation *>().isEmpty());
        for (auto *item : scene.items())
            if (item->data(2) == "employee") QVERIFY(QLineF(QPointF(), item->pos()).length() < 0.001);
    }
    void officeAnimationFollowsWindowSpeedPauseAndReset() {
        auto config = setup(); config.animationTickMs = 1000;
        SimulationWindow window(config);
        window.show();
        seedEvent(window, "Moving", QTime(9, 10), QTime(10, 40));
        QTRY_COMPARE(window.calendarService().calendar().events.size(), 1);
        for (int i = 0; i < 19; ++i) window.findChild<QPushButton *>("stepCalendar")->click();
        auto *scene = window.findChild<QGraphicsScene *>("officeScene");
        auto animations = scene->findChildren<QVariantAnimation *>("officeMovement");
        QVERIFY(!animations.isEmpty());
        for (auto *motion : animations) QVERIFY(motion->duration() <= 800);
        auto *speed = window.findChild<QComboBox *>("simulationSpeed");
        speed->setCurrentIndex(speed->findData(5.0));
        for (auto *motion : animations) QVERIFY(motion->duration() <= 160);
        window.findChild<QPushButton *>("startSimulation")->click();
        window.findChild<QPushButton *>("pauseSimulation")->click();
        for (auto *motion : animations) QCOMPARE(motion->state(), QAbstractAnimation::Paused);
        window.findChild<QPushButton *>("startSimulation")->click();
        for (auto *motion : animations) QCOMPARE(motion->state(), QAbstractAnimation::Running);
        window.findChild<QPushButton *>("resetSimulation")->click();
        QVERIFY(scene->findChildren<QVariantAnimation *>("officeMovement").isEmpty());
        for (auto *item : scene->items())
            if (item->data(2) == "employee") QCOMPARE(item->pos(), QPointF());
    }
    void officeSeparatesWorkplacesAndMeetings() {
        auto config = setup();
        CalendarService calendar(makeCompanyCatalog(config), modelDateTime(config.startDate), modelDateTime(config.startDate.addDays(config.dayCount)));
        QGraphicsScene scene;
        const UserContext actor{1, UserRole::Secretary, {}};
        auto employeeZones = [&] {
            populateOfficeScene(scene, calendar);
            QMap<int, QString> result;
            for (auto *item : scene.items())
                if (item->data(2).toString() == "employee") result.insert(item->data(0).toInt(), item->data(3).toString());
            return result;
        };
        auto zones = employeeZones();
        QCOMPARE(zones.size(), calendar.catalog().employees.size());
        for (const auto &zone : zones) QCOMPARE(zone, QString("work"));
        int workZones = 0, meetingZones = 0;
        const int hall = calendar.catalog().rooms.lastKey();
        for (auto *item : scene.items()) {
            const auto kind = item->data(2).toString();
            if (kind == "workZone" || kind == "meetingZone") {
                QVERIFY(item->data(0).toInt() != hall);
                if (kind == "workZone") ++workZones; else ++meetingZones;
            }
        }
        QCOMPARE(workZones, config.departmentCount);
        QCOMPARE(meetingZones, config.departmentCount);
        EventData meeting;
        meeting.title = "Department meeting"; meeting.type = EventType::Planning;
        meeting.start = modelDateTime(config.startDate, QTime(9, 0)); meeting.end = meeting.start.addSecs(3600);
        meeting.roomId = 1; meeting.participantIds = {1, config.employeesPerDepartment + 1}; meeting.organizerId = 1;
        QVERIFY(calendar.createEvent(meeting, actor).ok());
        QCOMPARE(employeeZones().value(1), QString("work"));
        QVERIFY(calendar.advanceTo(meeting.start).ok());
        zones = employeeZones();
        QCOMPARE(zones.value(1), QString("meeting"));
        QCOMPARE(zones.value(config.employeesPerDepartment + 1), QString("meeting"));
        QCOMPARE(zones.value(2), QString("work"));
        QMap<QString, QRectF> firstRoomZones;
        for (auto *item : scene.items())
            if (item->data(0).toInt() == 1 && (item->data(2) == "workZone" || item->data(2) == "meetingZone"))
                firstRoomZones.insert(item->data(2).toString(), item->sceneBoundingRect());
        for (auto *item : scene.items()) {
            if (item->data(2) != "employee" || item->data(1).toInt() != 1) continue;
            const auto key = item->data(3) == "meeting" ? "meetingZone" : "workZone";
            QVERIFY(firstRoomZones.value(key).contains(item->sceneBoundingRect()));
        }
        QVERIFY(calendar.advanceTo(meeting.end).ok());
        zones = employeeZones();
        QCOMPARE(zones.value(1), QString("work"));
        QCOMPARE(zones.value(config.employeesPerDepartment + 1), QString("work"));
        QCOMPARE(zones.size(), calendar.catalog().employees.size());
    }
    void stepStartsCompletesAndArchives() {
        SimulationWindow window(setup());
        window.show();
        seedEvent(window, "Short", QTime(9, 10), QTime(9, 40));
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
            seedEvent(window, title, QTime(hour, 0), QTime(hour + 1, 0));
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
        QCOMPARE(window->calendarService().calendar().events.size(), 26);
        QVERIFY(!window->findChildren<QPushButton *>("calendarEvent").isEmpty());
        window->findChild<QPushButton *>("stepCalendar")->click();
        QCOMPARE(window->calendarService().modelTime().time(), QTime(1, 0));
        for (int step = 0; step < 17; ++step) window->findChild<QPushButton *>("stepCalendar")->click();
        QVERIFY(!window->simulationEngine().log().isEmpty());
        QVERIFY(window->grab().save(QDir::temp().filePath("modeling-calendar-preview.png")));
    }
};
QTEST_MAIN(CalendarWindowTest)
#include "calendarwindow_test.moc"
