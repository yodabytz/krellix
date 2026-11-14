#pragma once
#include "SensorStat.h"
#include <QByteArray>

namespace WindowsSensors {
struct Snapshot {
    QList<SensorReading> readings;
    QString status;
    bool valid = false;
};
Snapshot parse(const QByteArray &line);
QList<SensorReading> read();
QString status();
}
