#include "KrellstackPlugin.h"
#include "remote/RemoteSource.h"
#include "widgets/Decal.h"
#include "widgets/Panel.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QSettings>

WindowsStackMonitor::WindowsStackMonitor(Theme *theme,QObject *parent):MonitorBase(theme,parent) {
    m_timeout.setSingleShot(true);
    connect(&m_timeout,&QTimer::timeout,this,[this]{finish();failed("Container query timed out");});
    if(auto *remote=RemoteSource::instance()) {
        // Imported Qt signals require name-based lookup across the Windows DLL.
        connect(remote,SIGNAL(sampleReceived()),this,SLOT(updateRemote()));
        connect(remote,SIGNAL(connectionStateChanged(bool)),this,SLOT(updateRemote()));
    }
}
WindowsStackMonitor::~WindowsStackMonitor(){shutdown();}
void WindowsStackMonitor::finish() {
    m_timeout.stop();m_finished.start();
    if(!m_probe)return;
    auto *process=m_probe;m_probe=nullptr;disconnect(process,nullptr,this,nullptr);
    if(process->state()==QProcess::NotRunning)process->deleteLater();
    else {connect(process,&QProcess::finished,process,&QObject::deleteLater);process->kill();}
}
void WindowsStackMonitor::shutdown() {
    m_stopped=true;m_timeout.stop();
    if(m_probe){disconnect(m_probe,nullptr,this,nullptr);m_probe->kill();m_probe->waitForFinished(1000);delete m_probe;m_probe=nullptr;}
}
QWidget *WindowsStackMonitor::createWidget(QWidget *parent) {
    auto *panel=new Panel(theme(),parent);panel->setTitle("Stack");panel->setSurfaceKey("panel_bg_krellstack");
    m_summary=panel->addDecal("label","text_primary");m_summary->setObjectName("stackSummary");m_summary->setAlignment(Qt::AlignHCenter);
    m_detail=panel->addDecal("label","text_secondary");m_detail->setObjectName("stackDetail");m_detail->setAlignment(Qt::AlignHCenter);
    m_attention=panel->addDecal("label","text_secondary");m_attention->setObjectName("stackAttention");m_attention->setAlignment(Qt::AlignHCenter);
    display("Checking stack...",{});QTimer::singleShot(0,this,&WindowsStackMonitor::tick);return panel;
}
void WindowsStackMonitor::display(const QString &summary,const QString &detail,const QString &tooltip,const QString &attention) {
    for(const auto &pair:{qMakePair(m_summary.data(),summary),qMakePair(m_detail.data(),detail),qMakePair(m_attention.data(),attention)}) {
        if(!pair.first)continue;
        if(pair.first->text()!=pair.second)pair.first->setText(pair.second);
        pair.first->setToolTip("<qt>"+tooltip.toHtmlEscaped().replace('\n',"<br>")+"</qt>");
        pair.first->setAccessibleDescription(tooltip);
    }
}
void WindowsStackMonitor::failed(const QString &detail){display("Stack unavailable","Check failed",detail);}
void WindowsStackMonitor::render(const WindowsStack::Snapshot &snapshot) {
    if(!snapshot.valid){failed("Invalid container data");return;}
    WindowsStack::Counts total;int successes=0,failures=0,missing=0;
    QStringList details{"Read-only checks; no containers, services or runtimes are started or changed.",
        "Refresh: 30 seconds after the previous check finishes. Hover for source details.",
        "Attention includes unhealthy, restarting, dead and exited/stopped containers; stopped does not necessarily mean broken."};
    for(const auto &source:snapshot.sources) {
        const auto &c=source.counts;
        if(source.status=="ok") {
            ++successes;total.containers+=c.containers;total.running+=c.running;total.images+=c.images;
            total.compose+=c.compose;total.networks+=c.networks;total.unhealthy+=c.unhealthy;total.pods+=c.pods;
            details<<QStringLiteral("%1: %2/%3 containers, %4 images, %5 Compose projects, %6 networks, %7 pods, %8 attention")
                .arg(source.name).arg(c.running).arg(c.containers).arg(c.images).arg(c.compose).arg(c.networks).arg(c.pods).arg(c.unhealthy);
        }else {if(source.status=="missing")++missing;else ++failures;details<<source.name+": "+source.status;}
        if(!source.detail.isEmpty())details<<source.detail;
    }
    if(!successes){
        if(missing&&!failures){display("No container tools","Local runtime needed",details.join('\n'));return;}
        QString title="Container check failed";
        if(failures==1)for(const auto &source:snapshot.sources)
            if(source.status!="ok"&&source.status!="missing"){
                if(source.name=="Docker"||source.name=="Podman"||source.name=="Kubernetes")title=source.name+" unavailable";
            }
        display(title,"Hover for error details",details.join('\n'));return;
    }
    QString summary=QStringLiteral("Running: %1 of %2").arg(total.running).arg(total.containers);
    QString detail=QStringLiteral("Images: %1 | Projects: %2").arg(total.images).arg(total.compose);
    if(total.pods)detail+=QStringLiteral(" | Pods: %1").arg(total.pods);
    if(failures)summary+=" !";
    display(summary,detail,details.join('\n'),QStringLiteral("Stopped or unhealthy: %1").arg(total.unhealthy));
}
void WindowsStackMonitor::updateRemote() {
    auto *remote=RemoteSource::instance();if(!remote||m_stopped)return;
    if(m_probe)finish();
    if(!remote->isConnected()){display("Server disconnected",{},"No local runtime fallback.");return;}
    const auto stack=remote->stackInfo();
    if(stack.source.isEmpty()){display("Server data unavailable",{},"This server has not supplied stack telemetry.");return;}
    if(!stack.error.isEmpty()){failed("Server: "+remote->remoteHostname().left(256)+"\n"+stack.error.left(4096));return;}
    for(int count:{stack.containers,stack.running,stack.images,stack.compose,stack.dockerNetworks,stack.unhealthy})
        if(count<0||count>1000000){failed("Invalid server stack counts");return;}
    if(stack.running>stack.containers||stack.unhealthy>stack.containers){failed("Invalid server container totals");return;}
    const QString tooltip=QStringLiteral("Server: %1\nSource: %2\n%3/%4 containers; %5 images; %6 Compose projects; %7 network interfaces; %8 attention.\nOnly this daemon's data is used. Network-interface counts are not Docker network-object counts.")
        .arg(remote->remoteHostname().left(256),stack.source.left(128)).arg(stack.running).arg(stack.containers).arg(stack.images).arg(stack.compose).arg(stack.dockerNetworks).arg(stack.unhealthy);
    display(QStringLiteral("Running: %1 of %2").arg(stack.running).arg(stack.containers),
        QStringLiteral("Images: %1 | Projects: %2").arg(stack.images).arg(stack.compose),
        tooltip+"\nStopped or unhealthy includes stopped, exited, restarting, dead and unhealthy containers. Intentionally stopped containers are not necessarily broken. Projects means Docker Compose projects.",
        QStringLiteral("Stopped or unhealthy: %1").arg(stack.unhealthy));
}
void WindowsStackMonitor::tick() {
    if(m_stopped)return;
    if(RemoteSource::instance()){updateRemote();return;}
    const auto hosts=QSettings().value("plugins/krellstack/hosts").toString();
    if(hosts!=m_hosts){finish();m_finished.invalidate();m_hosts=hosts;}
    if(m_probe||(m_finished.isValid()&&m_finished.elapsed()<30000))return;
    QStringList parsed;
    if(!WindowsStack::hosts(hosts,parsed)){m_finished.start();failed("Use at most four SSH hostnames or user@host entries; shell syntax and SSH options are not allowed.");return;}
    const auto helper=QCoreApplication::applicationDirPath()+"/krellstack-probe.exe";
    if(!QFileInfo::exists(helper)){m_finished.start();failed("Stack helper missing; reinstall the matching plugin.");return;}
    display("Checking stack...",{});m_output.clear();
    auto *process=new QProcess(this);m_probe=process;process->setProgram(helper);
    auto drain=[this,process](bool error){
        process->setReadChannel(error?QProcess::StandardError:QProcess::StandardOutput);
        const auto bytes=process->read(qMax<qint64>(1,1024*1024-m_output.size()+1));
        if(error){if(!bytes.isEmpty()){finish();failed("Unexpected stack helper error");}return;}
        m_output+=bytes;if(m_output.size()>1024*1024){finish();m_output.clear();failed("Stack response too large");}
    };
    connect(process,&QProcess::readyReadStandardOutput,this,[drain]{drain(false);});
    connect(process,&QProcess::readyReadStandardError,this,[drain]{drain(true);});
    connect(process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart){finish();failed("Stack helper failed to start");}});
    connect(process,&QProcess::started,this,[this,process]{
        process->write(QJsonDocument(QJsonObject{{"hosts",m_hosts}}).toJson(QJsonDocument::Compact));process->closeWriteChannel();
    });
    connect(process,&QProcess::finished,this,[this,drain](int code,QProcess::ExitStatus status){
        drain(false);if(!m_probe)return;finish();
        const auto document=QJsonDocument::fromJson(m_output);m_output.clear();
        if(code||status!=QProcess::NormalExit||!document.isObject()){failed("Container query failed");return;}
        render(WindowsStack::parse(document.object()));
    });
    m_timeout.start(65000);process->start();
}
QList<MonitorBase *> KrellstackPlugin::createMonitors(Theme *theme,QObject *parent) {
    if(!QSettings().value("plugins/krellstack/enabled",false).toBool())return{};
    return{new WindowsStackMonitor(theme,parent)};
}
