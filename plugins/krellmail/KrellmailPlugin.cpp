#include "KrellmailPlugin.h"

#include "theme/Theme.h"
#include "widgets/Panel.h"

#include <QDesktopServices>
#include <QEvent>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QSizePolicy>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QtMath>

namespace {

constexpr int kMaxAccounts = 3;
constexpr int kMaxDynamicAccounts = 10;
constexpr int kTimeoutMs = 12000;
constexpr qsizetype kMaxMailLineBytes = 64 * 1024;
constexpr qsizetype kMaxMailBufferBytes = 256 * 1024;
constexpr qsizetype kMaxOAuthBytes = 512 * 1024;

QString accountKey(int index, const QString &name)
{
    return QStringLiteral("plugins/krellmail/account%1/%2").arg(index).arg(name);
}

int defaultPort(const QString &protocol, bool ssl)
{
    if (protocol == QLatin1String("imap"))
        return ssl ? 993 : 143;
    return ssl ? 995 : 110;
}

int configuredAccountCount(const QSettings &settings)
{
    if (settings.contains(QStringLiteral("plugins/krellmail/account_count")))
        return qBound(0, settings.value(QStringLiteral("plugins/krellmail/account_count"), 1).toInt(),
                      kMaxDynamicAccounts);
    return kMaxAccounts;
}

QColor themeColor(Theme *theme, const QString &key, const QColor &fallback)
{
    if (!theme) return fallback;
    const QColor color = theme->color(key, fallback);
    return color.isValid() ? color : fallback;
}

QString cssColor(const QColor &color)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

QString countText(int count)
{
    if (count == 1)
        return QStringLiteral("1 message");
    return QStringLiteral("%1 messages").arg(count);
}

int imapSearchCount(const QByteArray &line)
{
    const QList<QByteArray> parts = line.simplified().split(' ');
    if (parts.size() < 2
        || parts.at(0) != QByteArrayLiteral("*")
        || parts.at(1).toUpper() != QByteArrayLiteral("SEARCH"))
        return -1;
    int count = 0;
    for (int i = 2; i < parts.size(); ++i) {
        bool ok = false;
        parts.at(i).toUInt(&ok);
        if (ok) ++count;
    }
    return count;
}

bool isGmailHost(const QString &host)
{
    const QString lower = host.trimmed().toLower();
    return lower.contains(QStringLiteral("gmail.com"))
        || lower.contains(QStringLiteral("googlemail.com"));
}

QByteArray imapQuoted(const QString &value)
{
    return value.toUtf8().replace("\\", "\\\\").replace("\"", "\\\"");
}

QString loginPassword(const KrellmailAccount &account)
{
    QString password = account.password;
    if (isGmailHost(account.host)) {
        QString stripped = password;
        stripped.remove(QLatin1Char(' '));
        if (stripped.size() == 16)
            password = stripped;
    }
    return password;
}

} // namespace

KrellmailEnvelope::KrellmailEnvelope(Theme *theme, QWidget *parent)
    : QWidget(parent)
    , m_theme(theme)
{
    setFixedSize(28, 20);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_timer.setInterval(120);
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        m_phase = (m_phase + 1) % 18;
        update();
    });
}

void KrellmailEnvelope::setMailCount(int count)
{
    count = qMax(0, count);
    if (m_count == count) return;
    m_count = count;
    if (m_count > 0) {
        if (!m_timer.isActive()) m_timer.start();
    } else {
        m_timer.stop();
        m_phase = 0;
    }
    update();
}

