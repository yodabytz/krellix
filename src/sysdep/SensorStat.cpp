#include "SensorStat.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcSensor, "krellix.sysdep.sensor")

namespace {

SensorStat::ReadFn g_readOverride = nullptr;

QString readTrimmed(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    return QString::fromLatin1(f.read(256)).trimmed();
}

QList<SensorReading> readLinuxHwmon()
{
    QList<SensorReading> out;

    QDir hwmon(QStringLiteral("/sys/class/hwmon"));
    if (!hwmon.exists()) return out;

    const QFileInfoList chips =
        hwmon.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &chipFi : chips) {
        const QString chipDir  = chipFi.absoluteFilePath();
        const QString chipName = readTrimmed(chipDir + QStringLiteral("/name"));
        if (chipName.isEmpty()) continue;

        // tempN_input is in millidegrees Celsius; tempN_label gives the
        // human-friendly name when present.
        QDir d(chipDir);
        const QStringList inputs = d.entryList(
            QStringList{QStringLiteral("temp*_input")},
            QDir::Files);
        for (const QString &fname : inputs) {
            // Extract N from tempN_input.
            static const QRegularExpression rx(QStringLiteral("^temp(\\d+)_input$"));
            const auto m = rx.match(fname);
            if (!m.hasMatch()) continue;
            const QString n = m.captured(1);

            const QString rawValue = readTrimmed(chipDir + QStringLiteral("/") + fname);
            bool ok = false;
            const qint64 milliC = rawValue.toLongLong(&ok);
            if (!ok) continue;

            QString label = readTrimmed(chipDir + QStringLiteral("/temp")
                                        + n + QStringLiteral("_label"));
            if (label.isEmpty()) label = QStringLiteral("temp") + n;

            auto readMilliC = [&](const QString &suffix) -> double {
                const QString raw = readTrimmed(chipDir + QStringLiteral("/temp") + n + suffix);
                if (raw.isEmpty()) return 0;
                bool ok = false;
                const qint64 v = raw.toLongLong(&ok);
                return (ok && v > 0) ? static_cast<double>(v) / 1000.0 : 0;
            };

            SensorReading r;
            r.chip  = chipName;
            r.label = label;
            r.value = static_cast<double>(milliC) / 1000.0;
            r.maxC  = readMilliC(QStringLiteral("_max"));
            r.critC = readMilliC(QStringLiteral("_crit"));
            r.type  = SensorReading::Temp;
            out.append(r);
        }
    }
    return out;
}

#if defined(Q_OS_DARWIN)
QString runProgram(const QString &program, const QStringList &args, int timeoutMs = 1200)
{
    const QString exe = QStandardPaths::findExecutable(program);
    if (exe.isEmpty()) return QString();

    QProcess p;
    p.setProgram(exe);
    p.setArguments(args);
    p.start();
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(200);
        return QString();
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return QString();
    return QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed();
}

QList<SensorReading> readDarwinSensors()
{
    QList<SensorReading> out;

    // Homebrew's osx-cpu-temp reads the SMC without requiring root. Prefer it
    // when present because it is an actual Celsius temperature.
    const QString cpuTemp = runProgram(QStringLiteral("osx-cpu-temp"), {});
    static const QRegularExpression tempRx(QStringLiteral("(-?\\d+(?:\\.\\d+)?)"));
    const auto tempMatch = tempRx.match(cpuTemp);
    if (tempMatch.hasMatch()) {
        SensorReading r;
        r.chip = QStringLiteral("macOS");
        r.label = QStringLiteral("CPU");
        r.value = tempMatch.captured(1).toDouble();
        r.type = SensorReading::Temp;
        out.append(r);
    }

    // Stock macOS exposes thermal pressure levels through sysctl even when
    // SMC temperatures are unavailable to regular users. These are percentages,
    // not degrees, so keep the unit explicit in the UI.
    const QString sysctl = runProgram(QStringLiteral("sysctl"),
                                      {QStringLiteral("-n"),
                                       QStringLiteral("machdep.xcpm.cpu_thermal_level"),
                                       QStringLiteral("machdep.xcpm.gpu_thermal_level"),
                                       QStringLiteral("machdep.xcpm.io_thermal_level")});
    const QStringList labels{
        QStringLiteral("CPU thermal"),
        QStringLiteral("GPU thermal"),
        QStringLiteral("I/O thermal"),
    };
    const QStringList values = sysctl.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (int i = 0; i < values.size() && i < labels.size(); ++i) {
        bool ok = false;
        const double value = values.at(i).trimmed().toDouble(&ok);
        if (!ok) continue;
        SensorReading r;
        r.chip = QStringLiteral("macOS");
        r.label = labels.at(i);
        r.value = value;
        r.type = SensorReading::Percent;
        out.append(r);
    }

    return out;
}
#endif

} // namespace

void SensorStat::setReadOverride(SensorStat::ReadFn fn) { g_readOverride = fn; }

QList<SensorReading> SensorStat::read()
{
    if (g_readOverride) return g_readOverride();

    QList<SensorReading> out = readLinuxHwmon();
#if defined(Q_OS_DARWIN)
    if (out.isEmpty())
        out = readDarwinSensors();
#endif
    return out;
}
