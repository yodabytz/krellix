#include "sysdep/BatteryStat.h"
#include "sysdep/CpuStat.h"
#include "sysdep/DiskStat.h"
#include "sysdep/MemStat.h"
#include "sysdep/NetPortStat.h"
#include "sysdep/NetStat.h"
#include "sysdep/ProcStat.h"
#include "sysdep/SensorStat.h"
#include "sysdep/UptimeStat.h"

#include <QCoreApplication>
#include <QDebug>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QUdpSocket>

#include <algorithm>
#include <memory>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    const auto check = [&](bool condition, const char *message) {
        if (!condition) {
            qCritical() << message;
            ++failures;
        }
    };

    const auto first = CpuStat::read();
    QThread::msleep(150);
    const auto cpus = CpuStat::read();
    check(!first.isEmpty() && !cpus.isEmpty(), "No native CPU samples");
    if (!first.isEmpty() && !cpus.isEmpty()) {
        check(cpus.first().index == -1 && cpus.first().name == QLatin1String("cpu"),
              "Aggregate CPU sample missing");
        check(cpus.first().totalAll() > first.first().totalAll(), "CPU counters did not advance");
        for (qsizetype i = 1; i < cpus.size(); ++i) {
            check(cpus[i].index > cpus[i - 1].index && cpus[i].totalAll() > 0,
                  "Invalid per-CPU counter or ordering");
        }
    }
    const auto mem = MemStat::read();
    const auto uptime = UptimeStat::secondsSinceBoot();
    const auto proc = ProcStat::read();
    const auto net = NetStat::read();
    const auto disks = DiskStat::read();
    const auto sensors = SensorStat::read();
    const auto battery = BatteryStat::read();
    check(mem.valid() && mem.availableKb <= mem.totalKb && mem.swapFreeKb <= mem.swapTotalKb,
          "Invalid physical memory or swap counters");
    check(uptime >= 0, "No native uptime sample");
    check(proc.processes > 0, "No native process data");
    for (const auto &iface : net)
        check(!iface.name.isEmpty() && iface.name != QLatin1String("lo0"), "Invalid network interface");
    for (const auto &disk : disks)
        check(!disk.name.isEmpty(), "Unnamed disk");

    QTcpServer listener;
    QTcpSocket client;
    QUdpSocket udp;
    const bool listening = listener.listen(QHostAddress::LocalHost);
    check(listening, "Could not open a TCP test listener on loopback");
    if (listening)
        client.connectToHost(QHostAddress::LocalHost, listener.serverPort());
    const bool connected = listening && client.waitForConnected(1500) && listener.waitForNewConnection(1500);
    check(connected, "Could not establish a TCP test connection");
    std::unique_ptr<QTcpSocket> peer(listener.nextPendingConnection());
    const bool bound = udp.bind(QHostAddress(QHostAddress::LocalHost), quint16(0));
    check(bound, "Could not bind a UDP test socket on loopback");

    QTcpServer listener6;
    QTcpSocket client6;
    const bool listening6 = listener6.listen(QHostAddress::LocalHostIPv6);
    if (listening6)
        client6.connectToHost(QHostAddress::LocalHostIPv6, listener6.serverPort());
    const bool connected6 = listening6 && client6.waitForConnected(1500) && listener6.waitForNewConnection(1500);
    if (listening6)
        check(connected6, "Could not establish an IPv6 TCP test connection");
    std::unique_ptr<QTcpSocket> peer6(listener6.nextPendingConnection());

    const auto ports = NetPortStat::read();
    if (connected) {
        const auto tcp = std::count_if(ports.cbegin(), ports.cend(), [&](const NetPortSample &sample) {
            return sample.protocol == QLatin1String("tcp") && sample.localPort == listener.serverPort()
                && sample.remotePort == client.localPort() && sample.state == QLatin1String("01")
                && QHostAddress(sample.remoteAddress) == QHostAddress::LocalHost;
        });
        check(tcp == 1, "Net Ports did not report exactly one established TCP peer");
    }
    if (connected6) {
        const auto tcp = std::count_if(ports.cbegin(), ports.cend(), [&](const NetPortSample &sample) {
            return sample.protocol == QLatin1String("tcp") && sample.localPort == listener6.serverPort()
                && sample.remotePort == client6.localPort() && sample.state == QLatin1String("01")
                && QHostAddress(sample.remoteAddress) == QHostAddress::LocalHostIPv6;
        });
        check(tcp == 1, "Net Ports did not decode the IPv6 peer and port");
    }
    if (bound) {
        const auto datagrams = std::count_if(ports.cbegin(), ports.cend(), [&](const NetPortSample &sample) {
            return sample.protocol == QLatin1String("udp") && sample.localPort == udp.localPort()
                && sample.remoteAddress.isEmpty() && sample.remotePort == 0;
        });
        check(datagrams == 1, "Net Ports did not report exactly one bound UDP socket");
    }

    NetStat::setReadOverride([] { return QList<NetSample>{}; });
    check(NetStat::read().isEmpty(), "Remote empty network data fell back to local interfaces");
    NetStat::setReadOverride(nullptr);
    UptimeStat::setReadOverride([]() -> qint64 { return 42; });
    check(UptimeStat::secondsSinceBoot() == 42, "Remote uptime override was ignored");
    UptimeStat::setReadOverride(nullptr);

    qInfo() << "CPU samples:" << cpus.size() << "memory KiB:" << mem.totalKb
            << "swap KiB:" << mem.swapTotalKb << "processes:" << proc.processes
            << "uptime seconds:" << uptime << "interfaces:" << net.size()
            << "disks:" << disks.size() << "ports:" << ports.size()
            << "sensors:" << sensors.size() << "battery available:" << battery.present;
    return failures ? 1 : 0;
}
