#include "BatteryStat.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

namespace {

BatteryStat::ReadFn g_readOverride = nullptr;

QString readTrimmed(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    return QString::fromLatin1(f.read(256)).trimmed();
}

bool acIsOnline()
{
    QDir ps(QStringLiteral("/sys/class/power_supply"));
    if (!ps.exists()) return false;
    const QFileInfoList entries =
        ps.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : entries) {
        const QString type = readTrimmed(fi.absoluteFilePath() + QStringLiteral("/type"));
        if (type == QStringLiteral("Mains")) {
            return readTrimmed(fi.absoluteFilePath() + QStringLiteral("/online"))
                   == QStringLiteral("1");
        }
    }
    return false;
}

} // namespace

void BatteryStat::setReadOverride(BatteryStat::ReadFn fn) { g_readOverride = fn; }

BatteryInfo BatteryStat::read()
{
    if (g_readOverride) return g_readOverride();

    BatteryInfo out;
    QDir ps(QStringLiteral("/sys/class/power_supply"));
    if (!ps.exists()) return out;

    const QFileInfoList entries =
        ps.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : entries) {
        const QString name = fi.fileName();
        if (!name.startsWith(QStringLiteral("BAT"))) continue;

        const QString dir = fi.absoluteFilePath();
        const QString type = readTrimmed(dir + QStringLiteral("/type"));
        if (type != QStringLiteral("Battery")) continue;

        out.present     = true;
        out.batteryName = name;

        bool ok = false;
        out.percent = readTrimmed(dir + QStringLiteral("/capacity")).toInt(&ok);
        if (!ok) out.percent = 0;
        out.status  = readTrimmed(dir + QStringLiteral("/status"));

        // Time remaining: derive from energy_now / power_now or
        // charge_now / current_now where available.
        const qint64 energyNow = readTrimmed(dir + QStringLiteral("/energy_now")).toLongLong();
        const qint64 powerNow  = readTrimmed(dir + QStringLiteral("/power_now")).toLongLong();
        if (energyNow > 0 && powerNow > 0
            && out.status == QStringLiteral("Discharging")) {
            out.timeRemainSec = (energyNow * 3600) / powerNow;
        }

        break;  // first battery only
    }
    out.acOnline = acIsOnline();
    return out;
}
