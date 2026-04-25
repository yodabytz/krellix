#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

struct NetSample {
    QString name;        // interface name (eth0, wlan0, ...)
    quint64 rxBytes  = 0;
    quint64 txBytes  = 0;
    quint64 rxPackets = 0;
    quint64 txPackets = 0;
};

class NetStat
{
public:
    // Reads /proc/net/dev. Filters out the loopback ("lo"); empty list on
    // failure. Order is the order in which the kernel listed them.
    static QList<NetSample> read();
};
