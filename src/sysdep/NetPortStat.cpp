#include "NetPortStat.h"

#include <QByteArray>
#include <QByteArrayList>
#include <QFile>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcNetPortStat, "krellix.sysdep.netport")

namespace {

constexpr qint64 kProcNetSocketMaxBytes = 1024 * 1024;
NetPortStat::ReadFn g_readOverride = nullptr;

quint16 hexPort(const QByteArray &address)
{
    const int colon = address.lastIndexOf(':');
    if (colon < 0 || colon + 1 >= address.size())
        return 0;
    bool ok = false;
    const uint value = address.mid(colon + 1).toUInt(&ok, 16);
    return ok && value <= 65535 ? static_cast<quint16>(value) : 0;
}

void readSocketFile(const QString &path,
                    const QString &protocol,
                    QList<NetPortSample> &out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCDebug(lcNetPortStat) << "cannot open" << path << f.errorString();
        return;
    }

    const QByteArray bytes = f.read(kProcNetSocketMaxBytes);
    int start = 0;
    int lineNo = 0;
    while (start < bytes.size()) {
        const int nl = bytes.indexOf('\n', start);
        const int end = nl < 0 ? bytes.size() : nl;
        if (end > start) {
            ++lineNo;
            if (lineNo > 1) {
                const QByteArrayList parts = bytes.mid(start, end - start)
                                               .simplified().split(' ');
                if (parts.size() >= 4) {
                    NetPortSample s;
                    s.protocol = protocol;
                    s.localPort = hexPort(parts.at(1));
                    s.remotePort = hexPort(parts.at(2));
                    s.state = QString::fromLatin1(parts.at(3)).toUpper();
                    if (s.localPort > 0)
                        out.append(s);
                }
            }
        }
        if (nl < 0) break;
        start = nl + 1;
    }
}

} // namespace

void NetPortStat::setReadOverride(NetPortStat::ReadFn fn)
{
    g_readOverride = fn;
}

QList<NetPortSample> NetPortStat::read()
{
    if (g_readOverride) return g_readOverride();

    QList<NetPortSample> out;
    readSocketFile(QStringLiteral("/proc/net/tcp"),  QStringLiteral("tcp"), out);
    readSocketFile(QStringLiteral("/proc/net/tcp6"), QStringLiteral("tcp"), out);
    readSocketFile(QStringLiteral("/proc/net/udp"),  QStringLiteral("udp"), out);
    readSocketFile(QStringLiteral("/proc/net/udp6"), QStringLiteral("udp"), out);
    return out;
}
