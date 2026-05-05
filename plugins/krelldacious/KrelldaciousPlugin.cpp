#include "KrelldaciousPlugin.h"

#include "theme/Theme.h"
#include "widgets/Decal.h"
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
#include <QProcess>
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

    explicit TransportButton(Kind kind, Theme *theme, QWidget *parent = nullptr)
        : QPushButton(parent)
        , m_kind(kind)
        , m_theme(theme)
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
        const QColor panel = themeColor(QStringLiteral("panel_bg"),
                                        QColor(15, 20, 25));
        const QColor border = themeColor(QStringLiteral("panel_border"),
                                         QColor(138, 185, 210));
        const QColor accent = themeColor(QStringLiteral("text_accent"),
                                         QColor(110, 205, 230));
        const QColor primary = themeColor(QStringLiteral("text_primary"),
                                          QColor(210, 235, 246));
        const QColor disabled = themeColor(QStringLiteral("text_secondary"),
                                           QColor(155, 165, 172));

        QLinearGradient bg(r.topLeft(), r.bottomLeft());
        if (!isEnabled()) {
            bg.setColorAt(0.0, withAlpha(panel.lighter(125), 120));
            bg.setColorAt(1.0, withAlpha(panel.darker(135), 150));
        } else if (pressed) {
            bg.setColorAt(0.0, withAlpha(accent.darker(120), 230));
            bg.setColorAt(1.0, withAlpha(panel.darker(155), 235));
        } else if (hot) {
            bg.setColorAt(0.0, withAlpha(accent, 225));
            bg.setColorAt(0.48, withAlpha(panel, 230));
            bg.setColorAt(1.0, withAlpha(panel.darker(150), 240));
        } else {
            bg.setColorAt(0.0, withAlpha(panel.lighter(135), 215));
            bg.setColorAt(0.45, withAlpha(panel, 230));
            bg.setColorAt(1.0, withAlpha(panel.darker(150), 240));
        }

        p.setPen(QPen(isEnabled() ? withAlpha(border, hot ? 210 : 150)
                                  : withAlpha(disabled, 90), 1.0));
        p.setBrush(bg);
        p.drawRoundedRect(r, 4.0, 4.0);

        p.setPen(Qt::NoPen);
        p.setBrush(isEnabled() ? withAlpha(primary, 235)
                               : withAlpha(disabled, 100));

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
    QColor themeColor(const QString &key, const QColor &fallback) const
    {
        if (!m_theme) return fallback;
        const QColor color = m_theme->color(key, fallback);
        return color.isValid() ? color : fallback;
    }

    QColor withAlpha(QColor color, int alpha) const
    {
        color.setAlpha(alpha);
        return color;
    }

    Kind m_kind;
    Theme *m_theme = nullptr;
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

QString cssColor(const QColor &color)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

QColor themeColor(Theme *theme, const QString &key, const QColor &fallback)
{
    if (!theme) return fallback;
    const QColor color = theme->color(key, fallback);
    return color.isValid() ? color : fallback;
}

QColor textColor(Theme *theme,
                 const QString &key,
                 const QString &fallbackKey = QStringLiteral("text_primary"))
{
    if (!theme) return QColor(226, 240, 246, 235);
    const QColor color = theme->textStyle(key, fallbackKey).color;
    return color.isValid() ? color : QColor(226, 240, 246, 235);
}

QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

void styleVolume(QSlider *slider, Theme *theme)
{
    slider->setFixedHeight(13);
    slider->setFocusPolicy(Qt::NoFocus);
    const QColor groove = themeColor(theme, QStringLiteral("chart_bg"), QColor(5, 8, 10));
    const QColor border = themeColor(theme, QStringLiteral("panel_border"), QColor(135, 170, 190));
    const QColor accent = textColor(theme, QStringLiteral("text_accent"));
    const QColor primary = textColor(theme, QStringLiteral("text_primary"));
    const QColor handle = themeColor(theme, QStringLiteral("panel_bg"), primary);
    const QString style = QStringLiteral(
        "QSlider::groove:horizontal {"
        "  height: 3px;"
        "  background: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 1px;"
        "}"
        "QSlider::sub-page:horizontal {"
        "  background: %3;"
        "  border-radius: 1px;"
        "}"
        "QSlider::handle:horizontal {"
        "  width: 7px;"
        "  height: 9px;"
        "  margin: -4px 0;"
        "  background: %4;"
        "  border: 1px solid %5;"
        "  border-radius: 2px;"
        "}"
    ).arg(cssColor(withAlpha(groove, 185)),
          cssColor(withAlpha(border, 120)),
          cssColor(withAlpha(accent, 220)),
          cssColor(withAlpha(handle.lighter(125), 235)),
          cssColor(withAlpha(border.darker(140), 230)));
    slider->setStyleSheet(style);
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

    m_track = new Decal(theme(), QStringLiteral("label"), QStringLiteral("text_primary"), body);
    m_track->setAlignment(Qt::AlignHCenter);
    m_track->setAlwaysScroll(true);

    m_openAudacious = new QLabel(body);
    m_openAudacious->setAlignment(Qt::AlignCenter);
    m_openAudacious->setTextFormat(Qt::RichText);
    m_openAudacious->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    m_openAudacious->setOpenExternalLinks(false);
    m_openAudacious->setText(QStringLiteral("<a href=\"open\">Open Audacious</a>"));
    connect(m_openAudacious, &QLabel::linkActivated, this, [](const QString &) {
        QProcess::startDetached(QStringLiteral("audacious"));
    });

    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(2);
    buttons->addStretch(1);
    m_prev = new TransportButton(TransportButton::Previous, theme(), body);
    m_playPause = new TransportButton(TransportButton::PlayPause, theme(), body);
    m_next = new TransportButton(TransportButton::Next, theme(), body);
    styleTransportButton(m_prev, QStringLiteral("Previous"));
    styleTransportButton(m_playPause, QStringLiteral("Play / pause"));
    styleTransportButton(m_next, QStringLiteral("Next"));
    buttons->addWidget(m_prev);
    buttons->addWidget(m_playPause);
    buttons->addWidget(m_next);
    buttons->addStretch(1);

    m_volume = new QSlider(Qt::Horizontal, body);
    m_volume->setRange(0, 100);
    styleVolume(m_volume, theme());

    layout->addWidget(m_track);
    layout->addWidget(m_openAudacious);
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
    connect(theme(), &Theme::themeChanged, this, [this]() {
        applyThemeColors();
        if (m_prev) m_prev->update();
        if (m_playPause) m_playPause->update();
        if (m_next) m_next->update();
    });

    applyThemeColors();
    tick();
    return panel;
}

void KrelldaciousMonitor::applyThemeColors()
{
    const QColor primary = textColor(theme(), QStringLiteral("text_primary"));
    if (m_track) {
        m_track->update();
    }
    if (m_openAudacious) {
        m_openAudacious->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 9px; font-weight: 700; color: %1; background: transparent; }")
            .arg(cssColor(primary)));
    }
    if (m_volume)
        styleVolume(m_volume, theme());
}

void KrelldaciousMonitor::tick()
{
    bool ok = false;
    const QString playback =
        playerProperty(QStringLiteral("PlaybackStatus"), &ok).toString();
    if (!ok) {
        if (m_track) m_track->setText(QStringLiteral("Audacious is not running"));
        if (m_openAudacious) m_openAudacious->show();
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
    if (m_openAudacious) m_openAudacious->hide();

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
