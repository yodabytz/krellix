#include "SensorsMonitor.h"

#include "sysdep/SensorStat.h"
#include "widgets/Decal.h"
#include "widgets/Panel.h"

#include <QSettings>
#include <QString>

namespace {

// Display mode values — must stay in sync with SettingsDialog.
constexpr int kModeTempAndPct = 0;
constexpr int kModeTempOnly   = 1;
constexpr int kModePctOnly    = 2;

// Effective critical threshold for a reading: prefer hardware crit, then max,
// then the user-configured fallback.
double effectiveCritC(const SensorReading &r, double settingCritC)
{
    if (r.critC > 0) return r.critC;
    if (r.maxC  > 0) return r.maxC;
    return settingCritC;
}

QString colorKeyForTemp(double tempC, double warnC, double critC)
{
    if (tempC >= critC) return QStringLiteral("accent_critical");
    if (tempC >= warnC) return QStringLiteral("accent_warning");
    return QStringLiteral("accent_ok");
}

QString colorKeyForPct(double pct)
{
    if (pct >= 85) return QStringLiteral("accent_critical");
    if (pct >= 70) return QStringLiteral("accent_warning");
    return QStringLiteral("accent_ok");
}

} // namespace

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
    QSettings s;
    const bool useFahrenheit = s.value(QStringLiteral("monitors/sensors/use_fahrenheit"), false).toBool();
    const int  displayMode   = s.value(QStringLiteral("monitors/sensors/display_mode"), kModeTempAndPct).toInt();
    const double warnC       = s.value(QStringLiteral("monitors/sensors/warn_celsius"),  70).toDouble();
    const double critC       = s.value(QStringLiteral("monitors/sensors/crit_celsius"),  85).toDouble();

    const QList<SensorReading> readings = SensorStat::read();
    for (const SensorReading &r : readings) {
        const QString key = r.chip + QStringLiteral(":") + r.label;
        auto it = m_rows.constFind(key);
        if (it == m_rows.constEnd() || !it.value()) continue;
        Decal *d = it.value().data();

        QString text;
        QString colorKey = QStringLiteral("text_primary");

        switch (r.type) {

        case SensorReading::Temp: {
            const double hwCrit = effectiveCritC(r, critC);
            const int    pct    = qBound(0, qRound(r.value / hwCrit * 100.0), 100);
            colorKey = colorKeyForTemp(r.value, warnC, critC);

            const double displayTemp = useFahrenheit ? r.value * 9.0 / 5.0 + 32.0 : r.value;
            const QString unitSuffix = useFahrenheit ? QStringLiteral("°F") : QStringLiteral("°C");
            const QString tempStr    = QStringLiteral("%1%2")
                                           .arg(displayTemp, 0, 'f', 0).arg(unitSuffix);
            const QString pctStr     = QStringLiteral("(%1%)").arg(pct);

            switch (displayMode) {
            case kModeTempOnly:
                text = QStringLiteral("%1  %2").arg(r.label, tempStr);
                break;
            case kModePctOnly:
                text = QStringLiteral("%1  %2").arg(r.label, pctStr);
                break;
            default: // kModeTempAndPct
                text = QStringLiteral("%1  %2 %3").arg(r.label, tempStr, pctStr);
                break;
            }
            break;
        }

        case SensorReading::Percent: {
            // macOS thermal-pressure levels: value is already a percentage.
            colorKey = colorKeyForPct(r.value);
            text     = QStringLiteral("%1  %2%")
                           .arg(r.label)
                           .arg(r.value, 0, 'f', 0);
            break;
        }

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

        d->setText(text);
        if (r.type == SensorReading::Temp || r.type == SensorReading::Percent)
            d->setColorKey(colorKey);
    }
}
