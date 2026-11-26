#include "KrellhealthPlugin.h"
#include "../HeartArtwork.h"
#include "remote/RemoteSource.h"
#include "widgets/Panel.h"
#include "widgets/Decal.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QSettings>

class HealthSummary final : public Decal
{
public:
    HealthSummary(Theme *theme, QWidget *parent)
        : Decal(theme, QStringLiteral("value"), QStringLiteral("text_secondary"), parent),
          m_theme(theme) {}

    QSize sizeHint() const override
    {
        auto hint = Decal::sizeHint();
        const auto size = KrellHealthArtwork::requestedSize(*m_theme, artwork());
        if (size.isEmpty()) return hint;
        const QFontMetrics fm(m_theme->font(QStringLiteral("value")));
        hint.rwidth() += size.width() - fm.horizontalAdvance(QStringLiteral("\u2665"));
        hint.rheight() = qMax(hint.height(), size.height() + 2);
        return hint;
    }

    QSize minimumSizeHint() const override { return QSize(0, sizeHint().height()); }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        const auto icon = artwork();
        if (icon.isNull()) {
            Decal::paintEvent(event);
            return;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setFont(m_theme->font(QStringLiteral("value")));
        const auto fm = painter.fontMetrics();
        const int glyph = fm.horizontalAdvance(QStringLiteral("\u2665"));
        const auto size = KrellHealthArtwork::requestedSize(*m_theme, icon);
        const int iconWidth = size.isEmpty() ? glyph : size.width();
        const int width = fm.horizontalAdvance(text()) - glyph + iconWidth;
        const int left = qMax(0, (this->width() - width) / 2);
        KrellHealthArtwork::paint(painter, icon, QRect(left, 0, iconWidth, height()));
        const auto style = m_theme->textStyle(QStringLiteral("accent_critical"));
        const auto ok = QStringLiteral("OK");
        const QRect label = rect().adjusted(left + width - fm.horizontalAdvance(ok), 0, 0, 0);
        if (style.shadow.present) {
            painter.setPen(style.shadow.color);
            painter.drawText(label.translated(style.shadow.offsetX, style.shadow.offsetY),
                             Qt::AlignLeft | Qt::AlignVCenter, ok);
        }
        painter.setPen(style.color.isValid() ? style.color : QColor(Qt::white));
        painter.drawText(label, Qt::AlignLeft | Qt::AlignVCenter, ok);
    }

private:
    QPixmap artwork() const
    {
        return text() == QStringLiteral("\u2665 OK") ? KrellHealthArtwork::heart(*m_theme) : QPixmap();
    }

    Theme *m_theme;
};

