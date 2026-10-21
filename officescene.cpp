#include "officescene.h"
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QFontMetrics>
#include <QPainterPath>
#include <QPen>
#include <QVariantAnimation>
#include <QElapsedTimer>
#include <cmath>
#include <algorithm>

namespace {
QColor departmentColor(int id) { return QColor::fromHsv(((id - 1) * 47) % 360, 130, 185); }
QGraphicsTextItem *text(QGraphicsScene &scene, const QString &value, qreal x, qreal y,
                        qreal width, const QColor &color, bool bold = false)
{
    auto *item = scene.addText(value);
    QFont font = item->font(); font.setBold(bold); font.setPointSize(10);
    item->setFont(font);
    item->setDefaultTextColor(color);
    item->setTextWidth(width);
    item->setPos(x, y);
    return item;
}
}

void populateOfficeScene(QGraphicsScene &scene, const CalendarService &calendar)
{
    scene.clear();
    const auto &catalog = calendar.catalog();
    QMap<int, int> homeRooms;
    for (const auto &room : catalog.rooms)
        if (room.departmentId) homeRooms.insert(*room.departmentId, room.id);
    QMap<int, int> destinations;
    QMap<int, QString> activities;
    QMap<int, QString> roomEvents;
    CalendarFilter filter; filter.status = EventStatus::InProgress;
    for (const auto &event : calendar.calendar(filter).events) {
        const int destination = event.data.type == EventType::BusinessTrip ? OfficeLocation::Outside
            : event.data.roomId.value_or(OfficeLocation::Corridor);
        if (destination > 0) roomEvents.insert(destination, event.data.title);
        for (int id : event.data.participantIds) {
            destinations.insert(id, destination);
            activities.insert(id, event.data.title);
        }
    }
    QMap<int, QVector<Employee>> occupants;
    QMap<int, QVector<Employee>> meetingOccupants;
    for (const auto &person : catalog.employees) {
        const int home = homeRooms.value(person.departmentId, OfficeLocation::Corridor);
        const int idleLocation = home > 0 && !calendar.roomAvailable(home) ? OfficeLocation::Corridor : home;
        const int location = !person.available ? OfficeLocation::Sick
            : destinations.value(person.id, idleLocation);
        if (location > 0 && person.available && destinations.contains(person.id))
            meetingOccupants[location].append(person);
        else
            occupants[location].append(person);
    }
    const qreal margin = 28, gap = 16, roomWidth = 294, pitch = 54;
    const int peopleColumns = 4;
    const int columns = std::max(1, (int(catalog.rooms.size()) + 1) / 2);
    int maxPeople = 8;
    for (const auto &people : occupants) maxPeople = std::max(maxPeople, int(people.size()));
    for (const auto &people : meetingOccupants) maxPeople = std::max(maxPeople, int(people.size()));
    const qreal sectionHeight = 80 + ((maxPeople + peopleColumns - 1) / peopleColumns) * pitch;
    const qreal roomHeight = 48 + 2 * sectionHeight + 44;
    const qreal corridorHeight = std::max(qreal(112), qreal(56 + ((occupants.value(OfficeLocation::Corridor).size() + columns * 4 - 1) / (columns * 4)) * pitch));
    const qreal officeWidth = columns * roomWidth + (columns - 1) * gap;
    const qreal corridorY = margin + 40 + roomHeight;
    const qreal buildingHeight = roomHeight * 2 + corridorHeight;
    const qreal top = margin + 40;
    const QColor wall("#475569"), ink("#1e293b"), muted("#64748b"), hallway("#e2e8f0");
    text(scene, QObject::tr("ОФИС КОМПАНИИ"), margin, margin - 8, officeWidth, ink, true);
    scene.addRect(margin - 8, top - 8, officeWidth + 16, buildingHeight + 16, QPen(wall, 3), QBrush(hallway));
    scene.addRect(margin, corridorY, officeWidth, corridorHeight, Qt::NoPen, QBrush(hallway));
    text(scene, QObject::tr("ОБЩИЙ КОРИДОР · проход к комнатам и выходу →"), margin + 12, corridorY + 8, officeWidth - 24, muted);
    scene.addLine(margin + 16, corridorY + corridorHeight - 20, margin + officeWidth - 12,
                  corridorY + corridorHeight - 20, QPen(QColor("#cbd5e1"), 1, Qt::DashLine));
    QMap<int, QRectF> areas;
    QMap<int, QRectF> meetingAreas;
    int index = 0;
    for (const auto &room : catalog.rooms) {
        const bool upper = index < columns;
        const qreal x = margin + (index % columns) * (roomWidth + gap);
        const qreal y = upper ? top : corridorY + corridorHeight;
        ++index;
        QRectF area(x, y, roomWidth, roomHeight);
        areas.insert(room.id, area);
        const QColor accent = room.departmentId ? departmentColor(*room.departmentId) : QColor("#64748b");
        const bool inUse = roomEvents.contains(room.id);
        const bool available = calendar.roomAvailable(room.id);
        auto *outline = scene.addRect(area, QPen(wall, 2), QBrush(available ? (inUse ? accent.lighter(190) : QColor("#ffffff")) : QColor("#f1f5f9")));
        outline->setData(0, room.id); outline->setData(2, "room");
        outline->setToolTip(room.name);
        scene.addRect(x + 2, y + 2, roomWidth - 4, 5, Qt::NoPen, QBrush(accent));
        text(scene, room.departmentId ? catalog.departments.value(*room.departmentId).name : room.name,
             x + 10, y + 12, roomWidth - 20, ink, true);
        if (room.departmentId) {
            const QRectF workArea(x, y + 48, roomWidth, sectionHeight);
            const QRectF meetingArea(x, y + 48 + sectionHeight, roomWidth, sectionHeight);
            areas.insert(room.id, workArea);
            meetingAreas.insert(room.id, meetingArea);
            auto *workZone = scene.addRect(workArea.adjusted(2, 0, -2, 0), Qt::NoPen, QBrush(QColor("#f8fafc")));
            workZone->setData(0, room.id); workZone->setData(2, "workZone");
            auto *meetingZone = scene.addRect(meetingArea.adjusted(2, 0, -2, 0), Qt::NoPen,
                QBrush(inUse ? accent.lighter(190) : QColor("#f0f5fa")));
            meetingZone->setData(0, room.id); meetingZone->setData(2, "meetingZone");
            const qreal divider = meetingArea.top();
            // A partition with a passage on the right keeps both parts connected.
            scene.addLine(x + 2, divider, x + roomWidth - 72, divider, QPen(QColor("#94a3b8"), 1.5, Qt::DashLine));
            text(scene, QObject::tr("Рабочие места"), x + 10, workArea.top() + 2, roomWidth - 20, ink, true);
            text(scene, QObject::tr("%1 сотрудников").arg(occupants.value(room.id).size()),
                 x + 10, workArea.top() + 26, roomWidth - 20, muted);
            text(scene, QObject::tr("Зона совещаний"), x + 10, meetingArea.top() + 2, roomWidth - 20, ink, true);
            const QString description = !available ? QObject::tr("Помещение на ремонте")
                : inUse ? roomEvents.value(room.id) : QObject::tr("Нет текущих мероприятий");
            auto *subtitle = text(scene, QFontMetrics(QFont()).elidedText(description, Qt::ElideRight, int(roomWidth - 28)),
                                 x + 10, meetingArea.top() + 26, roomWidth - 20, muted);
            subtitle->setToolTip(description);
        } else {
            meetingAreas.insert(room.id, area);
            const QString description = !available ? QObject::tr("Помещение на ремонте")
                : inUse ? roomEvents.value(room.id) : QObject::tr("Нет текущих мероприятий");
            auto *subtitle = text(scene, QFontMetrics(QFont()).elidedText(description, Qt::ElideRight, int(roomWidth - 28)),
                                 x + 10, y + 48, roomWidth - 20, muted);
            subtitle->setToolTip(description);
        }
        // A gap in the wall and an inward opening door connect every room to the corridor.
        const qreal doorX = x + roomWidth - 66;
        const qreal doorY = upper ? y + roomHeight : y;
        scene.addLine(doorX, doorY, doorX + 42, doorY, QPen(hallway, 5));
        const qreal inward = upper ? -1 : 1;
        scene.addLine(doorX, doorY, doorX, doorY + inward * 38, QPen(wall, 1.5));
        QPainterPath swing;
        swing.moveTo(doorX + 38, doorY);
        swing.quadTo(doorX + 38, doorY + inward * 38, doorX, doorY + inward * 38);
        scene.addPath(swing, QPen(QColor("#94a3b8"), 1, Qt::DashLine));
    }
    // The side zones are outside the building outline. The exit meets the corridor.
    const qreal sideX = margin + officeWidth + 64;
    const qreal sideWidth = roomWidth + 16;
    const qreal zoneHeight = roomHeight;
    scene.addLine(margin + officeWidth, corridorY + corridorHeight / 2,
                  sideX, corridorY + corridorHeight / 2, QPen(muted, 2, Qt::DashLine));
    scene.addLine(margin + officeWidth + 8, corridorY + 30,
                  margin + officeWidth + 8, corridorY + corridorHeight - 30, QPen(hallway, 7));
    text(scene, QObject::tr("ВЫХОД →"), margin + officeWidth + 10, corridorY + 10, 90, muted);
    auto zone = [&](int id, const QRectF &rect, const QString &title, const QString &hint, const QColor &color) {
        areas.insert(id, rect);
        scene.addRect(rect, QPen(color, 1.5, Qt::DashLine), QBrush(color.lighter(190)));
        text(scene, title, rect.x() + 10, rect.y() + 12, rect.width() - 20, color.darker(130), true);
        text(scene, hint, rect.x() + 10, rect.y() + 45, rect.width() - 20, muted);
    };
    zone(OfficeLocation::Outside, QRectF(sideX, top, sideWidth, zoneHeight),
         QObject::tr("ВНЕ ОФИСА"), QObject::tr("Командировки"), QColor("#3b82b6"));
    zone(OfficeLocation::Sick, QRectF(sideX, corridorY + corridorHeight, sideWidth, zoneHeight),
         QObject::tr("БОЛЕЮТ"), QObject::tr("Временно отсутствуют"), QColor("#b46a7c"));
    areas.insert(OfficeLocation::Corridor, QRectF(margin, corridorY - 48, officeWidth, corridorHeight + 48));
    auto drawPeople = [&](const QMap<int, QVector<Employee>> &groups, bool meeting) {
    for (auto group = groups.cbegin(); group != groups.cend(); ++group) {
        const QRectF area = meeting ? meetingAreas.value(group.key()) : areas.value(group.key());
        const bool dividedRoom = group.key() > 0 && catalog.rooms.value(group.key()).departmentId.has_value();
        const int countAcross = group.key() == OfficeLocation::Corridor ? columns * 4 : peopleColumns;
        int position = 0;
        for (const auto &person : group.value()) {
            const qreal x = area.x() + 12 + (position % countAcross) * pitch;
            const qreal y = area.y() + (dividedRoom ? 60 : 92) + (position / countAcross) * pitch;
            ++position;
            const bool sick = group.key() == OfficeLocation::Sick;
            const QColor color = sick ? QColor("#a8a4ac") : departmentColor(person.departmentId);
            auto *circle = scene.addEllipse(x, y, 42, 42, QPen(color.darker(120), 1.5), QBrush(color.lighter(175)));
            circle->setData(0, person.id); circle->setData(1, group.key()); circle->setData(2, "employee");
            circle->setData(3, meeting ? "meeting" : dividedRoom ? "work" : "other");
            // Navigation points are centers; a clear aisle leads through each partition and door.
            const qreal aisleX = group.key() > 0 ? areas.value(group.key()).x() + roomWidth - 45
                : group.key() == OfficeLocation::Corridor ? x + 21 : sideX - 32;
            circle->setData(4, QPointF(aisleX, y + 21));
            circle->setData(5, QPointF(aisleX, corridorY + corridorHeight / 2));
            circle->setZValue(10);
            const QString state = sick ? QObject::tr("Болеет") : group.key() == OfficeLocation::Outside ? QObject::tr("В командировке")
                : activities.contains(person.id) ? activities.value(person.id) : QObject::tr("На рабочем месте");
            circle->setToolTip(QStringLiteral("%1 · %2\n%3\n%4").arg(person.publicId, person.name,
                catalog.departments.value(person.departmentId).name, state));
            auto *label = new QGraphicsTextItem(person.publicId, circle);
            QFont font = label->font(); font.setPointSize(9); label->setFont(font);
            label->setDefaultTextColor(ink);
            const auto bounds = label->boundingRect();
            label->setScale(std::min(qreal(1), 39 / bounds.width()));
            label->setPos(x + (42 - bounds.width() * label->scale()) / 2, y + (42 - bounds.height() * label->scale()) / 2);
            if (sick) {
                auto *badge = new QGraphicsTextItem(QObject::tr("Б"), circle);
                badge->setDefaultTextColor(QColor("#9f3456"));
                QFont badgeFont = badge->font(); badgeFont.setBold(true); badge->setFont(badgeFont);
                badge->setPos(x + 30, y - 12);
            }
        }
    }
    };
    drawPeople(occupants, false);
    drawPeople(meetingOccupants, true);
    const qreal footerY = top + buildingHeight + 24;
    text(scene, QObject::tr("Цвет — отдел сотрудника · ID внутри кружка · Б — болезнь · Движение через проходы и коридор"),
         margin, footerY, sideX + sideWidth - margin, muted);
    scene.setSceneRect(scene.itemsBoundingRect().adjusted(-20, -20, 20, 20));
}

