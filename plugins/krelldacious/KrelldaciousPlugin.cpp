#include "KrelldaciousPlugin.h"

#include "theme/Theme.h"
#include "widgets/Panel.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QSizePolicy>
#include <QSlider>
#include <QVBoxLayout>
#include <QVariantMap>

namespace {

constexpr const char *kService = "org.mpris.MediaPlayer2.audacious";
constexpr const char *kPath = "/org/mpris/MediaPlayer2";
constexpr const char *kPlayerIface = "org.mpris.MediaPlayer2.Player";
constexpr const char *kPropsIface = "org.freedesktop.DBus.Properties";

class TransportButton : public QPushButton
{
public:
    enum Kind {
        Previous,
        PlayPause,
        Next,
    };

    explicit TransportButton(Kind kind, QWidget *parent = nullptr)
        : QPushButton(parent)
        , m_kind(kind)
    {
        setFixedSize(25, 19);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFlat(true);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        const bool hot = isEnabled() && underMouse();
        const bool pressed = isEnabled() && isDown();

        QLinearGradient bg(r.topLeft(), r.bottomLeft());
        if (!isEnabled()) {
            bg.setColorAt(0.0, QColor(28, 32, 36, 120));
            bg.setColorAt(1.0, QColor(8, 10, 12, 150));
        } else if (pressed) {
            bg.setColorAt(0.0, QColor(37, 88, 112, 230));
            bg.setColorAt(1.0, QColor(9, 24, 32, 235));
        } else if (hot) {
            bg.setColorAt(0.0, QColor(43, 92, 116, 225));
            bg.setColorAt(0.48, QColor(16, 36, 48, 230));
            bg.setColorAt(1.0, QColor(5, 12, 18, 240));
        } else {
            bg.setColorAt(0.0, QColor(45, 52, 58, 215));
            bg.setColorAt(0.45, QColor(15, 20, 25, 230));
            bg.setColorAt(1.0, QColor(4, 7, 10, 240));
        }

        p.setPen(QPen(isEnabled()
                          ? QColor(138, 185, 210, hot ? 210 : 140)
                          : QColor(110, 120, 128, 80), 1.0));
        p.setBrush(bg);
        p.drawRoundedRect(r, 4.0, 4.0);

        p.setPen(Qt::NoPen);
        p.setBrush(isEnabled()
                       ? QColor(210, 235, 246, 235)
                       : QColor(155, 165, 172, 90));

        const qreal y = height() / 2.0;
        const qreal shift = pressed ? 1.0 : 0.0;
        if (m_kind == PlayPause && property("playing").toBool()) {
            p.drawRoundedRect(QRectF(8 + shift, 5 + shift, 3.5, 9), 0.8, 0.8);
            p.drawRoundedRect(QRectF(14 + shift, 5 + shift, 3.5, 9), 0.8, 0.8);
            return;
        }

        auto drawTriangle = [&](qreal left, bool right) {
            QPainterPath path;
            if (right) {
                path.moveTo(left + shift, 5 + shift);
                path.lineTo(left + shift, 14 + shift);
                path.lineTo(left + 7.5 + shift, y + shift);
            } else {
                path.moveTo(left + 7.5 + shift, 5 + shift);
                path.lineTo(left + 7.5 + shift, 14 + shift);
                path.lineTo(left + shift, y + shift);
            }
            path.closeSubpath();
            p.drawPath(path);
        };

        if (m_kind == Previous) {
            p.drawRoundedRect(QRectF(6 + shift, 5 + shift, 2.2, 9), 0.6, 0.6);
            drawTriangle(9, false);
        } else if (m_kind == Next) {
            drawTriangle(8, true);
            p.drawRoundedRect(QRectF(17 + shift, 5 + shift, 2.2, 9), 0.6, 0.6);
        } else {
            drawTriangle(9, true);
        }
    }

private:
    Kind m_kind;
};

QVariant unwrapDbusVariant(QVariant v)
{
    if (v.canConvert<QDBusVariant>())
        return v.value<QDBusVariant>().variant();
    return v;
}

QString metadataString(const QVariantMap &metadata,
                       const QString &key)
{
    const QVariant raw = metadata.value(key);
    if (raw.typeId() == QMetaType::QStringList)
        return raw.toStringList().join(QStringLiteral(", "));
    return raw.toString();
}

QVariantMap metadataMapFromVariant(QVariant value)
{
    value = unwrapDbusVariant(value);
    if (value.canConvert<QVariantMap>())
        return value.toMap();

    QVariantMap out;
    const QDBusArgument arg = value.value<QDBusArgument>();
    if (arg.currentType() != QDBusArgument::MapType)
        return out;

    arg.beginMap();
    while (!arg.atEnd()) {
        QString key;
        QVariant val;
        arg.beginMapEntry();
        arg >> key >> val;
        arg.endMapEntry();
        out.insert(key, unwrapDbusVariant(val));
    }
    arg.endMap();
    return out;
}

QString trackTextFromMetadata(const QVariantMap &metadata)
{
    const QString artist = metadataString(metadata, QStringLiteral("xesam:artist"));
    const QString title = metadataString(metadata, QStringLiteral("xesam:title"));
    if (!artist.isEmpty() && !title.isEmpty())
        return artist + QStringLiteral(" - ") + title;
    if (!title.isEmpty()) return title;
    if (!artist.isEmpty()) return artist;
    const QString url = metadataString(metadata, QStringLiteral("xesam:url"));
    return url.isEmpty() ? QStringLiteral("(no track)") : url;
}

void styleTransportButton(QPushButton *button, const QString &tip)
{
    button->setToolTip(tip);
    button->setAccessibleName(tip);
    button->setFixedSize(25, 19);
    button->setFocusPolicy(Qt::NoFocus);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void styleVolume(QSlider *slider)
{
    slider->setFixedHeight(13);
    slider->setFocusPolicy(Qt::NoFocus);
    slider->setStyleSheet(QStringLiteral(
        "QSlider::groove:horizontal {"
        "  height: 3px;"
        "  background: rgba(5, 8, 10, 185);"
        "  border: 1px solid rgba(135, 170, 190, 90);"
        "  border-radius: 1px;"
        "}"
        "QSlider::sub-page:horizontal {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "                              stop:0 rgba(65, 175, 210, 210),"
        "                              stop:1 rgba(180, 245, 255, 230));"
        "  border-radius: 1px;"
        "}"
        "QSlider::handle:horizontal {"
        "  width: 7px;"
        "  height: 9px;"
        "  margin: -4px 0;"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "                              stop:0 #f7fbff, stop:1 #9bb3bf);"
        "  border: 1px solid rgba(20, 35, 44, 230);"
        "  border-radius: 2px;"
        "}"
    ));
}

} // namespace

KrelldaciousMonitor::KrelldaciousMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
}

