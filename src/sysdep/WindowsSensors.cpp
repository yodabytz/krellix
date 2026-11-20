#include "WindowsSensors.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QTimer>
#include <cmath>
#include <windows.h>

namespace WindowsSensors {
Snapshot parse(const QByteArray &line)
{
    Snapshot result;
    if (line.size() > 262144) return result;
    const auto document = QJsonDocument::fromJson(line);
    if (!document.isObject()) return result;
    const auto object = document.object();
    if (object.value(QStringLiteral("version")).toInt() != 1 || !object.value(QStringLiteral("readings")).isArray()) return result;
    const auto rows = object.value(QStringLiteral("readings")).toArray();
    if (rows.size() > 512) return result;
    const QString state = object.value(QStringLiteral("status")).toString();
    if (state == QStringLiteral("ok")) result.status = QStringLiteral("No supported sensors");
    else if (state == QStringLiteral("virtual-machine")) result.status = QStringLiteral("VM exposes no sensors");
    else if (state == QStringLiteral("unavailable")) result.status = QStringLiteral("Sensors unavailable");
    else if (state == QStringLiteral("error")) result.status = QStringLiteral("Sensor reader failed");
    else return result;
    QSet<QString> seen;
    for (const auto &item : rows) {
        const auto row = item.toObject();
        if (!row.value(QStringLiteral("value")).isDouble()) continue;
        SensorReading reading;
        reading.value = row.value(QStringLiteral("value")).toDouble();
        if (!std::isfinite(reading.value)) continue;
        const QString type = row.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("Temperature")) {
            reading.type = SensorReading::Temp;
            if (reading.value < -100 || reading.value > 250) continue;
        } else if (type == QStringLiteral("Fan")) {
            reading.type = SensorReading::Fan;
            if (reading.value < 0 || reading.value > 100000) continue;
        } else if (type == QStringLiteral("Voltage")) {
            reading.type = SensorReading::Voltage;
            if (reading.value < -100 || reading.value > 100) continue;
        } else continue;
        reading.chip = row.value(QStringLiteral("chip")).toString();
        reading.label = row.value(QStringLiteral("label")).toString();
        if (reading.chip.isEmpty() || reading.label.isEmpty()
            || reading.chip.size() > 128 || reading.label.size() > 128) continue;
        const QString key = reading.chip + QChar(0) + reading.label + QChar(0) + type;
        if (seen.contains(key)) continue;
        seen.insert(key);
        result.readings.append(reading);
    }
    result.valid = true;
    return result;
}

namespace {
class Reader final : public QObject {
public:
    explicit Reader(QObject *parent) : QObject(parent)
    {
        setObjectName(QStringLiteral("windows-sensor-reader"));
        process.setProcessChannelMode(QProcess::SeparateChannels);
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
            args->flags |= CREATE_NO_WINDOW;
        });
        connect(&process, &QProcess::readyReadStandardOutput, this, [this] {
            buffer += process.readAllStandardOutput();
            if (buffer.size() > 262144) { fail(); return; }
            qsizetype end;
            while ((end = buffer.indexOf('\n')) >= 0) {
                auto snapshot = parse(buffer.left(end));
                buffer.remove(0, end + 1);
                if (!snapshot.valid) { fail(); return; }
                cached = snapshot.readings;
                message = snapshot.status;
                deadline.start(20000);
            }
        });
        connect(&process, &QProcess::readyReadStandardError, this, [this] {
            process.readAllStandardError();
        });
        connect(&process, &QProcess::errorOccurred, this, [this] { fail(); });
        connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this] { fail(); });
        deadline.setSingleShot(true);
        connect(&deadline, &QTimer::timeout, this, [this] { fail(); });
        connect(qApp, &QCoreApplication::aboutToQuit, this, &Reader::stop);
    }
    ~Reader() override { stop(); }
    void stop()
    {
        if (stopping) return;
        stopping = true;
        deadline.stop();
        // A terminating helper can still deliver output and exit signals.
        disconnect(&process, nullptr, this, nullptr);
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(1000);
        }
    }
    void request()
    {
        if (stopping || process.state() != QProcess::NotRunning) return;
        if (retry.isValid() && retry.elapsed() < 60000) return;
        retry.start();
        const QString helper = QCoreApplication::applicationDirPath() + QStringLiteral("/sensors/krellix-sensors.exe");
        if (!QFileInfo::exists(helper)) {
            message = QStringLiteral("Sensor helper missing");
            return;
        }
        buffer.clear();
        message = QStringLiteral("Detecting sensors...");
        process.setWorkingDirectory(QFileInfo(helper).absolutePath());
        deadline.start(45000);
        process.start(helper, {QString::number(QCoreApplication::applicationPid())});
    }
    void fail()
    {
        deadline.stop();
        cached.clear();
        message = QStringLiteral("Sensor reader unavailable");
        if (process.state() != QProcess::NotRunning) process.kill();
    }
    QProcess process;
    QTimer deadline;
    QElapsedTimer retry;
    QByteArray buffer;
    QList<SensorReading> cached;
    QString message = QStringLiteral("Detecting sensors...");
    bool stopping = false;
};
Reader *reader()
{
    static QPointer<Reader> instance;
    if (!instance && qApp) instance = new Reader(qApp);
    return instance;
}
}
QList<SensorReading> read()
{
    auto *r = reader();
    if (!r) return {};
    r->request();
    return r->cached;
}
QString status() { auto *r = reader(); return r ? r->message : QString(); }
}
