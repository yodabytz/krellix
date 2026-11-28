#include "ProcStat.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QString>

#include <cstring>
#ifdef Q_OS_WIN
#include "WindowsStats.h"
#else
#include <utmpx.h>
#endif
#if defined(Q_OS_FREEBSD)
#include "FreeBsdSysctl.h"
#include <sys/user.h>
#endif

namespace {
ProcStat::ReadFn g_readOverride = nullptr;

#if !defined(Q_OS_FREEBSD)
bool isPidDir(const QString &name)
{
    if (name.isEmpty()) return false;
    for (const QChar ch : name) {
        if (!ch.isDigit()) return false;
    }
    return true;
}
#endif

} // namespace

void ProcStat::setReadOverride(ProcStat::ReadFn fn)
{
    g_readOverride = fn;
}

ProcInfo ProcStat::read()
{
    if (g_readOverride) return g_readOverride();
#ifdef Q_OS_WIN
    return WindowsStats::processes();
#else
    ProcInfo out;

#if defined(Q_OS_MACOS)
    QProcess p;
    p.start(QStringLiteral("ps"), {QStringLiteral("-A"), QStringLiteral("-o"), QStringLiteral("pid=")});
    if (p.waitForFinished(1500)
        && p.exitStatus() == QProcess::NormalExit
        && p.exitCode() == 0) {
        const QStringList lines = QString::fromLocal8Bit(p.readAllStandardOutput())
                                      .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        out.processes = lines.size();
    }
#elif defined(Q_OS_FREEBSD)
    const auto processes = FreeBsdSysctl::readArray<kinfo_proc>("kern.proc.all", 64 * 1024 * 1024);
    for (const auto &process : processes) {
        if (process.ki_structsize == sizeof(kinfo_proc) && process.ki_pid > 0)
            ++out.processes;
    }
#else
    const QFileInfoList procEntries =
        QDir(QStringLiteral("/proc")).entryInfoList(QDir::Dirs
                                                    | QDir::NoDotAndDotDot
                                                    | QDir::Readable);
    for (const QFileInfo &entry : procEntries) {
        if (isPidDir(entry.fileName()))
            ++out.processes;
    }
#endif

    QSet<QString> users;
    setutxent();
    while (utmpx *u = getutxent()) {
        if (u->ut_type != USER_PROCESS) continue;
        const qsizetype len = static_cast<qsizetype>(
            strnlen(u->ut_user, sizeof(u->ut_user)));
        const QString name =
            QString::fromLocal8Bit(QByteArray(u->ut_user, len)).trimmed();
        if (!name.isEmpty())
            users.insert(name);
    }
    endutxent();
    out.users = users.size();

    return out;
#endif
}
