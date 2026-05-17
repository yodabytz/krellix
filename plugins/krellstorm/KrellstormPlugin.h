#pragma once

#include "monitors/MonitorBase.h"
#include "sdk/KrellixPlugin.h"

#include <QDateTime>
#include <QHash>
#include <QNetworkAccessManager>
#include <QPixmap>
#include <QPointer>
#include <QSet>

class QJsonArray;
class QLabel;
class QNetworkReply;
class QWidget;
class StormSeverityChip;

class KrellstormMonitor : public MonitorBase
{
    Q_OBJECT

public:
    explicit KrellstormMonitor(Theme *theme, QObject *parent = nullptr);
    ~KrellstormMonitor() override;

    QString id() const override;
    QString displayName() const override;
    int tickIntervalMs() const override;
    QWidget *createWidget(QWidget *parent) override;
    void tick() override;
    void shutdown() override;

private:
    void fetchMap();
    void fetchAlerts();
    void handleMapReply(QNetworkReply *reply);
    void handleAlertsReply(QNetworkReply *reply);
    void renderMap(const QByteArray &png);
    void renderAlerts(const QJsonArray &features);
    void applyThemeColors();
    void cancelReply(QPointer<QNetworkReply> &slot);
    void maybeNotify(const QString &alertId,
                     int severityRank,
                     const QString &event,
                     const QString &headline,
                     const QString &area,
                     const QString &expiresIso);
    void playSoundForSeverity(int severityRank);
    void setMapStaleBadge(const QString &text);

    QPointer<QLabel> m_title;
    QPointer<StormSeverityChip> m_chip;
    QPointer<QLabel> m_mapLabel;
    QPointer<QLabel> m_headline;
    QPointer<QLabel> m_expiry;

    QPointer<QNetworkReply> m_mapReply;
    QPointer<QNetworkReply> m_alertReply;
    QNetworkAccessManager m_net;

    QPixmap m_lastMapPixmap;
    QDateTime m_lastMapAt;
    QDateTime m_lastAlertAt;
    QDateTime m_lastSoundAt;
    QDateTime m_snoozeUntil;

    QHash<QString, int> m_seenAlertSeverity;
    int m_currentSeverity = 0;

    bool m_tearingDown = false;
};

class KrellstormPlugin : public QObject, public IKrellixPlugin
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
