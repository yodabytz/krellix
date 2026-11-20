#include "Metar.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimeZone>

namespace KrellweatherWindows {
bool isStationId(const QString &station)
{
    static const QRegularExpression format(QStringLiteral("\\A[A-Z][A-Z0-9]{3}\\z"));
    return format.match(station).hasMatch();
}

QDateTime observationTime(const QJsonObject &observation)
{
    const auto observed = observation.value(QStringLiteral("obsTime"));
    const qint64 epochSeconds = observed.toInteger(-1);
    if (epochSeconds >= 0 && epochSeconds < 253402300800LL)
        return QDateTime::fromSecsSinceEpoch(epochSeconds, QTimeZone::utc());
    const auto iso = QDateTime::fromString(observed.toString(), Qt::ISODate);
    if (iso.isValid()) return iso.toUTC();
    return QDateTime::fromString(observation.value(QStringLiteral("reportTime")).toString(),
                                 Qt::ISODate).toUTC();
}

QJsonObject selectObservation(const QByteArray &payload, const QString &station)
{
    if (payload.size() > kMaxBytes || !isStationId(station)) return {};
    const auto document = QJsonDocument::fromJson(payload);
    if (!document.isArray()) return {};
    const auto observations = document.array();
    QJsonObject best;
    QDateTime bestTime;
    for (const auto &entry : observations) {
        const auto observation = entry.toObject();
        if (observation.value(QStringLiteral("icaoId")).toString() != station) continue;
        const auto observed = observationTime(observation);
        if (best.isEmpty() || (observed.isValid() && (!bestTime.isValid() || observed > bestTime))) {
            best = observation;
            bestTime = observed;
        }
    }
    return best;
}
}
