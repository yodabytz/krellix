#pragma once
#include "sdk/KrellixPlugin.h"
#include "monitors/MonitorBase.h"
#include <QElapsedTimer>
#include <QPointer>
#include <QProcess>
#include <QTimer>
class Panel;
class Decal;
class WindowsWifiMonitor final : public MonitorBase {
    Q_OBJECT
public:
    WindowsWifiMonitor(Theme *theme,QObject *parent);
    ~WindowsWifiMonitor() override;
    QString id() const override{return QStringLiteral("krellwifi");}
    QString displayName() const override{return QStringLiteral("KrellWiFi");}
    int tickIntervalMs() const override{return 1000;}
    QWidget *createWidget(QWidget *parent) override;
    void tick() override;
    void shutdown() override;
private:
    void unavailable(const QString &detail);
    void finish();
    QPointer<Panel> m_panel;
    QPointer<Decal> m_summary,m_detail;
    QProcess *m_probe=nullptr;
    QTimer m_timeout;
    QElapsedTimer m_attempt;
    QByteArray m_output;
    bool m_stopped=false;
};
class KrellwifiPlugin final : public QObject,public IKrellixPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID KrellixPlugin_iid)
    Q_INTERFACES(IKrellixPlugin)
public:
    QString pluginId() const override{return QStringLiteral("io.krellix.krellwifi");}
    QString pluginName() const override{return QStringLiteral("KrellWiFi");}
    QString pluginVersion() const override{return QStringLiteral("0.1.3-windows.1");}
    QList<MonitorBase *> createMonitors(Theme *theme,QObject *parent) override;
};