void KrellmailEnvelope::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(width() / 36.0, height() / 26.0);

    const QColor bg = themeColor(m_theme, QStringLiteral("chart_bg"), QColor(15, 22, 28));
    QColor paper = themeColor(m_theme, QStringLiteral("text_primary"), QColor(236, 244, 248));
    QColor edge = themeColor(m_theme, QStringLiteral("panel_border"), QColor(70, 94, 105));
    QColor accent = themeColor(m_theme, QStringLiteral("text_accent"), QColor(86, 199, 255));
    QColor glow = accent;
    paper.setAlpha(235);
    edge.setAlpha(210);
    accent.setAlpha(235);
    glow.setAlpha(m_count > 0 ? 80 + (m_phase % 9) * 12 : 0);

    const QRectF body(4.5, 7.5, 27.0, 15.0);
    if (m_count > 0) {
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(QPointF(18.0, 15.0), 16.0 + (m_phase % 6), 10.0 + (m_phase % 4));
    }

    p.setPen(QPen(edge, 1.1));
    p.setBrush(bg.lighter(125));
    p.drawRoundedRect(body, 2.5, 2.5);

    QPainterPath flap;
    flap.moveTo(body.left() + 1.0, body.top() + 1.0);
    flap.lineTo(body.center().x(), body.top() + 9.5 + (m_count > 0 ? qSin(m_phase / 18.0 * M_PI * 2.0) * 1.1 : 0.0));
    flap.lineTo(body.right() - 1.0, body.top() + 1.0);
    p.setBrush(paper);
    p.drawPath(flap);

    QPainterPath lower;
    lower.moveTo(body.left() + 1.0, body.bottom() - 1.0);
    lower.lineTo(body.center().x(), body.top() + 9.0);
    lower.lineTo(body.right() - 1.0, body.bottom() - 1.0);
    p.setBrush(paper.darker(108));
    p.drawPath(lower);

    if (m_count > 0) {
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawEllipse(QRectF(25.0, 2.5, 8.0, 8.0));
        p.setPen(QPen(bg, 1.5));
        p.drawLine(QPointF(29.0, 4.7), QPointF(29.0, 7.0));
        p.drawPoint(QPointF(29.0, 8.4));
    }
}

KrellmailMonitor::KrellmailMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, &KrellmailMonitor::onTimeout);
}

KrellmailMonitor::~KrellmailMonitor()
{
    shutdown();
}

QString KrellmailMonitor::id() const
{
    return QStringLiteral("krellmail");
}

QString KrellmailMonitor::displayName() const
{
    return QStringLiteral("Krellmail");
}

int KrellmailMonitor::tickIntervalMs() const
{
    return qBound(30000,
                  QSettings().value(QStringLiteral("plugins/krellmail/update_ms"),
                                     300000).toInt(),
                  3600000);
}

QWidget *KrellmailMonitor::createWidget(QWidget *parent)
{
    auto *panel = new Panel(theme(), parent);
    panel->setSurfaceKey(QStringLiteral("panel_bg_krellmail"));
    panel->setCursor(Qt::PointingHandCursor);
    panel->installEventFilter(this);

    auto *row = new QWidget(panel);
    row->installEventFilter(this);
    auto *hbox = new QHBoxLayout(row);
    hbox->setContentsMargins(3, 1, 3, 1);
    hbox->setSpacing(4);

    m_icon = new KrellmailEnvelope(theme(), row);
    m_icon->installEventFilter(this);
    hbox->addWidget(m_icon);

    auto *textBox = new QWidget(row);
    textBox->installEventFilter(this);
    auto *vbox = new QVBoxLayout(textBox);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);
    m_primary = new QLabel(QStringLiteral("Mail"), textBox);
    m_detail = new QLabel(QStringLiteral("checking..."), textBox);
    m_primary->installEventFilter(this);
    m_detail->installEventFilter(this);
    m_primary->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_detail->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_primary->setFixedHeight(12);
    m_detail->setFixedHeight(11);
    vbox->addWidget(m_primary);
    vbox->addWidget(m_detail);
    hbox->addWidget(textBox, 1);
    panel->addWidget(row);

    applyStatus();
    startCheck(true);
    return panel;
}

void KrellmailMonitor::tick()
{
    startCheck(true);
}

bool KrellmailMonitor::eventFilter(QObject *watched, QEvent *event)
{
    if (event && event->type() == QEvent::MouseButtonRelease) {
        if (watched == m_icon.data()) {
            QDesktopServices::openUrl(QUrl(QStringLiteral("mailto:")));
            return true;
        }
        startCheck(true);
        return true;
    }
    return MonitorBase::eventFilter(watched, event);
}

