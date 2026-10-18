#include "ProcStat.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QString>

#include <cstring>
#include <utmpx.h>

namespace {
ProcStat::ReadFn g_readOverride = nullptr;

bool isPidDir(const QString &name)
{
    if (name.isEmpty()) return false;
    for (const QChar ch : name) {
        if (!ch.isDigit()) return false;
    }
    return true;
}

} // namespace

void ProcStat::setReadOverride(ProcStat::ReadFn fn)
{
    g_readOverride = fn;
}

ProcInfo ProcStat::read()
{
    if (g_readOverride) return g_readOverride();

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
}
