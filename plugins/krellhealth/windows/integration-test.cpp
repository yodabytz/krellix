#include "sdk/KrellixPlugin.h"
#include "monitors/MonitorBase.h"
#include "remote/RemoteSource.h"
#include "theme/Theme.h"
#include "widgets/Decal.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QPluginLoader>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QTcpServer>
#include <QTcpSocket>
#include <QWidget>
#include <functional>
static void check(bool value, const char *message) { if (!value) qFatal("%s", message); }
static bool until(const std::function<bool()> &condition, int ms=18000)
{
    QElapsedTimer time; time.start();
    while(time.elapsed()<ms) { QApplication::processEvents(); if(condition()) return true; QThread::msleep(10); }
    return false;
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    QCoreApplication::setOrganizationName("health-test");
    QCoreApplication::setApplicationName("isolated");
    check(argc==2,"Missing plugin path");
    QPluginLoader loader(QString::fromLocal8Bit(argv[1]));
    auto *plugin=qobject_cast<IKrellixPlugin *>(loader.instance());
    if(!plugin) qFatal("DLL failed: %s",qPrintable(loader.errorString()));
    Theme theme; QObject parent;
    check(plugin->createMonitors(&theme,&parent).isEmpty(),"Disabled plugin appeared");
    QSettings().setValue("plugins/krellhealth/enabled",true);
    auto *monitor=plugin->createMonitors(&theme,&parent).first();
    auto *widget=monitor->createWidget(nullptr);
    check(until([&]{return widget->toolTip().contains("Source: local Windows");}),"Native Windows helper did not return valid health");
    check(widget->toolTip().contains("Physical memory pressure"),"Native memory probe missing");
    check(widget->toolTip().contains("volume(s)"),"Native disk probe missing");
    monitor->shutdown(); delete widget; delete monitor;
    monitor=plugin->createMonitors(&theme,&parent).first();widget=monitor->createWidget(nullptr);
    monitor->tick();
    QElapsedTimer shutdown;shutdown.start();
    monitor->shutdown();delete widget;delete monitor;
    check(shutdown.elapsed()<2500,"Stopping an active local helper blocked shutdown");

    QTcpServer server; check(server.listen(QHostAddress::LocalHost,0),"Fixture listen failed");
    QList<QTcpSocket *> sockets;
    const QByteArray sample="{\"type\":\"sample\",\"health\":{\"critical\":1,\"warnings\":0,\"findings\":[{\"severity\":\"critical\",\"title\":\"REMOTE_ONLY\",\"detail\":\"<b>escaped</b>\"}]}}\n";
    QObject::connect(&server,&QTcpServer::newConnection,&parent,[&]{while(server.hasPendingConnections()){auto *s=server.nextPendingConnection();sockets<<s;s->write(sample);}});
    RemoteSource source; source.connectToHost("127.0.0.1",server.serverPort());
    check(until([&]{return source.isConnected();}),"Source failed");
    monitor=plugin->createMonitors(&theme,&parent).first();widget=monitor->createWidget(nullptr);
    check(until([&]{monitor->tick();return widget->toolTip().contains("REMOTE_ONLY");}),"Remote health missing");
    check(monitor->findChildren<QProcess *>().isEmpty(),"Remote started local helper");
    check(widget->toolTip().contains("&lt;b&gt;"),"Remote text injected HTML");
    sockets.last()->write("{\"type\":\"sample\"}\n");
    check(until([&]{return widget->toolTip().contains("no health data");}),"Absent health reported healthy");
    for(auto *decal:widget->findChildren<Decal *>()) check(!decal->text().contains(QChar(0x2665)),"Missing data showed healthy heart");
    sockets.last()->write("{\"type\":\"sample\",\"health\":{\"critical\":0,\"warnings\":0,\"findings\":[]}}\n");
    check(until([&]{return widget->toolTip().contains("No active findings");}),"Explicit healthy report missing");
    bool heart=false;
    for(auto *decal:widget->findChildren<Decal *>()) if(decal->text()==QStringLiteral("\u2665 OK")) heart=true;
    check(heart,"Healthy status did not display heart");
    sockets.last()->write("garbage\n");
    check(until([&]{return widget->toolTip().contains("invalid remote health message");}),"Malformed message accepted");
    check(until([&]{monitor->tick();return widget->toolTip().contains("REMOTE_ONLY");}),"Reconnect failed");
    sockets.last()->write(QByteArray(1024*1024+2,'x'));
    check(until([&]{return widget->toolTip().contains("too large");}),"Oversized message accepted");
    check(until([&]{monitor->tick();return widget->toolTip().contains("REMOTE_ONLY");}),"Oversize recovery failed");
    check(until([&]{return widget->toolTip().contains("timed out");},18000),"Silent remote stream retained stale health");
    check(until([&]{monitor->tick();return widget->toolTip().contains("REMOTE_ONLY");}),"Timeout recovery failed");
    for(auto *socket:sockets) socket->abort();
    check(until([&]{return widget->toolTip().contains("disconnected");}),"Disconnected health left stale findings");
    check(monitor->findChildren<QProcess *>().isEmpty(),"Disconnected remote fell back locally");
    monitor->shutdown();delete widget;delete monitor;
    QSettings().setValue("plugins/krellhealth/enabled",false);
    check(plugin->createMonitors(&theme,&parent).isEmpty(),"Disable failed");
    qInfo("PASS actual DLL, native helper, remote-only findings, unknown data, escaped text, malformed/oversized input, reconnect, settings and shutdown");
}
