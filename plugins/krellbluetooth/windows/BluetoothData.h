#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QList>
#include <QMap>

namespace WindowsBluetooth {
struct Device { QString id, name, address, detail; int battery = -1; };
struct Snapshot { QString status, detail; QList<Device> devices; bool valid = false; };
inline QString clean(QString text)
{
    for (auto &c : text)
        if (c.isSpace() || c.category() == QChar::Other_Control || c.category() == QChar::Other_Format)
            c = QLatin1Char(' ');
    return text.trimmed();
}
inline Snapshot parse(const QJsonObject &object)
{
    Snapshot result;
    const QString state = object.value("state").toString();
    const QMap<QString, QString> states{{"ok", "No connected devices"}, {"no_adapter", "No Bluetooth adapter"},
        {"radio_off", "Bluetooth radio off"}, {"permission", "Bluetooth access denied"},
        {"timeout", "Bluetooth query timed out"}, {"error", "Bluetooth unavailable"}};
    if (!states.contains(state) || !object.value("detail").isString()
        || object.value("detail").toString().size() > 2048 || !object.value("devices").isArray()) return result;
    const auto rows = object.value("devices").toArray();
    if (rows.size() > 64 || (state != "ok" && !rows.isEmpty())) return result;
    QSet<QString> ids;
    for (const auto &entry : rows) {
        if (!entry.isObject()) return {};
        const auto row = entry.toObject();
        for (const auto *key : {"id", "name", "address", "detail"})
            if (!row.value(key).isString() || row.value(key).toString().size() > 2048) return {};
        const auto battery = row.value("battery");
        if (!row.value("connected").isBool() || !battery.isDouble() || battery.toDouble() != battery.toInt(-999)
            || battery.toInt() < -1 || battery.toInt() > 100) return {};
        const QString id = row.value("id").toString();
        if (id.isEmpty() || ids.contains(id)) return {};
        ids.insert(id);
        if (!row.value("connected").toBool()) continue;
        Device device{id, clean(row.value("name").toString()), clean(row.value("address").toString()),
            clean(row.value("detail").toString()), battery.toInt()};
        if (device.name.isEmpty()) device.name = QStringLiteral("Unnamed device");
        result.devices.append(device);
    }
    result.status = states.value(state);
    result.detail = clean(object.value("detail").toString());
    result.valid = true;
    return result;
}
}
