#include "MainWindow.h"
#include "remote/RemoteSource.h"
#include "sysdep/CpuStat.h"
#include "sysdep/DiskStat.h"
#include "sysdep/MemStat.h"
#include "sysdep/NetStat.h"
#include "sysdep/NetPortStat.h"
#include "sysdep/ProcStat.h"
#include "sysdep/UptimeStat.h"
#include "theme/Theme.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QHostAddress>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>

#ifndef KRELLIX_VERSION
#  define KRELLIX_VERSION "0.0.0"
#endif

namespace {

// Slug-safe per-instance suffix derived from a free-form name. Drops anything
// not alnum/_-, caps length. Used to namespace QSettings (each instance gets
// its own config file) so multiple krellix windows can coexist with
// independent settings.
QString slugify(const QString &raw)
{
    static const QRegularExpression re(QStringLiteral("[^A-Za-z0-9._-]+"));
    QString s = raw;
    s.replace(re, QStringLiteral("_"));
    s = s.left(64);
    while (s.startsWith(QLatin1Char('.'))) s.remove(0, 1);
    return s;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("krellix"));
    QApplication::setApplicationVersion(QString::fromUtf8(KRELLIX_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("A themeable Qt 6 system monitor in the spirit of GKrellM."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption themeOpt(
        QStringList{QStringLiteral("t"), QStringLiteral("theme")},
        QStringLiteral("Load named theme."),
        QStringLiteral("name"),
        QStringLiteral("default"));
    parser.addOption(themeOpt);

    const QCommandLineOption monitorsOpt(
        QStringLiteral("monitors"),
        QStringLiteral("Comma-separated monitor IDs to enable (default: all)."),
        QStringLiteral("ids"));
    parser.addOption(monitorsOpt);

    const QCommandLineOption instanceOpt(
        QStringList{QStringLiteral("i"), QStringLiteral("instance")},
        QStringLiteral("Named instance — gives this window its own QSettings "
                       "namespace so multiple krellix instances run independently."),
        QStringLiteral("name"));
    parser.addOption(instanceOpt);

    // Note: cannot use "h" as a short alias — it collides with Qt's
    // built-in --help short option that QCommandLineParser::addHelpOption
    // registers above. Long form only ("--host").
    const QCommandLineOption hostOpt(
        QStringLiteral("host"),
        QStringLiteral("Connect to a remote krellixd instance at HOST[:PORT] "
                       "and display its readings instead of the local system. "
                       "Implies a per-host instance namespace."),
        QStringLiteral("host[:port]"));
    parser.addOption(hostOpt);

    parser.process(app);

    // Resolve the per-instance application name. Precedence:
    //   --instance NAME  -> krellix-<slug(NAME)>
    //   --host HOST      -> krellix-<slug(HOST)>
    //   neither          -> krellix (the default config namespace)
    // Settings paths (~/.config/krellix/, ~/.config/krellix-foo/, ...) are
    // therefore distinct per instance, so windows can hold independent
    // theme, monitor enable/disable, and per-iface choices.
    QString appName = QStringLiteral("krellix");
    if (parser.isSet(instanceOpt)) {
        const QString slug = slugify(parser.value(instanceOpt));
        if (!slug.isEmpty()) appName = QStringLiteral("krellix-") + slug;
    } else if (parser.isSet(hostOpt)) {
        const QString slug = slugify(parser.value(hostOpt));
        if (!slug.isEmpty()) appName = QStringLiteral("krellix-") + slug;
    }
    QApplication::setApplicationName(appName);

    auto *theme = new Theme(&app);
    const QString themeName = parser.isSet(themeOpt)
        ? parser.value(themeOpt)
        : QSettings().value(QStringLiteral("theme/name"),
                            QStringLiteral("default")).toString();
    theme->load(themeName);

    QStringList enabledIds;
    if (parser.isSet(monitorsOpt)) {
        const QStringList raw =
            parser.value(monitorsOpt).split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &s : raw) {
            const QString trimmed = s.trimmed();
            if (!trimmed.isEmpty()) enabledIds << trimmed;
        }
    }

    if (parser.isSet(hostOpt)) {
        // Remote-monitoring mode: stand up a RemoteSource and route every
        // sysdep read through it. The MainWindow is otherwise unchanged
        // — every monitor calls e.g. CpuStat::read() as before, but the
        // override delivers the most-recent sample from the remote
        // krellixd JSON stream.
        const QString hostSpec = parser.value(hostOpt);
        QString  host = hostSpec;
        quint16  port = 19150;
        const int colon = hostSpec.lastIndexOf(QLatin1Char(':'));
        if (colon > 0 && hostSpec.indexOf(QLatin1Char(':')) == colon) {
            host = hostSpec.left(colon);
            port = hostSpec.mid(colon + 1).toUShort();
        }

        auto *remote = new RemoteSource(&app);
        remote->connectToHost(host, port);

        CpuStat::setReadOverride(   []() { auto* r = RemoteSource::instance(); return r ? r->cpuSamples()  : QList<CpuSample>{};  });
        MemStat::setReadOverride(   []() { auto* r = RemoteSource::instance(); return r ? r->memInfo()     : MemInfo{};           });
        NetStat::setReadOverride(   []() { auto* r = RemoteSource::instance(); return r ? r->netSamples()  : QList<NetSample>{};  });
        NetPortStat::setReadOverride([]() { auto* r = RemoteSource::instance(); return r ? r->netPortSamples() : QList<NetPortSample>{}; });
        DiskStat::setReadOverride(  []() { auto* r = RemoteSource::instance(); return r ? r->diskSamples() : QList<DiskSample>{}; });
        ProcStat::setReadOverride(  []() { auto* r = RemoteSource::instance(); return r ? r->procInfo()    : ProcInfo{};          });
        UptimeStat::setReadOverride([]() { auto* r = RemoteSource::instance(); return r ? r->uptimeSeconds() : qint64(-1);        });

        qInfo("krellix: connecting to remote krellixd at %s:%u",
              qUtf8Printable(host), port);
    }

    MainWindow w(theme, enabledIds);
    if (parser.isSet(hostOpt)) {
        // Connect the disconnect-banner to the live RemoteSource so the
        // user sees a flashing red bar at the top of the window when the
        // daemon stops responding for more than ~5 seconds.
        if (auto *r = RemoteSource::instance()) w.attachRemoteSource(r);
    }
    w.show();
    return app.exec();
}
