#include "MemMonitor.h"

#include "sysdep/MemStat.h"
#include "widgets/Chart.h"
#include "widgets/Decal.h"
#include "widgets/Krell.h"
#include "widgets/Panel.h"

#include <QString>

namespace {

QString humanKb(quint64 kb)
{
    const double v = static_cast<double>(kb);
    if (v >= 1024.0 * 1024.0)
        return QString::number(v / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" G");
    if (v >= 1024.0)
        return QString::number(v / 1024.0, 'f', 1) + QStringLiteral(" M");
    return QString::number(static_cast<int>(v)) + QStringLiteral(" K");
}

QString usedTotal(quint64 used, quint64 total)
{
    return humanKb(used) + QStringLiteral(" / ") + humanKb(total);
}

} // namespace

MemMonitor::MemMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
}

MemMonitor::~MemMonitor() = default;

QWidget *MemMonitor::createWidget(QWidget *parent)
{
    auto *p = new Panel(theme(), parent);
    p->setSurfaceKey(QStringLiteral("panel_bg_mem"));
    p->setTitle(QStringLiteral("Memory"));

    m_memText  = p->addDecal(QStringLiteral("label"),
                             QStringLiteral("text_primary"));
    m_memText->setAlignment(Qt::AlignHCenter);
    m_memKrell = p->addKrell();
    m_memChart = p->addChart(QStringLiteral("chart_line_mem"));
    if (m_memChart) m_memChart->setMaxValue(1.0);

    m_swapText  = p->addDecal(QStringLiteral("label"),
                              QStringLiteral("text_secondary"));
    m_swapText->setAlignment(Qt::AlignHCenter);
    m_swapKrell = p->addKrell();

    tick();
    return p;
}

void MemMonitor::tick()
{
    const MemInfo m = MemStat::read();
    if (!m.valid()) {
        if (m_memText) m_memText->setText(QStringLiteral("(no /proc/meminfo)"));
        if (m_memKrell) m_memKrell->setValue(0.0);
        if (m_swapText) m_swapText->setText(QString());
        if (m_swapKrell) m_swapKrell->setValue(0.0);
        return;
    }

    const double memRatio = m.memUsedRatio();
    if (m_memText)
        m_memText->setText(QStringLiteral("RAM ") + usedTotal(m.memUsedKb(), m.totalKb));
    if (m_memKrell) {
        m_memKrell->setValue(memRatio);
        m_memKrell->setAlertLevel(memRatio >= 0.95 ? Krell::AlertLevel::Critical
                                  : memRatio >= 0.85 ? Krell::AlertLevel::Warning
                                                     : Krell::AlertLevel::None);
    }
    if (m_memChart)
        m_memChart->appendSample(memRatio);

    if (m.swapTotalKb == 0) {
        if (m_swapText)  m_swapText->setText(QStringLiteral("Swap none"));
        if (m_swapKrell) {
            m_swapKrell->setValue(0.0);
            m_swapKrell->setAlertLevel(Krell::AlertLevel::None);
        }
    } else {
        const double swapRatio = m.swapUsedRatio();
        if (m_swapText)
            m_swapText->setText(QStringLiteral("Swap ") + usedTotal(m.swapUsedKb(), m.swapTotalKb));
        if (m_swapKrell) {
            m_swapKrell->setValue(swapRatio);
            // Swap usage is a stronger signal than RAM usage — even
            // moderate swap means active paging. Tighter thresholds.
            m_swapKrell->setAlertLevel(swapRatio >= 0.50 ? Krell::AlertLevel::Critical
                                       : swapRatio >= 0.20 ? Krell::AlertLevel::Warning
                                                           : Krell::AlertLevel::None);
        }
    }
}
