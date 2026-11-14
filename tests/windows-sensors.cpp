#include "sysdep/WindowsSensors.h"
#include "sysdep/SensorStat.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

static void require(bool condition, const char *message) { if (!condition) qFatal("%s", message); }
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QByteArray valid = R"({"version":1,"status":"ok","readings":[{"chip":"CPU","label":"Package","type":"Temperature","value":42.5},{"chip":"board","label":"Fan","type":"Fan","value":0},{"chip":"board","label":"Vcore","type":"Voltage","value":1.2}]})";
    auto snapshot = WindowsSensors::parse(valid);
    require(snapshot.valid && snapshot.readings.size() == 3, "Sensor types lost");
    require(snapshot.readings[0].value == 42.5 && snapshot.readings[1].value == 0, "Sensor values altered");
    require(!WindowsSensors::parse("invalid").valid, "Malformed input accepted");
    require(!WindowsSensors::parse(QByteArray(262145, 'x')).valid, "Oversized input accepted");
    require(!WindowsSensors::parse(R"({"version":2,"status":"ok","readings":[]})").valid, "Unknown protocol accepted");
    auto object = QJsonDocument::fromJson(valid).object();
    auto rows = object["readings"].toArray();
    rows.append(rows[0]);
    auto bad = rows[0].toObject(); bad["value"] = QJsonValue(); rows.append(bad);
    bad["value"] = 1000000; rows.append(bad);
    bad["value"] = "42"; rows.append(bad);
    bad["value"] = 42; bad["type"] = "Load"; rows.append(bad);
    object["readings"] = rows;
    require(WindowsSensors::parse(QJsonDocument(object).toJson()).readings.size() == 3, "Invalid/duplicate sensors accepted");
    object["status"] = "virtual-machine"; object["readings"] = QJsonArray();
    snapshot = WindowsSensors::parse(QJsonDocument(object).toJson());
    require(snapshot.valid && snapshot.readings.isEmpty() && snapshot.status.contains("VM"), "VM falsely reports readings");
    SensorStat::setReadOverride([]() { SensorReading r; r.chip = "REMOTE"; r.value = 61; return QList<SensorReading>{r}; });
    require(SensorStat::read().size() == 1 && SensorStat::read()[0].chip == "REMOTE", "Remote sensors lost");
    SensorStat::setReadOverride([]() { return QList<SensorReading>{}; });
    require(SensorStat::read().isEmpty(), "Empty remote sensors fell back locally");
    require(SensorStat::windowsStatus() == "No remote sensors", "Remote status starts local backend");
    qInfo("PASS: sensor parsing, bounds, types, empty VM and remote precedence");
}
