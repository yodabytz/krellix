#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <cmath>

struct HealthData {
    QJsonArray findings;
    QStringList coverage;
    QString error;
    int critical = 0;
    int warnings = 0;
};

inline void finding(HealthData &data, const QString &severity, const QString &title, const QString &detail = {})
{
    data.findings.append(QJsonObject{{"severity", severity}, {"title", title}, {"detail", detail}});
    if (severity == QStringLiteral("critical")) ++data.critical;
    if (severity == QStringLiteral("warning")) ++data.warnings;
}

inline HealthData parseHealth(const QJsonObject &object)
{
    HealthData data;
    const auto invalid = [&] { data.error = QStringLiteral("invalid health data"); return data; };
    if (!object.value("findings").isArray() || !object.value("critical").isDouble()
        || !object.value("warnings").isDouble()) return invalid();
    const auto rows = object.value("findings").toArray();
    if (rows.size() > 256) return invalid();
    for (const auto &value : rows) {
        if (!value.isObject()) return invalid();
        const auto row = value.toObject();
        const auto severity = row.value("severity").toString();
        const auto title = row.value("title").toString();
        const auto detail = row.value("detail").toString();
        if ((severity != QStringLiteral("critical") && severity != QStringLiteral("warning") && severity != QStringLiteral("info"))
            || title.isEmpty() || title.size() > 256 || detail.size() > 4096) return invalid();
        finding(data, severity, title, detail);
    }
    if (object.value("critical").toDouble() != data.critical || object.value("warnings").toDouble() != data.warnings)
        return invalid();
    const auto coverage = object.value("coverage").toArray();
    if (coverage.size() > 32) return invalid();
    for (const auto &value : coverage) {
        if (!value.isString() || value.toString().size() > 256) return invalid();
        data.coverage.append(value.toString());
    }
    return data;
}

inline void spaceFinding(HealthData &data, const QString &volume, quint64 total, quint64 available)
{
    if (!total || available > total) {
        finding(data, QStringLiteral("info"), QStringLiteral("Disk check unavailable"), volume);
        return;
    }
    const double used = 100.0 * double(total - available) / double(total);
    if (used >= 85.0)
        finding(data, used >= 95.0 ? QStringLiteral("critical") : QStringLiteral("warning"),
                used >= 95.0 ? QStringLiteral("Filesystem critical") : QStringLiteral("Filesystem high"),
                QStringLiteral("%1 is %2% full").arg(volume).arg(used, 0, 'f', 0));
}

inline QJsonObject healthJson(const HealthData &data)
{
    return {{"critical", data.critical}, {"warnings", data.warnings}, {"findings", data.findings},
            {"coverage", QJsonArray::fromStringList(data.coverage)}};
}
