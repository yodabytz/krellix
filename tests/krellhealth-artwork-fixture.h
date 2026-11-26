#pragma once

#include <QDir>
#include <QFile>
#include <QImage>
#include <QWidget>

inline void makeHealthTheme(const QString &name, const QByteArray &manifest)
{
    const QString root = QStringLiteral("themes/") + name;
    if (!QDir().mkpath(root)) qFatal("Could not create health theme fixture");
    QFile file(root + QStringLiteral("/theme.json"));
    if (!file.open(QIODevice::WriteOnly) || file.write(manifest) != manifest.size())
        qFatal("Could not create health theme manifest");
    QImage image(48, 24, QImage::Format_ARGB32);
    image.fill(Qt::magenta);
    if (!image.save(root + QStringLiteral("/heart.png"))) qFatal("Could not write health artwork");
}

inline QRect healthArtworkBounds(QWidget *widget)
{
    const auto image = widget->grab().toImage();
    QRect result;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (image.pixelColor(x, y) == Qt::magenta)
                result |= QRect(x, y, 1, 1);
    return result;
}

inline void makeHealthThemes()
{
    makeHealthTheme(QStringLiteral("health-test-plain"), "{}");
    makeHealthTheme(QStringLiteral("health-test-custom"),
                    R"({"images":{"panel_icon_krellhealth_heart":"heart.png"}})");
    makeHealthTheme(QStringLiteral("health-test-missing"),
                    R"({"images":{"panel_icon_krellhealth_heart":"not-found.png"}})");
}
