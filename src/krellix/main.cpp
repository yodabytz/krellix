#include "MainWindow.h"
#include "remote/RemoteSource.h"
#include "sysdep/CpuStat.h"
#include "sysdep/SensorStat.h"
#include "sysdep/BatteryStat.h"
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

bool isKrellixSettingKey(const QString &key)
{
    return key.startsWith(QStringLiteral("appearance/"))
        || key.startsWith(QStringLiteral("clock/"))
        || key.startsWith(QStringLiteral("host/"))
        || key.startsWith(QStringLiteral("monitors/"))
        || key.startsWith(QStringLiteral("plugins/"))
        || key.startsWith(QStringLiteral("theme/"))
        || key.startsWith(QStringLiteral("update/"))
        || key.startsWith(QStringLiteral("window/"));
}

void importSettings(QSettings &dst, const QSettings &src, bool overwriteExisting,
                    bool krellixKeysOnly = false)
{
    const QStringList keys = src.allKeys();
    for (const QString &key : keys) {
        if (krellixKeysOnly && !isKrellixSettingKey(key))
            continue;
        if (overwriteExisting || !dst.contains(key))
            dst.setValue(key, src.value(key));
    }
}

void migrateLegacySettings(const QString &orgName, const QString &appName,
                           const QString &baseAppName)
{
    QSettings ini(QSettings::IniFormat, QSettings::UserScope, orgName, appName);

    const QString migrationKey = QStringLiteral("migration/legacy_conf_imported");
    const QString baseMigrationKey = QStringLiteral("migration/base_defaults_imported_v2");
    const bool legacyAlreadyImported = ini.value(migrationKey, false).toBool();
    const bool isInstance = appName != baseAppName;

    if (isInstance && !ini.value(baseMigrationKey, false).toBool()) {
        const QVariant savedPos = ini.value(QStringLiteral("window/pos"));
        const QVariant savedSize = ini.value(QStringLiteral("window/size"));
        const QVariant savedSticky = ini.value(QStringLiteral("window/sticky"));
        const QVariant savedOnTop = ini.value(QStringLiteral("window/always_on_top"));

        QSettings baseIni(QSettings::IniFormat, QSettings::UserScope, orgName, baseAppName);
        QSettings baseNativeOrg(QSettings::NativeFormat, QSettings::UserScope, orgName, baseAppName);
        importSettings(ini, baseNativeOrg, false, true);
        importSettings(ini, baseIni, false);

        if (savedPos.isValid()) ini.setValue(QStringLiteral("window/pos"), savedPos);
        if (savedSize.isValid()) ini.setValue(QStringLiteral("window/size"), savedSize);
        if (savedSticky.isValid()) ini.setValue(QStringLiteral("window/sticky"), savedSticky);
        if (savedOnTop.isValid()) ini.setValue(QStringLiteral("window/always_on_top"), savedOnTop);
        ini.setValue(baseMigrationKey, true);
    }

    QSettings nativeOrg(QSettings::NativeFormat, QSettings::UserScope, orgName, appName);

    if (!legacyAlreadyImported) {
        const bool overwriteFromLegacy = ini.allKeys().isEmpty();
        importSettings(ini, nativeOrg, overwriteFromLegacy, true);
        ini.setValue(migrationKey, true);
    }

    ini.sync();
}

} // namespace

int main(int argc, char *argv[])
{
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QApplication app(argc, argv);
    // The monitor is a Qt::Tool window, which Qt excludes when counting
    // remaining primary windows. Closing a settings/about dialog must not
    // terminate the monitor. MainWindow handles explicit close requests.
    app.setQuitOnLastWindowClosed(false);
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
                       "Uses an independent instance seeded from the default user settings."),
        QStringLiteral("host[:port]"));
    parser.addOption(hostOpt);

    parser.process(app);

    // Resolve the application name used by QSettings. Precedence:
    //   --instance NAME  -> krellix-<slug(NAME)>
    //   --host HOST      -> krellix-<slug(HOST)>
    //   neither          -> krellix
    // Instance namespaces are seeded from the base ~/.config/krellix settings
    // so user defaults carry over, but each local/remote window keeps its own
    // runtime state such as position, size, and per-instance toggles.
    QString appName = QStringLiteral("krellix");
    if (parser.isSet(instanceOpt)) {
        const QString slug = slugify(parser.value(instanceOpt));
        if (!slug.isEmpty()) appName = QStringLiteral("krellix-") + slug;
    } else if (parser.isSet(hostOpt)) {
        const QString slug = slugify(parser.value(hostOpt));
        if (!slug.isEmpty()) appName = QStringLiteral("krellix-") + slug;
    }
    QApplication::setApplicationName(appName);
    migrateLegacySettings(QApplication::organizationName(), appName,
                          QStringLiteral("krellix"));

    QString remoteHost = parser.value(hostOpt).trimmed();
    if (remoteHost.isEmpty() && parser.isSet(instanceOpt))
        remoteHost = QSettings().value(QStringLiteral("remote/host")).toString();
    if (!remoteHost.isEmpty())
        QSettings().setValue(QStringLiteral("remote/host"), remoteHost);
    if (parser.isSet(themeOpt))
        QSettings().setValue(QStringLiteral("theme/name"), parser.value(themeOpt));
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

    if (!remoteHost.isEmpty()) {
        // Remote-monitoring mode: stand up a RemoteSource and route every
        // sysdep read through it. The MainWindow is otherwise unchanged
        // — every monitor calls e.g. CpuStat::read() as before, but the
        // override delivers the most-recent sample from the remote
        // krellixd JSON stream.
        const QString hostSpec = remoteHost;
        QString  host = hostSpec;
        quint16  port = 19150;
        const int colon = hostSpec.lastIndexOf(QLatin1Char(':'));
        if (colon > 0 && hostSpec.indexOf(QLatin1Char(':')) == colon) {
            host = hostSpec.left(colon);
            port = hostSpec.mid(colon + 1).toUShort();
        }

        auto *remote = new RemoteSource(&app);
        remote->connectToHost(host, port);

        SensorStat::setReadOverride([]() { auto *r = RemoteSource::instance(); return r ? r->sensorReadings() : QList<SensorReading>{}; });
        BatteryStat::setReadOverride([]() { auto *r = RemoteSource::instance(); return r ? r->batteryInfo() : BatteryInfo{}; });
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
    if (!remoteHost.isEmpty()) {
        // Connect the disconnect-banner to the live RemoteSource so the
        // user sees a flashing red bar at the top of the window when the
        // daemon stops responding for more than ~5 seconds.
        if (auto *r = RemoteSource::instance()) w.attachRemoteSource(r);
    }
    w.show();
    return app.exec();
}
