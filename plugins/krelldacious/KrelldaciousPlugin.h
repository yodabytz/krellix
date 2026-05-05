#pragma once

#include "monitors/MonitorBase.h"
#include "sdk/KrellixPlugin.h"

#include <QPointer>

class QLabel;
class QPushButton;
class QSlider;
class Decal;

class KrelldaciousMonitor : public MonitorBase
{
    Q_OBJECT

public:
    explicit KrelldaciousMonitor(Theme *theme, QObject *parent = nullptr);
    ~KrelldaciousMonitor() override;

    QString id() const override;
    QString displayName() const override;
    int tickIntervalMs() const override;
    QWidget *createWidget(QWidget *parent) override;
    void tick() override;

private:
    void applyThemeColors();
    void sendPlayerCommand(const QString &method);
    void setAudaciousVolume(int percent);
    QVariant playerProperty(const QString &name, bool *ok = nullptr) const;

    QPointer<Decal> m_track;
    QPointer<QLabel> m_openAudacious;
    QPointer<QPushButton> m_prev;
    QPointer<QPushButton> m_playPause;
    QPointer<QPushButton> m_next;
    QPointer<QSlider> m_volume;
    bool m_updatingVolume = false;
};

class KrelldaciousPlugin : public QObject, public IKrellixPlugin
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
