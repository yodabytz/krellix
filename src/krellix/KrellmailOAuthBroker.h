#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;
class QTcpSocket;

class KrellmailOAuthBroker : public QObject
{
    Q_OBJECT

public:
    explicit KrellmailOAuthBroker(QObject *parent = nullptr);

    bool isActive() const;
    int activeAccount() const;
    QString redirectUri() const;

    void begin(int accountIndex, const QString &user, const QString &clientId);
    void finishFromCallbackUrl(int accountIndex, const QString &callbackUrl);

signals:
    void statusChanged(int accountIndex, const QString &message);
    void authorized(int accountIndex);
    void settingsChanged();

private:
    void resetServer();
    bool listen();
    bool selfTest() const;
    void handleConnection();
    void readSocket(QTcpSocket *socket);
    void exchangeCode(const QString &code, const QString &state, const QString &redirectUri);
    void finishTokenReply(QNetworkReply *reply);
    void fail(const QString &message);

    QTcpServer *m_server = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    int m_accountIndex = -1;
    QString m_clientId;
    QString m_verifier;
    QString m_state;
    QString m_redirectUri;
};
