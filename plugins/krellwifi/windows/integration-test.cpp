#include "sdk/KrellixPlugin.h"
#include "monitors/MonitorBase.h"
#include "remote/RemoteSource.h"
#include "widgets/Decal.h"
#include "theme/Theme.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QPluginLoader>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <functional>
static void check(bool v,const char *m){if(!v)qFatal("%s",m);}
static bool until(const std::function<bool()> &condition,int timeout=25000){QElapsedTimer t;t.start();while(t.elapsed()<timeout){QApplication::processEvents();if(condition())return true;QThread::msleep(10);}return false;}
int main(int argc,char **argv)
{
    QApplication app(argc,argv);check(argc==4,"Missing test arguments");
    const QString root=QCoreApplication::applicationDirPath();check(root.endsWith("krellwifi-test-host"),"Refusing to alter helpers outside isolated test host");
    QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    QCoreApplication::setOrganizationName("wifi-test");QCoreApplication::setApplicationName("isolated");
    QPluginLoader loader(QString::fromLocal8Bit(argv[1]));auto *plugin=qobject_cast<IKrellixPlugin *>(loader.instance());
    if(!plugin)qFatal("Plugin failed: %s",qPrintable(loader.errorString()));
    Theme theme;QObject parent;check(plugin->createMonitors(&theme,&parent).isEmpty(),"Disabled plugin appeared");
    QSettings().setValue("plugins/krellwifi/enabled",true);
    QDir().mkpath(root+"/wifi");const QString helper=root+"/wifi/krellwifi-probe.exe";
    QFile::remove(helper);check(QFile::copy(QString::fromLocal8Bit(argv[2]),helper),"Native helper copy failed");
    auto *m=plugin->createMonitors(&theme,&parent).first();auto *w=m->createWidget(nullptr);
    check(until([&]{return w->toolTip().contains("Refreshes every 60 seconds");}),"Native probe did not return a valid hardware/scan state");
    m->shutdown();delete w;delete m;
    QFile::remove(helper);check(QFile::copy(QString::fromLocal8Bit(argv[3]),helper),"Fixture helper copy failed");
    for(const auto &mode:{"ok","no_adapter","permission","service_off","radio_off","malformed","oversize","timeout"}){
        qputenv("KRELLWIFI_TEST_CASE",mode);
        m=plugin->createMonitors(&theme,&parent).first();w=m->createWidget(nullptr);
        check(until([&]{const auto tip=w->toolTip();return tip.contains("Refreshes every 60 seconds")||tip.contains("invalid scan data")||tip.contains("check failed")||tip.contains("response too large")||tip.contains("helper timed out");}),"Fixture did not finish");
        if(QByteArray(mode)=="ok"){
            check(w->toolTip().contains("1 APs  best 91%")&&w->toolTip().contains("&lt;b&gt;TEST&lt;/b&gt;"),"Scan values wrong or SSID injected HTML");
        }else check(!w->toolTip().contains("1 APs"),"Failure retained stale AP data");
        m->shutdown();delete w;delete m;
    }
    qputenv("KRELLWIFI_TEST_CASE","timeout");m=plugin->createMonitors(&theme,&parent).first();w=m->createWidget(nullptr);m->tick();
    QElapsedTimer stopped;stopped.start();m->shutdown();delete w;delete m;check(stopped.elapsed()<2500,"Helper shutdown stalled");
    {RemoteSource remote;check(plugin->createMonitors(&theme,&parent).isEmpty(),"Remote instance created local Wi-Fi monitor");}
    QSettings().setValue("plugins/krellwifi/enabled",false);check(plugin->createMonitors(&theme,&parent).isEmpty(),"Disable failed");
    qunsetenv("KRELLWIFI_TEST_CASE");QFile::remove(helper);
    qInfo("PASS actual DLL, native hardware state, AP fixture, escaped SSID, permissions/radio/service states, malformed/oversized data, timeout, shutdown and remote isolation");
}