void KrellmailMonitor::shutdown()
{
    m_tearingDown = true;
    m_timeout.stop();
    cancelOAuthReply();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

QVector<KrellmailAccount> KrellmailMonitor::readAccounts() const
{
    QSettings s;
    QVector<KrellmailAccount> accounts;
    const int accountCount = configuredAccountCount(s);
    for (int i = 1; i <= accountCount; ++i) {
        KrellmailAccount account;
        account.protocol = s.value(accountKey(i, QStringLiteral("protocol")),
                                   QStringLiteral("imap")).toString().trimmed().toLower();
        if (account.protocol != QLatin1String("pop3"))
            account.protocol = QStringLiteral("imap");
        account.auth = s.value(accountKey(i, QStringLiteral("auth")),
                               QStringLiteral("password")).toString().trimmed().toLower();
        if (account.auth != QLatin1String("oauth"))
            account.auth = QStringLiteral("password");
        account.host = s.value(accountKey(i, QStringLiteral("host"))).toString().trimmed();
        account.ssl = s.value(accountKey(i, QStringLiteral("ssl")), true).toBool();
        account.port = s.value(accountKey(i, QStringLiteral("port")),
                               defaultPort(account.protocol, account.ssl)).toInt();
        account.username = s.value(accountKey(i, QStringLiteral("username"))).toString();
        account.password = s.value(accountKey(i, QStringLiteral("password"))).toString();
        account.oauthClientId = s.value(accountKey(i, QStringLiteral("oauth_client_id"))).toString().trimmed();
        account.oauthRefreshToken = s.value(accountKey(i, QStringLiteral("oauth_refresh_token"))).toString().trimmed();
        if (!account.host.isEmpty() && !account.username.isEmpty())
            accounts.append(account);
    }
    return accounts;
}

void KrellmailMonitor::startCheck(bool force)
{
    if (m_tearingDown) return;
    if (m_fetching) {
        if (!force) return;
        m_timeout.stop();
        cancelOAuthReply();
        if (m_socket) {
            m_socket->disconnect(this);
            m_socket->abort();
            m_socket->deleteLater();
            m_socket = nullptr;
        }
        m_fetching = false;
        m_state = State::Idle;
    }
    m_accounts = readAccounts();
    ++m_checkGeneration;
    m_accountIndex = -1;
    m_totalCount = 0;
    m_errorCount = 0;
    m_lastError.clear();
    m_oauthAccessToken.clear();
    if (m_accounts.isEmpty()) {
        applyStatus();
        return;
    }
    m_fetching = true;
    if (m_detail) m_detail->setText(QStringLiteral("checking..."));
    startNextAccount();
}

void KrellmailMonitor::finishCheck()
{
    m_fetching = false;
    m_state = State::Idle;
    m_timeout.stop();
    cancelOAuthReply();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    applyStatus();
}

void KrellmailMonitor::failCurrent(const QString &message)
{
    ++m_errorCount;
    m_lastError = message.left(80);
    startNextAccount();
}

void KrellmailMonitor::startNextAccount()
{
    m_timeout.stop();
    cancelOAuthReply();
    m_buffer.clear();
    m_oauthAccessToken.clear();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }

    ++m_accountIndex;
    if (m_accountIndex >= m_accounts.size()) {
        finishCheck();
        return;
    }

    const KrellmailAccount account = m_accounts.at(m_accountIndex);
    m_socket = new QSslSocket(this);
    connect(m_socket, &QSslSocket::connected, this, &KrellmailMonitor::onConnected);
    connect(m_socket, &QSslSocket::encrypted, this, &KrellmailMonitor::onEncrypted);
    connect(m_socket, &QSslSocket::readyRead, this, &KrellmailMonitor::onReadyRead);
    connect(m_socket, &QSslSocket::errorOccurred, this, &KrellmailMonitor::onSocketError);

    if (account.protocol == QLatin1String("pop3"))
        m_state = State::PopGreeting;
    else
        m_state = State::ImapGreeting;

    m_timeout.start(kTimeoutMs);
    if (account.ssl)
        m_socket->connectToHostEncrypted(account.host, account.port);
    else
        m_socket->connectToHost(account.host, account.port);
}

