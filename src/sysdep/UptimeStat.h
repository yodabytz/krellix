#pragma once

#include <QtGlobal>

class UptimeStat
{
public:
    // Reads /proc/uptime; returns -1 on failure. Honors a remote override
    // (setReadOverride) when set.
    static qint64 secondsSinceBoot();

    using ReadFn = qint64 (*)();
    static void setReadOverride(ReadFn fn);
};
