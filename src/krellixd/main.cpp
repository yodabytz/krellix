// krellixd — krellix remote monitoring daemon.
//
// Listens on TCP (default 127.0.0.1:19150), accepts allow-listed clients,
// and pushes a JSON-lines "sample" message containing /proc-derived
// readings at a configurable rate. Designed to run as an unprivileged
// systemd service. Configuration is read from a path given by --config
// (defaults to /etc/krellixd/krellixd.conf) or overridden by command-line
// flags.
//
// Security posture:
//   * Empty allow-list = deny everyone (no accidental exposure).
//   * Per-connection idle/I/O timeout drops silent peers.
//   * Max-client cap rejects floods at the accept stage.
//   * Bounded receive buffer per client (we never need to read much).
//   * Refuses to run as root unless --allow-root is passed; recommends a
//     dedicated systemd User= and DynamicUser=.

#include "KrellixdServer.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QLoggingCategory>
#include <QSet>
#include <QSettings>
#include <QStringList>

#include <unistd.h>

#ifndef KRELLIX_VERSION
#  define KRELLIX_VERSION "0.0.0"
#endif

namespace {

Q_LOGGING_CATEGORY(lcMain, "krellixd")

constexpr quint16 kDefaultPort = 19150;

// Parse "host", "host:port", "[v6addr]:port", or just "port".
bool parseListen(const QString &spec, QHostAddress &outAddr, quint16 &outPort)
{
    outPort = kDefaultPort;
    QString host = spec.trimmed();
    if (host.isEmpty()) {
        outAddr = QHostAddress(QHostAddress::LocalHost);
        return true;
    }
    if (host.startsWith(QLatin1Char('['))) {
        const int rb = host.indexOf(QLatin1Char(']'));
        if (rb < 0) return false;
        const QString h = host.mid(1, rb - 1);
        if (rb + 1 < host.size() && host.at(rb + 1) == QLatin1Char(':')) {
            outPort = host.mid(rb + 2).toUShort();
        }
        outAddr = QHostAddress(h);
        return !outAddr.isNull();
    }
    const int colon = host.lastIndexOf(QLatin1Char(':'));
    if (colon >= 0 && host.indexOf(QLatin1Char(':')) == colon) {
        // single colon -> host:port
        outAddr = QHostAddress(host.left(colon));
        outPort = host.mid(colon + 1).toUShort();
    } else {
        outAddr = QHostAddress(host);
    }
    return !outAddr.isNull();
}

QSet<QHostAddress> parseAllowList(const QStringList &raw)
{
    QSet<QHostAddress> out;
    for (const QString &s : raw) {
        const QString trimmed = s.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) continue;
        QHostAddress h(trimmed);
        if (h.isNull()) {
            qCWarning(lcMain) << "ignoring invalid allow entry:" << trimmed;
            continue;
        }
        out.insert(h);
    }
    return out;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("krellixd"));
    QCoreApplication::setApplicationVersion(QString::fromUtf8(KRELLIX_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("krellix remote monitoring daemon."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption configOpt({QStringLiteral("c"), QStringLiteral("config")},
        QStringLiteral("Read settings from INI config file."),
        QStringLiteral("path"),
        QStringLiteral("/etc/krellixd/krellixd.conf"));
    parser.addOption(configOpt);

    const QCommandLineOption listenOpt({QStringLiteral("l"), QStringLiteral("listen")},
        QStringLiteral("Bind address (e.g. 0.0.0.0, ::, 127.0.0.1, 192.0.2.1:19150)."),
        QStringLiteral("addr[:port]"));
    parser.addOption(listenOpt);

    const QCommandLineOption portOpt({QStringLiteral("p"), QStringLiteral("port")},
        QStringLiteral("Port to listen on (default 19150)."),
        QStringLiteral("number"));
    parser.addOption(portOpt);

    const QCommandLineOption allowOpt({QStringLiteral("a"), QStringLiteral("allow")},
        QStringLiteral("Allow connections only from this IP (repeatable, "
                       "additive on top of config-file entries)."),
        QStringLiteral("ip"));
    parser.addOption(allowOpt);

    const QCommandLineOption maxClientsOpt({QStringLiteral("m"), QStringLiteral("max-clients")},
        QStringLiteral("Max simultaneous clients (default 4)."),
        QStringLiteral("n"));
    parser.addOption(maxClientsOpt);

    const QCommandLineOption updateHzOpt({QStringLiteral("u"), QStringLiteral("update-hz")},
        QStringLiteral("Sample rate in Hz, 0.1..10 (default 1)."),
        QStringLiteral("hz"));
    parser.addOption(updateHzOpt);

    const QCommandLineOption ioTimeoutOpt(QStringLiteral("io-timeout"),
        QStringLiteral("Drop a client after N seconds of silence (default 30, min 5)."),
        QStringLiteral("seconds"));
    parser.addOption(ioTimeoutOpt);

    const QCommandLineOption pidfileOpt(QStringLiteral("pidfile"),
        QStringLiteral("Write process PID to this file at startup."),
        QStringLiteral("path"));
    parser.addOption(pidfileOpt);

    const QCommandLineOption allowRootOpt(QStringLiteral("allow-root"),
        QStringLiteral("Allow running as root (default refuses)."));
    parser.addOption(allowRootOpt);

    parser.process(app);

    // Refuse to run as root unless explicitly opted in. systemd unit uses
    // a DynamicUser=, so this only triggers when an admin runs by hand.
    if (geteuid() == 0 && !parser.isSet(allowRootOpt)) {
        qFatal("krellixd: refusing to run as root. Use a dedicated user "
               "(systemd DynamicUser=krellixd is fine), or pass --allow-root "
               "if you really mean it.");
    }

    // Defaults; config file overrides defaults; command-line overrides config.
    QString listenSpec = QStringLiteral("127.0.0.1");
    quint16 port       = kDefaultPort;
    QStringList allowEntries;
    int maxClients   = 4;
    int idleTimeoutMs = 30 * 1000;
    double updateHz  = 1.0;

    const QString cfgPath = parser.value(configOpt);
    if (QFileInfo::exists(cfgPath)) {
        QSettings cfg(cfgPath, QSettings::IniFormat);
        cfg.beginGroup(QStringLiteral("server"));
        if (cfg.contains(QStringLiteral("listen")))
            listenSpec = cfg.value(QStringLiteral("listen")).toString();
        if (cfg.contains(QStringLiteral("port")))
            port = cfg.value(QStringLiteral("port")).toUInt();
        if (cfg.contains(QStringLiteral("max_clients")))
            maxClients = cfg.value(QStringLiteral("max_clients")).toInt();
        if (cfg.contains(QStringLiteral("update_hz")))
            updateHz = cfg.value(QStringLiteral("update_hz")).toDouble();
        if (cfg.contains(QStringLiteral("io_timeout")))
            idleTimeoutMs = cfg.value(QStringLiteral("io_timeout")).toInt() * 1000;
        cfg.endGroup();

        cfg.beginGroup(QStringLiteral("security"));
        const QString hostsCsv = cfg.value(QStringLiteral("allow_hosts")).toString();
        for (const QString &s : hostsCsv.split(QRegularExpression(QStringLiteral("[\\s,]+")),
                                               Qt::SkipEmptyParts))
            allowEntries.append(s);
        cfg.endGroup();
    } else {
        qCInfo(lcMain) << "no config at" << cfgPath
                       << "— using built-in defaults + command-line overrides";
    }

    if (parser.isSet(listenOpt))    listenSpec = parser.value(listenOpt);
    if (parser.isSet(portOpt))      port = parser.value(portOpt).toUShort();
    if (parser.isSet(maxClientsOpt))maxClients = parser.value(maxClientsOpt).toInt();
    if (parser.isSet(updateHzOpt))  updateHz = parser.value(updateHzOpt).toDouble();
    if (parser.isSet(ioTimeoutOpt)) idleTimeoutMs = parser.value(ioTimeoutOpt).toInt() * 1000;
    for (const QString &s : parser.values(allowOpt)) allowEntries.append(s);

    QHostAddress addr;
    quint16      parsedPort = port;
    if (!parseListen(listenSpec, addr, parsedPort)) {
        qFatal("krellixd: invalid --listen value: %s", qUtf8Printable(listenSpec));
    }
    if (!parser.isSet(portOpt) && parsedPort != kDefaultPort) port = parsedPort;

    const int intervalMs = qBound(100,
                                  static_cast<int>(1000.0 / qMax(0.1, updateHz)),
                                  10000);
    idleTimeoutMs = qMax(5 * 1000, idleTimeoutMs);

    const QSet<QHostAddress> allowed = parseAllowList(allowEntries);
    if (allowed.isEmpty()) {
        qFatal("krellixd: empty allow-list — refusing to listen. Add at "
               "least one IP under [security] allow_hosts in the config "
               "file or via --allow.");
    }

    auto *server = new KrellixdServer(intervalMs, idleTimeoutMs, maxClients,
                                      allowed, &app);
    if (!server->listen(addr, port)) {
        qFatal("krellixd: listen failed on %s:%u — %s",
               qUtf8Printable(addr.toString()), port,
               qUtf8Printable(server->errorString()));
    }

    qCInfo(lcMain).nospace()
        << "krellixd " << KRELLIX_VERSION << " listening on "
        << addr.toString() << ":" << port
        << " (interval " << intervalMs << "ms, max " << maxClients
        << " clients, idle " << (idleTimeoutMs / 1000) << "s, allow=" << allowed.size() << ")";

    if (parser.isSet(pidfileOpt)) {
        QFile pf(parser.value(pidfileOpt));
        if (pf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            pf.write(QByteArray::number(QCoreApplication::applicationPid()));
        }
    }

    return app.exec();
}