void KrellmailMonitor::requestOAuthToken(const KrellmailAccount &account)
{
    if (account.oauthClientId.isEmpty() || account.oauthRefreshToken.isEmpty()) {
        failCurrent(QStringLiteral("OAuth account needs authorization"));
        return;
    }

    QNetworkRequest request(QUrl(QStringLiteral("https://oauth2.googleapis.com/token")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    request.setTransferTimeout(kTimeoutMs);

    QUrlQuery body;
    body.addQueryItem(QStringLiteral("client_id"), account.oauthClientId);
    body.addQueryItem(QStringLiteral("refresh_token"), account.oauthRefreshToken);
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));

    m_state = State::ImapOAuthToken;
    m_timeout.start(kTimeoutMs);
    const quint64 generation = m_checkGeneration;
    m_oauthGeneration = generation;
    QNetworkReply *reply = m_oauthManager.post(request, body.query(QUrl::FullyEncoded).toUtf8());
    m_oauthReply = reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        if (reply->bytesAvailable() > kMaxOAuthBytes)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
        if (generation != m_oauthGeneration || generation != m_checkGeneration) {
            reply->deleteLater();
            return;
        }
        onOAuthFinished(reply);
    });
}

void KrellmailMonitor::cancelOAuthReply()
{
    if (!m_oauthReply)
        return;
    QNetworkReply *reply = m_oauthReply.data();
    m_oauthReply = nullptr;
    if (!reply)
        return;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

void KrellmailMonitor::sendLine(const QByteArray &line)
{
    if (!m_socket) return;
    m_socket->write(line + "\r\n");
    m_socket->flush();
    m_timeout.start(kTimeoutMs);
}

QByteArray KrellmailMonitor::xoauth2InitialResponse(const KrellmailAccount &account,
                                                    const QString &accessToken) const
{
    QByteArray payload;
    payload += "user=";
    payload += account.username.toUtf8();
    payload += '\001';
    payload += "auth=Bearer ";
    payload += accessToken.toUtf8();
    payload += '\001';
    payload += '\001';
    return payload.toBase64();
}

void KrellmailMonitor::onConnected()
{
    m_timeout.start(kTimeoutMs);
}

void KrellmailMonitor::onEncrypted()
{
    m_timeout.start(kTimeoutMs);
}

void KrellmailMonitor::onReadyRead()
{
    if (!m_socket) return;
    m_buffer.append(m_socket->readAll());
    if (m_buffer.size() > kMaxMailBufferBytes) {
        failCurrent(QStringLiteral("mail response too large"));
        return;
    }
    int nl = -1;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        if (nl > kMaxMailLineBytes) {
            failCurrent(QStringLiteral("mail response line too large"));
            return;
        }
        QByteArray line = m_buffer.left(nl);
        m_buffer.remove(0, nl + 1);
        while (line.endsWith('\r') || line.endsWith('\n'))
            line.chop(1);
        processLine(line);
        if (!m_socket) return;
    }
}

void KrellmailMonitor::onSocketError(QAbstractSocket::SocketError)
{
    if (m_tearingDown) return;
    failCurrent(m_socket ? m_socket->errorString() : QStringLiteral("connection error"));
}

