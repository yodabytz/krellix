#pragma once
#include <QHash>
#include <QList>
#include <QMutex>
#include <QString>
#include <QThread>
#include <QWaitCondition>

struct WindowsProcess {
    int pid = 0;
    double cpu = 0;
    double mem = 0;
    qint64 rssKiB = 0;
    QString name;
    QString command;
};

class WindowsProcesses final : public QThread {
public:
    struct Result { QList<WindowsProcess> rows; QString error; };
    WindowsProcesses();
    ~WindowsProcesses() override;
    Result snapshot();
    static double cpuPercent(quint64 oldTicks, quint64 newTicks, quint64 elapsedMs,
                             quint64 oldCreation, quint64 newCreation, unsigned cores);
protected:
    void run() override;
private:
    QMutex mutex;
    QWaitCondition wake;
    Result result;
};
