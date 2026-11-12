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
        connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] { drain(false); });
        connect(&m_process, &QProcess::readyReadStandardError, this, [this] { drain(true); });
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
                m_output.clear();
                m_outputBytes = 0;
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
    void drain(bool error) {
        m_process.setReadChannel(error ? QProcess::StandardError : QProcess::StandardOutput);
        const QByteArray bytes = m_process.read(qMax<qint64>(1, 1024 * 1024 - m_outputBytes + 1));
        m_outputBytes += bytes.size();
        if (m_outputBytes > 1024 * 1024) {
            m_timedOut = true;
            m_process.kill();
        } else if (!error) {
            m_output.append(bytes);
        }
    }
    void finish(bool success) {
        if (!m_running) return;
        drain(false);
        drain(true);
        success = success && !m_timedOut;
        m_running = false;
        m_timeout.stop();
        m_nextCheck = QDateTime::currentMSecsSinceEpoch() + 300000;
        const QString output = QString::fromUtf8(m_output);
        m_output.clear();
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
    QByteArray m_output;
    qint64 m_outputBytes = 0;
};
inline QJsonObject packageUpdateObject() {
    static PackageProbe *probe = new PackageProbe(QCoreApplication::instance());
    return probe->snapshot();
}
