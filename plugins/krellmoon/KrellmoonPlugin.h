#pragma once

#include "monitors/MonitorBase.h"
#include "sdk/KrellixPlugin.h"

#include <QPointer>

class QLabel;
class QWidget;

class KrellmoonMonitor : public MonitorBase
{
    Q_OBJECT

public:
    explicit KrellmoonMonitor(Theme *theme, QObject *parent = nullptr);
    ~KrellmoonMonitor() override;

    QString id() const override;
    QString displayName() const override;
    int tickIntervalMs() const override;
    QWidget *createWidget(QWidget *parent) override;
    void tick() override;
    void shutdown() override;

private:
    void applyThemeColors();
    void refresh();

    QPointer<QWidget> m_moon;
    QPointer<QLabel> m_phaseLabel;
    bool m_tearingDown = false;
};

class KrellmoonPlugin : public QObject, public IKrellixPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID KrellixPlugin_iid)
    Q_INTERFACES(IKrellixPlugin)

public:
    QString pluginId() const override;
    QString pluginName() const override;
    QString pluginVersion() const override;
    QList<MonitorBase *> createMonitors(Theme *theme, QObject *parent) override;
};
