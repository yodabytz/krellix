#include "WindowsProcesses.h"
#include <QMutexLocker>
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <algorithm>
#include <iterator>

namespace {
quint64 ticks(FILETIME t) { return (quint64(t.dwHighDateTime) << 32) | t.dwLowDateTime; }
struct Counter { quint64 ticks; quint64 created; };
class Handle {
public:
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    HANDLE value;
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
};
}

WindowsProcesses::WindowsProcesses()
{
    result.error = QStringLiteral("sampling processes...");
    start(QThread::LowPriority);
}
WindowsProcesses::~WindowsProcesses()
{
    requestInterruption();
    wake.wakeAll();
    wait();
}
WindowsProcesses::Result WindowsProcesses::snapshot()
{
    QMutexLocker lock(&mutex);
    return result;
}
double WindowsProcesses::cpuPercent(quint64 oldTicks, quint64 newTicks, quint64 elapsedMs,
                                    quint64 oldCreation, quint64 newCreation, unsigned cores)
{
    if (!elapsedMs || oldCreation != newCreation || newTicks < oldTicks) return 0;
    // Match Unix ps: 100% is one fully occupied logical CPU, not the entire machine.
    return std::clamp(double(newTicks - oldTicks) / (double(elapsedMs) * 100.0),
                      0.0, double(std::max(1u, cores)) * 100.0);
}
void WindowsProcesses::run()
{
    QHash<DWORD, Counter> previous;
    quint64 previousTime = 0;
    bool warmed = false;
    while (!isInterruptionRequested()) {
        const quint64 now = GetTickCount64();
        Result next;
        QHash<DWORD, Counter> current;
        MEMORYSTATUSEX memory{};
        memory.dwLength = sizeof(memory);
        const bool hasMemory = GlobalMemoryStatusEx(&memory) && memory.ullTotalPhys;
        const unsigned cores = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (snapshot.value == INVALID_HANDLE_VALUE || !Process32FirstW(snapshot.value, &entry)) {
            next.error = QStringLiteral("process snapshot unavailable");
            warmed = false;
        } else {
            int count = 0;
            do {
                if (isInterruptionRequested()) return;
                if (++count > 16384) { next.error = QStringLiteral("process limit exceeded"); break; }
                if (!entry.th32ProcessID) continue;
                Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                                           FALSE, entry.th32ProcessID));
                if (!process.value) continue;
                FILETIME created{}, exited{}, kernel{}, user{};
                PROCESS_MEMORY_COUNTERS counters{};
                counters.cb = sizeof(counters);
                if (!GetProcessTimes(process.value, &created, &exited, &kernel, &user)
                    || !GetProcessMemoryInfo(process.value, &counters, sizeof(counters))) continue;
                const Counter counter{ticks(kernel) + ticks(user), ticks(created)};
                current.insert(entry.th32ProcessID, counter);
                WindowsProcess row;
                row.pid = int(entry.th32ProcessID);
                row.name = QString::fromWCharArray(entry.szExeFile);
                row.command = row.name;
                wchar_t path[32768];
                DWORD length = DWORD(std::size(path));
                if (QueryFullProcessImageNameW(process.value, 0, path, &length))
                    row.command = QString::fromWCharArray(path, int(length));
                row.rssKiB = qint64(counters.WorkingSetSize / 1024);
                row.mem = hasMemory ? double(counters.WorkingSetSize) * 100.0 / double(memory.ullTotalPhys) : 0;
                const auto old = previous.constFind(entry.th32ProcessID);
                if (old != previous.cend())
                    row.cpu = cpuPercent(old->ticks, counter.ticks, now - previousTime,
                                         old->created, counter.created, cores);
                next.rows.append(row);
            } while (Process32NextW(snapshot.value, &entry));
            if (!hasMemory) next.error = QStringLiteral("physical memory unavailable");
            else if (next.rows.isEmpty()) next.error = QStringLiteral("no accessible processes");
            else if (!warmed) next.error = QStringLiteral("sampling CPU interval...");
            warmed = !next.rows.isEmpty();
        }
        previous = std::move(current);
        previousTime = now;
        QMutexLocker lock(&mutex);
        result = std::move(next);
        if (!isInterruptionRequested()) wake.wait(&mutex, 1000);
    }
}
