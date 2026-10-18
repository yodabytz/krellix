#pragma once

#include "monitors/MonitorBase.h"
#include "sdk/KrellixPlugin.h"

#include <QPointer>

class Decal;

class KrellstackMonitor : public MonitorBase
{
    Q_OBJECT

public:
    explicit KrellstackMonitor(Theme *theme, QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("krellstack"); }
    QString displayName() const override { return QStringLiteral("KrellStack"); }
    int tickIntervalMs() const override { return 30000; }
    QWidget *createWidget(QWidget *parent) override;
    void tick() override;

private:
    QPointer<Decal> m_summary;
    QPointer<Decal> m_detail;
};

class KrellstackPlugin : public QObject, public IKrellixPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID KrellixPlugin_iid)
    Q_INTERFACES(IKrellixPlugin)

public:
    QString pluginId() const override { return QStringLiteral("io.krellix.krellstack"); }
    QString pluginName() const override { return QStringLiteral("KrellStack"); }
    QString pluginVersion() const override { return QStringLiteral("0.1.0"); }
    QList<MonitorBase *> createMonitors(Theme *theme, QObject *parent) override;
};
