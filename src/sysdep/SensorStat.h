#pragma once

#include <QList>
#include <QString>

struct SensorReading {
    enum Type { Temp, Fan, Voltage };

    QString chip;       // hwmon chip name ("coretemp", "k10temp", "nvme", ...)
    QString label;      // human label from temp{N}_label or "tempN"
    double  value = 0;  // °C for Temp, RPM for Fan, V for Voltage
    Type    type  = Temp;
};

class SensorStat
{
public:
    // Walks /sys/class/hwmon/hwmon*/ and returns the temperatures it can
    // read. (Fan and voltage support is stubbed in the type but not
    // populated in this first pass.) Empty list on failure.
    static QList<SensorReading> read();

    using ReadFn = QList<SensorReading> (*)();
    static void setReadOverride(ReadFn fn);
};