KrelldaciousMonitor::~KrelldaciousMonitor() = default;

QString KrelldaciousMonitor::id() const
{
    return QStringLiteral("krelldacious");
}

QString KrelldaciousMonitor::displayName() const
{
    return QStringLiteral("Krelldacious");
}

int KrelldaciousMonitor::tickIntervalMs() const
{
    return 1000;
}

QWidget *KrelldaciousMonitor::createWidget(QWidget *parent)
{
    auto *panel = new Panel(theme(), parent);
    panel->setSurfaceKey(QStringLiteral("panel_bg_krelldacious"));

    auto *body = new QWidget(panel);
    auto *layout = new QVBoxLayout(body);
    const int pad = theme()->metric(QStringLiteral("panel_spacing"), 2);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(pad);

    m_track = new QLabel(QStringLiteral("(waiting for Audacious)"), body);
    m_track->setAlignment(Qt::AlignCenter);
    m_track->setWordWrap(true);
    m_track->setStyleSheet(QStringLiteral(
        "font-size: 10px;"
        "font-weight: 600;"
        "color: rgba(226, 240, 246, 235);"
    ));

    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(2);
    buttons->addStretch(1);
    m_prev = new TransportButton(TransportButton::Previous, body);
    m_playPause = new TransportButton(TransportButton::PlayPause, body);
    m_next = new TransportButton(TransportButton::Next, body);
    styleTransportButton(m_prev, QStringLiteral("Previous"));
    styleTransportButton(m_playPause, QStringLiteral("Play / pause"));
    styleTransportButton(m_next, QStringLiteral("Next"));
    buttons->addWidget(m_prev);
    buttons->addWidget(m_playPause);
    buttons->addWidget(m_next);
    buttons->addStretch(1);

    m_volume = new QSlider(Qt::Horizontal, body);
    m_volume->setRange(0, 100);
    styleVolume(m_volume);

    layout->addWidget(m_track);
    layout->addLayout(buttons);
    layout->addWidget(m_volume);
    panel->addWidget(body);

    connect(m_prev, &QPushButton::clicked, this, [this]() {
        sendPlayerCommand(QStringLiteral("Previous"));
    });
    connect(m_playPause, &QPushButton::clicked, this, [this]() {
        sendPlayerCommand(QStringLiteral("PlayPause"));
    });
    connect(m_next, &QPushButton::clicked, this, [this]() {
        sendPlayerCommand(QStringLiteral("Next"));
    });
    connect(m_volume, &QSlider::valueChanged, this, [this](int value) {
        if (!m_updatingVolume)
            setAudaciousVolume(value);
    });

    tick();
    return panel;
}

