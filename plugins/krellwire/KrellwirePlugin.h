#pragma once

#include "monitors/MonitorBase.h"
#include "sdk/KrellixPlugin.h"

#include <QNetworkAccessManager>
#include <QPointer>
#include <QHash>
#include <QVector>

#ifdef Q_OS_WIN
#include <QElapsedTimer>
#include <QStringList>
#endif

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

#ifdef Q_OS_WIN
private slots:
    void updateTheme();
#endif

private:
    void fetch();
    void handleReply(QNetworkReply *reply);
    void cancelReplies();
    void publishItems();

    QPointer<QWidget> m_ticker;
    QNetworkAccessManager m_net;
    QVector<QPointer<QNetworkReply>> m_replies;
    QHash<QNetworkReply *, QByteArray> m_payloads;
    std::vector<KrellwireFeedItem> m_items;
    bool m_fetching = false;
    bool m_tearingDown = false;
#ifdef Q_OS_WIN
    QStringList m_requestedUrls;
    int m_requestedLimit = 0;
    QElapsedTimer m_requestedAt;
    bool m_feedSucceeded = false;
#endif
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
