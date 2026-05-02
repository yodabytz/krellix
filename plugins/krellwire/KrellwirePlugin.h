#pragma once

#include "monitors/MonitorBase.h"
#include "sdk/KrellixPlugin.h"

#include <QNetworkAccessManager>
#include <QPointer>
#include <QVector>

#include <vector>

class QNetworkReply;
class QWidget;

struct KrellwireFeedItem {
    QString title;
    QUrl url;
};

class KrellwireMonitor : public MonitorBase
{
    Q_OBJECT

public:
    explicit KrellwireMonitor(Theme *theme, QObject *parent = nullptr);
    ~KrellwireMonitor() override;

    QString id() const override;
    QString displayName() const override;
    int tickIntervalMs() const override;
    QWidget *createWidget(QWidget *parent) override;
    void tick() override;
    void shutdown() override;

private:
    void fetch();
    void handleReply(QNetworkReply *reply);
    void cancelReplies();
    void publishItems();

    QPointer<QWidget> m_ticker;
    QNetworkAccessManager m_net;
    QVector<QPointer<QNetworkReply>> m_replies;
    std::vector<KrellwireFeedItem> m_items;
    bool m_fetching = false;
    bool m_tearingDown = false;
};

class KrellwirePlugin : public QObject, public IKrellixPlugin
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
