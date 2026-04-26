#include "AlertBanner.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>

AlertBanner::AlertBanner(QWidget *parent)
    : QWidget(parent)
    , m_label(new QLabel(this))
    , m_blinkTimer(new QTimer(this))
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setVisible(false);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 3, 6, 3);
    layout->addWidget(m_label, 1, Qt::AlignCenter);

    m_label->setAlignment(Qt::AlignCenter);
    QFont f = m_label->font();
    f.setBold(true);
    m_label->setFont(f);

    m_blinkTimer->setInterval(550);
    m_blinkTimer->setTimerType(Qt::CoarseTimer);
    connect(m_blinkTimer, &QTimer::timeout, this, &AlertBanner::onBlinkTick);
}

AlertBanner::~AlertBanner() = default;

void AlertBanner::showAlert(const QString &message)
{
    m_label->setText(message);
    m_dim = false;
    setStyleSheet(QStringLiteral(
        "background-color: #c00000; color: white;"));
    setVisible(true);
    m_blinkTimer->start();
}

void AlertBanner::hideAlert()
{
    m_blinkTimer->stop();
    setVisible(false);
}

void AlertBanner::onBlinkTick()
{
    m_dim = !m_dim;
    setStyleSheet(m_dim
        ? QStringLiteral("background-color: #4a0000; color: #ffcccc;")
        : QStringLiteral("background-color: #ff2020; color: white;"));
}
