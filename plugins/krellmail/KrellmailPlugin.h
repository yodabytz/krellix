#pragma once

#include "monitors/MonitorBase.h"
#include "sdk/KrellixPlugin.h"

#include <QPaintEvent>
#include <QPointer>
#include <QSslSocket>
#include <QTimer>
#include <QVector>
#include <QWidget>

class QEvent;
class QLabel;

struct KrellmailAccount {
    QString protocol;
    QString host;
    int port = 0;
    bool ssl = true;
    QString username;
    QString password;
};

class KrellmailEnvelope : public QWidget
{
    Q_OBJECT

public:
    explicit KrellmailEnvelope(Theme *theme, QWidget *parent = nullptr);

    void setMailCount(int count);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Theme *m_theme = nullptr;
    QTimer m_timer;
    int m_count = 0;
    int m_phase = 0;
};

class KrellmailMonitor : public MonitorBase
{
    Q_OBJECT

public:
    explicit KrellmailMonitor(Theme *theme, QObject *parent = nullptr);
    ~KrellmailMonitor() override;

    QString id() const override;
    QString displayName() const override;
    int tickIntervalMs() const override;
    QWidget *createWidget(QWidget *parent) override;
    void tick() override;
    void shutdown() override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onConnected();
    void onEncrypted();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);
    void onTimeout();

private:
    enum class State {
        Idle,
        PopGreeting,
        PopUser,
        PopPass,
        PopStat,
        ImapGreeting,
        ImapLogin,
        ImapSelect,
        ImapSearch,
        ImapLogout
    };

    QVector<KrellmailAccount> readAccounts() const;
    void startCheck(bool force = false);
    void finishCheck();
    void failCurrent(const QString &message);
    void startNextAccount();
    void sendLine(const QByteArray &line);
    void processLine(const QByteArray &line);
    void applyStatus();

    QPointer<KrellmailEnvelope> m_icon;
    QPointer<QLabel> m_primary;
    QPointer<QLabel> m_detail;
    QSslSocket *m_socket = nullptr;
    QTimer m_timeout;
    QVector<KrellmailAccount> m_accounts;
    int m_accountIndex = -1;
    int m_totalCount = 0;
    int m_errorCount = 0;
    QString m_lastError;
    QByteArray m_buffer;
    QByteArray m_imapTag;
    State m_state = State::Idle;
    bool m_fetching = false;
    bool m_tearingDown = false;
};

class KrellmailPlugin : public QObject, public IKrellixPlugin
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
