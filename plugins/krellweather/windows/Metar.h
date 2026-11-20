#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QString>

namespace KrellweatherWindows {
inline constexpr int kMinIntervalMs = 60000;
inline constexpr int kMaxIntervalMs = 3600000;
inline constexpr qsizetype kMaxBytes = 256 * 1024;

bool isStationId(const QString &station);
QDateTime observationTime(const QJsonObject &observation);
QJsonObject selectObservation(const QByteArray &payload, const QString &station);
}
