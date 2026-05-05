#pragma once

#include <QList>
#include <QString>

struct NetPortSample {
    QString protocol;      // "tcp" or "udp"
    quint16 localPort = 0;
    quint16 remotePort = 0;
    QString state;         // TCP hex state; empty for UDP
};

class NetPortStat
{
public:
    // Reads Linux /proc/net/{tcp,tcp6,udp,udp6}. Honors a remote override
    // when the client is connected to krellixd.
    static QList<NetPortSample> read();

    using ReadFn = QList<NetPortSample> (*)();
    static void setReadOverride(ReadFn fn);
};
