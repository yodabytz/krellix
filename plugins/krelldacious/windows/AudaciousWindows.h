#pragma once

#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QTimer>

class AudaciousWindows final : public QObject
{
    Q_OBJECT

public:
    struct Status {
        bool installed = false;
        bool running = false;
        bool connected = false;
        bool playing = false;
        bool hasTrack = false;
        QString title;
        int volume = -1;
    };

    explicit AudaciousWindows(int instance, QObject *parent);
    ~AudaciousWindows() override;
    const Status &status() const { return m_status; }
    void tick();
    void open();
    void control(const QString &method);
    void setVolume(int volume);
    void stop();

signals:
    void changed();

private:
    enum class Job { None, Poll, Command };
    void start(Job job, QStringList args);
    void complete(int exitCode);
    void runNext();
    void launch(const QString &arg);
    QStringList instanceArgs() const;

    const int m_instance;
    Status m_status;
    QString m_player;
    QString m_tool;
    QProcess m_process;
    QTimer m_timeout;
    QTimer m_volumeDelay;
    QQueue<QString> m_commands;
    QByteArray m_output;
    Job m_job = Job::None;
    int m_pendingVolume = -1;
    bool m_invalidResponse = false;
    bool m_stopped = false;
};
