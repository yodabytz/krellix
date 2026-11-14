#include "KrellwifiPlugin.h"
#include "WifiData.h"
#include "remote/RemoteSource.h"
#include "widgets/Panel.h"
#include "widgets/Decal.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QSettings>
WindowsWifiMonitor::WindowsWifiMonitor(Theme *theme,QObject *parent):MonitorBase(theme,parent)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout,&QTimer::timeout,this,[this]{finish();unavailable(QStringLiteral("scan helper timed out"));});
}
WindowsWifiMonitor::~WindowsWifiMonitor(){shutdown();}
void WindowsWifiMonitor::finish()
{
    m_timeout.stop();
    if(m_probe){
        auto *p=m_probe;m_probe=nullptr;disconnect(p,nullptr,this,nullptr);
        if(p->state()!=QProcess::NotRunning){
            connect(p,&QProcess::finished,p,&QObject::deleteLater);
            p->kill();
        }else p->deleteLater();
    }
}
void WindowsWifiMonitor::shutdown()
{
    m_stopped=true;m_timeout.stop();
    if(m_probe){disconnect(m_probe,nullptr,this,nullptr);m_probe->kill();m_probe->waitForFinished(1000);delete m_probe;m_probe=nullptr;}
}
QWidget *WindowsWifiMonitor::createWidget(QWidget *parent)
{
    auto *panel=new Panel(theme(),parent);panel->setSurfaceKey(QStringLiteral("panel_bg_krellwifi"));panel->setTitle(QStringLiteral("WiFi"));m_panel=panel;
    m_summary=panel->addDecal(QStringLiteral("value"),QStringLiteral("text_primary"));m_summary->setAlignment(Qt::AlignHCenter);
    m_detail=panel->addDecal(QStringLiteral("label"),QStringLiteral("text_secondary"));m_detail->setAlignment(Qt::AlignHCenter);
    unavailable(QStringLiteral("checking Windows Wi-Fi..."));QTimer::singleShot(0,this,&WindowsWifiMonitor::tick);return panel;
}
void WindowsWifiMonitor::unavailable(const QString &detail)
{
    if(m_summary)m_summary->setText(QStringLiteral("WiFi unavailable"));
    if(m_detail)m_detail->setText(detail);
    if(m_panel)m_panel->setToolTip(QStringLiteral("KrellWiFi: %1").arg(detail));
}
void WindowsWifiMonitor::tick()
{
    if(m_stopped)return;
    if(RemoteSource::instance()){finish();unavailable(QStringLiteral("remote Wi-Fi telemetry is not supported"));return;}
    if(m_probe || (m_attempt.isValid() && m_attempt.elapsed()<60000))return;
    m_attempt.start();m_output.clear();
    const auto root=QCoreApplication::applicationDirPath();
    const auto helper=root+QStringLiteral("/wifi/krellwifi-probe.exe");
    if(!QFileInfo::exists(helper)){unavailable(QStringLiteral("Windows Wi-Fi helper missing"));return;}
    unavailable(QStringLiteral("scanning Windows Wi-Fi..."));
    auto *p=new QProcess(this);m_probe=p;
    p->setProgram(helper);p->setWorkingDirectory(root);
    auto environment=QProcessEnvironment::systemEnvironment();environment.insert(QStringLiteral("PATH"),QDir::toNativeSeparators(root)+QLatin1Char(';')+environment.value(QStringLiteral("PATH")));p->setProcessEnvironment(environment);
    connect(p,&QProcess::readyReadStandardError,this,[p]{p->readAllStandardError();});
    connect(p,&QProcess::readyReadStandardOutput,this,[this,p]{
        m_output+=p->readAllStandardOutput();
        if(m_output.size()>1024*1024){finish();m_output.clear();unavailable(QStringLiteral("scan response too large"));}
    });
    connect(p,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart){finish();unavailable(QStringLiteral("Windows Wi-Fi helper could not start"));}});
    connect(p,&QProcess::finished,this,[this,p](int code,QProcess::ExitStatus status){
        m_output+=p->readAllStandardOutput();finish();
        const auto doc=QJsonDocument::fromJson(m_output);
        if(code || status!=QProcess::NormalExit || m_output.size()>1024*1024 || !doc.isObject()){unavailable(QStringLiteral("Windows Wi-Fi check failed"));return;}
        const auto view=wifiView(doc.object());
        if(!view.valid){unavailable(QStringLiteral("invalid scan data"));return;}
        if(m_summary)m_summary->setText(view.summary);
        if(m_detail)m_detail->setText(view.detail);
        if(m_panel)m_panel->setToolTip(QStringLiteral("<qt>%1</qt>").arg((view.tooltip+QStringLiteral("\nLocal Windows only. Refreshes every 60 seconds. No stored passwords are read.")).toHtmlEscaped().replace(QLatin1Char('\n'),QStringLiteral("<br>"))));
        m_output.clear();
    });
    m_timeout.start(20000);p->start();
}
QList<MonitorBase *> KrellwifiPlugin::createMonitors(Theme *theme,QObject *parent)
{
    if(RemoteSource::instance() || !QSettings().value(QStringLiteral("plugins/krellwifi/enabled"),false).toBool())return {};
    return {new WindowsWifiMonitor(theme,parent)};
}
