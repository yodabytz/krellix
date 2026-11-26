#include "plugins/krellweather/KrellweatherPlugin.h"
#include "theme/Theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonObject>
#include <QLabel>
#include <QSettings>
#include <QTemporaryDir>
#include <memory>

class KrellweatherLayoutFixture
{
public:
    static void checkThemeOverrides()
    {
        QTemporaryDir scratch;
        const auto original = QDir::currentPath();
        if (!scratch.isValid() || !QDir::setCurrent(scratch.path()))
            qFatal("Cannot create theme override fixture");

        const auto writeTheme = [](const QString &name, const QByteArray &json) {
            const auto path = QStringLiteral("themes/") + name;
            if (!QDir().mkpath(path)) qFatal("Cannot create fixture theme");
            QFile file(path + QStringLiteral("/theme.json"));
            if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size())
                qFatal("Cannot write fixture theme");
        };
        const auto writeArtwork = [](const QString &name, QSize size, QRgb color) {
            QImage image(size, QImage::Format_ARGB32);
            image.fill(color);
            if (!image.save(QStringLiteral("themes/weather-fixture-art/") + name))
                qFatal("Cannot write fixture artwork");
        };
        writeTheme(QStringLiteral("weather-fixture-base"), QByteArrayLiteral("{}"));
        writeTheme(QStringLiteral("weather-fixture-art"), QByteArrayLiteral(R"({"images":{
            "panel_icon_krellweather_clear":"sun.png",
            "panel_icon_krellweather_clear_night":"moon.png",
            "panel_icon_krellweather_partly_cloudy":"sun.png",
            "panel_icon_krellweather_light_rain":"rain.png",
            "panel_icon_krellweather_snow":"missing.png"
        }})"));
        const auto sun = qRgb(14, 211, 59);
        const auto moon = qRgb(201, 17, 168);
        const auto rain = qRgb(235, 46, 25);
        writeArtwork(QStringLiteral("sun.png"), QSize(60, 20), sun);
        writeArtwork(QStringLiteral("moon.png"), QSize(20, 40), moon);
        writeArtwork(QStringLiteral("rain.png"), QSize(24, 24), rain);

        const auto key = QStringLiteral("plugins/krellweather/icon_mode");
        QSettings().setValue(key, QStringLiteral("theme"));
        Theme theme;
        if (!theme.load(QStringLiteral("weather-fixture-base"))) qFatal("Cannot load fixture theme");
        KrellweatherMonitor monitor(&theme);
        const std::unique_ptr<QWidget> panel(monitor.createWidget(nullptr));
        panel->setFixedWidth(120);
        panel->show();
        auto *icon = monitor.m_icon.data();
        const auto observation = [&](const QString &cover, const QString &wx, bool night) {
            monitor.renderWeather({
                {QStringLiteral("icaoId"), QStringLiteral("KSEA")},
                {QStringLiteral("temp"), 12.0},
                {QStringLiteral("cover"), cover},
                {QStringLiteral("wxString"), wx},
                {QStringLiteral("obsTime"), night ? QStringLiteral("2026-05-01T02:00:00Z")
                                                 : QStringLiteral("2026-05-01T14:00:00Z")},
            });
            QApplication::processEvents();
            return icon->grab().toImage();
        };
        const auto clear = QStringLiteral("CLR");
        const auto scattered = QStringLiteral("SCT");
        const auto snow = QStringLiteral("SN");
        const auto lightRain = QStringLiteral("-RA");
        const auto stockDay = observation(clear, {}, false);
        const auto stockSnow = observation(clear, snow, false);
        const auto stockPartlyNight = observation(scattered, {}, true);
        const auto stockHeavy = observation(clear, QStringLiteral("+RA"), false);
        const QRect originalSlot(icon->mapTo(panel.get(), QPoint()), icon->size());
        const QSize panelSize = panel->size();
        if (!theme.load(QStringLiteral("weather-fixture-art"))) qFatal("Cannot load artwork theme");
        const auto day = observation(clear, {}, false);
        if (day.pixel(1, 12) != sun || day.pixel(28, 12) != sun || day.pixel(15, 2) == sun
            || day.pixel(15, 21) == sun || day == stockDay)
            qFatal("Daytime artwork was not fitted proportionally");
        const auto night = observation(clear, {}, true);
        if (night.pixel(15, 1) != moon || night.pixel(15, 22) != moon || night.pixel(2, 12) == moon)
            qFatal("Night artwork did not override the default or lost its aspect ratio");
        for (const auto nightTime : {false, true}) {
            if (observation(clear, lightRain, nightTime).pixel(15, 12) != rain)
                qFatal("Rain artwork should work in either daylight or darkness");
        }
        if (observation(scattered, {}, true) != stockPartlyNight
            || observation(clear, snow, false) != stockSnow
            || observation(clear, QStringLiteral("+RA"), false) != stockHeavy)
            qFatal("An absent or broken image did not fall back to the correct vector");

        observation(clear, {}, false);
        for (const auto &mode : {QStringLiteral("color"), QStringLiteral("mono")}) {
            QSettings().setValue(key, mode);
            monitor.tick();
            if (icon->grab().toImage().pixel(15, 12) == sun)
                qFatal("A theme override was applied outside Theme mode");
        }
        QSettings().setValue(key, QStringLiteral("theme"));
        monitor.tick();
        if (icon->grab().toImage() != day) qFatal("Theme mode did not restore the artwork");
        if (!theme.load(QStringLiteral("weather-fixture-base"))) qFatal("Cannot restore baseline theme");
        if (icon->grab().toImage() != stockDay) qFatal("A theme reload retained stale artwork");
        if (QRect(icon->mapTo(panel.get(), QPoint()), icon->size()) != originalSlot || panel->size() != panelSize)
            qFatal("Artwork changed the panel's layout");

        QSettings().remove(key);
        monitor.shutdown();
        if (!QDir::setCurrent(original)) qFatal("Cannot restore original working directory");
    }

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
    KrellweatherLayoutFixture::checkThemeOverrides();
    qInfo("PASS: KrellWeather layout and fixed 30 x 24 icon slot");
}