void KrelldaciousMonitor::tick()
{
    bool ok = false;
    const QString playback =
        playerProperty(QStringLiteral("PlaybackStatus"), &ok).toString();
    if (!ok) {
        if (m_track) m_track->setText(QStringLiteral("Start Audacious to control music"));
        if (m_prev) m_prev->setEnabled(false);
        if (m_playPause) m_playPause->setEnabled(false);
        if (m_next) m_next->setEnabled(false);
        if (m_volume) m_volume->setEnabled(false);
        return;
    }

    if (m_prev) m_prev->setEnabled(true);
    if (m_playPause) m_playPause->setEnabled(true);
    if (m_next) m_next->setEnabled(true);
    if (m_volume) m_volume->setEnabled(true);

    if (m_playPause) {
        m_playPause->setProperty("playing", playback == QStringLiteral("Playing"));
        m_playPause->update();
    }
    if (m_track) {
        const QVariantMap metadata =
            metadataMapFromVariant(playerProperty(QStringLiteral("Metadata")));
        m_track->setText(trackTextFromMetadata(metadata));
    }
    if (m_volume) {
        const QVariant vol = playerProperty(QStringLiteral("Volume"));
        const int percent = qBound(0, static_cast<int>(vol.toDouble() * 100.0 + 0.5), 100);
        m_updatingVolume = true;
        m_volume->setValue(percent);
        m_updatingVolume = false;
    }
}

void KrelldaciousMonitor::sendPlayerCommand(const QString &method)
{
    QDBusInterface player(QString::fromLatin1(kService),
                          QString::fromLatin1(kPath),
                          QString::fromLatin1(kPlayerIface),
                          QDBusConnection::sessionBus());
    if (player.isValid())
        player.call(method);
    tick();
}

void KrelldaciousMonitor::setAudaciousVolume(int percent)
{
    QDBusInterface props(QString::fromLatin1(kService),
                         QString::fromLatin1(kPath),
                         QString::fromLatin1(kPropsIface),
                         QDBusConnection::sessionBus());
    if (!props.isValid()) return;
    const double volume = qBound(0, percent, 100) / 100.0;
    props.call(QStringLiteral("Set"),
               QString::fromLatin1(kPlayerIface),
               QStringLiteral("Volume"),
               QVariant::fromValue(QDBusVariant(volume)));
}

QVariant KrelldaciousMonitor::playerProperty(const QString &name, bool *ok) const
{
    if (ok) *ok = false;
    QDBusInterface props(QString::fromLatin1(kService),
                         QString::fromLatin1(kPath),
                         QString::fromLatin1(kPropsIface),
                         QDBusConnection::sessionBus());
    if (!props.isValid()) return {};

    const QDBusReply<QVariant> reply =
        props.call(QStringLiteral("Get"), QString::fromLatin1(kPlayerIface), name);
    if (!reply.isValid()) return {};
    if (ok) *ok = true;
    return unwrapDbusVariant(reply.value());
}

QString KrelldaciousPlugin::pluginId() const
{
    return QStringLiteral("io.krellix.krelldacious");
}

QString KrelldaciousPlugin::pluginName() const
{
    return QStringLiteral("Krelldacious");
}

QString KrelldaciousPlugin::pluginVersion() const
{
    return QStringLiteral("0.1.0");
}

QList<MonitorBase *> KrelldaciousPlugin::createMonitors(Theme *theme, QObject *parent)
{
    if (!QSettings().value(QStringLiteral("plugins/krelldacious/enabled"), true).toBool())
        return {};
    return {new KrelldaciousMonitor(theme, parent)};
}
