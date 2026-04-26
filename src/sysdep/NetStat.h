#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

struct NetSample {
    QString name;        // interface name (eth0, wlan0, ...)
    QString alias;       // friendly name when known (e.g. Docker network name)
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
    // Honors a remote override (setReadOverride) when set.
    static QList<NetSample> read();

    using ReadFn = QList<NetSample> (*)();
    static void setReadOverride(ReadFn fn);

    // Heuristic — true for "main" physical/wireless interfaces (eth*, en*,
    // wlan*, wlp*, ww*), false for virtualization/container plumbing
    // (docker*, virbr*, veth*, br-*, tun*, tap*, vmnet*, wg*).
    // Used as the default for monitors/net/<iface> when not explicitly set.
    static bool isMainInterface(const QString &name);
};
