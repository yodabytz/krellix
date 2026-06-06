#include "SensorStat.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#if defined(Q_OS_MACOS) || defined(Q_OS_DARWIN)
#include <IOKit/IOKitLib.h>
#include <cstring>
#endif

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

#if defined(Q_OS_MACOS) || defined(Q_OS_DARWIN)
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

bool appendTemperature(QList<SensorReading> &out, const QString &label, double value)
{
    if (value <= 0 || value > 130)
        return false;

    for (const SensorReading &r : out) {
        if (r.type == SensorReading::Temp
            && r.label.compare(label, Qt::CaseInsensitive) == 0) {
            return false;
        }
    }

    SensorReading r;
    r.chip = QStringLiteral("macOS");
    r.label = label;
    r.value = value;
    r.type = SensorReading::Temp;
    out.append(r);
    return true;
}

void appendFirstTemperatureMatch(QList<SensorReading> &out,
                                 const QString &text,
                                 const QString &label)
{
    static const QRegularExpression tempRx(QStringLiteral("(-?\\d+(?:\\.\\d+)?)"));
    const auto match = tempRx.match(text);
    if (match.hasMatch())
        appendTemperature(out, label, match.captured(1).toDouble());
}

void appendIstatsTemperatures(QList<SensorReading> &out)
{
    const QString cpuTemp = runProgram(QStringLiteral("istats"),
                                       {QStringLiteral("cpu"),
                                        QStringLiteral("temp"),
                                        QStringLiteral("--value-only")});
    appendFirstTemperatureMatch(out, cpuTemp, QStringLiteral("CPU"));

    const QString scan = runProgram(QStringLiteral("istats"),
                                    {QStringLiteral("scan")},
                                    1800);
    static const QRegularExpression lineRx(
        QStringLiteral("(?im)^\\s*([^\\n:]+?)\\s*:\\s*(-?\\d+(?:\\.\\d+)?)\\s*(?:C|°C|celsius)?"));
    auto it = lineRx.globalMatch(scan);
    while (it.hasNext()) {
        const auto match = it.next();
        QString label = match.captured(1).trimmed();
        if (label.isEmpty())
            label = QStringLiteral("Temp");
        appendTemperature(out, label.left(24), match.captured(2).toDouble());
    }
}

void appendSmcTemperature(QList<SensorReading> &out,
                          const QString &key,
                          const QString &label)
{
    const QString smc = runProgram(QStringLiteral("smc"),
                                   {QStringLiteral("-k"), key, QStringLiteral("-r")});
    appendFirstTemperatureMatch(out, smc, label);
}

void appendPowermetricsTemperatures(QList<SensorReading> &out)
{
    const QString output = runProgram(QStringLiteral("powermetrics"),
                                      {QStringLiteral("--samplers"),
                                       QStringLiteral("smc"),
                                       QStringLiteral("-n"),
                                       QStringLiteral("1"),
                                       QStringLiteral("-i"),
                                       QStringLiteral("1")},
                                      3500);
    if (output.isEmpty())
        return;

    static const QRegularExpression lineRx(
        QStringLiteral("(?im)^\\s*([^\\n:]+temperature[^\\n:]*|CPU die|GPU die)\\s*:?\\s*(-?\\d+(?:\\.\\d+)?)\\s*(?:C|°C)?"));
    auto it = lineRx.globalMatch(output);
    while (it.hasNext()) {
        const auto match = it.next();
        QString label = match.captured(1).trimmed();
        label.replace(QRegularExpression(QStringLiteral("\\s+temperature\\b"),
                                         QRegularExpression::CaseInsensitiveOption),
                      QString());
        appendTemperature(out, label.left(24), match.captured(2).toDouble());
    }
}

using SmcKeyDataBytes = unsigned char[32];

struct SmcKeyDataVersion {
    unsigned char major = 0;
    unsigned char minor = 0;
    unsigned char build = 0;
    unsigned char reserved = 0;
    unsigned short release = 0;
};

struct SmcKeyDataPLimitData {
    unsigned short version = 0;
    unsigned short length = 0;
    unsigned int cpuPLimit = 0;
    unsigned int gpuPLimit = 0;
    unsigned int memPLimit = 0;
};

struct SmcKeyDataKeyInfo {
    unsigned int dataSize = 0;
    unsigned int dataType = 0;
    unsigned char dataAttributes = 0;
};

struct SmcKeyData {
    unsigned int key = 0;
    SmcKeyDataVersion vers;
    SmcKeyDataPLimitData pLimitData;
    SmcKeyDataKeyInfo keyInfo;
    unsigned char result = 0;
    unsigned char status = 0;
    unsigned char data8 = 0;
    unsigned int data32 = 0;
    SmcKeyDataBytes bytes = {};
};

struct SmcVal {
    unsigned int dataSize = 0;
    unsigned int dataType = 0;
    SmcKeyDataBytes bytes = {};
};

unsigned int smcFourCharCode(const char *key)
{
    return (static_cast<unsigned int>(key[0]) << 24)
         | (static_cast<unsigned int>(key[1]) << 16)
         | (static_cast<unsigned int>(key[2]) << 8)
         |  static_cast<unsigned int>(key[3]);
}

