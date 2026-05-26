#pragma once

#include <QList>
#include <QString>

struct SensorReading {
    enum Type { Temp, Fan, Voltage, Percent };

    QString chip;       // hwmon chip name ("coretemp", "k10temp", "nvme", ...)
    QString label;      // human label from temp{N}_label, sysctl key, etc.
    double  value = 0;  // °C for Temp, RPM for Fan, V for Voltage, % for Percent
    Type    type  = Temp;
};

class SensorStat
{
public:
    // Reads platform sensor data. Linux walks /sys/class/hwmon/hwmon*/.
    // macOS uses native/installed command probes when direct hwmon data is
    // unavailable. Empty list on failure.
    static QList<SensorReading> read();

    using ReadFn = QList<SensorReading> (*)();
    static void setReadOverride(ReadFn fn);
};
