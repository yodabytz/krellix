#include "Decal.h"

#include "theme/Theme.h"

#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>

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
    connect(m_theme, &Theme::themeChanged, this, &Decal::onThemeChanged);
}

Decal::~Decal() = default;

void Decal::setText(const QString &text)
{
    if (m_text == text) return;
    m_text = text;
    updateGeometry();
    update();
}

void Decal::onThemeChanged()
{
    updateGeometry();
    update();
}

QSize Decal::sizeHint() const
{
    const QFont f = m_theme->font(m_fontKey);
    const QFontMetrics fm(f);
    const int w = fm.horizontalAdvance(m_text.isEmpty() ? QStringLiteral("M") : m_text);
    return QSize(w + 4, fm.height() + 2);
}

QSize Decal::minimumSizeHint() const
{
    const QFont f = m_theme->font(m_fontKey);
    const QFontMetrics fm(f);
    return QSize(0, fm.height() + 2);
}

void Decal::paintEvent(QPaintEvent *)
{
    if (m_text.isEmpty()) return;

    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QFont f = m_theme->font(m_fontKey);
    const QColor c = m_theme->color(m_colorKey, QColor(Qt::white));

    p.setFont(f);
    p.setPen(c);
    p.drawText(rect(), Qt::AlignVCenter | Qt::AlignLeft, m_text);
}
