#include "Panel.h"

#include "theme/Theme.h"
#include "widgets/Chart.h"
#include "widgets/Decal.h"
#include "widgets/Krell.h"

#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QVBoxLayout>
#include <QWindow>

namespace {

// Render `src` into `target` with 9-slice (border-image) scaling:
// corner squares of `slice` pixels stay 1:1 native size, the four
// edge bands stretch only along their long axis, and the center
// rectangle stretches both ways. This is what we want for theme
// background images that have a *frame* baked in — the user's
// cthulhain panel.png, for example, has a 3-4 px highlighted border
// around a flat dark interior. Plain stretch makes the border
// thickness scale with panel height, so taller panels (memory,
// network) end up with comically large bright top edges. 9-slice
// fixes that: the border is always exactly `slice` pixels tall on
// every panel, and the flat interior absorbs the height variance.
//
// Falls back to plain stretch when the slice is non-positive or when
// either source or target is smaller than 2*slice in either dimension
// (no room for both corners + a stretchable middle).
void drawNineSlice(QPainter &p, const QPixmap &src,
                   const QRect &target, int slice)
{
    const int sw = src.width(), sh = src.height();
    const int tw = target.width(), th = target.height();
    if (slice <= 0
        || sw < 2 * slice || sh < 2 * slice
        || tw < 2 * slice || th < 2 * slice) {
        p.drawPixmap(target, src);
        return;
    }
    const int s     = slice;
    const int sMidW = sw - 2 * s;
    const int sMidH = sh - 2 * s;
    const int tMidW = tw - 2 * s;
    const int tMidH = th - 2 * s;
    const int tx    = target.x();
    const int ty    = target.y();

    // Four corners — drawn from (0,0), (sw-s,0), (0,sh-s), (sw-s,sh-s)
    // at native size into the matching target corners.
    p.drawPixmap(QRect(tx,             ty,             s, s),
                 src, QRect(0,         0,             s, s));
    p.drawPixmap(QRect(tx + tw - s,    ty,             s, s),
                 src, QRect(sw - s,    0,             s, s));
    p.drawPixmap(QRect(tx,             ty + th - s,    s, s),
                 src, QRect(0,         sh - s,        s, s));
    p.drawPixmap(QRect(tx + tw - s,    ty + th - s,    s, s),
                 src, QRect(sw - s,    sh - s,        s, s));

    // Four edges — top/bottom stretch horizontally only, left/right
    // stretch vertically only. Corner thickness preserved.
    p.drawPixmap(QRect(tx + s,         ty,             tMidW, s),
                 src, QRect(s,         0,             sMidW, s));
    p.drawPixmap(QRect(tx + s,         ty + th - s,    tMidW, s),
                 src, QRect(s,         sh - s,        sMidW, s));
    p.drawPixmap(QRect(tx,             ty + s,         s, tMidH),
                 src, QRect(0,         s,             s, sMidH));
    p.drawPixmap(QRect(tx + tw - s,    ty + s,         s, tMidH),
                 src, QRect(sw - s,    s,             s, sMidH));

    // Center stretches both axes — picks up flat interior color so
    // visible distortion is minimal even at strong stretch ratios.
    p.drawPixmap(QRect(tx + s,         ty + s,         tMidW, tMidH),
                 src, QRect(s,         s,             sMidW, sMidH));
}

} // namespace

Panel::Panel(Theme *theme, QWidget *parent)
    : QWidget(parent)
    , m_theme(theme)
{
    Q_ASSERT(m_theme);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(
        m_theme->metric(QStringLiteral("panel_padding"), 4),
        m_theme->metric(QStringLiteral("panel_padding"), 4),
        m_theme->metric(QStringLiteral("panel_padding"), 4),
        m_theme->metric(QStringLiteral("panel_padding"), 4));
    m_layout->setSpacing(m_theme->metric(QStringLiteral("panel_spacing"), 2));

    connect(m_theme, &Theme::themeChanged, this, &Panel::onThemeChanged);
}

Panel::~Panel() = default;

