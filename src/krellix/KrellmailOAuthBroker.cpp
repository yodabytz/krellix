#include "KrellmailOAuthBroker.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRandomGenerator>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {

constexpr quint16 kOAuthFirstPort = 53682;
constexpr quint16 kOAuthLastPort = 53691;
constexpr int kMaxRequestBytes = 64 * 1024;

QString accountKey(int index, const QString &name)
{
    return QStringLiteral("plugins/krellmail/account%1/%2").arg(index + 1).arg(name);
}

QString randomUrlToken(int bytes)
{
    QByteArray data;
    data.resize(bytes);
    for (int i = 0; i < bytes; ++i)
        data[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    return QString::fromLatin1(data.toBase64(QByteArray::Base64UrlEncoding
                                             | QByteArray::OmitTrailingEquals));
}

QString pkceChallenge(const QString &verifier)
{
    const QByteArray hash = QCryptographicHash::hash(verifier.toLatin1(),
                                                     QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toBase64(QByteArray::Base64UrlEncoding
                                             | QByteArray::OmitTrailingEquals));
}

QString htmlResponse(int code, const QString &title, const QString &body)
{
    const QString reason = code == 200 ? QStringLiteral("OK") : QStringLiteral("Not Found");
    return QStringLiteral(
        "HTTP/1.1 %1 %2\r\nContent-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n\r\n"
        "<!doctype html><html><body><h3>%3</h3><p>%4</p></body></html>")
        .arg(code)
        .arg(reason, title, body);
}

} // namespace

KrellmailOAuthBroker::KrellmailOAuthBroker(QObject *parent)
    : QObject(parent)
{
}

bool KrellmailOAuthBroker::isActive() const
{
    return m_server && m_server->isListening();
}

int KrellmailOAuthBroker::activeAccount() const
{
    return m_accountIndex;
}

QString KrellmailOAuthBroker::redirectUri() const
{
    return m_redirectUri;
}

void KrellmailOAuthBroker::begin(int accountIndex, const QString &user, const QString &clientId,
                                 const QString &clientSecret)
{
    if (accountIndex < 0)
        return;
    if (clientId.trimmed().isEmpty() || user.trimmed().isEmpty()) {
        emit statusChanged(accountIndex, QStringLiteral(
            "OAuth needs the full Gmail address and a Google desktop OAuth client ID."));
        return;
    }

    resetServer();
    m_server = new QTcpServer(this);
    m_accountIndex = accountIndex;
    m_clientId = clientId.trimmed();
    m_clientSecret = clientSecret.trimmed();
    m_verifier = randomUrlToken(48);
    m_state = randomUrlToken(24);
    m_exchangeInProgress = false;

    if (!listen()) {
        fail(QStringLiteral("Could not start local OAuth callback: %1").arg(m_server->errorString()));
        return;
    }
    connect(m_server, &QTcpServer::newConnection, this, &KrellmailOAuthBroker::handleConnection);
    m_redirectUri = QStringLiteral("http://127.0.0.1:%1/").arg(m_server->serverPort());

    if (!selfTest()) {
        fail(QStringLiteral("OAuth callback self-test failed on %1").arg(m_redirectUri));
        return;
    }

    QUrl authUrl(QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), m_clientId);
    query.addQueryItem(QStringLiteral("redirect_uri"), m_redirectUri);
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("scope"), QStringLiteral("https://mail.google.com/"));
    query.addQueryItem(QStringLiteral("access_type"), QStringLiteral("offline"));
    query.addQueryItem(QStringLiteral("prompt"), QStringLiteral("consent"));
    query.addQueryItem(QStringLiteral("login_hint"), user.trimmed());
    query.addQueryItem(QStringLiteral("code_challenge"), pkceChallenge(m_verifier));
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    query.addQueryItem(QStringLiteral("state"), m_state);
    authUrl.setQuery(query);

    emit statusChanged(accountIndex, QStringLiteral("OAuth listener active on %1. Waiting for Google authorization.")
                                       .arg(m_redirectUri));
    const QString authUrlText = authUrl.toString(QUrl::FullyEncoded);
    bool opened = QDesktopServices::openUrl(authUrl);
    if (!opened)
        opened = QProcess::startDetached(QStringLiteral("xdg-open"), {authUrlText});
    if (!opened) {
        emit statusChanged(accountIndex, QStringLiteral(
            "OAuth listener active on %1. Could not open a browser; copy this URL:\n%2")
            .arg(m_redirectUri, authUrlText));
    }
}

void KrellmailOAuthBroker::finishFromCallbackUrl(int accountIndex, const QString &callbackUrl)
{
    if (accountIndex != m_accountIndex || m_redirectUri.isEmpty()) {
        emit statusChanged(accountIndex, QStringLiteral("Callback URL does not match the active OAuth request"));
        return;
    }
    const QUrl url(callbackUrl.trimmed());
    const QUrlQuery query(url);
    const QString code = query.queryItemValue(QStringLiteral("code"));
    const QString state = query.queryItemValue(QStringLiteral("state"));
    if (code.isEmpty() || state.isEmpty()) {
        emit statusChanged(accountIndex, QStringLiteral("Callback URL does not contain an OAuth code"));
        return;
    }
    exchangeCode(code, state, m_redirectUri);
}

void KrellmailOAuthBroker::resetServer()
{
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    m_accountIndex = -1;
    m_clientId.clear();
    m_clientSecret.clear();
    m_verifier.clear();
    m_state.clear();
    m_redirectUri.clear();
    m_exchangeInProgress = false;
}