namespace {
struct OfficeSeat {
    QPointF center;
    QPointF aisle;
    QPointF corridor;
    int location;
    QString section;
};
OfficeSeat seat(QGraphicsItem *item) {
    return {item->sceneBoundingRect().center(), item->data(4).toPointF(), item->data(5).toPointF(),
        item->data(1).toInt(), item->data(3).toString()};
}
QVector<QPointF> route(const OfficeSeat &from, const OfficeSeat &to) {
    QVector<QPointF> points{from.center};
    auto append = [&](QPointF point) { if (points.last() != point) points.append(point); };
    if (from.location == to.location && from.section == to.section) {
        append(QPointF(to.center.x(), from.center.y()));
    } else {
        append(from.aisle);
        if (from.location != to.location) {
            append(from.corridor);
            append(QPointF(to.corridor.x(), from.corridor.y()));
            append(to.corridor);
        }
        append(QPointF(to.aisle.x(), points.last().y()));
        append(to.aisle);
    }
    append(to.center);
    return points;
}
qreal distance(QPointF a, QPointF b) { return std::abs(a.x() - b.x()) + std::abs(a.y() - b.y()); }
QPointF along(const QVector<QPointF> &points, qreal travelled) {
    for (int i = 1; i < points.size(); ++i) {
        const qreal length = distance(points[i - 1], points[i]);
        if (travelled <= length) return points[i - 1] + (points[i] - points[i - 1]) * (travelled / length);
        travelled -= length;
    }
    return points.last();
}
}

