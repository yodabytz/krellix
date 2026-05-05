#include "Decal.h"

#include "theme/Theme.h"

#include <QFontMetrics>
#include <QHideEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QSettings>
#include <QShowEvent>
#include <QTimer>

#include <cmath>

namespace {
constexpr int kScrollGapPx        = 24;
constexpr int kDefaultScrollPps   = 30;     // pixels/sec
constexpr int kDefaultScrollFps   = 12;
constexpr int kMinIntervalMs      = 33;
constexpr int kMaxIntervalMs      = 250;
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
    m_scrollTimer->setInterval(33);   // ~30 fps; updated by onScrollTick from settings
    m_scrollTimer->setTimerType(Qt::CoarseTimer);
    connect(m_scrollTimer, &QTimer::timeout, this, &Decal::onScrollTick);

    connect(m_theme, &Theme::themeChanged, this, &Decal::onThemeChanged);
}

Decal::~Decal() = default;

void Decal::setText(const QString &text)
{
    if (m_text == text) return;
    m_text = text;
    const int loop = textPixelWidth() + kScrollGapPx;
    if (loop > 0)
        m_scrollOffset = std::fmod(m_scrollOffset, static_cast<double>(loop));
    updateGeometry();
    updateScrollState();
    update();
}

void Decal::setAlignment(Qt::Alignment alignment)
{
    if (m_alignment == alignment) return;
    m_alignment = alignment;
    update();
}

void Decal::setAlwaysScroll(bool alwaysScroll)
{
    if (m_alwaysScroll == alwaysScroll) return;
    m_alwaysScroll = alwaysScroll;
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
    // Re-read scroll speed each tick so SettingsDialog changes apply live.
    // QSettings caches its file, so this is a hash lookup, not disk I/O.
    const int pps = qBound(1,
        QSettings().value(QStringLiteral("appearance/scroll_pps"),
                          kDefaultScrollPps).toInt(),
        1000);
    const int fps = qBound(4,
        QSettings().value(QStringLiteral("appearance/scroll_fps"),
                          kDefaultScrollFps).toInt(),
        30);
    const int interval = qBound(kMinIntervalMs, 1000 / fps, kMaxIntervalMs);
    if (m_scrollTimer->interval() != interval)
        m_scrollTimer->setInterval(interval);

    const int tw = textPixelWidth();
    if (tw <= 0) return;
    const qint64 elapsed = m_scrollClock.isValid() ? m_scrollClock.restart() : interval;
    if (!m_scrollClock.isValid())
        m_scrollClock.start();
    const double loop = static_cast<double>(tw + kScrollGapPx);
    m_scrollOffset = std::fmod(m_scrollOffset + (pps * elapsed / 1000.0), loop);
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
    const bool shouldScroll = ((tw > width()) || m_alwaysScroll)
        && isVisible()
        && !m_text.isEmpty();
    if (shouldScroll == m_scrolling) return;
    m_scrolling = shouldScroll;
    if (m_scrolling) {
        m_scrollClock.start();
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

    // v2 text style: color + optional drop shadow. Falls back to a bare
    // color (no shadow) when the theme uses the v1 flat colors block.
    const Theme::TextStyle ts = m_theme->textStyle(m_colorKey);
    const QColor fg = ts.color.isValid() ? ts.color : QColor(Qt::white);

    const QRect r = rect();
    const QFontMetrics fm(p.font());
    const int yBaseline = (r.height() + fm.ascent() - fm.descent()) / 2;

    auto drawAt = [&](int x, int y, bool shadowPass) {
        if (shadowPass) {
            p.setPen(ts.shadow.color);
        } else {
            p.setPen(fg);
        }
        if (m_scrolling) {
            p.drawText(x, y, m_text);
        } else {
            p.drawText(QRect(x, 0, r.width() - x, r.height()),
                       Qt::AlignVCenter | m_alignment, m_text);
        }
    };

    // Marquee mode draws the text twice with a gap so it scrolls
    // seamlessly. Static mode draws once, aligned. Either way: shadow
    // first underneath, then the foreground on top.
    const int tw   = textPixelWidth();
    const int loop = tw + kScrollGapPx;
    const int x1   = m_scrolling ? -static_cast<int>(std::round(m_scrollOffset)) : 0;
    const int x2   = x1 + loop;
    const int yPos = m_scrolling ? yBaseline : 0;

    if (ts.shadow.present) {
        drawAt(x1 + ts.shadow.offsetX, yPos + ts.shadow.offsetY, true);
        if (m_scrolling)
            drawAt(x2 + ts.shadow.offsetX, yPos + ts.shadow.offsetY, true);
    }
    drawAt(x1, yPos, false);
    if (m_scrolling) drawAt(x2, yPos, false);
}
