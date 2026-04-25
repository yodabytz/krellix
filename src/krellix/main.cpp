#include "MainWindow.h"
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

    const QCommandLineOption hostOpt(
        QStringList{QStringLiteral("h"), QStringLiteral("host")},
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
        // Remote-monitoring mode (krellixd connection) is planned but not
        // yet implemented. For now the window opens with local readings
        // but the per-host instance namespace still applies, so settings
        // for that 'host' are persisted separately.
        qWarning("krellix: --host %s noted but krellixd remote protocol is "
                 "not yet implemented; showing LOCAL system readings.",
                 qUtf8Printable(parser.value(hostOpt)));
    }

    MainWindow w(theme, enabledIds);
    w.show();
    return app.exec();
}
