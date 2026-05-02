#include "ProcStat.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
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

    const QFileInfoList procEntries =
        QDir(QStringLiteral("/proc")).entryInfoList(QDir::Dirs
                                                    | QDir::NoDotAndDotDot
                                                    | QDir::Readable);
    for (const QFileInfo &entry : procEntries) {
        if (isPidDir(entry.fileName()))
            ++out.processes;
    }

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
