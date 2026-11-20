#include "Metar.h"

#include <QByteArray>
#include <QtGlobal>

using namespace KrellweatherWindows;

int main()
{
    if (!isStationId(QStringLiteral("KMIA")) || !isStationId(QStringLiteral("EGLL"))
        || isStationId(QStringLiteral("KMIA,KJFK")) || isStationId(QStringLiteral("KMIA\n")))
        qFatal("ICAO identifier validation failed");

    const QByteArray data(R"([
        {"icaoId":"KJFK","obsTime":1795211800,"temp":11},
        {"icaoId":"KMIA","obsTime":1795211000,"temp":25},
        {"icaoId":"KMIA","obsTime":1795211580,"temp":26.1,"wspd":8,"cover":"SCT"}
    ])");
    const auto selected = selectObservation(data, QStringLiteral("KMIA"));
    if (observationTime(selected).toSecsSinceEpoch() != 1795211580
        || selected.value(QStringLiteral("temp")).toDouble() != 26.1)
        qFatal("Latest matching numeric METAR was not selected");
    const auto iso = selectObservation(
        QByteArray(R"([{"icaoId":"EGLL","reportTime":"2026-11-20T22:00:00.000Z"}])"),
        QStringLiteral("EGLL"));
    if (observationTime(iso).time() != QTime(22, 0))
        qFatal("Missing obsTime did not fall back to the published report time");
    for (const auto &bad : {QByteArray(), QByteArray("[]"), QByteArray("{"),
                            QByteArray(R"([{"icaoId":"KJFK","temp":5}])"),
                            QByteArray(kMaxBytes + 1, ' ')}) {
        if (!selectObservation(bad, QStringLiteral("KMIA")).isEmpty())
            qFatal("An invalid, oversized or mismatched METAR was accepted");
    }
}
