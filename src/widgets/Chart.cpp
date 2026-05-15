#include "Chart.h"

#include "theme/Theme.h"

#include <QLinearGradient>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QResizeEvent>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

Chart::Chart(Theme *theme, const QString &colorKey, QWidget *parent)
    : QWidget(parent)
    , m_theme(theme)
    , m_colorKey(colorKey)
{
    Q_ASSERT(m_theme);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // No Qt::WA_OpaquePaintEvent — same reason as Panel: translucent
    // themes paint alpha < 1 and the optimization flag confuses Qt's
    // focus / z-order repaint scheduling, leaving stale pixels.
    connect(m_theme, &Theme::themeChanged, this, &Chart::onThemeChanged);
    m_samples.assign(static_cast<std::size_t>(m_capacity), 0.0);
    m_seriesColors.clear();
}

Chart::~Chart() = default;

void Chart::appendSample(double value)
{
    appendSampleAt(0, value);
}

void Chart::appendSampleAt(int seriesIdx, double value)
{
    if (m_capacity <= 0) return;
    if (value < 0.0) value = 0.0;

    if (m_multiSamples.empty()) {
        // Single-series mode (legacy).
        if (seriesIdx != 0 || m_samples.empty()) return;
        m_samples[static_cast<std::size_t>(m_head)] = value;
        if (seriesIdx == 0) m_head = (m_head + 1) % m_capacity;
    } else {
        if (seriesIdx < 0
            || seriesIdx >= static_cast<int>(m_multiSamples.size())) return;
        auto &ring = m_multiSamples[static_cast<std::size_t>(seriesIdx)];
        if (ring.empty()) return;
        ring[static_cast<std::size_t>(m_head)] = value;
        // Advance the head only after the LAST series of this tick is written
        // so all series stay aligned. Caller convention: write series 0..N-1
        // every tick, and the head bumps once after the last.
        if (seriesIdx == static_cast<int>(m_multiSamples.size()) - 1)
            m_head = (m_head + 1) % m_capacity;
    }
    update();
}

void Chart::setSeriesColors(const QList<QColor> &colors)
{
    m_seriesColors = colors;
    // Only reset sample data when the series TOPOLOGY actually changes —
    // switching between single/multi mode, or changing the number of series
    // (CPU core count, docker bridge count). When only the palette changes
    // (theme switch with the same series count) the history stays put and
    // re-renders in the new colors at the next paint.
    if (colors.isEmpty()) {
        if (!m_multiSamples.empty()) {
            m_multiSamples.clear();
            m_samples.assign(static_cast<std::size_t>(m_capacity), 0.0);
            m_head = 0;
        }
    } else if (static_cast<int>(m_multiSamples.size()) != colors.size()) {
        m_multiSamples.assign(
            static_cast<std::size_t>(colors.size()),
            std::vector<double>(static_cast<std::size_t>(m_capacity), 0.0));
        m_samples.clear();
        m_head = 0;
    }
    update();
}

void Chart::setRainbowSeries(int n)
{
    n = qMax(1, n);
    QList<QColor> palette;
    palette.reserve(n);
    // Cool-to-warm cores: descend hue 220° (deep blue) → 0° (red), so
    // adjacent cores in the chart get visually-similar colors and the
    // palette walks through the full blue → cyan → green → yellow →
    // orange → red progression as core count climbs. This is much
    // easier to read than the previous evenly-spread rainbow, where a
    // bright red `cpu1` next to a deep blue `cpu2` made the chart look
    // alarmist by default. Themes can still override with explicit
    // chart_line_cpu_<n> entries via setSeriesColors() — this is just
    // the default when no per-series color is provided.
    for (int i = 0; i < n; ++i) {
        const double t = (n == 1) ? 0.0
                                  : static_cast<double>(i)
                                  / static_cast<double>(n - 1);
        const int hue = static_cast<int>(220.0 - t * 220.0 + 0.5);
        palette.append(QColor::fromHsv(hue, 200, 230));
    }
    setSeriesColors(palette);
}

void Chart::setMaxValue(double maxValue)
{
    m_max = (maxValue > 0.0) ? maxValue : 1.0;
    update();
}

