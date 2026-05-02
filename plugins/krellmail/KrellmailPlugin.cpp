#include "KrellmailPlugin.h"

#include "theme/Theme.h"
#include "widgets/Panel.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSettings>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QtMath>

namespace {

constexpr int kMaxAccounts = 3;
constexpr int kTimeoutMs = 12000;

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

int imapStatusValue(const QByteArray &line, const QByteArray &field)
{
    const QString pattern = QStringLiteral("\\b%1\\s+(\\d+)\\b")
        .arg(QString::fromLatin1(field));
    const QRegularExpression re(pattern,
                                QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = re.match(QString::fromUtf8(line));
    if (!match.hasMatch()) return -1;
    return match.captured(1).toInt();
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
        startCheck(true);
        return true;
    }
    return MonitorBase::eventFilter(watched, event);
}

void KrellmailMonitor::shutdown()
{
    m_tearingDown = true;
    m_timeout.stop();
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
    for (int i = 1; i <= kMaxAccounts; ++i) {
        KrellmailAccount account;
        account.protocol = s.value(accountKey(i, QStringLiteral("protocol")),
                                   QStringLiteral("imap")).toString().trimmed().toLower();
        if (account.protocol != QLatin1String("pop3"))
            account.protocol = QStringLiteral("imap");
        account.host = s.value(accountKey(i, QStringLiteral("host"))).toString().trimmed();
        account.ssl = s.value(accountKey(i, QStringLiteral("ssl")), true).toBool();
        account.port = s.value(accountKey(i, QStringLiteral("port")),
                               defaultPort(account.protocol, account.ssl)).toInt();
        account.username = s.value(accountKey(i, QStringLiteral("username"))).toString();
        account.password = s.value(accountKey(i, QStringLiteral("password"))).toString();
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
    m_accountIndex = -1;
    m_totalCount = 0;
    m_errorCount = 0;
    m_lastError.clear();
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
    m_buffer.clear();
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

void KrellmailMonitor::sendLine(const QByteArray &line)
{
    if (!m_socket) return;
    m_socket->write(line + "\r\n");
    m_socket->flush();
    m_timeout.start(kTimeoutMs);
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
    int nl = -1;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
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
            m_state = State::ImapStatus;
            m_imapTag = "a001";
            sendLine(m_imapTag + " STATUS INBOX (UNSEEN)");
            return;
        }
        m_state = State::ImapLogin;
        m_imapTag = "a001";
        const QByteArray user = account.username.toUtf8().replace("\\", "\\\\").replace("\"", "\\\"");
        const QByteArray pass = account.password.toUtf8().replace("\\", "\\\\").replace("\"", "\\\"");
        sendLine(m_imapTag + " LOGIN \"" + user + "\" \"" + pass + "\"");
    } else if (m_state == State::ImapLogin) {
        if (trimmed.startsWith(m_imapTag + " OK")) {
            m_state = State::ImapStatus;
            m_imapTag = "a002";
            sendLine(m_imapTag + " STATUS INBOX (UNSEEN)");
        } else if (trimmed.startsWith(m_imapTag + " NO") || trimmed.startsWith(m_imapTag + " BAD")) {
            failCurrent(QString::fromUtf8(trimmed));
        }
    } else if (m_state == State::ImapStatus) {
        if (trimmed.startsWith("* STATUS")) {
            const int messages = imapStatusValue(trimmed, "UNSEEN");
            if (messages >= 0)
                m_totalCount += messages;
        } else if (trimmed.startsWith(m_imapTag + " OK")) {
            m_state = State::ImapLogout;
            m_imapTag = "a003";
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
