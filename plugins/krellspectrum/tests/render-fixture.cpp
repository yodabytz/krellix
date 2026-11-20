#include "../KrellSpectrumPlugin.h"
#include "theme/Theme.h"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QSettings>
#include <QTemporaryDir>

int main(int argc, char **argv)
{
    QApplication::setAttribute(Qt::AA_Use96Dpi);
    QApplication app(argc, argv);
    if (app.arguments().size() != 2) qFatal("Specify an output directory");
    const QDir output(app.arguments().at(1));
    if (!output.mkpath(QStringLiteral("."))) qFatal("Cannot create the fixture directory");
    QTemporaryDir settings;
    if (!settings.isValid()) qFatal("Cannot isolate fixture settings");
    app.setOrganizationName(QStringLiteral("krellspectrum-render"));
    app.setApplicationName(QStringLiteral("fixture"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    const QStringList modes = {QStringLiteral("bars"), QStringLiteral("smooth_bars"),
        QStringLiteral("waveform"), QStringLiteral("filled_waveform"), QStringLiteral("circular"),
        QStringLiteral("particles"), QStringLiteral("blur_scope"), QStringLiteral("center_bars")};
    const QStringList colors = {QStringLiteral("gradient"), QStringLiteral("theme"),
        QStringLiteral("reactive"), QStringLiteral("per_band"), QStringLiteral("static")};
    for (const auto &themeName : {QStringLiteral("default"), QStringLiteral("nord")}) {
        Theme theme;
        if (!theme.load(themeName)) qFatal("Cannot load the fixture theme");
        for (const auto &mode : modes) {
            for (const auto &color : colors) {
                KrellSpectrumConfig config;
                config.visualMode = mode;
                config.colorMode = color;
                config.staticColor = QColor("#56c7ff");
                KrellSpectrumWidget widget(&theme);
                widget.setConfig(config);
                widget.resize(144, 72);
                QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
                for (int frame = 0; frame < 6; ++frame) {
                    QVector<float> samples(256);
                    for (int i = 0; i < samples.size(); ++i) {
                        const auto t = i + frame * 13;
                        const auto v = ((t % 37) - 18) * 272 + ((t % 17) - 8) * 360
                            + ((t % 53) - 26) * 130;
                        samples[i] = static_cast<float>(v) / 32768.0f;
                    }
                    widget.setSamples(samples);
                    image.fill(Qt::transparent);
                    widget.render(&image);
                }
                const auto path = output.filePath(
                    QStringLiteral("%1-%2-%3.png").arg(themeName, mode, color));
                if (!image.save(path)) qFatal("Failed to write a fixture image");
            }
        }
    }
}
