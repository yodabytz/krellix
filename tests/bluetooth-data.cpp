#include "BluetoothData.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QDebug>

static int failures = 0;
static void check(bool ok, const char *description) {
    if (!ok) { qCritical() << "FAIL:" << description; ++failures; }
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto mac = Bluetooth::parseMac(R"({"SPBluetoothDataType":[{
      "controller_properties":{"controller_state":"attrib_on"},
      "device_connected":[{"Long Mouse Name":{"device_address":"AA:BB:CC:DD:EE:FF","device_batteryLevelMain":"0%"}},
        {"Headphones":{"device_batteryLevelLeft":"84%","device_batteryLevelRight":"73%"}},
        {"Keyboard":{"device_batteryLevelMain":"unknown"}}],
      "device_not_connected":[{"Old mouse":{"device_batteryLevelMain":"99%"}}]}]})");
    check(mac.devices.size() == 3, "Only connected Mac devices");
    check(mac.devices.value(0).battery == 0, "Zero battery is valid");
    check(mac.devices.value(1).battery == -1 && mac.devices.value(1).batteryDetail.contains(QStringLiteral("73%")), "Separate earbud levels retained without invented average");
    check(mac.devices.value(2).battery == -1, "Unknown battery stays unknown");
    check(Bluetooth::parseMac("broken").status == QStringLiteral("Invalid Bluetooth data"), "Malformed Mac output");
    check(Bluetooth::parseMac(R"({"SPBluetoothDataType":[{"controller_properties":{"controller_state":"attrib_off"}}]})").status == QStringLiteral("Bluetooth off"), "Powered off Mac");
    const auto objects = QJsonDocument::fromJson(R"({
      "/org/bluez/hci0":{"org.bluez.Adapter1":{"Powered":true}},
      "/org/bluez/hci0/a":{"org.bluez.Device1":{"Connected":true,"Alias":"Mouse","Address":"A"},"org.bluez.Battery1":{"Percentage":42}},
      "/org/bluez/hci0/b":{"org.bluez.Device1":{"Connected":true,"Name":"Keyboard","Address":"B"}},
      "/org/bluez/hci0/c":{"org.bluez.Device1":{"Connected":false,"Alias":"Old"}},
      "/org/bluez/hci0/d":{"org.bluez.Device1":{"Connected":true,"Address":"D"},"org.bluez.Battery1":{"Percentage":255}}
    })").object();
    const auto linux = Bluetooth::parseBluez(objects);
    check(linux.devices.size() == 3, "Only connected BlueZ devices");
    check(linux.devices.value(0).battery == 42, "BlueZ percentage");
    check(linux.devices.value(1).name == QStringLiteral("Keyboard") && linux.devices.value(1).battery == -1, "BlueZ name fallback and unavailable battery");
    check(linux.devices.value(2).battery == -1, "Invalid battery not displayed");
    check(Bluetooth::parseBluez({}).status == QStringLiteral("No adapter"), "No adapter");
    const auto bsd = Bluetooth::parseFreeBsd("Remote BD_ADDR    Handle Type Mode Role Encrypt Pending Queue State\n00:80:37:29:19:a4 41 ACL 0 MAST NONE 0 0 OPEN\n00:80:37:29:19:a4 42 SCO 0 MAST NONE 0 0 OPEN\n00:80:37:29:19:a5 43 ACL 0 MAST NONE 0 0 W4_CONN_COMPLETE\n");
    check(bsd.devices.size() == 1 && bsd.devices.first().battery == -1, "FreeBSD established only, deduplicated, no fake battery");
    check(Bluetooth::parseFreeBsd("permission denied").status == QStringLiteral("Invalid Bluetooth data"), "FreeBSD failure not empty success");
    if (argc == 2) {
        QFile file(QString::fromLocal8Bit(argv[1]));
        check(file.open(QIODevice::ReadOnly), "Live Mac capture opens");
        auto live = Bluetooth::parseMac(file.readAll());
#ifdef Q_OS_MACOS
        Bluetooth::supplementMacBatteries(live);
        int batteryCount = 0;
        for (const auto &device : live.devices) if (device.battery >= 0) ++batteryCount;
        qInfo() << "Live Mac devices reporting battery:" << batteryCount;
#endif
        qInfo() << "Live Mac connected device count:" << live.devices.size();
        check(!live.devices.isEmpty(), "Live hardware capture contains connected devices");
    }
    qInfo() << "Bluetooth parser failures:" << failures;
    return failures ? 1 : 0;
}
