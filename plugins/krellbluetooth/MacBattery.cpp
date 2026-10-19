#include "BluetoothData.h"
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <QJsonDocument>
#include <QJsonArray>

namespace Bluetooth {
void mergeMacAccessoryBatteries(Snapshot &snapshot, const QByteArray &json)
{
    const auto records = QJsonDocument::fromJson(json).array();
    QJsonArray ble;
    for (const auto &record : records) {
        const auto object = record.toObject();
        if (object.value(QStringLiteral("source")).toString() == QStringLiteral("ble")) {
            ble.append(record);
            continue;
        }
        const QString address = object.value(QStringLiteral("address")).toString().replace(QLatin1Char('-'), QLatin1Char(':'));
        const int battery = object.value(QStringLiteral("battery")).toInt(-1);
        if (address.isEmpty() || battery < 0 || battery > 100) continue;
        for (auto &device : snapshot.devices) {
            if (device.battery < 0 && device.address.compare(address, Qt::CaseInsensitive) == 0) {
                device.battery = battery;
                device.batteryDetail = QStringLiteral("Battery: %1% (macOS accessory power)").arg(battery);
            }
        }
    }
    mergeBleBatteries(snapshot, QJsonDocument(ble).toJson(QJsonDocument::Compact));
}
void supplementMacBatteries(Snapshot &snapshot)
{
    // Apple HID devices often expose their battery in IORegistry rather than
    // System Information. Match only already-connected devices by address.
    const char *classes[] = {"AppleDeviceManagementHIDEventService", "AppleBluetoothHIDKeyboard", "AppleBluetoothHIDMouse"};
    for (const char *className : classes) {
        io_iterator_t iterator = IO_OBJECT_NULL;
        if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching(className), &iterator) != KERN_SUCCESS) continue;
        io_object_t service;
        while ((service = IOIteratorNext(iterator))) {
            CFTypeRef address = IORegistryEntryCreateCFProperty(service, CFSTR("DeviceAddress"), kCFAllocatorDefault, 0);
            CFTypeRef battery = IORegistryEntryCreateCFProperty(service, CFSTR("BatteryPercent"), kCFAllocatorDefault, 0);
            int percent = -1;
            char buffer[128] = {};
            if (address && battery && CFGetTypeID(address) == CFStringGetTypeID() && CFGetTypeID(battery) == CFNumberGetTypeID()
                && CFStringGetCString(static_cast<CFStringRef>(address), buffer, sizeof(buffer), kCFStringEncodingUTF8)
                && CFNumberGetValue(static_cast<CFNumberRef>(battery), kCFNumberIntType, &percent) && percent >= 0 && percent <= 100) {
                const QString normalized = QString::fromUtf8(buffer).replace(QLatin1Char('-'), QLatin1Char(':'));
                for (auto &device : snapshot.devices) {
                    if (device.battery < 0 && device.address.compare(normalized, Qt::CaseInsensitive) == 0)
                        device.battery = percent;
                }
            }
            if (address) CFRelease(address);
            if (battery) CFRelease(battery);
            IOObjectRelease(service);
        }
        IOObjectRelease(iterator);
    }
}
}
