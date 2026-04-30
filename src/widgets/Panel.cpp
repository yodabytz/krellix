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

    // Background image rendering. Earlier iterations:
    //   v1: scale-to-height + tile-X — same image rendered at hugely
    //       different visual size between a short CPU panel and a tall
    //       Net panel.
    //   v2: stretch in both dimensions — kept the *width* consistent
    //       but tall panels (Net, with multiple ifaces stacked) made
    //       the image render visibly stretched, again huge near the
    //       bottom of the window.
    //   v3 (this): scale to panel WIDTH preserving aspect ratio. The
    //       same image now renders at the same *visual* size on every
    //       panel (because every panel is the same width). If the
    //       panel is taller than the scaled image, fill the gap below
    //       with the image's bottom row stretched down — gives a
    //       continuous-feeling background without a hard color seam,
    //       and lets theme authors design a "header band + tail
    //       gradient" image without having to think about each panel's
    //       height. Panels narrower than the image height just clip
    //       (no theme realistically hits this case).
    const QPixmap bgPix = m_theme->pixmap(QStringLiteral("panel_bg"));
    if (!bgPix.isNull() && r.width() > 0 && r.height() > 0) {
        const QPixmap scaled = bgPix.scaledToWidth(r.width(),
                                                   Qt::SmoothTransformation);
        const int imgH = scaled.height();
        if (imgH >= r.height()) {
            // Image at least as tall as the panel — paint clipped to rect.
            p.drawPixmap(0, 0, scaled);
        } else {
            p.drawPixmap(0, 0, scaled);
            // Extend bottom row down to fill remainder. Use a 1-px-tall
            // strip stretched to remaining height so the seam between
            // image and fill is invisible at the per-pixel level.
            if (imgH > 0) {
                const QPixmap tail = scaled.copy(0, imgH - 1,
                                                 scaled.width(), 1);
                p.drawPixmap(QRect(0, imgH, r.width(), r.height() - imgH),
                             tail);
            }
        }
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
