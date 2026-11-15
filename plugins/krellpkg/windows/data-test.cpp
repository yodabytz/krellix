#include "PackageData.h"
#include <QCoreApplication>
#include <QJsonDocument>
static void check(bool value, const char *message) { if (!value) qFatal("%s", message); }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QJsonObject row{{"id", "Vendor.App"}, {"name", "Example App"}, {"installed", "1"}, {"available", "2"}};
    auto object = QJsonObject{{"status", "ok"}, {"packages", QJsonArray{row, row}}};
    auto snapshot = WindowsPackages::parse(object);
    check(snapshot.valid && snapshot.packages.size() == 1, "Deduplication failed");
    check(WindowsPackages::filtered(snapshot, "vendor.*", "").size() == 1, "ID wildcard failed");
    check(WindowsPackages::filtered(snapshot, "Example*", "").size() == 1, "Name wildcard failed");
    check(WindowsPackages::filtered(snapshot, "*", "VENDOR.*").isEmpty(), "Excludes must win");
    check(WindowsPackages::filtered(snapshot, "Other", "").isEmpty(), "Include mismatch");
    object["packages"] = QJsonArray{};
    check(WindowsPackages::parse(object).valid, "Zero updates rejected");
    object["packages"] = QJsonArray{1};
    check(!WindowsPackages::parse(object).valid, "Malformed row accepted");
    QJsonArray huge; for (int i = 0; i < 4097; ++i) huge.append(row);
    object["packages"] = huge;
    check(!WindowsPackages::parse(object).valid, "Oversized array accepted");
    check(!WindowsPackages::parse({}).valid, "Empty object accepted");
    check(WindowsPackages::parse({{"status", "error"}, {"detail", "offline"}}).valid, "Error rejected");
    qInfo("PASS package schema, bounds, deduplication and include/exclude filters");
}
