#include "remote/RemoteSource.h"
#include "widgets/AlertBanner.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <functional>

static void require(bool ok, const char *message) { if (!ok) qFatal("%s", message); }
static void waitFor(const std::function<bool()> &done) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (!done() && elapsed.elapsed() < 3000) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    require(done(), "Timed out waiting for socket state");
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost), "Cannot reserve test port");
    const auto port = server.serverPort();
    server.close();
    RemoteSource remote;
    AlertBanner banner;
    int exhausted = 0;
    QObject::connect(&remote, &RemoteSource::connectionStateChanged, &banner, [&](bool connected) {
        if (connected) banner.hideAlert(); else banner.showAlert(QStringLiteral("Disconnected"));
    });
    QObject::connect(&remote, &RemoteSource::retriesExhausted, &app, [&]() { ++exhausted; });
    auto *retry = remote.findChild<QTimer *>(QStringLiteral("remoteReconnectTimer"));
    auto *timeout = remote.findChild<QTimer *>(QStringLiteral("remoteConnectTimeout"));
    require(retry && timeout, "Missing connection timers");
    remote.connectToHost(QStringLiteral("127.0.0.1"), port);
    require(!banner.isHidden(), "Initial failure must show the banner");
    for (int delay : {5000, 10000, 20000, 60000, 300000, 1200000}) {
        waitFor([&]() { return retry->isActive(); });
        require(retry->interval() == delay, "Incorrect retry interval");
        QMetaObject::invokeMethod(&remote, "scheduleReconnect", Qt::DirectConnection);
        require(retry->interval() == delay, "Duplicate failure advanced retry sequence");
        // Exercise real retry/socket callbacks without waiting 26 minutes.
        retry->start(0);
        waitFor([&]() { return retry->interval() != 0 || exhausted; });
    }
    require(exhausted > 0 && !retry->isActive(), "Retries did not stop");
    require(!banner.isHidden(), "Exhaustion hid disconnected warning");
    const auto beforeBlink = banner.styleSheet();
    QMetaObject::invokeMethod(&banner, "onBlinkTick", Qt::DirectConnection);
    require(banner.styleSheet() != beforeBlink, "Red warning does not flash");

    require(server.listen(QHostAddress::LocalHost, port), "Cannot start recovered server");
    remote.connectToHost(QStringLiteral("127.0.0.1"), port);
    waitFor([&]() { return remote.isConnected() && server.hasPendingConnections(); });
    require(banner.isHidden() && !retry->isActive() && !timeout->isActive(), "Recovery did not clear warning/timers");
    auto *peer = server.nextPendingConnection();
    server.close();
    peer->abort();
    waitFor([&]() { return retry->isActive(); });
    require(retry->interval() == 5000, "Recovery did not reset retry sequence");
    require(!banner.isHidden(), "Lost established connection did not show warning");
    require(server.listen(QHostAddress::LocalHost, port), "Cannot reopen server for automatic recovery");
    retry->start(0);
    waitFor([&]() { return remote.isConnected() && server.hasPendingConnections(); });
    require(banner.isHidden(), "Automatic reconnect did not clear warning");
    server.nextPendingConnection()->deleteLater();
    server.close();
    remote.connectToHost(QStringLiteral("127.0.0.1"), port);
    timeout->stop();
    QMetaObject::invokeMethod(timeout, "timeout", Qt::DirectConnection);
    require(retry->isActive() && retry->interval() == 5000, "Timed-out attempt did not schedule retry");
    remote.connectToHost(QString(), 0);
    require(!retry->isActive(), "Explicit reset left an old retry pending");
    qInfo("PASS: retry intervals, exhaustion, duplicates, recovery, timeout, and flashing warning");
}
