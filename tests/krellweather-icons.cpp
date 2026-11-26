#include "WeatherIcons.h"

#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>

#include <array>

using namespace KrellWeather;

namespace {
struct Sample {
    const char *label;
    Condition condition;
};
constexpr Sample samples[] = {
    {"Unknown", {Kind::Unknown}}, {"Sunny", {Kind::Clear}},
    {"Mostly sunny", {Kind::MostlyClear}}, {"Partly cloudy", {Kind::PartlyCloudy}},
    {"Mostly cloudy", {Kind::MostlyCloudy}}, {"Cloudy", {Kind::Cloudy}}, {"Overcast", {Kind::Overcast}},
    {"Drizzle", {Kind::Drizzle}}, {"Light rain", {Kind::LightRain}}, {"Rain", {Kind::Rain}},
    {"Heavy rain", {Kind::HeavyRain}}, {"Rain showers", {Kind::RainShowers}},
    {"Freezing rain", {Kind::FreezingRain}}, {"Light snow", {Kind::LightSnow}},
    {"Snow", {Kind::Snow}}, {"Heavy snow", {Kind::HeavySnow}}, {"Snow showers", {Kind::SnowShowers}},
    {"Sleet", {Kind::Sleet}}, {"Mixed rain and snow", {Kind::Mixed}}, {"Hail", {Kind::Hail}},
    {"Blowing snow", {Kind::BlowingSnow}}, {"Fog", {Kind::Fog}}, {"Mist", {Kind::Mist}},
    {"Haze", {Kind::Haze}}, {"Smoke", {Kind::Smoke}}, {"Dust", {Kind::Dust}},
    {"Windy", {Kind::Wind}}, {"Strong wind", {Kind::StrongWind}},
    {"Thunderstorm (dry)", {Kind::Thunderstorm}},
    {"Thunderstorm (rain)", {Kind::Thunderstorm, false, true}},
    {"Severe thunderstorm", {Kind::SevereThunderstorm, false, true}},
    {"Tornado", {Kind::Tornado}}, {"Hurricane", {Kind::Hurricane}},
    {"Tropical storm", {Kind::TropicalStorm}}, {"Clear night", {Kind::Clear, true}},
    {"Mostly clear night", {Kind::MostlyClear, true}}, {"Partly cloudy night", {Kind::PartlyCloudy, true}},
    {"Mostly cloudy night", {Kind::MostlyCloudy, true}}, {"Cloudy night", {Kind::Cloudy, true}},
    {"Rainy night", {Kind::Rain, true}}, {"Stormy night", {Kind::Thunderstorm, true, true}},
    {"Snowy night", {Kind::Snow, true}}, {"Foggy night", {Kind::Fog, true}},
};

constexpr std::array<int, 5> sizes{16, 24, 32, 48, 64};
const Colors palettes[] = {
    {QColor("#f4f1e9"), QColor("#2a3236"), QColor("#4d5c64"), QColor("#075978")},
    {QColor("#18212c"), QColor("#deebf8"), QColor("#a2b8d0"), QColor("#6cc3ee")},
    {QColor("#2b6555"), QColor("#ecf4da"), QColor("#c9d6b2"), QColor("#ffdb7e")},
};

void checkMapping()
{
    const struct { const char *code; Kind kind; } cases[] = {
        {"-DZ", Kind::Drizzle}, {"-RA", Kind::LightRain}, {"RA", Kind::Rain}, {"+RA", Kind::HeavyRain},
        {"-SHRA", Kind::RainShowers}, {"VCSH", Kind::RainShowers}, {"-FZDZ", Kind::FreezingRain},
        {"FZRA", Kind::FreezingRain}, {"-SN", Kind::LightSnow}, {"SN", Kind::Snow},
        {"+SN", Kind::HeavySnow}, {"SHSN", Kind::SnowShowers}, {"PL", Kind::Sleet},
        {"PE", Kind::Sleet}, {"-RASN", Kind::Mixed}, {"BL SN", Kind::Snow},
        {"DRSN", Kind::BlowingSnow}, {"BLSN", Kind::BlowingSnow}, {"GS", Kind::Hail},
        {"GR", Kind::Hail}, {"BCFG", Kind::Fog}, {"FZFG", Kind::Fog}, {"VCFG", Kind::Fog},
        {"BR", Kind::Mist}, {"HZ", Kind::Haze}, {"FU", Kind::Smoke}, {"BLDU", Kind::Dust},
        {"SQ", Kind::StrongWind}, {"FC", Kind::Tornado}, {"+FC", Kind::Tornado},
        {"VCTS", Kind::Thunderstorm}, {"+TSRA BR", Kind::Thunderstorm}, {"UP", Kind::Unknown},
        {"CLEARLY NOT A CODE", Kind::Unknown}, {"HURRICANE", Kind::Unknown}, {"FOORABAR", Kind::Unknown},
    };
    QJsonObject obs{{QStringLiteral("obsTime"), QStringLiteral("2026-11-26T14:00:00Z")},
                    {QStringLiteral("cover"), QStringLiteral("CLR")}};
    for (const auto &row : cases) {
        obs.insert(QStringLiteral("wxString"), QString::fromLatin1(row.code));
        const auto actual = classifyObservation(obs);
        if (actual.kind != row.kind || actual.night) qFatal("Wrong METAR classification: %s", row.code);
    }
    obs.insert(QStringLiteral("wxString"), QStringLiteral("TS"));
    if (classifyObservation(obs).stormPrecipitation) qFatal("Dry thunderstorm gained rain");
    obs.insert(QStringLiteral("wxString"), QStringLiteral("+TSRA"));
    if (!classifyObservation(obs).stormPrecipitation) qFatal("Wet thunderstorm lost rain");
    obs.remove(QStringLiteral("wxString"));
    for (const auto &entry : {std::pair{"CLR", Kind::Clear}, {"SKC", Kind::Clear},
                              {"FEW", Kind::MostlyClear}, {"SCT", Kind::PartlyCloudy},
                              {"BKN", Kind::MostlyCloudy}, {"OVC", Kind::Overcast}}) {
        obs.insert(QStringLiteral("cover"), QString::fromLatin1(entry.first));
        if (classifyObservation(obs).kind != entry.second) qFatal("Wrong sky cover: %s", entry.first);
    }
    obs.remove(QStringLiteral("cover"));
    obs.insert(QStringLiteral("clouds"), QJsonArray{QJsonObject{{QStringLiteral("cover"), QStringLiteral("FEW")}},
                                                    QJsonObject{{QStringLiteral("cover"), QStringLiteral("BKN")}}});
    if (classifyObservation(obs).kind != Kind::MostlyCloudy) qFatal("Cloud layers were ignored");
    const auto night = QDateTime::fromString(QStringLiteral("2026-11-26T02:00:00Z"), Qt::ISODate);
    obs.insert(QStringLiteral("obsTime"), static_cast<double>(night.toSecsSinceEpoch()));
    if (!classifyObservation(obs).night) qFatal("Unix-seconds METAR must render at night");
    obs.insert(QStringLiteral("obsTime"), static_cast<double>(night.toMSecsSinceEpoch()));
    if (!classifyObservation(obs).night) qFatal("Unix-milliseconds METAR must render at night");
    obs.insert(QStringLiteral("lon"), -120);
    if (classifyObservation(obs).night) qFatal("Station longitude was ignored");
    obs.remove(QStringLiteral("obsTime"));
    obs.insert(QStringLiteral("reportTime"), QStringLiteral("2026-11-26T12:00:00Z"));
    if (!classifyObservation(obs).night) qFatal("Report-time fallback was ignored");
    if (classifyObservation(QJsonObject{}).kind != Kind::Unknown) qFatal("Missing weather must not look clear");
}

void checkRendering()
{
    for (const auto &sample : samples) {
        for (const auto &palette : palettes) {
            for (auto mode : {Mode::Color, Mode::Theme, Mode::Monochrome}) {
                for (const int side : sizes) {
                    const auto image = renderIcon(sample.condition, mode, palette, {side, side});
                    int pixels = 0;
                    for (int y = 0; y < side; ++y) {
                        for (int x = 0; x < side; ++x) {
                            const auto color = image.pixelColor(x, y);
                            if (color.alpha() > 0) ++pixels;
                            if ((x == 0 || y == 0 || x == side - 1 || y == side - 1) && color.alpha() > 100)
                                qFatal("Clipped icon: %s at %d, edge (%d,%d)", sample.label, side, x, y);
                            if (mode == Mode::Monochrome && color.alpha() == 255
                                && (qAbs(color.red() - palette.primary.red()) > 1
                                    || qAbs(color.green() - palette.primary.green()) > 1
                                    || qAbs(color.blue() - palette.primary.blue()) > 1))
                                qFatal("Multiple colors in monochrome icon: %s (%s)", sample.label, qPrintable(color.name()));
                        }
                    }
                    if (pixels < side || pixels >= side * side)
                        qFatal("Icon must have visible artwork and a transparent background: %s", sample.label);
                }
            }
        }
    }
    const auto &palette = palettes[0];
    const auto night = renderIcon({Kind::Clear, true}, Mode::Color, palette, {64, 64});
    const auto day = renderIcon({Kind::Clear}, Mode::Color, palette, {64, 64});
    if (night == day || night.pixelColor(20, 22).alpha() != 0)
        qFatal("Night must render a crescent with a true cutout");
    if (renderIcon({Kind::Rain}, Mode::Color, palette, {30, 24}, 2).size() != QSize(60, 48))
        qFatal("Incorrect Retina backing dimensions");
    if (renderIcon({Kind::Rain}, Mode::Color, palette, {30, 24})
        == renderIcon({Kind::Rain}, Mode::Theme, palette, {30, 24}))
        qFatal("Theme mode did not use the theme palette");
}

void gallery(const QString &target)
{
    QDir dir(target);
    if (!QDir().mkpath(target)) qFatal("Cannot create weather preview directory");
    const char *backgrounds[] = {"light", "dark", "colored"};
    const char *modes[] = {"color", "theme", "monochrome"};
    constexpr int rowHeight = 78;
    constexpr int headerHeight = 90;
    constexpr int labelWidth = 195;
    constexpr int columnWidth = 98;
    for (int bg = 0; bg < 3; ++bg) {
        const auto &colors = palettes[bg];
        for (int md = 0; md < 3; ++md) {
            QImage sheet(labelWidth + columnWidth * sizes.size(),
                         headerHeight + rowHeight * std::size(samples), QImage::Format_RGB32);
            sheet.fill(colors.background);
            QPainter p(&sheet);
            p.setPen(colors.primary);
            QFont heading = p.font();
            heading.setPixelSize(22);
            heading.setBold(true);
            p.setFont(heading);
            p.drawText(QPoint(20, 32), QStringLiteral("KrellWeather / %1 / %2")
                       .arg(QString::fromLatin1(modes[md]), QString::fromLatin1(backgrounds[bg])));
            QFont body = p.font();
            body.setPixelSize(13);
            body.setBold(false);
            p.setFont(body);
            for (int i = 0; i < static_cast<int>(sizes.size()); ++i)
                p.drawText(QRect(labelWidth + i * columnWidth, 50, columnWidth, 32), Qt::AlignCenter,
                           QStringLiteral("%1 px").arg(sizes[i]));
            for (int row = 0; row < static_cast<int>(std::size(samples)); ++row) {
                const int y = headerHeight + row * rowHeight;
                p.setPen(colors.secondary);
                p.drawText(QRect(20, y, labelWidth - 25, rowHeight), Qt::AlignVCenter,
                           QString::fromLatin1(samples[row].label));
                for (int i = 0; i < static_cast<int>(sizes.size()); ++i) {
                    const int side = sizes[i];
                    const auto icon = renderIcon(samples[row].condition, static_cast<Mode>(md), colors, {side, side});
                    p.drawImage(labelWidth + i * columnWidth + (columnWidth - side) / 2,
                                y + (rowHeight - side) / 2, icon);
                }
            }
            p.end();
            if (!sheet.save(dir.filePath(QStringLiteral("%1-%2.png").arg(QString::fromLatin1(modes[md]),
                                                                          QString::fromLatin1(backgrounds[bg])))))
                qFatal("Could not save weather icon preview");
        }
    }
}
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    checkMapping();
    checkRendering();
    if (argc == 3 && QByteArray(argv[1]) == "--preview") gallery(QString::fromLocal8Bit(argv[2]));
    else if (argc != 1) qFatal("Usage: krellweather-icons [--preview DIRECTORY]");
    qInfo("PASS: METAR codes, all icon variants, all modes and five sizes");
}
