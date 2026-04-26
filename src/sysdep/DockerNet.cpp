#include "DockerNet.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocalSocket>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcDockerNet, "krellix.sysdep.dockernet")

namespace {

constexpr int  kCacheTtlSeconds   = 30;
constexpr const char *kSocketPath = "/var/run/docker.sock";
constexpr int  kConnectTimeoutMs  = 500;
constexpr int  kReadTimeoutMs     = 2000;
constexpr qint64 kMaxResponseBytes = 4 * 1024 * 1024;  // hard cap

struct Cache {
    QHash<QString, QString> ifaceToName;
    qint64 expiresAtSec      = 0;
    qint64 lastFailureAtSec  = 0;
};
Cache g_cache;

// Talks HTTP/1.0 to /var/run/docker.sock, GET /networks. We use 1.0
// instead of 1.1 deliberately: the server then closes the connection
// after the body, so we don't have to handle Transfer-Encoding: chunked.
QHash<QString, QString> fetchFromDocker()
{
    QHash<QString, QString> map;

    QLocalSocket sock;
    sock.connectToServer(QString::fromLatin1(kSocketPath));
    if (!sock.waitForConnected(kConnectTimeoutMs)) {
        qCDebug(lcDockerNet) << "no docker socket:" << sock.errorString();
        return map;
    }

    sock.write("GET /networks HTTP/1.0\r\n"
               "Host: docker\r\n"
               "Accept: */*\r\n"
               "\r\n");
    if (!sock.waitForBytesWritten(kReadTimeoutMs)) return map;

    QByteArray response;
    while (sock.waitForReadyRead(kReadTimeoutMs)) {
        response.append(sock.readAll());
        if (response.size() > kMaxResponseBytes) {
            qCWarning(lcDockerNet) << "response exceeded cap, truncating";
            break;
        }
    }
    response.append(sock.readAll());

    const int bodyStart = response.indexOf("\r\n\r\n");
    if (bodyStart < 0) {
        qCWarning(lcDockerNet) << "no HTTP body marker in response";
        return map;
    }
    const QByteArray body = response.mid(bodyStart + 4);

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        qCWarning(lcDockerNet) << "JSON parse error:" << err.errorString();
        return map;
    }

    for (const QJsonValue &v : doc.array()) {
        const QJsonObject net = v.toObject();
        if (net.value(QStringLiteral("Driver")).toString()
            != QLatin1String("bridge"))
            continue;

        const QString name = net.value(QStringLiteral("Name")).toString();
        const QString id   = net.value(QStringLiteral("Id")).toString();
        if (name.isEmpty() || id.isEmpty()) continue;

        // Two interface naming schemes in Docker:
        //   * default "bridge" network → docker0 (or whatever
        //     com.docker.network.bridge.name says)
        //   * user-defined networks    → br-<first 12 chars of id>
        // Honor any explicit override via the bridge.name option
        // (Compose / network --opt), fall back to the synthesized
        // br-<id12> name otherwise.
        const QJsonObject opts =
            net.value(QStringLiteral("Options")).toObject();
        QString iface = opts.value(
            QStringLiteral("com.docker.network.bridge.name")).toString();
        if (iface.isEmpty()) {
            iface = QStringLiteral("br-") + id.left(12);
        }
        map.insert(iface, name);
    }
    return map;
}

} // namespace

QString DockerNet::aliasForBridge(const QString &iface)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (now >= g_cache.expiresAtSec) {
        g_cache.ifaceToName  = fetchFromDocker();
        g_cache.expiresAtSec = now + kCacheTtlSeconds;
    }
    return g_cache.ifaceToName.value(iface);
}
