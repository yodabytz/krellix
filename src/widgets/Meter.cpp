#include "Meter.h"

#include "theme/Theme.h"

#include <QPaintEvent>
#include <QPainter>
#include <QtGlobal>

Meter::Meter(Theme *theme, const QString &colorKey, QWidget *parent)
    : QWidget(parent)
    , m_theme(theme)
    , m_colorKey(colorKey)
{
    Q_ASSERT(m_theme);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    connect(m_theme, &Theme::themeChanged, this, &Meter::onThemeChanged);
}

Meter::~Meter() = default;

void Meter::setValue(double normalized)
{
    const double clamped = qBound(0.0, normalized, 1.0);
    if (qFuzzyCompare(1.0 + clamped, 1.0 + m_value)) return;
    m_value = clamped;
    update();
}

void Meter::setText(const QString &text)
{
    if (m_text == text) return;
    const bool hadText = !m_text.isEmpty();
    m_text = text;
    if (hadText != !m_text.isEmpty())
        updateGeometry();
    update();
}

void Meter::setFillVisible(bool visible)
{
    if (m_fillVisible == visible) return;
    m_fillVisible = visible;
    update();
}

QSize Meter::sizeHint() const
{
    const int fallback = m_text.isEmpty()
        ? qMax(4, m_theme->metric(QStringLiteral("krell_height"), 8))
        : qMax(12, m_theme->metric(QStringLiteral("krell_height"), 8) + 6);
    const int themed = m_theme->metric(QStringLiteral("meter_height"), fallback);
    return QSize(0, themed > 0 ? themed : fallback);
}

QSize Meter::minimumSizeHint() const
{
    return sizeHint();
}

void Meter::onThemeChanged()
{
    updateGeometry();
    update();
}

void Meter::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRect r = rect();
    if (r.width() <= 0 || r.height() <= 0) return;

    const QPixmap bgPix = m_theme->pixmap(QStringLiteral("chart_bg"));
    if (!bgPix.isNull()) {
        p.drawPixmap(r, bgPix);
    } else {
        p.fillRect(r, m_theme->brush(QStringLiteral("chart_bg"), r,
                                     m_theme->color(QStringLiteral("chart_bg"))));
    }

    if (m_fillVisible && m_value > 0.0) {
        const int fillW = qBound(0, static_cast<int>(m_value * r.width() + 0.5), r.width());
        QRect fillRect = r;
        fillRect.setWidth(fillW);
        QColor fallback = m_theme->color(QStringLiteral("krell_indicator"));
        QBrush fillBrush = m_theme->brush(m_colorKey, r, fallback);
        p.fillRect(fillRect, fillBrush);
    }

    if (!m_text.isEmpty()) {
        p.setRenderHint(QPainter::TextAntialiasing, true);
        p.setFont(m_theme->font(QStringLiteral("label")));
        const Theme::TextStyle ts = m_theme->textStyle(QStringLiteral("text_primary"));
        const QRect textRect = r.adjusted(3, 0, -3, 0);
        if (ts.shadow.present) {
            p.setPen(ts.shadow.color);
            p.drawText(textRect.translated(ts.shadow.offsetX, ts.shadow.offsetY),
                       Qt::AlignCenter, m_text);
        }
        p.setPen(ts.color.isValid() ? ts.color
                                    : m_theme->color(QStringLiteral("text_primary")));
        p.drawText(textRect, Qt::AlignCenter, m_text);
    }
}
