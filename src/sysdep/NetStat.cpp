#include "NetStat.h"

#include "DockerNet.h"

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
        if (!members.isEmpty()) {
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
            if (anyMember) {
                s.rxBytes   = rxB;
                s.txBytes   = txB;
                s.rxPackets = rxP;
                s.txPackets = txP;
            }
        }

        // Friendly name lookup for Docker bridges. Done outside the
        // members block on purpose — a Docker network with no
        // currently-attached containers has an empty brif/, but the
        // user still benefits from seeing 'cerberix-wan' instead of
        // 'br-458bd2654c82' in the panel title. Non-Docker interfaces
        // (eth0, ens6, ...) return an empty alias; the call is cheap
        // because DockerNet caches the lookup.
        const QString alias = DockerNet::aliasForBridge(s.name);
        if (!alias.isEmpty()) s.alias = alias;
    }

    // Combined Docker view: take the existing docker0 entry and
    // override its counters with the SUM of every Docker-managed
    // bridge (docker0 + all br-<hash> with a friendly alias).  This
    // makes "docker0" function as a single "all containers" pseudo-
    // interface — the natural panel a user looking at the krellix
    // window expects to be the one-stop Docker reading. Individual
    // Compose-network bridges are still in the sample list (and the
    // settings dialog can toggle them on for a per-network breakdown),
    // but they're default-disabled so the default render is one
    // single combined panel rather than seven separate ones.
    //
    // Per-bridge counters are NOT subtracted out — docker0's own
    // member counters are already part of the bridge fixup above
    // (member-summed), and we add docker0 itself to the total below
    // along with every other aliased bridge. No double-counting
    // because each interface is counted once on its own line.
    {
        const auto dockerIt = indexByName.constFind(QStringLiteral("docker0"));
        if (dockerIt != indexByName.constEnd()) {
            quint64 rxB = 0, txB = 0, rxP = 0, txP = 0;
            int counted = 0;
            for (const NetSample &b : samples) {
                if (b.alias.isEmpty()) continue;     // not a Docker bridge
                rxB += b.rxBytes;   txB += b.txBytes;
                rxP += b.rxPackets; txP += b.txPackets;
                ++counted;
            }
            if (counted > 0) {
                NetSample &d = samples[*dockerIt];
                d.rxBytes   = rxB;
                d.txBytes   = txB;
                d.rxPackets = rxP;
                d.txPackets = txP;
                // Replace alias with a "(N networks)" hint so the
                // panel title makes the combined nature obvious.
                d.alias = QStringLiteral("Docker (%1 networks)").arg(counted);
            }
        }
    }

    return samples;
}
