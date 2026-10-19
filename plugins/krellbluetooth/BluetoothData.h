#pragma once
#include <QJsonObject>
#include <QList>
#include <QString>

namespace Bluetooth {
struct Device {
    QString name, address;
    int battery = -1;
    QString batteryDetail;
};
struct Snapshot {
    QString status;
    QList<Device> devices;
};
Snapshot parseMac(const QByteArray &json);
Snapshot parseBluez(const QJsonObject &objects);
Snapshot parseFreeBsd(const QByteArray &output);
void mergeBleBatteries(Snapshot &snapshot, const QByteArray &json);
#ifdef Q_OS_MACOS
void supplementMacBatteries(Snapshot &snapshot);
void mergeMacAccessoryBatteries(Snapshot &snapshot, const QByteArray &json);
#endif
}
