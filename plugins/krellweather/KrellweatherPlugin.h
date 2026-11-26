#pragma once

#include "monitors/MonitorBase.h"
#include "sdk/KrellixPlugin.h"

#include <QNetworkAccessManager>
#include <QPointer>

#ifdef Q_OS_WIN
#include <QElapsedTimer>
#include <QJsonObject>
#endif

class QLabel;
class QNetworkReply;
class QJsonObject;
class QWidget;

class KrellweatherMonitor : public MonitorBase
{
    Q_OBJECT

public:
    explicit KrellweatherMonitor(Theme *theme, QObject *parent = nullptr);
    ~KrellweatherMonitor() override;

    QString id() const override;
    QString displayName() const override;
    int tickIntervalMs() const override;
    QWidget *createWidget(QWidget *parent) override;
    void tick() override;
    void shutdown() override;

#ifdef Q_OS_WIN
private slots:
    void updateTheme();
#endif

private:
    friend class KrellweatherLayoutFixture;
    void fetch();
    void handleReply(QNetworkReply *reply);
    void cancelPendingReply();
    void applyThemeColors();
    void renderWeather(const QJsonObject &obj);
    void setMessage(const QString &line1, const QString &line2);

    QPointer<QLabel> m_location;
    QPointer<QWidget> m_icon;
    QPointer<QLabel> m_primary;
    QPointer<QLabel> m_detail;
    QPointer<QNetworkReply> m_reply;
    QNetworkAccessManager m_net;
    bool m_fetching = false;
    bool m_tearingDown = false;
#ifdef Q_OS_WIN
    QString m_requestedStation;
    QElapsedTimer m_requestedAt;
    QJsonObject m_observation;
    bool m_lastUnitsCelsius = false;
#endif
};

class KrellweatherPlugin : public QObject, public IKrellixPlugin
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
