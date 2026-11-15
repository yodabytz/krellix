#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace WindowsPackages {
struct Package { QString id, name, installed, available; };
struct Snapshot { bool valid = false; QString status, detail, checked; QList<Package> packages; };
inline Snapshot parse(const QJsonObject &object) {
    Snapshot out;
    out.status = object.value("status").toString();
    if (out.status == "error") {
        out.detail = object.value("detail").toString().left(2048);
        out.valid = true; return out;
    }
    if (out.status != "ok" || !object.value("packages").isArray()) return out;
    const auto array = object.value("packages").toArray();
    if (array.size() > 4096) return out;
    QSet<QString> seen;
    for (const auto &value : array) {
        if (!value.isObject()) return out;
        const auto row = value.toObject();
        for (const auto *key : {"id", "name", "installed", "available"})
            if (!row.value(key).isString() || row.value(key).toString().size() > 2048) return out;
        Package package{row.value("id").toString(), row.value("name").toString(),
                        row.value("installed").toString(), row.value("available").toString()};
        if (package.id.trimmed().isEmpty()) return out;
        const auto key = package.id.toCaseFolded();
        if (!seen.contains(key)) { out.packages.append(package); seen.insert(key); }
    }
    out.checked = object.value("checked_at").toString().left(128);
    out.valid = true; return out;
}
inline bool matches(const Package &package, const QString &patterns) {
    const auto parts = patterns.left(8192).split(QRegularExpression("[,;\n]"), Qt::SkipEmptyParts);
    for (const auto &part : parts) {
        if (part.trimmed().isEmpty()) continue;
        QRegularExpression expression(QRegularExpression::wildcardToRegularExpression(part.trimmed()),
                                      QRegularExpression::CaseInsensitiveOption);
        if (expression.match(package.id).hasMatch() || expression.match(package.name).hasMatch()) return true;
    }
    return false;
}
inline QList<Package> filtered(const Snapshot &snapshot, const QString &includes, const QString &excludes) {
    QList<Package> result;
    for (const auto &package : snapshot.packages)
        if ((includes.trimmed().isEmpty() || matches(package, includes)) && !matches(package, excludes))
            result.append(package);
    return result;
}
}
