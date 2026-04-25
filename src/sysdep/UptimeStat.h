#pragma once

#include <QtGlobal>

class UptimeStat
{
public:
    // Reads /proc/uptime; returns -1 on failure. The integer floor of the
    // first whitespace-separated value (system uptime in seconds).
    static qint64 secondsSinceBoot();
};
