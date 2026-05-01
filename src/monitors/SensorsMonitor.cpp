#include "SensorsMonitor.h"

#include "sysdep/SensorStat.h"
#include "widgets/Decal.h"
#include "widgets/Panel.h"

#include <QString>

SensorsMonitor::SensorsMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
}

SensorsMonitor::~SensorsMonitor() = default;

QWidget *SensorsMonitor::createWidget(QWidget *parent)
{
    auto *p = new Panel(theme(), parent);
    p->setSurfaceKey(QStringLiteral("panel_bg_sensors"));
    p->setTitle(QStringLiteral("Sensors"));

    const QList<SensorReading> readings = SensorStat::read();
    if (readings.isEmpty()) {
        Decal *d = p->addDecal(QStringLiteral("label"),
                               QStringLiteral("text_secondary"));
        d->setAlignment(Qt::AlignHCenter);
        d->setText(QStringLiteral("(no sensors found)"));
        return p;
    }

    for (const SensorReading &r : readings) {
        const QString key = r.chip + QStringLiteral(":") + r.label;
        Decal *d = p->addDecal(QStringLiteral("label"),
                               QStringLiteral("text_primary"));
        d->setAlignment(Qt::AlignHCenter);
        m_rows.insert(key, d);
    }
    tick();
    return p;
}

void SensorsMonitor::tick()
{
    const QList<SensorReading> readings = SensorStat::read();
    for (const SensorReading &r : readings) {
        const QString key = r.chip + QStringLiteral(":") + r.label;
        auto it = m_rows.constFind(key);
        if (it == m_rows.constEnd() || !it.value()) continue;

        QString text;
        switch (r.type) {
        case SensorReading::Temp:
            text = QStringLiteral("%1  %2°C")
                       .arg(r.label)
                       .arg(r.value, 0, 'f', 0);
            break;
        case SensorReading::Fan:
            text = QStringLiteral("%1  %2 RPM")
                       .arg(r.label)
                       .arg(static_cast<qint64>(r.value));
            break;
        case SensorReading::Voltage:
            text = QStringLiteral("%1  %2 V")
                       .arg(r.label)
                       .arg(r.value, 0, 'f', 2);
            break;
        }
        it.value()->setText(text);
    }
}
