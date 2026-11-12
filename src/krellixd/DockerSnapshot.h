#pragma once

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

inline QJsonObject dockerSnapshot()
{
    const QString path = QStringLiteral("/run/krellixd-docker/snapshot.json");
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink() || info.ownerId() != 0
        || (info.permissions() & (QFileDevice::WriteGroup | QFileDevice::WriteOther)))
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray bytes = file.read(1024 * 1024 + 1);
    if (bytes.size() > 1024 * 1024) return {};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    const QJsonObject object = doc.object();
    const qint64 age = QDateTime::currentSecsSinceEpoch()
        - static_cast<qint64>(object.value(QStringLiteral("collected_at")).toDouble());
    return age >= 0 && age <= 120 ? object : QJsonObject{};
}
