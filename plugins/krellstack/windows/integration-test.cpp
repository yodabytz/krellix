#include "sdk/KrellixPlugin.h"
#include "monitors/MonitorBase.h"
#include "remote/RemoteSource.h"
#include "theme/Theme.h"
#include "widgets/Decal.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QPluginLoader>
#include <QProcess>
#include <QPixmap>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <functional>
static bool badSignal=false;
static void messages(QtMsgType,const QMessageLogContext &,const QString &message){
    if(message.contains("signal not found")||message.contains("No such slot"))badSignal=true;
    fprintf(stderr,"%s\n",qPrintable(message));
}
static void check(bool ok,const char *message){if(!ok)qFatal("%s",message);}
static bool until(const std::function<bool()> &predicate,int deadline=10000){
    QElapsedTimer timer;timer.start();
    while(timer.elapsed()<deadline){QApplication::processEvents();if(predicate())return true;QThread::msleep(10);}return false;
}
int main(int argc,char **argv){
    QApplication app(argc,argv);check(argc==3,"Missing paths");qInstallMessageHandler(messages);
    const auto root=app.applicationDirPath();check(root.endsWith("krellstack-test-host"),"Wrong isolated directory");
    QTemporaryDir temporary;QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temporary.path());app.setOrganizationName("stack-test");app.setApplicationName("isolated");
    QPluginLoader loader(QString::fromLocal8Bit(argv[1]));auto *plugin=qobject_cast<IKrellixPlugin *>(loader.instance());
    if(!plugin)qFatal("DLL: %s",qPrintable(loader.errorString()));
    Theme theme;QObject parent;check(plugin->createMonitors(&theme,&parent).isEmpty(),"Enabled by default");
    QSettings().setValue("plugins/krellstack/enabled",true);
    const auto helper=root+"/krellstack-probe.exe";QFile::remove(helper);check(QFile::copy(QString::fromLocal8Bit(argv[2]),helper),"Fixture copy failed");
    for(const auto *mode:{"ok","empty","missing","partial","error","docker-error","malformed","oversize","timeout"}){
        qputenv("KRELLSTACK_TEST_CASE",mode);auto *monitor=plugin->createMonitors(&theme,&parent).first();
        auto *widget=monitor->createWidget(nullptr);widget->setFixedWidth(120);widget->show();
        auto *label=widget->findChild<Decal *>("stackSummary");check(label,"No label");
        check(until([&]{return !label->text().startsWith("Checking");},75000),"Query stuck");
        if(QByteArray(mode)=="ok"){
            check(label->text()=="Running: 1 of 2","Wrong local count");check(label->toolTip().contains("&lt;b&gt;"),"HTML injection");
            check(widget->findChild<Decal *>("stackDetail")->text()=="Images: 1","Unclear image label");
            check(widget->findChild<Decal *>("stackProjects")->text()=="Projects: 1","Unclear project label");
            check(widget->findChild<Decal *>("stackAttention")->text()=="Stopped/alerts: 1","Unclear attention label");
            widget->grab();check(widget->width()==120,"Narrow layout expanded");
            qputenv("KRELLSTACK_TEST_CASE","error");QSettings().setValue("plugins/krellstack/hosts","test@server");monitor->tick();
            check(until([&]{return label->text()=="Container check failed";}),"Changed config retained stale data");
            QSettings().remove("plugins/krellstack/hosts");
        }else if(QByteArray(mode)=="empty")check(label->text()=="Running: 0 of 0","Empty engine wrong");
        else if(QByteArray(mode)=="missing")check(label->text()=="No container tools","Missing runtime shown as zero");
        else if(QByteArray(mode)=="partial")check(label->text().endsWith(" !"),"Partial failure hidden");
        else if(QByteArray(mode)=="docker-error")check(label->text()=="Docker unavailable","Source-specific failure hidden");
        else check(label->text()==(QByteArray(mode)=="error"?"Container check failed":"Stack unavailable"),"Error shown as zero");
        monitor->shutdown();delete widget;delete monitor;
    }
    {
        RemoteSource remote;QTcpServer server;check(server.listen(QHostAddress::LocalHost),"Listen failed");
        QSettings().setValue("plugins/krellstack/hosts","ignored@other-server");
        auto *monitor=plugin->createMonitors(&theme,&parent).first();auto *widget=monitor->createWidget(nullptr);
        auto *label=widget->findChild<Decal *>("stackSummary");monitor->tick();
        check(label->text()=="Server disconnected","Local fallback while disconnected");
        remote.connectToHost("127.0.0.1",server.serverPort());
        check(until([&]{return remote.isConnected()&&server.hasPendingConnections();}),"Remote connect failed");
        auto *peer=server.nextPendingConnection();
        auto send=[&](const QByteArray &stack){peer->write("{\"type\":\"sample\",\"stack\":"+stack+"}\n");peer->flush();};
        const QByteArray valid="{\"source\":\"krellixd\",\"containers\":7,\"running\":6,\"images\":8,\"compose\":2,\"unhealthy\":1,\"docker_networks\":3}";
        send(valid);check(until([&]{return label->text()=="Running: 6 of 7";}),"Daemon counts wrong");
        check(widget->findChild<Decal *>("stackAttention")->text()=="Stopped/alerts: 1","Remote attention unclear");
        check(monitor->findChildren<QProcess *>().isEmpty(),"Remote launched local/SSH helper");
        send("{\"source\":\"krellixd\",\"containers\":0,\"running\":0,\"images\":0,\"compose\":0,\"unhealthy\":0,\"docker_networks\":0}");
        check(until([&]{return label->text()=="Running: 0 of 0";}),"Server without Docker was not handled safely");
        check(monitor->findChildren<QProcess *>().isEmpty(),"Empty remote server started local helper");
        send("{\"source\":\"krellixd\",\"error\":\"<b>Denied</b>\"}");
        check(until([&]{return label->text()=="Stack unavailable";}),"Daemon error ignored");
        check(label->toolTip().contains("&lt;b&gt;"),"Daemon error injected HTML");
        send("{}");check(until([&]{return label->text()=="Server data unavailable";}),"Missing daemon data became zero");
        send("{\"source\":\"krellixd\",\"containers\":-1}");check(until([&]{return label->text()=="Stack unavailable";}),"Negative totals accepted");
        check(widget->findChild<Decal *>("stackAttention")->text().isEmpty(),"Stale attention after error");
        send(valid);check(until([&]{return label->text()=="Running: 6 of 7";}),"Valid recovery failed");
        peer->abort();check(until([&]{return label->text()=="Server disconnected";}),"Stale disconnected data");
        auto *retry=remote.findChild<QTimer *>("remoteReconnectTimer");check(retry,"No retry timer");retry->start(0);
        check(until([&]{return remote.isConnected()&&server.hasPendingConnections();}),"Reconnect failed");
        peer=server.nextPendingConnection();send(valid);check(until([&]{return label->text()=="Running: 6 of 7";}),"Reconnected data failed");
        monitor->shutdown();delete widget;delete monitor;
    }
    QSettings().remove("plugins/krellstack/hosts");qputenv("KRELLSTACK_TEST_CASE","timeout");
    auto *monitor=plugin->createMonitors(&theme,&parent).first();auto *widget=monitor->createWidget(nullptr);monitor->tick();
    QElapsedTimer timer;timer.start();monitor->shutdown();delete widget;delete monitor;check(timer.elapsed()<2500,"Shutdown stalled");
    check(!badSignal,"Invalid DLL signal connection");QFile::remove(helper);
    qInfo("PASS actual DLL, 120px layout, errors/partial/missing/timeout, config refresh, remote isolation and reconnect");
}
