#pragma once

#include <QString>

struct BatteryInfo {
    bool    present       = false;
    int     percent       = 0;        // 0..100
    QString status;                   // "Charging" / "Discharging" / "Full" / "Unknown"
    bool    acOnline      = false;
    qint64  timeRemainSec = -1;       // -1 when unknown
    QString batteryName;              // e.g. "BAT0"
};

class BatteryStat
{
public:
    // Reads /sys/class/power_supply/. Picks the first BAT* directory
    // found. Returns BatteryInfo with present=false if no battery present.
    static BatteryInfo read();

    using ReadFn = BatteryInfo (*)();
    static void setReadOverride(ReadFn fn);
};