void Chart::setCapacity(int samples)
{
    const int clamped = qBound(16, samples, 4096);
    if (clamped == m_capacity) return;

    // Preserve history across capacity changes (theme switches that change
    // chart_height, layout reflows when monitors toggle, panel-width edits).
    // The old behaviour zeroed every sample, which made any setting that
    // touched chart geometry feel like the data restarted. Walk the ring in
    // chronological order, copy the newest `min(oldN, clamped)` samples into
    // the end of a new buffer so the chart stays right-anchored, zero-pad
    // any leading slack when growing. Reset m_head to 0 — after the reflow
    // index 0 is the oldest sample and the next write naturally wraps over
    // it, matching the ring's intended semantics.
    auto reflow = [&](std::vector<double> &ring) {
        const int oldN = static_cast<int>(ring.size());
        if (oldN == 0) {
            ring.assign(static_cast<std::size_t>(clamped), 0.0);
            return;
        }
        std::vector<double> next(static_cast<std::size_t>(clamped), 0.0);
        const int copy = std::min(oldN, clamped);
        for (int i = 0; i < copy; ++i) {
            const int srcIdx = ((m_head - copy + i) % oldN + oldN) % oldN;
            const int dstIdx = clamped - copy + i;
            next[static_cast<std::size_t>(dstIdx)] =
                ring[static_cast<std::size_t>(srcIdx)];
        }
        ring = std::move(next);
    };

    m_capacity = clamped;
    if (m_multiSamples.empty()) {
        reflow(m_samples);
    } else {
        for (auto &ring : m_multiSamples) reflow(ring);
    }
    m_head = 0;
    update();
}

void Chart::setOverlayText(const QString &text)
{
    if (m_overlayText == text) return;
    m_overlayText = text;
    update();
}

void Chart::onThemeChanged()
{
    updateGeometry();
    update();
}

void Chart::rebuildCapacityForWidth()
{
    // One sample per visible pixel keeps draw work trivial and avoids
    // re-allocating per frame. Multi-series mode also benefits — each
    // series gets the same per-pixel resolution.
    const int target = qMax(16, width());
    setCapacity(target);
}

double Chart::visiblePeak() const
{
    double peak = 0.0;
    if (m_multiSamples.empty()) {
        for (double v : m_samples)
            peak = qMax(peak, v);
    } else {
        for (const auto &ring : m_multiSamples)
            for (double v : ring)
                peak = qMax(peak, v);
    }
    return peak;
}

void Chart::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    rebuildCapacityForWidth();
}

QSize Chart::sizeHint() const
{
    return QSize(0, m_theme->metric(QStringLiteral("chart_height"), 32));
}

QSize Chart::minimumSizeHint() const
{
    return QSize(0, m_theme->metric(QStringLiteral("chart_height"), 16));
}

