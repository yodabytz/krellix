#include "BluetoothData.h"
#include <QCoreApplication>
static void check(bool value, const char *message) { if (!value) qFatal("%s", message); }
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QJsonObject device{{"id", "one"}, {"name", "<b>Mouse</b>"}, {"address", "01:02:03:04:05:06"},
        {"connected", true}, {"battery", 0}, {"detail", "test"}};
    QJsonObject root{{"state", "ok"}, {"detail", "test"}, {"devices", QJsonArray{device}}};
    auto result = WindowsBluetooth::parse(root);
    check(result.valid && result.devices.size() == 1 && result.devices[0].battery == 0, "Zero battery lost");
    for (int value : {-1, 100}) {
        device["battery"] = value; root["devices"] = QJsonArray{device};
        check(WindowsBluetooth::parse(root).devices[0].battery == value, "Valid battery lost");
    }
    for (double value : {-2., 101., 12.5}) {
        device["battery"] = value; root["devices"] = QJsonArray{device};
        check(!WindowsBluetooth::parse(root).valid, "Invalid battery accepted");
    }
    device["battery"] = -1; device["connected"] = false; root["devices"] = QJsonArray{device};
    check(WindowsBluetooth::parse(root).devices.isEmpty(), "Disconnected device shown");
    device["connected"] = true; root["devices"] = QJsonArray{device, device};
    check(!WindowsBluetooth::parse(root).valid, "Duplicate identity accepted");
    QJsonArray many; for (int i = 0; i < 65; ++i) many.append(device); root["devices"] = many;
    check(!WindowsBluetooth::parse(root).valid, "Oversized device list accepted");
    root["devices"] = QJsonArray{}; root["state"] = "permission";
    check(WindowsBluetooth::parse(root).valid, "Permission state rejected");
    root["devices"] = QJsonArray{device};
    check(!WindowsBluetooth::parse(root).valid, "Error state retained devices");
    check(!WindowsBluetooth::parse({}).valid, "Malformed schema accepted");
    check(WindowsBluetooth::clean(QStringLiteral("a\nb\u202ec")) == "a b c", "Unsafe device name");
    qInfo("PASS battery bounds/unknown/zero, duplicates, disconnected filtering, schema and name sanitation");
}
