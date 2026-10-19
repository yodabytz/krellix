#include "BluetoothData.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace Bluetooth {
static int percentage(const QJsonValue &value)
{
    QString text = value.isDouble() ? QString::number(value.toInt(-1)) : value.toString();
    text.remove(QLatin1Char('%'));
    bool ok = false;
    const int n = text.trimmed().toInt(&ok);
    return ok && n >= 0 && n <= 100 ? n : -1;
}

Snapshot parseMac(const QByteArray &json)
{
    const auto doc = QJsonDocument::fromJson(json);
    if (!doc.isObject() || !doc.object().value(QStringLiteral("SPBluetoothDataType")).isArray())
        return {QStringLiteral("Invalid Bluetooth data"), {}};
    Snapshot result{QStringLiteral("No adapter"), {}};
    bool powered = false;
    for (const auto &entry : doc.object().value(QStringLiteral("SPBluetoothDataType")).toArray()) {
        const auto root = entry.toObject();
        const auto controller = root.value(QStringLiteral("controller_properties")).toObject();
        if (!controller.isEmpty()) {
            powered |= controller.value(QStringLiteral("controller_state")).toString() == QStringLiteral("attrib_on");
            result.status = powered ? QStringLiteral("No connections") : QStringLiteral("Bluetooth off");
        }
        for (const auto &item : root.value(QStringLiteral("device_connected")).toArray()) {
            const auto names = item.toObject();
            for (auto it = names.begin(); it != names.end(); ++it) {
                const auto props = it.value().toObject();
                Device device{it.key(), props.value(QStringLiteral("device_address")).toString(), -1, {}};
                QStringList levels;
                for (auto p = props.begin(); p != props.end(); ++p) {
                    if (!p.key().contains(QStringLiteral("battery"), Qt::CaseInsensitive)) continue;
                    const int level = percentage(p.value());
                    if (level < 0) continue;
                    // Keep left/right/case values separate; never invent a combined level.
                    if (p.key() == QStringLiteral("device_batteryLevelMain") || p.key() == QStringLiteral("device_batteryLevel"))
                        device.battery = level;
                    QString label = p.key();
                    label.remove(QStringLiteral("device_batteryLevel"));
                    levels << QStringLiteral("%1: %2%").arg(label.isEmpty() ? QStringLiteral("Battery") : label).arg(level);
                }
                device.batteryDetail = levels.join(QStringLiteral(", "));
                result.devices.append(device);
            }
        }
    }
    return result;
}

Snapshot parseBluez(const QJsonObject &objects)
{
    Snapshot result{QStringLiteral("No adapter"), {}};
    bool adapter = false, powered = false;
    for (auto it = objects.begin(); it != objects.end(); ++it) {
        const auto interfaces = it.value().toObject();
        if (interfaces.contains(QStringLiteral("org.bluez.Adapter1"))) {
            adapter = true;
            powered |= interfaces.value(QStringLiteral("org.bluez.Adapter1")).toObject().value(QStringLiteral("Powered")).toBool();
        }
        const auto props = interfaces.value(QStringLiteral("org.bluez.Device1")).toObject();
        if (!props.value(QStringLiteral("Connected")).toBool()) continue;
        Device device;
        device.address = props.value(QStringLiteral("Address")).toString();
        device.name = props.value(QStringLiteral("Alias")).toString(props.value(QStringLiteral("Name")).toString(device.address));
        device.battery = percentage(interfaces.value(QStringLiteral("org.bluez.Battery1")).toObject().value(QStringLiteral("Percentage")));
        result.devices.append(device);
    }
    if (adapter) result.status = powered ? QStringLiteral("No connections") : QStringLiteral("Bluetooth off");
    return result;
}

Snapshot parseFreeBsd(const QByteArray &output)
{
    const QString text = QString::fromUtf8(output);
    if (!text.contains(QStringLiteral("Remote BD_ADDR")))
        return {QStringLiteral("Invalid Bluetooth data"), {}};
    Snapshot result{QStringLiteral("No connections"), {}};
    const QRegularExpression row(QStringLiteral("^([0-9a-fA-F:]{17})\\s+.*\\sOPEN\\s*$"));
    for (const auto &line : text.split(QLatin1Char('\n'))) {
        const auto match = row.match(line.trimmed());
        if (!match.hasMatch()) continue;
        const QString address = match.captured(1);
        bool exists = false;
        for (const auto &device : result.devices) exists |= device.address == address;
        if (!exists) result.devices.append({address, address, -1, {}});
    }
    return result;
}

void mergeBleBatteries(Snapshot &snapshot, const QByteArray &json)
{
    const auto readings = QJsonDocument::fromJson(json).array();
    // CoreBluetooth exposes a UUID, not a hardware address. Match only names
    // unique in BOTH connected-device lists; ambiguous devices stay unknown.
    for (auto &device : snapshot.devices) {
        if (device.battery >= 0 || device.name.isEmpty()) continue;
        int devicesWithName = 0, readingsWithName = 0, battery = -1;
        for (const auto &candidate : snapshot.devices)
            if (candidate.name == device.name) ++devicesWithName;
        for (const auto &reading : readings) {
            const auto object = reading.toObject();
            if (object.value(QStringLiteral("name")).toString() != device.name) continue;
            ++readingsWithName;
            battery = percentage(object.value(QStringLiteral("battery")));
        }
        if (devicesWithName == 1 && readingsWithName == 1 && battery >= 0) {
            device.battery = battery;
            device.batteryDetail = QStringLiteral("Battery: %1% (Bluetooth Battery Service)").arg(battery);
        }
    }
}
}
