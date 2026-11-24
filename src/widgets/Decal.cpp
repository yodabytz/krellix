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
#include <QVariant>

#include <cmath>

namespace {
constexpr int kScrollGapPx        = 24;
constexpr int kIconGapPx          = 4;
constexpr int kDefaultScrollPps   = 30;     // pixels/sec
constexpr int kDefaultScrollFps   = 12;
constexpr int kMinIntervalMs      = 33;
constexpr int kMaxIntervalMs      = 250;
#ifdef Q_OS_WIN
int scrollPps = kDefaultScrollPps;
int scrollFps = kDefaultScrollFps;
#endif
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

void Decal::setColorKey(const QString &colorKey)
{
    if (m_colorKey == colorKey) return;
    m_colorKey = colorKey;
    update();
}

void Decal::setThemeIcon(const QString &key)
{
    if (property("krellThemeIcon").toString() == key) return;
    // Keep the layout of the exported widget unchanged for bundled plugins.
    setProperty("krellThemeIcon", key);
    onThemeChanged();
}

QPixmap Decal::themeIcon() const
{
    const auto key = property("krellThemeIcon").toString();
    return key.isEmpty() ? QPixmap() : m_theme->pixmap(key);
}

QSize Decal::themeIconSize(const QPixmap &icon) const
{
    if (icon.isNull()) return QSize(0, 0);
    const int limit = qBound(8, m_theme->metric(QStringLiteral("panel_icon_size"), 16), 32);
    return icon.size().scaled(limit, limit, Qt::KeepAspectRatio);
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

#ifdef Q_OS_WIN
void Decal::setScrollSettings(int pixelsPerSecond, int framesPerSecond)
{
    scrollPps = qBound(1, pixelsPerSecond, 1000);
    scrollFps = qBound(4, framesPerSecond, 30);
}
#endif

void Decal::onScrollTick()
{
#ifdef Q_OS_WIN
    // Keep settings-file access out of the Windows animation timer.
    const int pps = scrollPps;
    const int fps = scrollFps;
#else
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
#endif
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
    const int iconWidth = themeIconSize(themeIcon()).width();
    const int available = width() - (iconWidth > 0 ? iconWidth + kIconGapPx : 0);
    const bool shouldScroll = available > 0 && ((tw > available) || m_alwaysScroll)
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
    const auto icon = themeIconSize(themeIcon());
    return QSize(w + 4 + (icon.width() > 0 ? icon.width() + kIconGapPx : 0),
                 qMax(fm.height(), icon.height()) + 2);
}

QSize Decal::minimumSizeHint() const
{
    const QFont f = m_theme->font(m_fontKey);
    const QFontMetrics fm(f);
    return QSize(0, qMax(fm.height(), themeIconSize(themeIcon()).height()) + 2);
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
    const Theme::TextStyle ts = m_fontKey == QStringLiteral("time")
        ? m_theme->textStyle(QStringLiteral("text_clock"), m_colorKey)
        : m_theme->textStyle(m_colorKey);
    const QColor fg = ts.color.isValid() ? ts.color : QColor(Qt::white);

    const QRect r = rect();
    auto textRect = r;
    auto alignment = m_alignment;
    const auto icon = themeIcon();
    if (!icon.isNull()) {
        const auto size = themeIconSize(icon);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawPixmap(QRect(QPoint(0, (r.height() - size.height()) / 2), size), icon);
        textRect.adjust(size.width() + kIconGapPx, 0, 0, 0);
        if (textRect.isEmpty()) return;
        p.setClipRect(textRect);
        alignment = Qt::AlignLeft;
    }
    const QFontMetrics fm(p.font());
    const int yBaseline = (r.height() + fm.ascent() - fm.descent()) / 2;

    auto drawAt = [&](int x, int y, bool shadowPass) {
        if (shadowPass) {
            p.setPen(ts.shadow.color);
        } else {
            p.setPen(fg);
        }
        if (m_scrolling) {
            p.drawText(textRect.left() + x, y, m_text);
        } else {
            p.drawText(textRect.adjusted(x, 0, 0, 0),
                       Qt::AlignVCenter | alignment, m_text);
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
