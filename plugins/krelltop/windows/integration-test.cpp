#include "sdk/KrellixPlugin.h"
#include "monitors/MonitorBase.h"
#include "theme/Theme.h"
#include "remote/RemoteSource.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QPluginLoader>
#include <QSettings>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QWidget>
#include <functional>

static void check(bool value, const char *message) { if (!value) qFatal("%s", message); }
static bool until(const std::function<bool()> &condition, int ms = 8000)
{
    QElapsedTimer time; time.start();
    while (time.elapsed() < ms) {
        QApplication::processEvents();
        if (condition()) return true;
        QThread::msleep(10);
    }
    return false;
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QCoreApplication::setOrganizationName("krelltop-test");
    QCoreApplication::setApplicationName("isolated");
    check(argc == 2, "Plugin path missing");
    QPluginLoader loader(QString::fromLocal8Bit(argv[1]));
    auto *plugin = qobject_cast<IKrellixPlugin *>(loader.instance());
    if (!plugin) qFatal("Plugin load failed: %s", qPrintable(loader.errorString()));
    Theme theme;
    QObject parent;
    QSettings().setValue("plugins/krelltop/enabled", false);
    check(plugin->createMonitors(&theme, &parent).isEmpty(), "Disabled plugin created monitor");
    QSettings().setValue("plugins/krelltop/enabled", true);
    auto monitors = plugin->createMonitors(&theme, &parent);
    check(monitors.size() == 1, "Plugin factory failed");
    auto *local = monitors.first();
    auto *widget = local->createWidget(nullptr);
    check(until([&] { local->tick(); return widget->toolTip().contains("Top memory"); }), "Local process panel never populated");
    check(local->findChildren<QThread *>().size() == 1, "Local reader missing");
    local->shutdown(); delete widget; delete local;

    QTcpServer server;
    check(server.listen(QHostAddress::LocalHost, 0), "Fixture listen failed");
    QList<QTcpSocket *> sockets;
    const QByteArray sample = "{\"type\":\"sample\",\"top\":[{\"pid\":4242,\"name\":\"REMOTE_ONLY\",\"cmd\":\"remote command\",\"cpu\":12.5,\"mem\":2.5,\"rss\":8192}]}\n";
    QObject::connect(&server, &QTcpServer::newConnection, &parent, [&] {
        while (server.hasPendingConnections()) {
            auto *socket = server.nextPendingConnection(); sockets.append(socket); socket->write(sample);
        }
    });
    RemoteSource source;
    source.connectToHost("127.0.0.1", server.serverPort());
    check(until([&] { return source.isConnected(); }), "Fixture connection failed");
    auto *remote = plugin->createMonitors(&theme, &parent).first();
    widget = remote->createWidget(nullptr);
    check(until([&] { remote->tick(); return widget->toolTip().contains("REMOTE_ONLY"); }), "Remote process data missing");
    check(remote->findChildren<QThread *>().isEmpty(), "Remote created local process sampler");
    check(widget->toolTip().contains("pid 4242") && widget->toolTip().contains("8 MiB"), "Remote fields corrupted");
    for (auto *socket : sockets) socket->write("{\"type\":\"sample\",\"top\":[]}\n");
    check(until([&] { return widget->toolTip().contains("server provides no process data"); }), "Empty remote data retained stale rows");
    for (auto *socket : sockets) socket->write(sample);
    check(until([&] { return widget->toolTip().contains("REMOTE_ONLY"); }), "Persistent remote stream failed");
    auto *pluginSocket = sockets.last();
    pluginSocket->write("{\"type\":\"sample\",\"top\":[{\"pid\":-1,\"name\":\"INVALID\",\"cpu\":-5}]}\n");
    check(until([&] { return widget->toolTip().contains("server provides no process data"); }), "Invalid remote rows accepted");
    pluginSocket->write("not-json\n");
    check(until([&] { return widget->toolTip().contains("invalid remote process message"); }), "Malformed JSON not rejected");
    check(until([&] { remote->tick(); return widget->toolTip().contains("REMOTE_ONLY"); }), "Plugin failed to reconnect after malformed input");
    sockets.last()->write(QByteArray(1024 * 1024 + 2, 'x'));
    check(until([&] { return widget->toolTip().contains("message too large"); }), "Oversized remote message not rejected");
    check(until([&] { remote->tick(); return widget->toolTip().contains("REMOTE_ONLY"); }), "Plugin failed to reconnect after oversized input");
    for (auto *socket : sockets) socket->abort();
    check(until([&] { return widget->toolTip().contains("disconnected"); }), "Disconnect did not clear remote rows");
    check(remote->findChildren<QThread *>().isEmpty(), "Disconnect fell back to local sampler");
    remote->shutdown(); delete widget; delete remote;
    QSettings().setValue("plugins/krelltop/enabled", false);
    check(plugin->createMonitors(&theme, &parent).isEmpty(), "Disabling plugin failed");
    qInfo("PASS: actual DLL load, local panel, independent enable setting, remote-only stream, absent data, disconnect, shutdown");
}
