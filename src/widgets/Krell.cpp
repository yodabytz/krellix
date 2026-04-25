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
    return QSize(0, m_theme->metric(QStringLiteral("krell_height"), 8));
}

QSize Krell::minimumSizeHint() const
{
    return QSize(0, m_theme->metric(QStringLiteral("krell_height"), 8));
}

void Krell::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QColor track = m_theme->color(QStringLiteral("krell_track"));
    const QColor ind   = m_theme->color(QStringLiteral("krell_indicator"));

    const QRect r = rect();
    p.fillRect(r, track);

    if (r.width() <= 0) return;
    const int notchW = qMax(2, r.width() / 24);
    const int xMax   = r.width() - notchW;
    const int x      = static_cast<int>(m_value * xMax);
    p.fillRect(QRect(x, r.y(), notchW, r.height()), ind);
}
