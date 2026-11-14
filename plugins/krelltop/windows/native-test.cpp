#include "WindowsProcesses.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <cmath>
static void check(bool value, const char *message) { if (!value) qFatal("%s", message); }
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    check(WindowsProcesses::cpuPercent(0, 10000000, 1000, 1, 1, 2) == 100, "CPU interval units incorrect");
    check(WindowsProcesses::cpuPercent(0, 10000000, 1000, 1, 2, 2) == 0, "PID reuse caused CPU spike");
    check(WindowsProcesses::cpuPercent(100, 50, 1000, 1, 1, 2) == 0, "Counter reversal caused CPU spike");
    check(WindowsProcesses::cpuPercent(0, 9999999999, 1, 1, 1, 2) == 200, "CPU bound failed");
    QElapsedTimer shutdown;
    {
        WindowsProcesses sampler;
        QElapsedTimer deadline; deadline.start();
        bool found = false;
        while (deadline.elapsed() < 10000) {
            auto result = sampler.snapshot();
            if (result.error.isEmpty()) {
                for (const auto &row : result.rows) {
                    check(std::isfinite(row.cpu) && row.cpu >= 0 && row.mem >= 0 && row.mem <= 100, "Invalid native process counters");
                    if (row.pid == QCoreApplication::applicationPid()) {
                        check(row.rssKiB > 0 && !row.name.isEmpty(), "Own process memory/name missing");
                        found = true;
                    }
                }
            }
            if (found) break;
            QThread::msleep(100);
        }
        check(found, "Native sampler failed to enumerate itself");
        shutdown.start();
    }
    check(shutdown.elapsed() < 1500, "Sampler shutdown stalled");
    qInfo("PASS: native process enumeration, memory, CPU deltas, PID reuse, bounds, shutdown");
}
