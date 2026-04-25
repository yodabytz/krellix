#include "Decal.h"

#include "theme/Theme.h"

#include <QFontMetrics>
#include <QHideEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>

namespace {
constexpr int kScrollIntervalMs = 33;   // ~30 fps
constexpr int kScrollPxPerTick  = 1;
constexpr int kScrollGapPx      = 24;
}

Decal::Decal(Theme *theme,
             const QString &fontKey,
             const QString &colorKey,
             QWidget *parent)
    : QWidget(parent)
    , m_theme(theme)
    , m_fontKey(fontKey)
    , m_colorKey(colorKey)
{
    Q_ASSERT(m_theme);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAttribute(Qt::WA_OpaquePaintEvent, false);

    m_scrollTimer = new QTimer(this);
    m_scrollTimer->setInterval(kScrollIntervalMs);
    m_scrollTimer->setTimerType(Qt::CoarseTimer);
    connect(m_scrollTimer, &QTimer::timeout, this, &Decal::onScrollTick);

    connect(m_theme, &Theme::themeChanged, this, &Decal::onThemeChanged);
}

Decal::~Decal() = default;

void Decal::setText(const QString &text)
{
    if (m_text == text) return;
    m_text = text;
    m_scrollOffset = 0;
    updateGeometry();
    updateScrollState();
    update();
}

void Decal::onThemeChanged()
{
    updateGeometry();
    updateScrollState();
    update();
}

void Decal::onScrollTick()
{
    const int tw = textPixelWidth();
    if (tw <= 0) return;
    m_scrollOffset = (m_scrollOffset + kScrollPxPerTick) % (tw + kScrollGapPx);
    update();
}

int Decal::textPixelWidth() const
{
    const QFont f = m_theme->font(m_fontKey);
    return QFontMetrics(f).horizontalAdvance(m_text);
}

void Decal::updateScrollState()
{
    const int tw = textPixelWidth();
    const bool shouldScroll = (tw > width()) && isVisible() && !m_text.isEmpty();
    if (shouldScroll == m_scrolling) return;
    m_scrolling = shouldScroll;
    if (m_scrolling) {
        if (!m_scrollTimer->isActive()) m_scrollTimer->start();
    } else {
        if (m_scrollTimer->isActive()) m_scrollTimer->stop();
        m_scrollOffset = 0;
        update();
    }
}

QSize Decal::sizeHint() const
{
    const QFont f = m_theme->font(m_fontKey);
    const QFontMetrics fm(f);
    // We don't grow the widget to fit long text; the panel width controls
    // it and we scroll on overflow. Hint at "M" so a non-empty decal
    // always claims a vertical strip.
    const int w = fm.horizontalAdvance(m_text.isEmpty()
                                       ? QStringLiteral("M") : QStringLiteral("M"));
    return QSize(w + 4, fm.height() + 2);
}

QSize Decal::minimumSizeHint() const
{
    const QFont f = m_theme->font(m_fontKey);
    const QFontMetrics fm(f);
    return QSize(0, fm.height() + 2);
}

void Decal::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateScrollState();
}

void Decal::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateScrollState();
}

void Decal::hideEvent(QHideEvent *event)
{
    if (m_scrollTimer && m_scrollTimer->isActive()) m_scrollTimer->stop();
    m_scrolling = false;
    QWidget::hideEvent(event);
}

void Decal::paintEvent(QPaintEvent *)
{
    if (m_text.isEmpty()) return;

    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setFont(m_theme->font(m_fontKey));
    p.setPen(m_theme->color(m_colorKey, QColor(Qt::white)));

    const QRect r = rect();
    if (!m_scrolling) {
        p.drawText(r, Qt::AlignVCenter | Qt::AlignLeft, m_text);
        return;
    }

    // Marquee: draw the text twice (with a gap) starting at -offset, so as
    // it scrolls left the second copy follows seamlessly.
    const int tw   = textPixelWidth();
    const int loop = tw + kScrollGapPx;
    const int x1   = -m_scrollOffset;
    const int x2   = x1 + loop;
    const QFontMetrics fm(p.font());
    const int y    = (r.height() + fm.ascent() - fm.descent()) / 2;
    p.drawText(x1, y, m_text);
    p.drawText(x2, y, m_text);
}