WindowsHealthMonitor::WindowsHealthMonitor(Theme *theme, QObject *parent) : MonitorBase(theme, parent)
{
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        if (m_probe) { disconnect(m_probe, nullptr, this, nullptr); m_probe->kill(); m_probe->deleteLater(); m_probe = nullptr; }
        if (m_socket) m_socket->abort();
        unavailable(QStringLiteral("health check timed out"));
    });
    if (auto *source = RemoteSource::instance())
        connect(source, &RemoteSource::connectionStateChanged, this, [this](bool connected) {
            if (m_stopped) return;
            m_deadline.stop();
            if (m_socket) m_socket->abort();
            m_attempt.invalidate();
            if (connected) tick(); else unavailable(QStringLiteral("remote disconnected"));
        });
}
WindowsHealthMonitor::~WindowsHealthMonitor() { shutdown(); }
void WindowsHealthMonitor::shutdown()
{
    m_stopped = true;
    m_deadline.stop();
    if (m_socket) { disconnect(m_socket, nullptr, this, nullptr); m_socket->abort(); }
    if (m_probe) {
        disconnect(m_probe, nullptr, this, nullptr);
        m_probe->kill();
        m_probe->waitForFinished(1000);
        delete m_probe;
        m_probe = nullptr;
    }
}
QWidget *WindowsHealthMonitor::createWidget(QWidget *parent)
{
    auto *panel = new Panel(theme(), parent);
    panel->setSurfaceKey(QStringLiteral("panel_bg_krellhealth"));
    panel->setTitle(QStringLiteral("Health"));
    m_panel = panel;
    m_summary = new HealthSummary(theme(), panel);
    panel->addWidget(m_summary);
    m_detail = panel->addDecal(QStringLiteral("label"), QStringLiteral("text_secondary"));
    m_summary->setAlignment(Qt::AlignHCenter);
    m_detail->setAlignment(Qt::AlignHCenter);
    unavailable(QStringLiteral("checking health..."));
    QTimer::singleShot(0, this, &WindowsHealthMonitor::tick);
    return panel;
}
void WindowsHealthMonitor::tick()
{
    if (m_stopped) return;
    if (auto *source = RemoteSource::instance()) {
        if (source->isConnected()) remote(source->remoteAddress());
        else unavailable(QStringLiteral("remote disconnected"));
        return;
    }
    local();
}
void WindowsHealthMonitor::local()
{
    if (m_probe || (m_attempt.isValid() && m_attempt.elapsed() < 60000)) return;
    m_attempt.start();
    const QString root = QCoreApplication::applicationDirPath();
    const QString helper = root + QStringLiteral("/health/krellhealth-probe.exe");
    if (!QFileInfo::exists(helper)) { unavailable(QStringLiteral("Windows health helper missing")); return; }
    unavailable(QStringLiteral("checking Windows health..."));
    m_buffer.clear();
    auto *process = new QProcess(this);
    m_probe = process;
    process->setWorkingDirectory(root);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), QDir::toNativeSeparators(root) + QLatin1Char(';') + environment.value(QStringLiteral("PATH")));
    process->setProcessEnvironment(environment);
    process->setProgram(helper);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process, &QProcess::readyReadStandardError, this, [process] { process->readAllStandardError(); });
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        m_buffer += process->readAllStandardOutput();
        if (m_buffer.size() > 128 * 1024) {
            disconnect(process, nullptr, this, nullptr);
            process->kill(); process->deleteLater(); m_probe = nullptr;
            m_deadline.stop(); m_buffer.clear();
            unavailable(QStringLiteral("health response too large"));
        }
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return; // Crashes finish through the single completion path below.
        m_deadline.stop(); m_probe = nullptr; process->deleteLater();
        unavailable(QStringLiteral("Windows health helper could not start"));
    });
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        m_deadline.stop(); m_probe = nullptr;
        m_buffer += process->readAllStandardOutput();
        process->deleteLater();
        const auto doc = QJsonDocument::fromJson(m_buffer);
        if (code || status != QProcess::NormalExit || m_buffer.size() > 128 * 1024 || !doc.isObject())
            unavailable(QStringLiteral("Windows health check failed"));
        else display(parseHealth(doc.object()));
        m_buffer.clear();
    });
    m_deadline.start(15000);
    process->start();
}
void WindowsHealthMonitor::remote(const QString &address)
{
    if (!m_socket) {
        m_socket = new QTcpSocket(this);
        m_socket->setReadBufferSize(1024 * 1024 + 1);
        connect(m_socket, &QTcpSocket::errorOccurred, this, [this] {
            m_deadline.stop(); unavailable(QStringLiteral("remote health connection unavailable"));
        });
        connect(m_socket, &QTcpSocket::disconnected, this, [this] {
            m_deadline.stop(); m_buffer.clear(); unavailable(QStringLiteral("remote health disconnected"));
        });
        connect(m_socket, &QTcpSocket::readyRead, this, [this] {
            m_buffer += m_socket->readAll();
            if (m_buffer.size() > 1024 * 1024) {
                m_socket->abort(); m_buffer.clear(); unavailable(QStringLiteral("remote health message too large")); return;
            }
            qsizetype newline;
            while ((newline = m_buffer.indexOf('\n')) >= 0) {
                const auto doc = QJsonDocument::fromJson(m_buffer.left(newline));
                m_buffer.remove(0, newline + 1);
                if (!doc.isObject()) { m_socket->abort(); unavailable(QStringLiteral("invalid remote health message")); return; }
                const auto object = doc.object();
                if (object.value("type").toString() != QStringLiteral("sample")) continue;
                m_deadline.start(15000);
                if (!object.value("health").isObject()) unavailable(QStringLiteral("server provides no health data"));
                else display(parseHealth(object.value("health").toObject()));
            }
        });
    }
    if (m_socket->state() != QAbstractSocket::UnconnectedState) return;
    if (m_attempt.isValid() && m_attempt.elapsed() < 5000) return;
    const auto colon = address.lastIndexOf(QLatin1Char(':'));
    bool valid = false;
    const uint port = address.mid(colon + 1).toUInt(&valid);
    if (colon < 1 || !valid || port < 1 || port > 65535) { unavailable(QStringLiteral("invalid remote address")); return; }
    QString host = address.left(colon);
    if (host.startsWith(QLatin1Char('[')) && host.endsWith(QLatin1Char(']'))) host = host.mid(1, host.size() - 2);
    m_attempt.start(); m_buffer.clear(); m_deadline.start(15000);
    m_socket->connectToHost(host, quint16(port));
}
void WindowsHealthMonitor::unavailable(const QString &reason)
{
    if (m_summary) { m_summary->setText(QStringLiteral("? CHECK")); m_summary->setColorKey(QStringLiteral("text_secondary")); }
    if (m_detail) m_detail->setText(reason);
    if (m_panel) m_panel->setToolTip(QStringLiteral("KrellHealth: %1").arg(reason));
}
void WindowsHealthMonitor::display(const HealthData &data)
{
    if (!data.error.isEmpty()) { unavailable(data.error); return; }
    const QString summary = data.critical ? QStringLiteral("%1 CRIT").arg(data.critical)
        : data.warnings ? QStringLiteral("%1 WARN").arg(data.warnings)
        : !data.findings.isEmpty() ? QStringLiteral("%1 INFO").arg(data.findings.size()) : QStringLiteral("\u2665 OK");
    if (m_summary) {
        m_summary->setText(summary);
        m_summary->setColorKey(data.critical || data.findings.isEmpty() ? QStringLiteral("accent_critical") : data.warnings ? QStringLiteral("accent_warning") : QStringLiteral("text_primary"));
    }
    if (m_detail) m_detail->setText(data.findings.isEmpty() ? QStringLiteral("tracked checks clear") : data.findings.first().toObject().value("title").toString());
    QStringList text{QStringLiteral("KrellHealth checks:")};
    for (const auto &item : data.findings) {
        const auto row = item.toObject();
        text << row.value("severity").toString().toUpper() + QStringLiteral(": ") + row.value("title").toString()
            + QStringLiteral(" - ") + row.value("detail").toString();
    }
    if (data.findings.isEmpty()) text << QStringLiteral("No active findings from reported probes.");
    if (RemoteSource::instance()) text << QStringLiteral("Source: selected remote daemon only.");
    else {
        text << QStringLiteral("Source: local Windows, read-only checks every 60 seconds.") << data.coverage;
        text << QStringLiteral("SMART, backups, certificates and event-log history are not checked by this Windows probe.");
    }
    if (m_panel) m_panel->setToolTip(QStringLiteral("<qt>%1</qt>").arg(text.join(QLatin1Char('\n')).toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"))));
}
QList<MonitorBase *> KrellhealthPlugin::createMonitors(Theme *theme, QObject *parent)
{
    if (!QSettings().value(QStringLiteral("plugins/krellhealth/enabled"), false).toBool()) return {};
    return {new WindowsHealthMonitor(theme, parent)};
}