double smcSp78ToCelsius(const unsigned char *bytes)
{
    const qint16 raw = static_cast<qint16>((static_cast<unsigned short>(bytes[0]) << 8)
                                           | static_cast<unsigned short>(bytes[1]));
    return static_cast<double>(raw) / 256.0;
}

bool smcCall(io_connect_t conn, int index, const SmcKeyData &input, SmcKeyData &output)
{
    size_t inSize = sizeof(SmcKeyData);
    size_t outSize = sizeof(SmcKeyData);
    return IOConnectCallStructMethod(conn, static_cast<uint32_t>(index),
                                     &input, inSize, &output, &outSize) == KERN_SUCCESS;
}

bool smcReadKeyInfo(io_connect_t conn, unsigned int key, SmcKeyDataKeyInfo &info)
{
    SmcKeyData input;
    SmcKeyData output;
    input.key = key;
    input.data8 = 9; // kSMCGetKeyInfo
    if (!smcCall(conn, 2, input, output))
        return false;
    info = output.keyInfo;
    return output.result == 0 && info.dataSize > 0;
}

bool smcReadKey(io_connect_t conn, const char *keyName, SmcVal &val)
{
    const unsigned int key = smcFourCharCode(keyName);
    SmcKeyDataKeyInfo info;
    if (!smcReadKeyInfo(conn, key, info))
        return false;

    SmcKeyData input;
    SmcKeyData output;
    input.key = key;
    input.keyInfo.dataSize = info.dataSize;
    input.data8 = 5; // kSMCReadKey
    if (!smcCall(conn, 2, input, output) || output.result != 0)
        return false;

    val.dataSize = info.dataSize;
    val.dataType = info.dataType;
    std::memcpy(val.bytes, output.bytes, sizeof(val.bytes));
    return true;
}

void appendNativeSmcTemperatures(QList<SensorReading> &out)
{
    io_service_t service = IOServiceGetMatchingService(kIOMainPortDefault,
                                                       IOServiceMatching("AppleSMC"));
    if (!service)
        return;

    io_connect_t conn = IO_OBJECT_NULL;
    const kern_return_t openResult = IOServiceOpen(service, mach_task_self(), 0, &conn);
    IOObjectRelease(service);
    if (openResult != KERN_SUCCESS || conn == IO_OBJECT_NULL)
        return;

    struct KeyLabel {
        const char *key;
        const char *label;
    };
    const KeyLabel keys[] = {
        {"TC0P", "CPU Proximity"},
        {"TC0D", "CPU Diode"},
        {"TC0E", "CPU"},
        {"TC0F", "CPU"},
        {"TC0H", "CPU Heatsink"},
        {"TG0P", "GPU Proximity"},
        {"TG0D", "GPU Diode"},
        {"TB0T", "Battery"},
        {"TW0P", "Wireless"},
    };

    for (const KeyLabel &entry : keys) {
        SmcVal val;
        if (!smcReadKey(conn, entry.key, val) || val.dataSize < 2)
            continue;
        const double tempC = smcSp78ToCelsius(val.bytes);
        appendTemperature(out, QString::fromLatin1(entry.label), tempC);
    }

    IOServiceClose(conn);
}

QList<SensorReading> readDarwinSensors()
{
    QList<SensorReading> out;

    appendNativeSmcTemperatures(out);

    // Homebrew's osx-cpu-temp reads the SMC without requiring root. Prefer it
    // when present because it is an actual Celsius temperature.
    const QString cpuTemp = runProgram(QStringLiteral("osx-cpu-temp"), {});
    appendFirstTemperatureMatch(out, cpuTemp, QStringLiteral("CPU"));
    appendIstatsTemperatures(out);
    appendSmcTemperature(out, QStringLiteral("TC0P"), QStringLiteral("CPU Proximity"));
    appendSmcTemperature(out, QStringLiteral("TC0D"), QStringLiteral("CPU Diode"));
    appendSmcTemperature(out, QStringLiteral("TG0P"), QStringLiteral("GPU Proximity"));
    appendPowermetricsTemperatures(out);

    // Stock macOS exposes thermal pressure levels (0-100) via sysctl. These
    // are not temperatures, so show them as percentages unless osx-cpu-temp
    // supplied an actual Celsius CPU reading above.
    const QString sysctl = runProgram(QStringLiteral("sysctl"),
                                      {QStringLiteral("-n"),
                                       QStringLiteral("machdep.xcpm.cpu_thermal_level"),
                                       QStringLiteral("machdep.xcpm.gpu_thermal_level"),
                                       QStringLiteral("machdep.xcpm.io_thermal_level")});
    const QStringList labels{
        QStringLiteral("CPU"),
        QStringLiteral("GPU"),
        QStringLiteral("I/O"),
    };
    const QStringList values = sysctl.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (int i = 0; i < values.size() && i < labels.size(); ++i) {
        bool ok = false;
        const double value = values.at(i).trimmed().toDouble(&ok);
        if (!ok) continue;
        SensorReading r;
        r.chip  = QStringLiteral("macOS");
        r.label = labels.at(i);
        r.value = value;
        r.type  = SensorReading::Percent;
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
#if defined(Q_OS_MACOS) || defined(Q_OS_DARWIN)
    if (out.isEmpty())
        out = readDarwinSensors();
#endif
    return out;
}
