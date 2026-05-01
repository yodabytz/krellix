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
    setAttribute(Qt::WA_OpaquePaintEvent, true);
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
    if (colors.isEmpty()) {
        m_multiSamples.clear();
        m_samples.assign(static_cast<std::size_t>(m_capacity), 0.0);
    } else {
        m_multiSamples.assign(
            static_cast<std::size_t>(colors.size()),
            std::vector<double>(static_cast<std::size_t>(m_capacity), 0.0));
        m_samples.clear();
    }
    m_head = 0;
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
    m_capacity = clamped;
    if (m_multiSamples.empty()) {
        m_samples.assign(static_cast<std::size_t>(m_capacity), 0.0);
    } else {
        for (auto &ring : m_multiSamples)
            ring.assign(static_cast<std::size_t>(m_capacity), 0.0);
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

    // Same GKrellM convention as Panel: scale chart_bg vertically to chart
    // height, then tile horizontally. Preserves the 3D shading without
    // distortion at any chart width.
    const QPixmap bgPix = m_theme->pixmap(QStringLiteral("chart_bg"));
    if (!bgPix.isNull() && r.height() > 0) {
        const QString mode = m_theme->imageMode(QStringLiteral("chart_bg"),
                                                QStringLiteral("tile"));
        if (mode == QStringLiteral("stretch")) {
            p.drawPixmap(r, bgPix);
        } else {
            const QPixmap scaled = (bgPix.height() == r.height())
                ? bgPix
                : bgPix.scaledToHeight(r.height(), Qt::SmoothTransformation);
            p.drawTiledPixmap(r, scaled);
        }
    } else {
        // No image — fill with the chart_bg solid OR gradient brush.
        p.fillRect(r, m_theme->brush(QStringLiteral("chart_bg"), r, bg));
    }

    const int maxGridLines = qMax(0, m_theme->metric(QStringLiteral("chart_grid_lines"), 6));
    const double peakRatio = (m_max > 0.0)
        ? std::clamp(visiblePeak() / m_max, 0.0, 1.0)
        : 0.0;
    const int gridLines = qBound(0,
                                 static_cast<int>(std::ceil(peakRatio * maxGridLines)),
                                 maxGridLines);
    if (gridLines > 0 && r.height() > 1) {
        p.setPen(grid);
        for (int i = 1; i <= gridLines; ++i) {
            const double value = static_cast<double>(i) / maxGridLines;
            const int y = r.bottom() - static_cast<int>(value * (r.height() - 1) + 0.5);
            p.drawLine(r.left(), y, r.right(), y);
        }
    }

    auto drawSeries = [&](const std::vector<double> &ring,
                          const QBrush &linePaint,
                          const QColor &fillColor,
                          bool fillBelow) {
        const int n = static_cast<int>(ring.size());
        if (n < 2 || r.width() < 2) return;

        QPolygonF poly;
        poly.reserve(n);
        const double yScale = static_cast<double>(r.height() - 1);
        for (int i = 0; i < n; ++i) {
            const int idx = (m_head + i) % n;
            const double v = ring[static_cast<std::size_t>(idx)] / m_max;
            const double clamped = std::clamp(v, 0.0, 1.0);
            const double x = (static_cast<double>(i) * (r.width() - 1)) / (n - 1);
            const double y = (r.height() - 1) - clamped * yScale;
            poly << QPointF(x, y);
        }

        if (fillBelow) {
            QPolygonF area = poly;
            area << QPointF(static_cast<double>(r.right()),
                            static_cast<double>(r.bottom()))
                 << QPointF(static_cast<double>(r.left()),
                            static_cast<double>(r.bottom()));
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

    // Optional in-chart overlay label (e.g. "cpu0  37%") — drawn after
    // the line so it sits on top, anti-aliased, in the chart's primary
    // text color. Saves a full decal row of vertical space per panel.
    if (!m_overlayText.isEmpty()) {
        p.setRenderHint(QPainter::TextAntialiasing, true);
        QFont f = m_theme->font(QStringLiteral("label"));
        p.setFont(f);
        const Theme::TextStyle ts = m_theme->textStyle(QStringLiteral("text_primary"));
        const QRect textRect = r.adjusted(4, 1, -4, -1);
        if (ts.shadow.present) {
            p.setPen(ts.shadow.color);
            p.drawText(textRect.translated(ts.shadow.offsetX, ts.shadow.offsetY),
                       Qt::AlignTop | Qt::AlignLeft, m_overlayText);
        }
        p.setPen(ts.color.isValid() ? ts.color
                                    : m_theme->color(QStringLiteral("text_primary")));
        p.drawText(textRect, Qt::AlignTop | Qt::AlignLeft, m_overlayText);
    }
}
