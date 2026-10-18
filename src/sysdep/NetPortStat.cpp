#include "NetPortStat.h"

#include <QByteArray>
#include <QByteArrayList>
#include <QFile>
#include <QHostAddress>
#include <QLoggingCategory>
#include <QProcess>

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

QString hexAddress(const QByteArray &address)
{
    const int colon = address.lastIndexOf(':');
    const QByteArray hex = colon < 0 ? address : address.left(colon);

    bool ok = false;
    if (hex.size() == 8) {
        const quint32 raw = hex.toUInt(&ok, 16);
        if (!ok)
            return {};
        const quint32 addr = ((raw & 0x000000ffU) << 24)
                           | ((raw & 0x0000ff00U) << 8)
                           | ((raw & 0x00ff0000U) >> 8)
                           | ((raw & 0xff000000U) >> 24);
        if (addr == 0)
            return {};
        return QHostAddress(addr).toString();
    }

    if (hex.size() == 32) {
        QByteArray bytes;
        bytes.reserve(16);
        for (int word = 0; word < 4; ++word) {
            const int base = word * 8;
            for (int i = 3; i >= 0; --i) {
                const QByteArray part = hex.mid(base + i * 2, 2);
                const int value = part.toInt(&ok, 16);
                if (!ok)
                    return {};
                bytes.append(char(value));
            }
        }

        Q_IPV6ADDR addr{};
        std::copy(bytes.cbegin(), bytes.cend(), addr.c);
        const QHostAddress host(addr);
        if (host.isNull() || host == QHostAddress::AnyIPv6)
            return {};
        return host.toString();
    }

    return {};
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
                    s.remoteAddress = hexAddress(parts.at(2));
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

#if defined(Q_OS_MACOS)
void parseBsdEndpoint(const QString &endpoint, QString &address, quint16 &port)
{
    address.clear();
    port = 0;

    const QString value = endpoint.trimmed();
    if (value.isEmpty() || value == QStringLiteral("*.*"))
        return;

    const int dot = value.lastIndexOf(QLatin1Char('.'));
    if (dot < 0) {
        address = value;
        return;
    }

    address = value.left(dot);
    if (address == QStringLiteral("*"))
        address.clear();

    const QString portText = value.mid(dot + 1);
    if (portText == QStringLiteral("*"))
        return;

    bool ok = false;
    const uint raw = portText.toUInt(&ok);
    if (ok && raw <= 65535)
        port = static_cast<quint16>(raw);
}

void readMacSocketTable(const QString &protocol, QList<NetPortSample> &out)
{
    QProcess p;
    p.start(QStringLiteral("netstat"),
            {QStringLiteral("-an"), QStringLiteral("-p"), protocol});
    if (!p.waitForFinished(2000)
        || p.exitStatus() != QProcess::NormalExit
        || p.exitCode() != 0) {
        return;
    }

    const QStringList lines = QString::fromLocal8Bit(p.readAllStandardOutput())
                                  .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.startsWith(protocol))
            continue;

        const QStringList parts = trimmed.simplified().split(QLatin1Char(' '));
        if ((protocol == QStringLiteral("tcp") && parts.size() < 6)
            || (protocol == QStringLiteral("udp") && parts.size() < 5)) {
            continue;
        }

        NetPortSample s;
        s.protocol = protocol;
        QString localAddress;
        parseBsdEndpoint(parts.at(3), localAddress, s.localPort);
        quint16 remotePort = 0;
        QString remoteAddress;
        parseBsdEndpoint(parts.at(4), remoteAddress, remotePort);
        s.remotePort = remotePort;
        if (!remoteAddress.isEmpty())
            s.remoteAddress = remoteAddress;
        if (protocol == QStringLiteral("tcp"))
            s.state = parts.last().toUpper();
        if (s.localPort > 0)
            out.append(s);
    }
}
#endif

} // namespace

void NetPortStat::setReadOverride(NetPortStat::ReadFn fn)
{
    g_readOverride = fn;
}

QList<NetPortSample> NetPortStat::read()
{
    if (g_readOverride) return g_readOverride();

    QList<NetPortSample> out;
#if defined(Q_OS_MACOS)
    readMacSocketTable(QStringLiteral("tcp"), out);
    readMacSocketTable(QStringLiteral("udp"), out);
#else
    readSocketFile(QStringLiteral("/proc/net/tcp"),  QStringLiteral("tcp"), out);
    readSocketFile(QStringLiteral("/proc/net/tcp6"), QStringLiteral("tcp"), out);
    readSocketFile(QStringLiteral("/proc/net/udp"),  QStringLiteral("udp"), out);
    readSocketFile(QStringLiteral("/proc/net/udp6"), QStringLiteral("udp"), out);
#endif
    return out;
}
