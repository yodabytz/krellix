#include "HealthData.h"
#include <QCoreApplication>
static void check(bool value, const char *message) { if (!value) qFatal("%s", message); }
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    check(!parseHealth({}).error.isEmpty(), "Missing health reported healthy");
    HealthData data;
    spaceFinding(data, QStringLiteral("C:"), 100, 20);
    check(data.findings.isEmpty(), "Healthy volume flagged");
    spaceFinding(data, QStringLiteral("D:"), 100, 15);
    spaceFinding(data, QStringLiteral("E:"), 100, 5);
    check(data.warnings == 1 && data.critical == 1, "Disk thresholds wrong");
    check(parseHealth(healthJson(data)).error.isEmpty(), "Valid native data rejected");
    auto bad = healthJson(data); bad.insert("critical", 0);
    check(!parseHealth(bad).error.isEmpty(), "Contradictory counts accepted");
    bad = healthJson(data); bad.insert("findings", QJsonArray{QJsonObject{{"severity","nonsense"},{"title","test"}}});
    check(!parseHealth(bad).error.isEmpty(), "Unknown severity accepted");
    HealthData empty;
    check(parseHealth(healthJson(empty)).error.isEmpty(), "Explicit clear health rejected");
    spaceFinding(empty, QStringLiteral("X:"), 0, 0);
    check(!empty.findings.isEmpty(), "Unknown disk reported healthy");
    qInfo("PASS health schema, explicit unknown states, bounded severity and disk thresholds");
}
