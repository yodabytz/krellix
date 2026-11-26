#include "plugins/krellweather/KrellweatherPlugin.h"
#include "theme/Theme.h"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QJsonObject>
#include <QLabel>
#include <QSettings>
#include <QTemporaryDir>
#include <memory>

class KrellweatherLayoutFixture
{
public:
    static void check(const QString &themeName, int width, const QString &condition, bool night)
    {
        Theme theme;
        if (!theme.load(themeName)) qFatal("Could not load weather fixture theme");
        KrellweatherMonitor monitor(&theme);
        std::unique_ptr<QWidget> panel(monitor.createWidget(nullptr));
        panel->setFixedWidth(width);
        monitor.renderWeather(QJsonObject{
            {QStringLiteral("icaoId"), QStringLiteral("KSEA")},
            {QStringLiteral("temp"), 12.0},
            {QStringLiteral("wxString"), condition},
            {QStringLiteral("cover"), QStringLiteral("SCT")},
            {QStringLiteral("wdir"), 210},
            {QStringLiteral("wspd"), 8},
            {QStringLiteral("lon"), 0.0},
            {QStringLiteral("obsTime"), night ? QStringLiteral("2026-05-01T02:00:00Z")
                                             : QStringLiteral("2026-05-01T14:00:00Z")},
        });
        panel->adjustSize();
        panel->show();
        QApplication::processEvents();
        auto *icon = monitor.m_icon.data();
        if (!icon || icon->size() != QSize(30, 24)) qFatal("KrellWeather icon geometry changed");
        if (monitor.m_location->text() != QLatin1String("KSEA")
            || monitor.m_primary->text() != QLatin1String("54F")
            || monitor.m_detail->text() != (condition.isEmpty() ? QStringLiteral("Scattered") : condition)
                                             + QStringLiteral("  210 8 kt"))
            qFatal("KrellWeather text changed");
        const QRect iconRect(icon->mapTo(panel.get(), QPoint()), icon->size());
        const QString name = QStringLiteral("%1-%2-%3.png").arg(themeName).arg(width).arg(
            night ? QStringLiteral("night") : condition.isEmpty() ? QStringLiteral("day") : QStringLiteral("rain"));
        const QString output = qEnvironmentVariable("KRELLIX_WEATHER_SNAPSHOTS");
        const QString baseline = qEnvironmentVariable("KRELLIX_WEATHER_BASELINE");
        if (!output.isEmpty() || !baseline.isEmpty()) {
            const QImage image = panel->grab().toImage().convertToFormat(QImage::Format_ARGB32);
            if (!output.isEmpty() && (!QDir().mkpath(output) || !image.save(QDir(output).filePath(name))))
                qFatal("Could not save weather fixture screenshot");
            if (!baseline.isEmpty()) {
                const auto before = QImage(QDir(baseline).filePath(name)).convertToFormat(QImage::Format_ARGB32);
                if (before.size() != image.size()) qFatal("Weather panel dimensions changed");
                if (before.copy(iconRect) == image.copy(iconRect)) qFatal("Weather icon did not change");
                for (int y = 0; y < image.height(); ++y)
                    for (int x = 0; x < image.width(); ++x)
                        if (!iconRect.contains(x, y) && before.pixel(x, y) != image.pixel(x, y))
                            qFatal("Weather panel changed outside the icon: %s at (%d,%d)", qPrintable(name), x, y);
            }
        }
        const QString key = QStringLiteral("plugins/krellweather/icon_mode");
        const auto color = icon->grab();
        QSettings().setValue(key, QStringLiteral("mono"));
        monitor.tick();
        QApplication::processEvents();
        if (icon->grab().toImage() == color.toImage() || monitor.m_location->text() != QLatin1String("KSEA"))
            qFatal("Icon mode did not apply to the existing observation without fetching");
        if (QRect(icon->mapTo(panel.get(), QPoint()), icon->size()) != iconRect)
            qFatal("Icon mode changed its layout");
        QSettings().remove(key);
        monitor.shutdown();
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir fixture;
    if (!fixture.isValid() || argc != 2 || !QDir::setCurrent(QString::fromLocal8Bit(argv[1])))
        qFatal("Provide a source directory containing the bundled themes");
    QCoreApplication::setOrganizationName(QStringLiteral("krellix-weather-layout"));
    QCoreApplication::setApplicationName(QStringLiteral("fixture"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, fixture.path());
    QSettings().setValue(QStringLiteral("plugins/krellweather/station"), QString());
    QSettings().setValue(QStringLiteral("plugins/krellweather/units"), QStringLiteral("F"));
    for (const QString &theme : {QStringLiteral("default"), QStringLiteral("gorilla"), QStringLiteral("bluesteel")})
        for (int width : {120, 240}) {
            KrellweatherLayoutFixture::check(theme, width, {}, false);
            KrellweatherLayoutFixture::check(theme, width, QStringLiteral("-RA"), false);
            KrellweatherLayoutFixture::check(theme, width, {}, true);
        }
    qInfo("PASS: KrellWeather layout and fixed 30 x 24 icon slot");
}
