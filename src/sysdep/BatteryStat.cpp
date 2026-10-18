#include "BatteryStat.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
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
#if defined(Q_OS_MACOS)
    return false;
#else
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
#endif
}

#if defined(Q_OS_MACOS)
QString runProgram(const QString &program, const QStringList &args, int timeoutMs = 1200)
{
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(200);
        return {};
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return {};
    return QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed();
}

QString normalizeStatus(const QString &raw)
{
    const QString status = raw.trimmed().toLower();
    if (status.contains(QStringLiteral("charged")) || status.contains(QStringLiteral("full")))
        return QStringLiteral("Full");
    if (status.contains(QStringLiteral("discharg")))
        return QStringLiteral("Discharging");
    if (status.contains(QStringLiteral("charg")) && !status.contains(QStringLiteral("discharg")))
        return QStringLiteral("Charging");
    return QStringLiteral("Unknown");
}

qint64 parseRemainingSeconds(const QString &text)
{
    static const QRegularExpression rx(QStringLiteral("(\\d+):(\\d+)"));
    const auto match = rx.match(text);
    if (!match.hasMatch())
        return -1;
    return match.captured(1).toLongLong() * 3600
         + match.captured(2).toLongLong() * 60;
}

BatteryInfo readMacBattery()
{
    BatteryInfo out;
    const QString output = runProgram(QStringLiteral("pmset"),
                                      {QStringLiteral("-g"), QStringLiteral("batt")});
    if (output.isEmpty() || output.contains(QStringLiteral("No batteries available"),
                                            Qt::CaseInsensitive)) {
        return out;
    }

    out.acOnline = output.contains(QStringLiteral("AC Power"), Qt::CaseInsensitive);

    static const QRegularExpression lineRx(
        QStringLiteral("^\\s*-?([^\\(]+?)\\s*(?:\\(id=.*?\\))?\\s+(\\d+)%;\\s*(.*)$"));

    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (!line.contains(QLatin1Char('%')))
            continue;

        const auto match = lineRx.match(line.trimmed());
        if (!match.hasMatch())
            continue;

        out.present = true;
        out.batteryName = match.captured(1).trimmed();
        out.percent = match.captured(2).toInt();

        const QStringList fields = match.captured(3).split(QLatin1Char(';'),
                                                           Qt::SkipEmptyParts);
        if (!fields.isEmpty())
            out.status = normalizeStatus(fields.first());
        for (const QString &field : fields) {
            const qint64 remain = parseRemainingSeconds(field);
            if (remain >= 0) {
                out.timeRemainSec = remain;
                break;
            }
        }
        break;
    }

    return out;
}
#endif

} // namespace

void BatteryStat::setReadOverride(BatteryStat::ReadFn fn) { g_readOverride = fn; }

BatteryInfo BatteryStat::read()
{
    if (g_readOverride) return g_readOverride();

    BatteryInfo out;
#if defined(Q_OS_MACOS)
    return readMacBattery();
#else
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
#endif
}