void KrellmailMonitor::onOAuthFinished(QNetworkReply *reply)
{
    if (!reply) return;
    if (m_oauthReply == reply)
        m_oauthReply = nullptr;
    reply->deleteLater();
    if (m_tearingDown || !m_fetching)
        return;
    if (m_accountIndex < 0 || m_accountIndex >= m_accounts.size())
        return;

    const QByteArray payload = reply->readAll();
    if (payload.size() > kMaxOAuthBytes) {
        failCurrent(QStringLiteral("OAuth response too large"));
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        QString message = reply->errorString();
        const QJsonDocument doc = QJsonDocument::fromJson(payload);
        if (doc.isObject()) {
            const QString error = doc.object().value(QStringLiteral("error")).toString();
            const QString description = doc.object().value(QStringLiteral("error_description")).toString();
            if (!error.isEmpty())
                message = description.isEmpty() ? error : error + QStringLiteral(": ") + description;
        }
        failCurrent(QStringLiteral("OAuth: %1").arg(message));
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    const QString accessToken = doc.object().value(QStringLiteral("access_token")).toString();
    if (accessToken.isEmpty()) {
        failCurrent(QStringLiteral("OAuth did not return an access token"));
        return;
    }

    m_oauthAccessToken = accessToken;
    const KrellmailAccount account = m_accounts.at(m_accountIndex);
    m_state = State::ImapOAuthAuth;
    sendLine(m_imapTag + " AUTHENTICATE XOAUTH2 "
             + xoauth2InitialResponse(account, m_oauthAccessToken));
}

void KrellmailMonitor::onTimeout()
{
    if (m_tearingDown || !m_fetching) return;
    failCurrent(QStringLiteral("mail check timed out"));
}

void KrellmailMonitor::processLine(const QByteArray &line)
{
    if (m_accountIndex < 0 || m_accountIndex >= m_accounts.size())
        return;
    const KrellmailAccount account = m_accounts.at(m_accountIndex);
    const QByteArray trimmed = line.trimmed();

    if (account.protocol == QLatin1String("pop3")) {
        if (m_state == State::PopGreeting) {
            if (!trimmed.startsWith("+OK")) { failCurrent(QString::fromUtf8(trimmed)); return; }
            m_state = State::PopUser;
            sendLine("USER " + account.username.toUtf8());
        } else if (m_state == State::PopUser) {
            if (!trimmed.startsWith("+OK")) { failCurrent(QString::fromUtf8(trimmed)); return; }
            m_state = State::PopPass;
            sendLine("PASS " + account.password.toUtf8());
        } else if (m_state == State::PopPass) {
            if (!trimmed.startsWith("+OK")) { failCurrent(QString::fromUtf8(trimmed)); return; }
            m_state = State::PopStat;
            sendLine("STAT");
        } else if (m_state == State::PopStat) {
            if (!trimmed.startsWith("+OK")) { failCurrent(QString::fromUtf8(trimmed)); return; }
            const QList<QByteArray> parts = trimmed.split(' ');
            if (parts.size() >= 2)
                m_totalCount += qMax(0, parts.at(1).toInt());
            sendLine("QUIT");
            startNextAccount();
        }
        return;
    }

    if (m_state == State::ImapGreeting) {
        if (!trimmed.startsWith("* OK") && !trimmed.startsWith("* PREAUTH")) {
            failCurrent(QString::fromUtf8(trimmed));
            return;
        }
        if (trimmed.startsWith("* PREAUTH")) {
            m_state = State::ImapSelect;
            m_imapTag = "a001";
            sendLine(m_imapTag + " EXAMINE INBOX");
            return;
        }
        m_state = State::ImapLogin;
        m_imapTag = "a001";
        if (account.auth == QLatin1String("oauth")) {
            requestOAuthToken(account);
            return;
        }
        const QByteArray user = imapQuoted(account.username);
        const QByteArray pass = imapQuoted(loginPassword(account));
        sendLine(m_imapTag + " LOGIN \"" + user + "\" \"" + pass + "\"");
    } else if (m_state == State::ImapLogin) {
        if (trimmed.startsWith(m_imapTag + " OK")) {
            m_state = State::ImapSelect;
            m_imapTag = "a002";
            sendLine(m_imapTag + " EXAMINE INBOX");
        } else if (trimmed.startsWith(m_imapTag + " NO") || trimmed.startsWith(m_imapTag + " BAD")) {
            failCurrent(QString::fromUtf8(trimmed));
        }
    } else if (m_state == State::ImapOAuthAuth) {
        if (trimmed.startsWith("+")) {
            sendLine(QByteArray());
        } else if (trimmed.startsWith(m_imapTag + " OK")) {
            m_state = State::ImapSelect;
            m_imapTag = "a002";
            sendLine(m_imapTag + " EXAMINE INBOX");
        } else if (trimmed.startsWith(m_imapTag + " NO") || trimmed.startsWith(m_imapTag + " BAD")) {
            failCurrent(QString::fromUtf8(trimmed));
        }
    } else if (m_state == State::ImapSelect) {
        if (trimmed.startsWith(m_imapTag + " OK")) {
            if (isGmailHost(account.host)) {
                m_state = State::ImapGmailSearch;
                m_imapTag = "a003";
                sendLine(m_imapTag + " SEARCH X-GM-RAW \"in:unread\"");
            } else {
                m_state = State::ImapSearch;
                m_imapTag = "a003";
                sendLine(m_imapTag + " SEARCH UNSEEN");
            }
        } else if (trimmed.startsWith(m_imapTag + " NO") || trimmed.startsWith(m_imapTag + " BAD")) {
            failCurrent(QString::fromUtf8(trimmed));
        }
    } else if (m_state == State::ImapGmailSearch) {
        const int searchCount = imapSearchCount(trimmed);
        if (searchCount >= 0) {
            m_totalCount += searchCount;
        } else if (trimmed.startsWith(m_imapTag + " OK")) {
            m_state = State::ImapLogout;
            m_imapTag = "a005";
            sendLine(m_imapTag + " LOGOUT");
        } else if (trimmed.startsWith(m_imapTag + " NO") || trimmed.startsWith(m_imapTag + " BAD")) {
            m_state = State::ImapSearch;
            m_imapTag = "a004";
            sendLine(m_imapTag + " SEARCH UNSEEN");
        }
    } else if (m_state == State::ImapSearch) {
        const int searchCount = imapSearchCount(trimmed);
        if (searchCount >= 0) {
            m_totalCount += searchCount;
        } else if (trimmed.startsWith(m_imapTag + " OK")) {
            m_state = State::ImapLogout;
            m_imapTag = "a004";
            sendLine(m_imapTag + " LOGOUT");
        } else if (trimmed.startsWith(m_imapTag + " NO") || trimmed.startsWith(m_imapTag + " BAD")) {
            failCurrent(QString::fromUtf8(trimmed));
        }
    } else if (m_state == State::ImapLogout) {
        if (trimmed.startsWith(m_imapTag))
            startNextAccount();
    }
}

void KrellmailMonitor::applyStatus()
{
    if (m_icon) m_icon->setMailCount(m_totalCount);

    const Theme::TextStyle primary =
        theme()->textStyle(QStringLiteral("text_primary"), QStringLiteral("text_primary"));
    const Theme::TextStyle secondary =
        theme()->textStyle(QStringLiteral("text_secondary"), QStringLiteral("text_secondary"));
    const QColor pcol = primary.color.isValid() ? primary.color
        : themeColor(theme(), QStringLiteral("text_primary"), QColor(235, 242, 246));
    const QColor scol = secondary.color.isValid() ? secondary.color
        : themeColor(theme(), QStringLiteral("text_secondary"), QColor(170, 190, 200));
    if (m_primary) {
        m_primary->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 10px; font-weight: 700; }")
                                 .arg(cssColor(pcol)));
    }
    if (m_detail) {
        m_detail->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 9px; }").arg(cssColor(scol)));
    }

    if (m_accounts.isEmpty()) {
        if (m_primary) m_primary->setText(QStringLiteral("Mail"));
        if (m_detail) m_detail->setText(QStringLiteral("configure account"));
        return;
    }

    if (m_primary)
        m_primary->setText(m_totalCount > 0 ? QStringLiteral("Mail has arrived")
                                            : QStringLiteral("No new mail"));
    if (m_detail) {
        QString detail = countText(m_totalCount);
        if (m_errorCount > 0 && !m_lastError.isEmpty())
            detail += QStringLiteral("  %1").arg(m_lastError);
        m_detail->setText(detail);
    }
}

QString KrellmailPlugin::pluginId() const
{
    return QStringLiteral("io.krellix.krellmail");
}

QString KrellmailPlugin::pluginName() const
{
    return QStringLiteral("Krellmail");
}

QString KrellmailPlugin::pluginVersion() const
{
    return QStringLiteral("0.1.0");
}

QList<MonitorBase *> KrellmailPlugin::createMonitors(Theme *theme, QObject *parent)
{
    if (!QSettings().value(QStringLiteral("plugins/krellmail/enabled"), true).toBool())
        return {};
    return {new KrellmailMonitor(theme, parent)};
}
