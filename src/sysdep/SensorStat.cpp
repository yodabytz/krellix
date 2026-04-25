#include "SensorStat.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRegularExpression>

Q_LOGGING_CATEGORY(lcSensor, "krellix.sysdep.sensor")

namespace {

SensorStat::ReadFn g_readOverride = nullptr;

QString readTrimmed(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    return QString::fromLatin1(f.read(256)).trimmed();
}

} // namespace

void SensorStat::setReadOverride(SensorStat::ReadFn fn) { g_readOverride = fn; }

QList<SensorReading> SensorStat::read()
{
    if (g_readOverride) return g_readOverride();

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

            SensorReading r;
            r.chip  = chipName;
            r.label = label;
            r.value = static_cast<double>(milliC) / 1000.0;
            r.type  = SensorReading::Temp;
            out.append(r);
        }
    }
    return out;
}
