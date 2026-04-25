#include "Panel.h"

#include "theme/Theme.h"
#include "widgets/Chart.h"
#include "widgets/Decal.h"
#include "widgets/Krell.h"

#include <QPaintEvent>
#include <QPainter>
#include <QVBoxLayout>

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
        m_titleDecal = new Decal(m_theme,
                                 QStringLiteral("value"),
                                 QStringLiteral("text_secondary"),
                                 this);
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

void Panel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRect r = rect();

    // Image-themed background tiles across the panel. Falls back to a
    // flat color fill if no panel_bg pixmap is configured or the file
    // failed to load.
    const QPixmap bgPix = m_theme->pixmap(QStringLiteral("panel_bg"));
    if (!bgPix.isNull()) {
        const QString mode = m_theme->imageMode(QStringLiteral("panel_bg"));
        if (mode == QStringLiteral("stretch"))
            p.drawPixmap(r, bgPix);
        else
            p.drawTiledPixmap(r, bgPix);
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
