#pragma once
#include "SensorStat.h"
#include <QHash>

// Compact temperatures by chip, keeping the hottest actual reading and its
// thresholds. Detailed mode preserves the complete telemetry unchanged.
inline QList<SensorReading> sensorDisplayReadings(const QList<SensorReading> &readings, bool detailed)
{
    if (detailed) return readings;
    QList<SensorReading> result;
    QHash<QString, int> temperatures;
    for (const auto &reading : readings) {
        if (reading.type != SensorReading::Temp || reading.chip == QStringLiteral("macOS")) {
            result.append(reading);
            continue;
        }
        const QString chip = reading.chip.isEmpty() ? QStringLiteral("Temperature") : reading.chip;
        if (!temperatures.contains(chip)) {
            temperatures.insert(chip, result.size());
            result.append(reading);
        } else if (reading.value > result[temperatures.value(chip)].value) {
            result[temperatures.value(chip)] = reading;
        }
        result[temperatures.value(chip)].label = chip + QStringLiteral(" max");
    }
    return result;
}
