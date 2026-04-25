#include "MainWindow.h"
#include "theme/Theme.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QSettings>
#include <QStringList>

#ifndef KRELLIX_VERSION
#  define KRELLIX_VERSION "0.0.0"
#endif

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("krellix"));
    QApplication::setApplicationVersion(QString::fromUtf8(KRELLIX_VERSION));
    QApplication::setOrganizationName(QStringLiteral("krellix"));

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

    parser.process(app);

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

    MainWindow w(theme, enabledIds);
    w.show();
    return app.exec();
}
