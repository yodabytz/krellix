#pragma once
#include "sdk/KrellixPlugin.h"
#include "monitors/MonitorBase.h"
#include "StackData.h"
#include <QElapsedTimer>
#include <QPointer>
#include <QProcess>
#include <QTimer>
class Decal;
class WindowsStackMonitor final:public MonitorBase {
    Q_OBJECT
public:
    WindowsStackMonitor(Theme *theme,QObject *parent);
    ~WindowsStackMonitor() override;
    QString id()const override{return QStringLiteral("krellstack");}
    QString displayName()const override{return QStringLiteral("KrellStack");}
    int tickIntervalMs()const override{return 1000;}
    QWidget *createWidget(QWidget *parent)override;
    void tick()override;
    void shutdown()override;
private slots:
    void updateRemote();
private:
    void finish();
    void display(const QString &summary,const QString &detail,const QString &tooltip={},const QString &attention={},const QString &projects={});
    void failed(const QString &detail);
    void render(const WindowsStack::Snapshot &snapshot);
    QPointer<Decal> m_summary,m_detail,m_attention,m_projects;
    QProcess *m_probe=nullptr;
    QTimer m_timeout;
    QElapsedTimer m_finished;
    QByteArray m_output;
    QString m_hosts;
    bool m_stopped=false;
};
class KrellstackPlugin final:public QObject,public IKrellixPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID KrellixPlugin_iid)
    Q_INTERFACES(IKrellixPlugin)
public:
    QString pluginId()const override{return QStringLiteral("io.krellix.krellstack");}
    QString pluginName()const override{return QStringLiteral("KrellStack");}
    QString pluginVersion()const override{return QStringLiteral("0.1.1-windows.3");}
    QList<MonitorBase *> createMonitors(Theme *theme,QObject *parent)override;
};
