#include "Krell.h"

#include "theme/Theme.h"

#include <QPaintEvent>
#include <QPainter>
#include <QtGlobal>

Krell::Krell(Theme *theme, QWidget *parent)
    : QWidget(parent)
    , m_theme(theme)
{
    Q_ASSERT(m_theme);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_theme, &Theme::themeChanged, this, &Krell::onThemeChanged);
}

Krell::~Krell() = default;

void Krell::setValue(double normalized)
{
    const double clamped = qBound(0.0, normalized, 1.0);
    if (qFuzzyCompare(1.0 + clamped, 1.0 + m_value)) return;
    m_value = clamped;
    update();
}

void Krell::setAlertLevel(AlertLevel level)
{
    if (m_alertLevel == level) return;
    m_alertLevel = level;
    update();
}

void Krell::onThemeChanged()
{
    updateGeometry();
    update();
}

QSize Krell::sizeHint() const
{
    // If a krell sprite is themed, its (single-)frame height drives the
    // row height; otherwise fall back to the krell_height metric.
    const QPixmap krellPix = m_theme->pixmap(QStringLiteral("krell"));
    if (!krellPix.isNull()) {
        const int  frames   = qMax(1, m_theme->imageInt(QStringLiteral("krell.frames"), 1));
        const bool vertical = m_theme->imageInt(QStringLiteral("krell.vertical"), 0) != 0;
        const int  frameH   = vertical ? krellPix.height() / frames
                                       : krellPix.height();
        return QSize(0, frameH);
    }
    return QSize(0, m_theme->metric(QStringLiteral("krell_height"), 8));
}

QSize Krell::minimumSizeHint() const
{
    return sizeHint();
}

void Krell::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRect r = rect();
    if (r.width() <= 0 || r.height() <= 0) return;

    // Track: prefer themed pixmap (tiled across width), else solid or
    // gradient brush via Theme::brush() — lets a theme paint
    // krell_track as a vertical gradient (e.g. metal sheen) without
    // needing a sprite.
    const QPixmap trackPix = m_theme->pixmap(QStringLiteral("krell_track"));
    if (!trackPix.isNull()) {
        const QString mode = m_theme->imageMode(QStringLiteral("krell_track"),
                                                QStringLiteral("tile"));
        if (mode == QStringLiteral("stretch"))
            p.drawPixmap(r, trackPix);
        else
            p.drawTiledPixmap(r, trackPix);
    } else {
        p.fillRect(r, m_theme->brush(QStringLiteral("krell_track"), r));
    }

    // Indicator: prefer sprite-sheet frame, else flat colored notch.
    // Sprite layout defaults to horizontal frames (frames laid left→right).
    // If "krell.vertical" is non-zero the frames are stacked top→bottom
    // (gkrellm legacy convention).
    const QPixmap krellPix = m_theme->pixmap(QStringLiteral("krell"));
    if (!krellPix.isNull() && krellPix.width() > 0 && krellPix.height() > 0) {
        const int  frames   = qMax(1, m_theme->imageInt(QStringLiteral("krell.frames"), 1));
        const bool vertical = m_theme->imageInt(QStringLiteral("krell.vertical"), 0) != 0;
        const int  frameW   = vertical ? krellPix.width()
                                       : krellPix.width() / frames;
        const int  frameH   = vertical ? krellPix.height() / frames
                                       : krellPix.height();
        if (frameW > 0 && frameH > 0) {
            const int idx = qBound(0, static_cast<int>(m_value * frames),
                                   frames - 1);
            const QRect src = vertical
                ? QRect(0, idx * frameH, frameW, frameH)
                : QRect(idx * frameW, 0, frameW, frameH);
            const int xMax = qMax(0, r.width() - frameW);
            const int x    = static_cast<int>(m_value * xMax);
            const int y    = r.y() + (r.height() - frameH) / 2;
            p.drawPixmap(QRect(x, y, frameW, frameH), krellPix, src);
            return;
        }
    }

    // Gradient-aware indicator. We look up via Theme::brush() against
    // the FULL track rect (not the small notch rect) — that way a
    // krell_indicator gradient runs the whole width of the krell, and
    // as the notch slides right it reveals different stops. Far more
    // useful than constraining the gradient to the notch's own width.
    QString indKey = QStringLiteral("krell_indicator");
    switch (m_alertLevel) {
    case AlertLevel::Warning:
        indKey = QStringLiteral("accent_warning");
        break;
    case AlertLevel::Critical:
        indKey = QStringLiteral("accent_critical");
        break;
    case AlertLevel::None:
    default: break;
    }
    const QBrush indBrush = m_theme->brush(
        indKey, r,
        m_theme->color(QStringLiteral("krell_indicator")));

    const int notchW = qMax(2, r.width() / 24);
    const int xMax   = r.width() - notchW;
    const int x      = static_cast<int>(m_value * xMax);
    p.fillRect(QRect(x, r.y(), notchW, r.height()), indBrush);
}
