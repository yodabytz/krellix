#pragma once
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

// Read-only checks run asynchronously, once per daemon, not once per client.
class PackageProbe : public QObject {
public:
    explicit PackageProbe(QObject *parent) : QObject(parent) {
        m_timeout.setSingleShot(true);
        connect(&m_timeout, &QTimer::timeout, this, [this] { m_timedOut = true; m_process.kill(); });
        connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
            finish(code == 0 && status == QProcess::NormalExit && !m_timedOut);
        });
        connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) finish(false);
        });
    }
    QJsonObject snapshot() {
        if (!m_running && QDateTime::currentMSecsSinceEpoch() >= m_nextCheck) {
            const QString apt = QStandardPaths::findExecutable(QStringLiteral("apt"));
            if (apt.isEmpty()) {
                m_result = {{QStringLiteral("status"), QStringLiteral("unsupported")}};
                m_nextCheck = QDateTime::currentMSecsSinceEpoch() + 300000;
            } else {
                m_running = true;
                m_timedOut = false;
                if (m_result.value(QStringLiteral("status")) != QStringLiteral("ok"))
                    m_result = {{QStringLiteral("status"), QStringLiteral("checking")}, {QStringLiteral("system"), QStringLiteral("apt")}};
                auto env = QProcessEnvironment::systemEnvironment();
                env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
                m_process.setProcessEnvironment(env);
                m_process.setProcessChannelMode(QProcess::SeparateChannels);
                m_process.start(apt, {QStringLiteral("list"), QStringLiteral("--upgradable")});
                m_timeout.start(30000);
            }
        }
        return m_result;
    }
private:
    void finish(bool success) {
        if (!m_running) return;
        m_running = false;
        m_timeout.stop();
        m_nextCheck = QDateTime::currentMSecsSinceEpoch() + 300000;
        const QString output = QString::fromUtf8(m_process.readAllStandardOutput());
        m_process.readAllStandardError();
        m_result = {{QStringLiteral("system"), QStringLiteral("apt")},
                    {QStringLiteral("checked_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                    {QStringLiteral("status"), success ? QStringLiteral("ok") : QStringLiteral("error")}};
        if (!success) return;
        QJsonArray names;
        int count = 0;
        const QRegularExpression row(QStringLiteral("^([^/\\s]+)/\\S+\\s+\\S+\\s+\\S+\\s+\\[upgradable from:"));
        for (const QString &line : output.split(QLatin1Char('\n'))) {
            if (line.trimmed().isEmpty() || line.startsWith(QStringLiteral("Listing"))) continue;
            const auto match = row.match(line);
            if (!match.hasMatch()) {
                m_result.insert(QStringLiteral("status"), QStringLiteral("error"));
                return; // Unknown output must not be reported as zero updates.
            }
            ++count;
            if (names.size() < 100) names.append(match.captured(1));
        }
        QDateTime newest;
        for (const QFileInfo &file : QDir(QStringLiteral("/var/lib/apt/lists")).entryInfoList(QDir::Files))
            if (file.fileName() != QStringLiteral("lock") && file.lastModified() > newest) newest = file.lastModified();
        m_result.insert(QStringLiteral("count"), count);
        m_result.insert(QStringLiteral("packages"), names);
        m_result.insert(QStringLiteral("index_updated_at"), newest.toUTC().toString(Qt::ISODate));
        m_result.insert(QStringLiteral("cached_metadata"), true);
    }
    QProcess m_process;
    QTimer m_timeout;
    QJsonObject m_result;
    qint64 m_nextCheck = 0;
    bool m_running = false;
    bool m_timedOut = false;
};
inline QJsonObject packageUpdateObject() {
    static PackageProbe *probe = new PackageProbe(QCoreApplication::instance());
    return probe->snapshot();
}