bool KrellmailOAuthBroker::listen()
{
    if (!m_server)
        return false;
    const QHostAddress loopback(QStringLiteral("127.0.0.1"));
    for (quint16 port = kOAuthFirstPort; port <= kOAuthLastPort; ++port) {
        if (m_server->listen(loopback, port))
            return true;
    }
    return m_server->listen(loopback, 0);
}

bool KrellmailOAuthBroker::selfTest() const
{
    if (!m_server || !m_server->isListening())
        return false;
    QTcpSocket socket;
    socket.connectToHost(QHostAddress(QStringLiteral("127.0.0.1")), m_server->serverPort());
    return socket.waitForConnected(500);
}

void KrellmailOAuthBroker::handleConnection()
{
    if (!m_server)
        return;
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        socket->setParent(this);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { readSocket(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
        QTimer::singleShot(3000, socket, [socket]() {
            if (socket->state() != QAbstractSocket::UnconnectedState) {
                socket->disconnectFromHost();
            }
        });
    }
}

void KrellmailOAuthBroker::readSocket(QTcpSocket *socket)
{
    if (!socket)
        return;
    const QByteArray request = socket->readAll();
    if (request.size() > kMaxRequestBytes) {
        socket->disconnectFromHost();
        return;
    }

    const QList<QByteArray> lines = request.split('\n');
    const QByteArray first = lines.isEmpty() ? QByteArray() : lines.first();
    const QList<QByteArray> parts = first.split(' ');
    const QByteArray path = parts.size() > 1 ? parts.at(1) : QByteArray("/");
    const QUrl url(QStringLiteral("http://127.0.0.1:%1").arg(m_server ? m_server->serverPort() : 0)
                   + QString::fromUtf8(path));
    const QUrlQuery query(url);
    const QString code = query.queryItemValue(QStringLiteral("code"));
    const QString state = query.queryItemValue(QStringLiteral("state"));

    if (code.isEmpty()) {
        const QString response = htmlResponse(404,
            QStringLiteral("Krellmail OAuth listener is running."),
            QStringLiteral("Return to Google authorization and allow access."));
        socket->write(response.toUtf8());
        socket->disconnectFromHost();
        return;
    }

    const QString response = htmlResponse(200,
        QStringLiteral("Krellmail is authorized."),
        QStringLiteral("You can close this window and return to Krellix."));
    socket->write(response.toUtf8());
    socket->disconnectFromHost();
    exchangeCode(code, state, m_redirectUri);
}

void KrellmailOAuthBroker::exchangeCode(const QString &code, const QString &state, const QString &redirectUri)
{
    if (m_accountIndex < 0)
        return;
    if (m_exchangeInProgress) {
        emit statusChanged(m_accountIndex, QStringLiteral("OAuth code already received; waiting for token response..."));
        return;
    }
    if (code.isEmpty() || state != m_state) {
        fail(QStringLiteral("OAuth failed: callback state did not match"));
        return;
    }
    m_exchangeInProgress = true;
    if (m_server)
        m_server->close();
    if (!m_network)
        m_network = new QNetworkAccessManager(this);

    QNetworkRequest tokenRequest(QUrl(QStringLiteral("https://oauth2.googleapis.com/token")));
    tokenRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                           QStringLiteral("application/x-www-form-urlencoded"));
    QUrlQuery body;
    body.addQueryItem(QStringLiteral("client_id"), m_clientId);
    if (!m_clientSecret.isEmpty())
        body.addQueryItem(QStringLiteral("client_secret"), m_clientSecret);
    body.addQueryItem(QStringLiteral("code"), code);
    body.addQueryItem(QStringLiteral("code_verifier"), m_verifier);
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    body.addQueryItem(QStringLiteral("redirect_uri"), redirectUri);
    emit statusChanged(m_accountIndex, QStringLiteral("Exchanging OAuth code..."));
    QNetworkReply *reply = m_network->post(tokenRequest, body.query(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { finishTokenReply(reply); });
}

void KrellmailOAuthBroker::finishTokenReply(QNetworkReply *reply)
{
    if (!reply)
        return;
    reply->deleteLater();
    const int account = m_accountIndex;
    if (account < 0)
        return;

    const QByteArray payload = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    const QString refreshToken = doc.object().value(QStringLiteral("refresh_token")).toString();
    if (reply->error() != QNetworkReply::NoError || refreshToken.isEmpty()) {
        const QJsonObject obj = doc.object();
        QString detail = obj.value(QStringLiteral("error_description")).toString();
        if (detail.isEmpty())
            detail = obj.value(QStringLiteral("error")).toString();
        if (detail.isEmpty() && !payload.isEmpty())
            detail = QString::fromUtf8(payload.left(300));
        if (detail.isEmpty())
            detail = reply->errorString();
        emit statusChanged(account, QStringLiteral("OAuth token failed: %1").arg(detail));
        m_exchangeInProgress = false;
        return;
    }

    QSettings s;
    s.setValue(accountKey(account, QStringLiteral("oauth_refresh_token")), refreshToken);
    s.setValue(accountKey(account, QStringLiteral("auth")), QStringLiteral("oauth"));
    emit statusChanged(account, QStringLiteral("Gmail authorized"));
    emit authorized(account);
    emit settingsChanged();
    resetServer();
}

void KrellmailOAuthBroker::fail(const QString &message)
{
    const int account = m_accountIndex;
    if (account >= 0)
        emit statusChanged(account, message);
    resetServer();
}
