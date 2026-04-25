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
    p->setTitle(QStringLiteral("Memory"));

    m_memText  = p->addDecal(QStringLiteral("label"),
                             QStringLiteral("text_primary"));
    m_memKrell = p->addKrell();
    m_memChart = p->addChart(QStringLiteral("chart_line_mem"));
    if (m_memChart) m_memChart->setMaxValue(1.0);

    m_swapText  = p->addDecal(QStringLiteral("label"),
                              QStringLiteral("text_secondary"));
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

    if (m_memText)
        m_memText->setText(QStringLiteral("RAM ") + usedTotal(m.memUsedKb(), m.totalKb));
    if (m_memKrell)
        m_memKrell->setValue(m.memUsedRatio());
    if (m_memChart)
        m_memChart->appendSample(m.memUsedRatio());

    if (m.swapTotalKb == 0) {
        if (m_swapText)  m_swapText->setText(QStringLiteral("Swap none"));
        if (m_swapKrell) m_swapKrell->setValue(0.0);
    } else {
        if (m_swapText)
            m_swapText->setText(QStringLiteral("Swap ") + usedTotal(m.swapUsedKb(), m.swapTotalKb));
        if (m_swapKrell)
            m_swapKrell->setValue(m.swapUsedRatio());
    }
}
