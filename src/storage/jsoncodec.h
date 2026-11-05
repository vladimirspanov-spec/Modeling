#pragma once
#include "src/services/calendarsetup.h"
#include <QJsonObject>

namespace JsonCodec {
QJsonObject setup(const CalendarSetup &value);
CalendarSetup setup(const QJsonObject &object);
QJsonObject event(const EventData &value);
EventData event(const QJsonObject &object);
QJsonObject actor(const UserContext &value);
UserContext actor(const QJsonObject &object);
QDateTime time(const QJsonValue &value);
QJsonObject read(const QString &path);
void write(const QString &path, const QJsonObject &object);
void writeBytes(const QString &path, const QByteArray &bytes);
}