OfficeAnimator::OfficeAnimator(QGraphicsScene &scene) : QObject(&scene), scene_(scene) {}

void OfficeAnimator::finish()
{
    for (auto *animation : findChildren<QVariantAnimation *>(QString(), Qt::FindDirectChildrenOnly)) {
        animation->setCurrentTime(animation->duration());
        animation->stop();
        delete animation;
    }
}
void OfficeAnimator::pause()
{
    for (auto *animation : findChildren<QVariantAnimation *>())
        if (animation->state() == QAbstractAnimation::Running) animation->pause();
}
void OfficeAnimator::resume()
{
    for (auto *animation : findChildren<QVariantAnimation *>())
        if (animation->state() == QAbstractAnimation::Paused) animation->resume();
}
void OfficeAnimator::retime(int remainingMs)
{
    if (remainingMs <= 0) { finish(); return; }
    for (auto *animation : findChildren<QVariantAnimation *>()) {
        if (animation->state() == QAbstractAnimation::Stopped) continue;
        const bool paused = animation->state() == QAbstractAnimation::Paused;
        const QVariant current = animation->currentValue();
        animation->stop();
        animation->setStartValue(current);
        animation->setDuration(remainingMs);
        animation->start();
        if (paused) animation->pause();
    }
}
void OfficeAnimator::update(const CalendarService &calendar, int durationMs)
{
    finish(); // No callback may reference items that populateOfficeScene is about to delete.
    QMap<int, OfficeSeat> previous;
    for (auto *item : scene_.items())
        if (item->data(2) == "employee") previous.insert(item->data(0).toInt(), seat(item));
    QElapsedTimer preparation; preparation.start();
    populateOfficeScene(scene_, calendar);
    durationMs = std::max(0, durationMs - int(preparation.elapsed()));
    if (durationMs == 0) return;
    for (auto *item : scene_.items()) {
        if (item->data(2) != "employee" || !previous.contains(item->data(0).toInt())) continue;
        const auto destination = seat(item);
        const auto points = route(previous.value(item->data(0).toInt()), destination);
        qreal length = 0;
        QVariantList waypoints;
        for (const auto &point : points) waypoints.append(point);
        for (int i = 1; i < points.size(); ++i) length += distance(points[i - 1], points[i]);
        if (length == 0) continue;
        auto *animation = new QVariantAnimation(this);
        animation->setObjectName("officeMovement");
        animation->setProperty("employeeId", item->data(0));
        animation->setProperty("waypoints", waypoints);
        animation->setDuration(durationMs);
        animation->setStartValue(qreal(0)); animation->setEndValue(length);
        connect(animation, &QVariantAnimation::valueChanged, this, [item, points, destination](const QVariant &value) {
            item->setPos(along(points, value.toReal()) - destination.center);
        });
        connect(animation, &QVariantAnimation::finished, this, [item] { item->setPos(QPointF()); });
        // Text and sickness badges are children of the circle and move with it.
        item->setPos(points.first() - destination.center);
        animation->start();
    }
}