void Chart::paintEvent(QPaintEvent *)
{
    QPainter p(this);

    const QRect r = rect();
    const QColor bg   = m_theme->color(QStringLiteral("chart_bg"));
    const QColor grid = m_theme->color(QStringLiteral("chart_grid"));
    const QColor line = m_theme->color(m_colorKey,
                          m_theme->color(QStringLiteral("text_primary")));
    const QBrush lineBrush =
        m_theme->brush(m_colorKey, r,
                       m_theme->color(QStringLiteral("text_primary")));

    // Background. Prefer the v2 surface entry (image + slice + opacity +
    // tint, all in one lookup) — that's what Panel already uses, and
    // themes that put chart_bg under the v2 "surfaces" block went
    // unrendered before because Chart was only reading the v1 "images"
    // map. Falls back to the legacy v1 pixmap path, then to the
    // colour/gradient brush, in that order.
    Theme::Surface surf = m_theme->surface(QStringLiteral("chart_bg"));
    if (surf.image.isNull()) {
        // v1 fallback — old themes only wrote "images" not "surfaces".
        const QPixmap legacy = m_theme->pixmap(QStringLiteral("chart_bg"));
        if (!legacy.isNull()) {
            surf.image = legacy;
            // No slice info in v1 — keep the previous tile/stretch
            // behavior driven by imageMode.
            surf.slice = 0;
        }
    }
    if (!surf.image.isNull() && r.height() > 0) {
        const QString mode = m_theme->imageMode(QStringLiteral("chart_bg"),
                                                QStringLiteral("tile"));
        const qreal prevOpacity = p.opacity();
        if (surf.opacity < 1.0) p.setOpacity(prevOpacity * surf.opacity);
        if (surf.slice > 0
            && surf.image.width()  >= 2 * surf.slice
            && surf.image.height() >= 2 * surf.slice
            && r.width()           >= 2 * surf.slice
            && r.height()          >= 2 * surf.slice) {
            // 9-slice keeps the chart frame's corner / edge artwork
            // crisp regardless of how wide the chart ends up.
            const int s     = surf.slice;
            const int sw    = surf.image.width();
            const int sh    = surf.image.height();
            const int sMidW = sw - 2 * s;
            const int sMidH = sh - 2 * s;
            const int tw    = r.width();
            const int th    = r.height();
            const int tMidW = tw - 2 * s;
            const int tMidH = th - 2 * s;
            const int tx    = r.x();
            const int ty    = r.y();
            p.drawPixmap(QRect(tx,            ty,             s, s),
                         surf.image, QRect(0,    0,    s, s));
            p.drawPixmap(QRect(tx + tw - s,   ty,             s, s),
                         surf.image, QRect(sw-s, 0,    s, s));
            p.drawPixmap(QRect(tx,            ty + th - s,    s, s),
                         surf.image, QRect(0,    sh-s, s, s));
            p.drawPixmap(QRect(tx + tw - s,   ty + th - s,    s, s),
                         surf.image, QRect(sw-s, sh-s, s, s));
            p.drawPixmap(QRect(tx + s,        ty,             tMidW, s),
                         surf.image, QRect(s,    0,    sMidW, s));
            p.drawPixmap(QRect(tx + s,        ty + th - s,    tMidW, s),
                         surf.image, QRect(s,    sh-s, sMidW, s));
            p.drawPixmap(QRect(tx,            ty + s,         s, tMidH),
                         surf.image, QRect(0,    s,    s, sMidH));
            p.drawPixmap(QRect(tx + tw - s,   ty + s,         s, tMidH),
                         surf.image, QRect(sw-s, s,    s, sMidH));
            p.drawPixmap(QRect(tx + s,        ty + s,         tMidW, tMidH),
                         surf.image, QRect(s,    s,    sMidW, sMidH));
        } else if (mode == QStringLiteral("stretch")) {
            p.drawPixmap(r, surf.image);
        } else {
            const QPixmap scaled = (surf.image.height() == r.height())
                ? surf.image
                : surf.image.scaledToHeight(r.height(),
                                            Qt::SmoothTransformation);
            p.drawTiledPixmap(r, scaled);
        }
        if (surf.tint.isValid()) p.fillRect(r, surf.tint);
        if (surf.opacity < 1.0) p.setOpacity(prevOpacity);
    } else {
        // No image anywhere — fill with the chart_bg solid or gradient.
        const QBrush bgBrush =
            m_theme->brush(QStringLiteral("chart_bg"), r, bg);
        // When chart_bg is translucent (alpha < 255) and no image is bound,
        // the theme wants the chart area to read at its OWN alpha, not
        // chart_bg composited on top of the panel. CompositionMode_Source
        // makes the fill OVERWRITE panel pixels — so a chart_bg of
        // `#0affffff` (4% white) makes the chart 4% opaque, which is more
        // see-through than a 10% panel. Without this, alpha < 255 would
        // composite over panel_bg and make the chart area MORE opaque than
        // the surrounding panel — the opposite of what translucent themes
        // want. Gradient brushes still take the normal SourceOver path.
        if (bgBrush.style() == Qt::SolidPattern
            && bgBrush.color().alpha() < 255) {
            const auto prev = p.compositionMode();
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.fillRect(r, bgBrush);
            p.setCompositionMode(prev);
        } else {
            p.fillRect(r, bgBrush);
        }
    }

    if (!surf.overlays.isEmpty() && r.width() > 0 && r.height() > 0) {
        for (const QPixmap &overlay : surf.overlays)
            p.drawTiledPixmap(r, overlay);
    }

    QFont overlayFont = m_theme->font(QStringLiteral("label"));
    const bool hasOverlay = !m_overlayText.isEmpty();
    const QFontMetrics overlayMetrics(overlayFont);
    int overlayBand = 0;
    if (hasOverlay && r.height() >= overlayMetrics.height() + 12)
        overlayBand = overlayMetrics.height() + 2;

    QRect graphRect = r;
    if (overlayBand > 0)
        graphRect.setTop(r.top() + overlayBand);

    const int maxGridLines = qMax(0, m_theme->metric(QStringLiteral("chart_grid_lines"), 5));
    const double fullScale = (m_max > 0.0) ? m_max : 1.0;
    const double gridStep = maxGridLines > 0
        ? fullScale / static_cast<double>(maxGridLines)
        : fullScale;
    const double peak = std::clamp(visiblePeak(), 0.0, fullScale);
    const int gridLines = (maxGridLines > 0)
        ? qBound(1,
                 static_cast<int>(std::ceil(peak / gridStep)),
                 maxGridLines)
        : 0;
    const double displayMax = (gridLines > 0)
        ? gridStep * static_cast<double>(gridLines)
        : fullScale;
    if (gridLines > 0 && graphRect.height() > 1) {
        p.setPen(grid);
        for (int i = 1; i <= gridLines; ++i) {
            const double value = static_cast<double>(i) / gridLines;
            const int y = graphRect.bottom()
                - static_cast<int>(value * (graphRect.height() - 1) + 0.5);
            p.drawLine(graphRect.left(), y, graphRect.right(), y);
        }
    }

    auto drawSeries = [&](const std::vector<double> &ring,
                          const QBrush &linePaint,
                          const QColor &fillColor,
                          bool fillBelow) {
        const int n = static_cast<int>(ring.size());
        if (n < 2 || graphRect.width() < 2 || graphRect.height() < 2) return;

        QPolygonF poly;
        poly.reserve(n);
        const double yScale = static_cast<double>(graphRect.height() - 1);
        for (int i = 0; i < n; ++i) {
            const int idx = (m_head + i) % n;
            const double v = ring[static_cast<std::size_t>(idx)] / displayMax;
            const double clamped = std::clamp(v, 0.0, 1.0);
            const double x = graphRect.left()
                + (static_cast<double>(i) * (graphRect.width() - 1)) / (n - 1);
            const double y = graphRect.top()
                + (graphRect.height() - 1) - clamped * yScale;
            poly << QPointF(x, y);
        }

        if (fillBelow) {
            QPolygonF area = poly;
            area << QPointF(static_cast<double>(graphRect.right()),
                            static_cast<double>(graphRect.bottom()))
                 << QPointF(static_cast<double>(graphRect.left()),
                            static_cast<double>(graphRect.bottom()));
            QColor fill = fillColor;
            fill.setAlpha(110);
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawPolygon(area);
        }

        QPolygon intPoly;
        intPoly.reserve(static_cast<int>(poly.size()));
        for (const QPointF &pt : poly) intPoly << pt.toPoint();
        QPen linePen;
        linePen.setBrush(linePaint);
        linePen.setWidth(1);
        linePen.setCosmetic(true);
        p.setPen(linePen);
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(intPoly);
    };

    if (m_multiSamples.empty()) {
        drawSeries(m_samples, lineBrush, line, /*fillBelow=*/true);
    } else {
        // Multi-series: don't fill (overlapping translucent fills become
        // muddy). Just stroke each line in its own color.
        for (std::size_t i = 0; i < m_multiSamples.size(); ++i) {
            const QColor c = (i < static_cast<std::size_t>(m_seriesColors.size()))
                ? m_seriesColors.at(static_cast<int>(i)) : line;
            drawSeries(m_multiSamples[i], QBrush(c), c, /*fillBelow=*/false);
        }
    }

    // Optional overlay label: reserve a short top band when there is enough
    // height so 100% samples do not run through the number.
    if (hasOverlay) {
        p.setRenderHint(QPainter::TextAntialiasing, true);
        p.setFont(overlayFont);
        // Themes can give chart-overlay text its own color/shadow via
        // the dedicated "chart_overlay" key (e.g. amber-on-grey on the
        // egan-grey theme). Falls back to text_primary so themes that
        // don't define it get the previous behavior.
        const Theme::TextStyle ts = m_theme->textStyle(
            QStringLiteral("chart_overlay"),
            QStringLiteral("text_primary"));
        const QRect textRect = overlayBand > 0
            ? QRect(r.left() + 4, r.top() + 1, qMax(0, r.width() - 8), overlayBand)
            : r.adjusted(4, 1, -4, -1);
        const QString displayText =
            overlayMetrics.elidedText(m_overlayText, Qt::ElideRight, textRect.width());
        if (ts.shadow.present) {
            p.setPen(ts.shadow.color);
            p.drawText(textRect.translated(ts.shadow.offsetX, ts.shadow.offsetY),
                       Qt::AlignTop | Qt::AlignLeft, displayText);
        }
        const QColor textColor = ts.color.isValid()
            ? ts.color
            : m_theme->color(QStringLiteral("text_primary"));
        QColor outline = ts.shadow.present ? ts.shadow.color : QColor(0, 0, 0, 200);
        if (!outline.isValid() || outline.alpha() == 0)
            outline = QColor(0, 0, 0, 200);
        QPainterPath textPath;
        textPath.addText(QPointF(textRect.left(),
                                 textRect.top() + overlayMetrics.ascent()),
                         overlayFont,
                         displayText);
        QPen outlinePen(outline);
        outlinePen.setWidthF(2.0);
        outlinePen.setJoinStyle(Qt::RoundJoin);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.strokePath(textPath, outlinePen);
        p.fillPath(textPath, textColor);
    }
}
