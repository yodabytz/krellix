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

void Krell::onThemeChanged()
{
    updateGeometry();
    update();
}

QSize Krell::sizeHint() const
{
    // If a krell sprite is themed, its frame height drives the row height;
    // otherwise fall back to the krell_height metric.
    const QPixmap krellPix = m_theme->pixmap(QStringLiteral("krell"));
    if (!krellPix.isNull())
        return QSize(0, krellPix.height());
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

    // Track: prefer themed pixmap (tiled across width), else flat color.
    const QPixmap trackPix = m_theme->pixmap(QStringLiteral("krell_track"));
    if (!trackPix.isNull()) {
        p.drawTiledPixmap(r, trackPix);
    } else {
        p.fillRect(r, m_theme->color(QStringLiteral("krell_track")));
    }

    // Indicator: prefer sprite-sheet frame, else flat colored notch.
    const QPixmap krellPix = m_theme->pixmap(QStringLiteral("krell"));
    if (!krellPix.isNull() && krellPix.width() > 0) {
        const int frames = qMax(1, m_theme->imageInt(QStringLiteral("krell.frames"), 1));
        const int frameW = krellPix.width() / frames;
        const int frameH = krellPix.height();
        if (frameW > 0 && frameH > 0) {
            const int idx = qBound(0, static_cast<int>(m_value * frames),
                                   frames - 1);
            const QRect src(idx * frameW, 0, frameW, frameH);
            const int xMax = qMax(0, r.width() - frameW);
            const int x    = static_cast<int>(m_value * xMax);
            const int y    = r.y() + (r.height() - frameH) / 2;
            p.drawPixmap(QRect(x, y, frameW, frameH), krellPix, src);
            return;
        }
    }

    const QColor ind = m_theme->color(QStringLiteral("krell_indicator"));
    const int notchW = qMax(2, r.width() / 24);
    const int xMax   = r.width() - notchW;
    const int x      = static_cast<int>(m_value * xMax);
    p.fillRect(QRect(x, r.y(), notchW, r.height()), ind);
}
