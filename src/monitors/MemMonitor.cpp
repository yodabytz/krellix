#include "MemMonitor.h"

#include "sysdep/MemStat.h"
#include "widgets/Decal.h"
#include "widgets/Meter.h"
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
    m_memMeter = p->addMeter(QStringLiteral("chart_line_mem"));

    m_swapText  = p->addDecal(QStringLiteral("label"),
                              QStringLiteral("text_secondary"));
    m_swapText->setAlignment(Qt::AlignHCenter);
    m_swapMeter = p->addMeter(QStringLiteral("chart_line_swap"));

    tick();
    return p;
}

void MemMonitor::tick()
{
    const MemInfo m = MemStat::read();
    if (!m.valid()) {
        if (m_memText) m_memText->setText(QStringLiteral("(no /proc/meminfo)"));
        if (m_memMeter) {
            m_memMeter->setValue(0.0);
            m_memMeter->setText(QString());
        }
        if (m_swapText) m_swapText->setText(QString());
        if (m_swapMeter) {
            m_swapMeter->setValue(0.0);
            m_swapMeter->setText(QString());
        }
        return;
    }

    const double memRatio = m.memUsedRatio();
    if (m_memText)
        m_memText->setText(QStringLiteral("RAM ") + usedTotal(m.memUsedKb(), m.totalKb));
    if (m_memMeter) {
        m_memMeter->setValue(memRatio);
        m_memMeter->setText(QString());
    }

    if (m.swapTotalKb == 0) {
        if (m_swapText)  m_swapText->setText(QStringLiteral("Swap none"));
        if (m_swapMeter) {
            m_swapMeter->setValue(0.0);
            m_swapMeter->setText(QString());
        }
    } else {
        const double swapRatio = m.swapUsedRatio();
        if (m_swapText)
            m_swapText->setText(QStringLiteral("Swap ") + usedTotal(m.swapUsedKb(), m.swapTotalKb));
        if (m_swapMeter) {
            m_swapMeter->setValue(swapRatio);
            m_swapMeter->setText(QString());
        }
    }
}
