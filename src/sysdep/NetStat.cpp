#include "NetStat.h"

#include <QByteArray>
#include <QByteArrayList>
#include <QFile>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcNetStat, "krellix.sysdep.net")

namespace {
constexpr qint64 kProcNetDevMaxBytes = 1024 * 1024;  // generous cap
}

QList<NetSample> NetStat::read()
{
    QFile f(QStringLiteral("/proc/net/dev"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(lcNetStat) << "cannot open /proc/net/dev:" << f.errorString();
        return {};
    }
    const QByteArray bytes = f.read(kProcNetDevMaxBytes);
    if (bytes.isEmpty()) return {};

    QList<NetSample> samples;

    // Format (after two header lines):
    //   "eth0:  rxBytes rxPackets rxErrs ... txBytes txPackets txErrs ..."
    int start = 0;
    int lineNo = 0;
    while (start < bytes.size()) {
        const int nl  = bytes.indexOf('\n', start);
        const int end = (nl < 0) ? bytes.size() : nl;
        if (end > start) {
            ++lineNo;
            if (lineNo > 2) {                 // skip the two header lines
                const QByteArray line = bytes.mid(start, end - start);
                const int colon = line.indexOf(':');
                if (colon > 0) {
                    const QString name =
                        QString::fromLatin1(line.left(colon).trimmed());
                    if (name != QLatin1String("lo")) {
                        const QByteArrayList parts =
                            line.mid(colon + 1).simplified().split(' ');
                        if (parts.size() >= 16) {
                            NetSample s;
                            s.name      = name;
                            s.rxBytes   = parts[0].toULongLong();
                            s.rxPackets = parts[1].toULongLong();
                            s.txBytes   = parts[8].toULongLong();
                            s.txPackets = parts[9].toULongLong();
                            samples.append(s);
                        }
                    }
                }
            }
        }
        if (nl < 0) break;
        start = nl + 1;
    }
    return samples;
}
