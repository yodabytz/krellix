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
}

Chart::~Chart() = default;

void Chart::appendSample(double value)
{
    if (m_capacity <= 0 || m_samples.empty()) return;
    if (value < 0.0) value = 0.0;
    m_samples[static_cast<std::size_t>(m_head)] = value;
    m_head = (m_head + 1) % m_capacity;
    update();
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
    m_samples.assign(static_cast<std::size_t>(m_capacity), 0.0);
    m_head = 0;
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
    // re-allocating per frame.
    const int target = qMax(16, width());
    setCapacity(target);
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

    const QPixmap bgPix = m_theme->pixmap(QStringLiteral("chart_bg"));
    if (!bgPix.isNull()) {
        const QString mode = m_theme->imageMode(QStringLiteral("chart_bg"));
        if (mode == QStringLiteral("stretch"))
            p.drawPixmap(r, bgPix);
        else
            p.drawTiledPixmap(r, bgPix);
    } else {
        p.fillRect(r, bg);
    }

    const int gridLines = qMax(0, m_theme->metric(QStringLiteral("chart_grid_lines"), 4));
    if (gridLines > 0 && r.height() > 1) {
        p.setPen(grid);
        for (int i = 1; i < gridLines; ++i) {
            const int y = r.y() + (r.height() * i) / gridLines;
            p.drawLine(r.left(), y, r.right(), y);
        }
    }

    const int n = static_cast<int>(m_samples.size());
    if (n < 2 || r.width() < 2) return;

    QPolygonF poly;
    poly.reserve(n);
    const double yScale = static_cast<double>(r.height() - 1);
    for (int i = 0; i < n; ++i) {
        const int idx = (m_head + i) % n;
        const double v = m_samples[static_cast<std::size_t>(idx)] / m_max;
        const double clamped = std::clamp(v, 0.0, 1.0);
        const double x = (static_cast<double>(i) * (r.width() - 1)) / (n - 1);
        const double y = (r.height() - 1) - clamped * yScale;
        poly << QPointF(x, y);
    }

    p.setRenderHint(QPainter::Antialiasing, true);

    // Filled area chart: closed polygon with a vertical gradient that
    // fades from the line color (top, mostly opaque) down to almost
    // transparent at the floor — much richer than a hairline polyline.
    QPolygonF area = poly;
    area << QPointF(static_cast<double>(r.right()), static_cast<double>(r.bottom()))
         << QPointF(static_cast<double>(r.left()),  static_cast<double>(r.bottom()));

    // Multi-stop vertical gradient: a bright highlight right under the
    // curve, then a quick falloff to a soft mid-tone, fading to nearly
    // transparent at the floor. Reads as a glassy filled area.
    QLinearGradient gradient(0, r.top(), 0, r.bottom());
    QColor highlight    = line; highlight.setAlpha(235);
    QColor topColor     = line; topColor.setAlpha(180);
    QColor midColor     = line; midColor.setAlpha(80);
    QColor bottomColor  = line; bottomColor.setAlpha(12);
    gradient.setColorAt(0.00, highlight);
    gradient.setColorAt(0.08, topColor);
    gradient.setColorAt(0.55, midColor);
    gradient.setColorAt(1.00, bottomColor);

    p.setPen(Qt::NoPen);
    p.setBrush(gradient);
    p.drawPolygon(area);

    // A subtle wider glow behind the crisp top line — pushes the curve
    // forward visually without smudging it.
    QColor glow = line;
    glow.setAlpha(80);
    QPen glowPen(glow);
    glowPen.setWidthF(3.0);
    glowPen.setCosmetic(true);
    p.setBrush(Qt::NoBrush);
    p.setPen(glowPen);
    p.drawPolyline(poly);

    // Crisp top line.
    QPen linePen(line);
    linePen.setWidthF(1.5);
    linePen.setCosmetic(true);
    p.setPen(linePen);
    p.drawPolyline(poly);
}
