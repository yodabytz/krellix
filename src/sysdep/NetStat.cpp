#include "NetStat.h"

#include <QByteArray>
#include <QByteArrayList>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QLoggingCategory>
#include <QStringList>

Q_LOGGING_CATEGORY(lcNetStat, "krellix.sysdep.net")

namespace {
constexpr qint64 kProcNetDevMaxBytes = 1024 * 1024;  // generous cap
NetStat::ReadFn g_readOverride = nullptr;

// A Linux bridge's own /proc/net/dev counters do not include traffic
// that the bridge merely forwards between member ports — and on Docker,
// where almost everything is FORWARD-chain NAT, that means docker0
// (and br-*) report wildly understated numbers (often frozen for hours
// while containers are actively pushing GBs through veth pairs).
//
// Recover the real per-bridge throughput by summing the member-port
// counters listed under /sys/class/net/<bridge>/brif/. The resulting
// sum reflects all traffic crossing the bridge regardless of whether
// it was locally delivered or forwarded.
QStringList bridgeMembers(const QString &bridgeName)
{
    QDir d(QStringLiteral("/sys/class/net/") + bridgeName +
           QStringLiteral("/brif"));
    if (!d.exists()) return {};
    return d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
}
}

void NetStat::setReadOverride(NetStat::ReadFn fn) { g_readOverride = fn; }

bool NetStat::isMainInterface(const QString &name)
{
    static const QStringList virtualPrefixes = {
        QStringLiteral("docker"), QStringLiteral("virbr"),
        QStringLiteral("veth"),   QStringLiteral("br-"),
        QStringLiteral("tun"),    QStringLiteral("tap"),
        QStringLiteral("vmnet"),  QStringLiteral("wg"),
        QStringLiteral("vnet"),   QStringLiteral("zt"),
    };
    for (const QString &prefix : virtualPrefixes) {
        if (name.startsWith(prefix)) return false;
    }
    return true;
}

QList<NetSample> NetStat::read()
{
    if (g_readOverride) return g_readOverride();
    QFile f(QStringLiteral("/proc/net/dev"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(lcNetStat) << "cannot open /proc/net/dev:" << f.errorString();
        return {};
    }
    const QByteArray bytes = f.read(kProcNetDevMaxBytes);
    if (bytes.isEmpty()) return {};

    QList<NetSample> samples;

    // Format (after two header lines):
    //   "eth0:  rxBytes rxPackets rxErrs ... txBytes txPackets txErrs ..."
    int start = 0;
    int lineNo = 0;
    while (start < bytes.size()) {
        const int nl  = bytes.indexOf('\n', start);
        const int end = (nl < 0) ? bytes.size() : nl;
        if (end > start) {
            ++lineNo;
            if (lineNo > 2) {                 // skip the two header lines
                const QByteArray line = bytes.mid(start, end - start);
                const int colon = line.indexOf(':');
                if (colon > 0) {
                    const QString name =
                        QString::fromLatin1(line.left(colon).trimmed());
                    if (name != QLatin1String("lo")) {
                        const QByteArrayList parts =
                            line.mid(colon + 1).simplified().split(' ');
                        if (parts.size() >= 16) {
                            NetSample s;
                            s.name      = name;
                            s.rxBytes   = parts[0].toULongLong();
                            s.rxPackets = parts[1].toULongLong();
                            s.txBytes   = parts[8].toULongLong();
                            s.txPackets = parts[9].toULongLong();
                            samples.append(s);
                        }
                    }
                }
            }
        }
        if (nl < 0) break;
        start = nl + 1;
    }

    // Bridge fix-up: replace each bridge interface's own counters with
    // the sum of its member-port counters. See comment on bridgeMembers().
    // Indexed-by-name lookup to keep this O(N+M) rather than quadratic.
    QHash<QString, int> indexByName;
    indexByName.reserve(samples.size());
    for (int i = 0; i < samples.size(); ++i)
        indexByName.insert(samples[i].name, i);

    for (NetSample &s : samples) {
        const QStringList members = bridgeMembers(s.name);
        if (members.isEmpty()) continue;

        quint64 rxB = 0, txB = 0, rxP = 0, txP = 0;
        bool anyMember = false;
        for (const QString &m : members) {
            const auto it = indexByName.constFind(m);
            if (it == indexByName.constEnd()) continue;
            const NetSample &mb = samples[*it];
            rxB += mb.rxBytes;   txB += mb.txBytes;
            rxP += mb.rxPackets; txP += mb.txPackets;
            anyMember = true;
        }
        if (!anyMember) continue;
        s.rxBytes   = rxB;
        s.txBytes   = txB;
        s.rxPackets = rxP;
        s.txPackets = txP;
    }

    return samples;
}