void Panel::setSurfaceKey(const QString &key)
{
    if (m_surfaceKey == key || key.isEmpty()) return;
    m_surfaceKey = key;
    update();
}

void Panel::setTitle(const QString &title)
{
    if (!m_titleDecal) {
        // Non-bold "label" font with center alignment — bold/left-aligned
        // titles overpowered the data below them.
        m_titleDecal = new Decal(m_theme,
                                 QStringLiteral("label"),
                                 QStringLiteral("text_secondary"),
                                 this);
        m_titleDecal->setAlignment(Qt::AlignHCenter);
        m_layout->insertWidget(0, m_titleDecal);
    }
    m_titleDecal->setText(title);
}

Decal *Panel::addDecal(const QString &fontKey, const QString &colorKey)
{
    auto *d = new Decal(m_theme, fontKey, colorKey, this);
    m_layout->addWidget(d);
    return d;
}

Krell *Panel::addKrell()
{
    auto *k = new Krell(m_theme, this);
    m_layout->addWidget(k);
    return k;
}

Chart *Panel::addChart(const QString &colorKey)
{
    auto *c = new Chart(m_theme, colorKey, this);
    m_layout->addWidget(c);
    return c;
}

void Panel::onThemeChanged()
{
    const int pad = m_theme->metric(QStringLiteral("panel_padding"), 4);
    m_layout->setContentsMargins(pad, pad, pad, pad);
    m_layout->setSpacing(m_theme->metric(QStringLiteral("panel_spacing"), 2));
    update();
}

void Panel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // Initiate window move from any panel surface — the hostname,
        // monitor titles, and inert decals all become drag handles. Charts
        // and krells inside the panel inherit QWidget's "ignore" default
        // for mouse events, so clicks on them propagate up to here.
        if (QWidget *top = window()) {
            if (QWindow *wh = top->windowHandle()) {
                if (wh->startSystemMove()) {
                    event->accept();
                    return;
                }
            }
            // Manual fallback when the compositor doesn't honor
            // startSystemMove (some X11 setups).
            m_dragging = true;
            m_dragOffset = event->globalPosition().toPoint()
                         - top->frameGeometry().topLeft();
        }
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void Panel::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        if (QWidget *top = window()) {
            top->move(event->globalPosition().toPoint() - m_dragOffset);
        }
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void Panel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void Panel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QRect r = rect();

    // Background via the v2 Surface API — image, 9-slice border, opacity,
    // and tint all in one lookup. Lookup chain: m_surfaceKey (e.g.
    // "panel_bg_cpu" for the CPU monitor) → "panel_bg" → empty. When
    // there's no image, fall back to the panel_bg color.
    const Theme::Surface surf =
        m_theme->surface(m_surfaceKey, QStringLiteral("panel_bg"));
    if (!surf.image.isNull() && r.width() > 0 && r.height() > 0) {
        const qreal prevOpacity = p.opacity();
        if (surf.opacity < 1.0) p.setOpacity(prevOpacity * surf.opacity);
        drawNineSlice(p, surf.image, r, surf.slice);
        if (surf.tint.isValid()) {
            // Tint = a translucent color overlay using the tint's alpha
            // as the strength. Source-over for natural blending; pure
            // opaque tints would obliterate the image, so the theme is
            // expected to set the alpha (e.g. #1a203080).
            p.fillRect(r, surf.tint);
        }
        if (surf.opacity < 1.0) p.setOpacity(prevOpacity);
    } else {
        p.fillRect(r, m_theme->color(QStringLiteral("panel_bg")));
    }

    const int bw = m_theme->metric(QStringLiteral("panel_border"), 1);
    if (bw <= 0) return;

    QPen pen(m_theme->color(QStringLiteral("panel_border")));
    pen.setWidth(bw);
    p.setPen(pen);
    // Inset so the line is fully inside the widget.
    const QRectF inset = QRectF(r).adjusted(bw / 2.0, bw / 2.0,
                                            -bw / 2.0, -bw / 2.0);
    p.drawRect(inset);
}
