#include "monitors/CpuMonitor.h"
#include "monitors/DiskMonitor.h"
#include "monitors/MemMonitor.h"
#include "monitors/NetMonitor.h"
#include "sysdep/MemStat.h"
#include "theme/Theme.h"
#include "widgets/Chart.h"
#include "widgets/Decal.h"
#include "widgets/Krell.h"
#include "widgets/Meter.h"
#include "widgets/Panel.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontMetrics>
#include <QImage>
#include <QLayout>
#include <QPainter>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <memory>

namespace {

quint64 ticks = 100;

QList<CpuSample> cpuSamples()
{
    return {{QStringLiteral("cpu"), -1, ticks, 0, 0, ticks},
            {QStringLiteral("cpu0"), 0, ticks, 0, 0, ticks}};
}

QList<NetSample> netSamples()
{
    return {{QStringLiteral("eth0"), QStringLiteral("Uplink"), ticks * 1024, ticks * 2048}};
}

QList<DiskSample> diskSamples()
{
    return {{QStringLiteral("testdisk"), ticks, ticks * 2}};
}

void checkOptInSurfaces(const QString &configPath)
{
    const QString original = QDir::currentPath();
    if (!QDir::setCurrent(configPath)) qFatal("Could not enter theme fixture");
    const QByteArray common = R"("colors":{"panel_bg":"#101010","chart_bg":"#ff00ff","chart_line_default":"#101010"},
                "images":{"panel_icon_cpu":"icon.png"},)";
    const auto make = [&](const QString &name, const QByteArray &settings) {
        const auto dir = QStringLiteral("themes/") + name;
        if (!QDir().mkpath(dir)) qFatal("Could not create surface fixture");
        QFile json(dir + QStringLiteral("/theme.json"));
        if (!json.open(QIODevice::WriteOnly) || json.write("{" + common + settings + "}") < 0)
            qFatal("Could not write surface fixture");
        QImage blocks(40, 30, QImage::Format_RGB32);
        QPainter paint(&blocks);
        paint.fillRect(blocks.rect(), QColor("#403040"));
        paint.fillRect(0, 0, 40, 7, Qt::green);
        paint.fillRect(0, 0, 5, 7, Qt::red);
        paint.fillRect(28, 0, 12, 7, Qt::blue);
        paint.fillRect(0, 24, 40, 6, Qt::yellow);
        paint.end();
        QImage icon(32, 32, QImage::Format_ARGB32);
        icon.fill(Qt::transparent);
        QPainter dot(&icon);
        dot.setPen(Qt::NoPen);
        dot.setBrush(Qt::cyan);
        dot.drawEllipse(icon.rect().adjusted(1, 1, -1, -1));
        dot.end();
        if (!blocks.save(dir + QStringLiteral("/panel.png")) || !icon.save(dir + QStringLiteral("/icon.png")))
            qFatal("Could not save surface artwork");
    };
    make(QStringLiteral("surface-legacy"), R"("metrics":{"panel_border":0},
         "surfaces":{"panel_bg":{"image":"panel.png","slice":5}})");
    make(QStringLiteral("surface-insets"), R"("metrics":{"panel_border":0},
         "surfaces":{"panel_bg":{"image":"panel.png","mode":"nine-slice-insets",
         "slice_insets":{"left":5,"right":12,"top":7,"bottom":6},
         "destination_insets":{"left":10,"right":4,"top":8,"bottom":6}}})");
    make(QStringLiteral("surface-caps"), R"("metrics":{"panel_border":0},
         "surfaces":{"panel_bg":{"image":"panel.png","mode":"vertical-caps","cap_top":7,"cap_bottom":6}})");
    make(QStringLiteral("surface-chart"), R"("metrics":{"panel_border":0,"panel_padding":6,
        "panel_padding_left":9,"panel_padding_right":11,"panel_bg_cpu_padding_top":4,
        "panel_spacing":2,"chart_corner_radius":7,"chart_padding_left":3,
        "chart_glass":0,"chart_grid_lines":0,"panel_icon_size":20,"panel_icon_spacing":6,
        "panel_icon_offset_x":3,"panel_icon_offset_y":4})");

    const auto pixel = [](const QImage &image, int x, int y) {
        const double scale = image.devicePixelRatio();
        return image.pixelColor(qFloor((x + .5) * scale), qFloor((y + .5) * scale));
    };
    Theme theme;
    Panel sample(&theme);
    sample.layout()->setSizeConstraint(QLayout::SetNoConstraint);
    sample.setSurfaceKey(QStringLiteral("panel_bg_cpu"));
    sample.resize(80, 55);
    if (!theme.load(QStringLiteral("surface-legacy"))) qFatal("Could not load legacy surface");
    const auto legacy = sample.grab().toImage();
    if (pixel(legacy, 2, 2) != Qt::red || pixel(legacy, 7, 2) != Qt::green)
        qFatal("Legacy numeric slices changed");
    if (!theme.load(QStringLiteral("surface-insets"))) qFatal("Could not load inset surface");
    const auto insets = sample.grab().toImage();
    if (pixel(insets, 8, 3) != Qt::red || pixel(insets, 15, 3) != Qt::green
        || pixel(insets, 78, 3) != Qt::blue || pixel(insets, 41, 53) != Qt::yellow)
        qFatal("Independent source and destination insets did not render");
    sample.resize(7, 5);
    const auto tiny = sample.grab().toImage();
    if (pixel(tiny, 0, 0) != Qt::red || pixel(tiny, 6, 0) != Qt::blue
        || pixel(tiny, 3, 4) != Qt::yellow)
        qFatal("Small inset panel lost its proportional caps");
    if (!theme.load(QStringLiteral("surface-caps"))) qFatal("Could not load cap surface");
    sample.resize(80, 55);
    const auto caps = sample.grab().toImage();
    if (pixel(caps, 8, 10) != Qt::red || pixel(caps, 14, 10) != Qt::green
        || pixel(caps, 71, 10) != Qt::blue || pixel(caps, 41, 53) != Qt::yellow)
        qFatal("Vertical caps did not scale together across the panel");
    sample.resize(80, 5);
    const auto shortCaps = sample.grab().toImage();
    if (pixel(shortCaps, 5, 1) != Qt::red || pixel(shortCaps, 40, 4) != Qt::yellow)
        qFatal("Short vertical caps overlapped or disappeared");
    sample.resize(80, 55);
    if (!theme.load(QStringLiteral("surface-legacy")) || sample.grab().toImage() != legacy)
        qFatal("Switching to and from optional surfaces altered legacy rendering");

    if (!theme.load(QStringLiteral("surface-chart"))) qFatal("Could not load chart fixture");
    sample.setTitle(QStringLiteral("CPU"));
    auto *chart = sample.addChart();
    auto *track = sample.addKrell();
    sample.resize(120, sample.sizeHint().height());
    sample.show();
    QApplication::processEvents();
    if (sample.layout()->contentsMargins() != QMargins(9, 4, 11, 6)
        || track->y() - chart->geometry().bottom() != 3)
        qFatal("Optional padding failed to inherit or chart and track overlap");
    const auto at = chart->pos();
    const auto chartImage = sample.grab().toImage();
    if (pixel(chartImage, at.x(), at.y() + 3) != QColor("#101010")
        || pixel(chartImage, at.x() + 3, at.y()) != QColor("#101010")
        || pixel(chartImage, at.x() + 12, at.y() + 6) != Qt::magenta)
        qFatal("Rounded chart clip or independent plot padding did not render");
    theme.setMetric(QStringLiteral("chart_glass"), 1);
    if (pixel(sample.grab().toImage(), at.x() + 12, at.y() + 6) != QColor("#101010"))
        qFatal("Glass mode obscured the parent artwork");
    const auto *header = qobject_cast<Decal *>(sample.layout()->itemAt(0)->widget());
    const auto head = header->pos();
    const auto iconImage = sample.grab().toImage();
    if (header->height() < 26
        || pixel(iconImage, head.x() + 1, head.y() + 13) != QColor("#101010")
        || pixel(iconImage, head.x() + 12, head.y() + 13) != Qt::cyan)
        qFatal("Offset icon clipped or did not reserve heading space");
    if (!QDir::setCurrent(original)) qFatal("Could not restore working directory");
}

void checkMemoryMeters()
{
    Theme theme;
    theme.setMetric(QStringLiteral("krell_height"), 12);
    MemStat::setReadOverride([]() -> MemInfo {
        MemInfo data;
        data.totalKb = 8 * 1024 * 1024;
        data.availableKb = data.totalKb / 2;
        data.swapTotalKb = 4 * 1024 * 1024;
        data.swapFreeKb = data.swapTotalKb / 4;
        return data;
    });
    MemMonitor memory(&theme);
    std::unique_ptr<QWidget> panel(memory.createWidget(nullptr));
    MemStat::setReadOverride(nullptr);
    auto *layout = panel->layout();
    auto *ramText = qobject_cast<Decal *>(layout->itemAt(1)->widget());
    auto *ram = qobject_cast<Meter *>(layout->itemAt(2)->widget());
    auto *swapText = qobject_cast<Decal *>(layout->itemAt(3)->widget());
    auto *swap = qobject_cast<Meter *>(layout->itemAt(4)->widget());
    if (!ram || !swap || !ramText || !swapText
        || ramText->text() != QStringLiteral("RAM 4.0 G / 8.0 G")
        || swapText->text() != QStringLiteral("Swap 3.0 G / 4.0 G"))
        qFatal("Memory or Swap readout changed");

    const auto verifyHeight = [&](int expected) {
        if (ram->sizeHint().height() != expected || ram->minimumSizeHint() != ram->sizeHint()
            || swap->sizeHint() != ram->sizeHint() || swap->minimumSizeHint() != swap->sizeHint())
            qFatal("Memory and Swap meters must be one-third shorter");
    };
    verifyHeight(8);
    Meter uptime(&theme);
    uptime.setText(QStringLiteral("5d 12:00"));
    uptime.setFillVisible(false);
    if (uptime.sizeHint().height() != 18) qFatal("Text meter height changed");
    theme.setMetric(QStringLiteral("meter_height"), 15);
    verifyHeight(10);
    if (uptime.sizeHint().height() != 15) qFatal("Text meter theme height changed");

    const auto verifyFill = [&](Meter *meter, const QString &color, int percent) {
        meter->resize(120, meter->sizeHint().height());
        const auto image = meter->grab().toImage();
        const int x = image.width() * percent / 100;
        const int y = image.height() / 2;
        if (image.pixelColor(x - 2, y) != theme.color(color, theme.color(QStringLiteral("krell_indicator")))
            || image.pixelColor(x + 2, y) != theme.color(QStringLiteral("chart_bg")))
            qFatal("The compact meter changed the fill color or usage width");
    };
    verifyFill(ram, QStringLiteral("chart_line_mem"), 50);
    verifyFill(swap, QStringLiteral("chart_line_swap"), 75);
}

void checkCombinedCpuFill(const QString &configPath)
{
    Theme theme;
    const QString originalPath = QDir::currentPath();
    if (!QDir::setCurrent(configPath)) qFatal("Could not enter the CPU fill fixture directory");
    const auto make = [](const QString &name, const QByteArray &fill) {
        const QString path = QStringLiteral("themes/") + name;
        if (!QDir().mkpath(path)) qFatal("Could not create the CPU fill theme");
        QFile json(path + QStringLiteral("/theme.json"));
        const QByteArray data = R"({"metrics":{"chart_grid_lines":0},"colors":{"chart_bg":"#121416","chart_line_cpu_series":"#ff426a")"
            + fill + "}}";
        if (!json.open(QIODevice::WriteOnly) || json.write(data) != data.size())
            qFatal("Could not write the CPU fill theme");
    };
    make(QStringLiteral("combined-default-fill"), {});
    make(QStringLiteral("combined-filled"), R"(,"chart_fill_cpu_series":"#aa2248")");
    if (!theme.load(QStringLiteral("combined-default-fill"))) qFatal("Could not load default CPU fill theme");

    Chart cpu(&theme, QStringLiteral("chart_line_cpu"));
    Chart net(&theme, QStringLiteral("chart_line_net_rx"));
    for (auto *chart : {&cpu, &net}) {
        chart->resize(120, 80);
        chart->setMaxValue(1.0);
        chart->setRainbowSeries(2);
    }
    const auto idleCpu = cpu.grab().toImage();
    if (idleCpu.pixelColor(idleCpu.width() / 2, idleCpu.height() - 3) != QColor("#121416"))
        qFatal("An idle combined CPU chart should not fill above its baseline");
    for (auto *chart : {&cpu, &net}) {
        for (int i = 0; i < 200; ++i) {
            chart->appendSampleAt(0, .32);
            chart->appendSampleAt(1, .68);
        }
    }
    const auto defaultFill = cpu.grab().toImage();
    const auto plainNet = net.grab().toImage();
    if (!theme.load(QStringLiteral("combined-filled"))) qFatal("Could not load CPU fill theme");
    const auto filled = cpu.grab().toImage();
    const int x = filled.width() / 2;
    const int bottom = filled.height() - 1;
    if (filled.pixelColor(x, bottom) != QColor("#aa2248")
        || filled.pixelColor(x, filled.height() / 8) != QColor("#121416")
        || defaultFill.pixelColor(x, bottom) == QColor("#121416")
        || defaultFill.pixelColor(x, filled.height() / 8) != QColor("#121416"))
        qFatal("Combined CPU fill did not rise from the baseline to the outline");
    const QColor base("#ff426a");
    QSet<QRgb> outline;
    for (int y = 0; y < filled.height(); ++y) {
        const auto pixel = filled.pixel(x, y);
        if (pixel == base.lighter(85).rgb() || pixel == base.lighter(93).rgb())
            outline.insert(pixel);
    }
    if (outline.size() != 2 || net.grab().toImage() != plainNet)
        qFatal("CPU fill hid an outline or changed another monitor");
    if (!theme.load(QStringLiteral("combined-default-fill")) || cpu.grab().toImage() != defaultFill)
        qFatal("Switching fill removed combined chart history");

    if (!theme.load(QStringLiteral("combined-filled"))) qFatal("Could not restore CPU fill theme");
    theme.setMetric(QStringLiteral("chart_grid_lines"), 5);
    Chart aggregate(&theme, QStringLiteral("chart_line_cpu"));
    aggregate.resize(120, 80);
    cpu.setRainbowSeries(4);
    for (int i = 0; i < 200; ++i) {
        aggregate.appendSample(.23);
        cpu.appendSampleAt(0, .80);
        cpu.appendSampleAt(1, .08);
        cpu.appendSampleAt(2, .04);
        cpu.appendSampleAt(3, .00);
    }
    const auto aggregateImage = aggregate.grab().toImage();
    const auto combinedImage = cpu.grab().toImage();
    const auto pixel = [x](const QImage &image, double fromBottom) {
        return image.pixelColor(x, qRound((1.0 - fromBottom) * (image.height() - 1)));
    };
    if (pixel(aggregateImage, .30) != QColor("#121416")
        || pixel(aggregateImage, .15) == QColor("#121416")
        || pixel(aggregateImage, .60) != theme.color(QStringLiteral("chart_grid")))
        qFatal("A 23%% CPU load should stay low under a fixed 0-100%% grid");
    if (pixel(combinedImage, .15) != QColor("#aa2248")
        || pixel(combinedImage, .30) != QColor("#121416"))
        qFatal("The combined fill should follow the 23%% average, not the busiest core");
    const int busyY = qRound(.20 * (combinedImage.height() - 1));
    bool busyCoreVisible = false;
    for (int y = busyY - 2; y <= busyY + 2; ++y)
        busyCoreVisible |= combinedImage.pixel(x, y) == base.lighter(85).rgb();
    if (!busyCoreVisible) qFatal("The 80%% core should still be drawn above the average");

    if (!QDir::setCurrent(originalPath)) qFatal("Could not restore the working directory");
}

Chart *checkPanel(QWidget &container, const QString &title, int tracks)
{
    const auto panels = container.findChildren<Panel *>(QString(), Qt::FindDirectChildrenOnly);
    if (panels.size() != 1) qFatal("Expected one chart panel");
    auto *layout = panels.first()->layout();
    if (layout->count() != 2 + tracks) qFatal("Unexpected widget rows");
    const auto *decal = qobject_cast<Decal *>(layout->itemAt(0)->widget());
    if (!decal || decal->text() != title) qFatal("Chart title is not the first row");
    auto *chart = qobject_cast<Chart *>(layout->itemAt(1)->widget());
    if (!chart) qFatal("Chart does not immediately follow the title");
    for (int i = 0; i < tracks; ++i) {
        auto *krell = qobject_cast<Krell *>(layout->itemAt(i + 2)->widget());
        if (!krell) qFatal("Moving krell track is not below the chart");
    }
    return chart;
}

void checkSavedAggregateCpu()
{
    const auto modeKey = QStringLiteral("monitors/cpu/mode");
    const auto originalName = QCoreApplication::applicationName();
    const auto originalMode = QSettings().value(modeKey);
    QCoreApplication::setApplicationName(originalName + QStringLiteral("-remote"));
    {
        QSettings saved;
        saved.setValue(modeKey, QStringLiteral("aggregate"));
        saved.sync();
        if (saved.status() != QSettings::NoError) qFatal("Could not save the remote CPU profile");
    }

    // One saturated core and three idle cores: the host is at 25%, not 100%.
    CpuStat::setReadOverride(+[]() -> QList<CpuSample> {
        return {{QStringLiteral("cpu"), -1, ticks, 0, 0, 3 * ticks},
                {QStringLiteral("cpu0"), 0, ticks, 0, 0, 0},
                {QStringLiteral("cpu1"), 1, 0, 0, 0, ticks},
                {QStringLiteral("cpu2"), 2, 0, 0, 0, ticks},
                {QStringLiteral("cpu3"), 3, 0, 0, 0, ticks}};
    });
    for (const int glass : {0, 1}) {
        Theme theme;
        theme.setMetric(QStringLiteral("chart_glass"), glass);
        theme.setMetric(QStringLiteral("chart_grid_lines"), 0);
        CpuMonitor cpu(&theme);
        std::unique_ptr<QWidget> panel(cpu.createWidget(nullptr));
        auto *chart = checkPanel(*panel, QStringLiteral("CPU"), 1);
        chart->resize(120, 64);
        const auto idle = chart->grab().toImage();
        for (int i = 0; i < 200; ++i) {
            ++ticks;
            cpu.tick();
        }
        if (chart->accessibleName() != QStringLiteral("25%"))
            qFatal("The saved remote profile must show aggregate CPU usage after reopening");
        const auto busy = chart->grab().toImage();
        const int x = busy.width() * 3 / 4;
        const int upper = busy.height() / 2;
        const int lower = busy.height() - qRound(2 * busy.devicePixelRatio());
        if (idle.pixelColor(x, upper) != busy.pixelColor(x, upper)
            || idle.pixelColor(x, lower) == busy.pixelColor(x, lower))
            qFatal("The aggregate CPU chart must fill from the bottom without drawing hot-core spikes");
    }
    CpuStat::setReadOverride(cpuSamples);
    QCoreApplication::setApplicationName(originalName);
    if (QSettings().value(modeKey) != originalMode)
        qFatal("Saving the remote CPU mode changed the default profile");
}

void checkTitleFont(Theme &theme, const QString &configPath)
{
    Panel panel(&theme);
    panel.setTitle(QStringLiteral("Memory"));
    const auto *title = panel.layout()->itemAt(0)->widget();
    auto *label = panel.addDecal(QStringLiteral("label"));
    label->setText(QStringLiteral("75%"));
    const auto check = [&](int overrideSize = 0) {
        const QFont base = theme.font(QStringLiteral("label"));
        QFont expected = base;
        expected.setPointSizeF(overrideSize ? overrideSize : base.pointSizeF() + 2.0);
        expected.setWeight(QFont::DemiBold);
        const QFont actual = theme.font(QStringLiteral("panel_title"));
        if (actual != expected) qFatal("Wrong panel title font");
        if (title->sizeHint().height() != QFontMetrics(expected).height() + 2)
            qFatal("Panel title did not size itself for its font");
        if (label->sizeHint().height() != QFontMetrics(base).height() + 2)
            qFatal("Ordinary label did not keep the theme label font");
    };
    check();

    const auto originalPath = QDir::currentPath();
    if (!QDir::setCurrent(configPath)) qFatal("Could not enter the theme fixture directory");
    for (const int size : {0, 17}) {
        const auto name = QStringLiteral("title-font-test-%1").arg(size);
        const auto fixture = QStringLiteral("themes/") + name;
        if (!QDir().mkpath(fixture)) qFatal("Could not create the custom theme directory");
        QFile json(fixture + QStringLiteral("/theme.json"));
        QByteArray data = R"({"fonts":{"label":{"family":"Monospace","size":14})";
        if (size) data += R"(,"panel_title":{"size":17})";
        data += "}}";
        if (!json.open(QIODevice::WriteOnly) || json.write(data) != data.size())
            qFatal("Could not write the custom theme fixture");
        json.close();
        if (!theme.load(name) || theme.font(QStringLiteral("label")).pointSize() != 14)
            qFatal("Could not load the custom theme fixture");
        check(size);
    }
    if (!QDir::setCurrent(originalPath)) qFatal("Could not restore the working directory");
}

void checkOverlay(Theme &theme)
{
    Chart chart(&theme);
    chart.resize(160, 32);
    chart.show();
    QApplication::processEvents();
    for (int i = 0; i < 160; ++i) chart.appendSample(0.75);
    const QImage graph = chart.grab().toImage();
    chart.setOverlayText(QStringLiteral("75%"));
    const QImage readout = chart.grab().toImage();
    if (graph == readout) qFatal("The chart did not render its readout");
    const QRect unaffected(graph.width() / 2, 0, graph.width() / 2, graph.height());
    if (graph.copy(unaffected) != readout.copy(unaffected))
        qFatal("The readout resized the graph or painted a full-width backing strip");
    const QRect below(0, graph.height() * 3 / 4, graph.width(), graph.height() / 4);
    if (graph.copy(below) != readout.copy(below))
        qFatal("The readout is not near the top of the chart");
}

void checkChartStyling(Theme &theme, const QString &configPath, QWidget &network)
{
    Chart chart(&theme);
    chart.resize(120, 80);
    chart.show();
    QApplication::processEvents();
    chart.appendSample(0.9);
    const auto gridPixels = [&] {
        const auto image = chart.grab().toImage();
        const auto grid = theme.color(QStringLiteral("chart_grid"));
        int count = 0;
        for (int y = 0; y < image.height(); ++y)
            if (image.pixelColor(image.width() / 2, y) == grid) ++count;
        return count;
    };
    const int thin = gridPixels();
    theme.setMetric(QStringLiteral("chart_grid_width"), 2);
    if (thin == 0 || gridPixels() <= thin) qFatal("Grid did not paint thicker lines");

    chart.setRainbowSeries(12);
    for (int x = 0; x < 120; ++x)
        for (int i = 0; i < 12; ++i)
            chart.appendSampleAt(i, 0.08 + i * 0.065);
    const auto original = chart.grab().toImage();
    theme.setMetric(QStringLiteral("chart_series_grayscale"), 1);
    const auto grayscale = chart.grab().toImage();
    QSet<QRgb> shades;
    for (int y = 0; y < grayscale.height(); ++y) {
        const auto pixel = grayscale.pixel(grayscale.width() / 2, y);
        if (qRed(pixel) >= 191 && qRed(pixel) == qGreen(pixel) && qRed(pixel) == qBlue(pixel))
            shades.insert(pixel);
    }
    if (shades.size() != 12 || original == grayscale)
        qFatal("Combined chart did not paint 12 distinct grayscale lines");
    theme.setMetric(QStringLiteral("chart_series_grayscale"), 0);
    if (chart.grab().toImage() != original) qFatal("Palette changes lost history");

    const auto originalPath = QDir::currentPath();
    const auto fixture = configPath + QStringLiteral("/themes/text-meter-test");
    if (!QDir().mkpath(fixture) || !QDir::setCurrent(configPath))
        qFatal("Could not set up the text meter fixture");
    QFile json(fixture + QStringLiteral("/theme.json"));
    QImage icon(16, 16, QImage::Format_ARGB32);
    icon.fill(QColor("#ff5500"));
    if (!icon.save(fixture + QStringLiteral("/icon.png"))) qFatal("Could not write the title icon fixture");
    const QByteArray data = R"({"colors":{"text_meter_bg":"#eeeeec",
        "net_krell_track":"#a9a9a9","net_krell_indicator":"#3e3e3e"},
        "images":{"panel_icon_net":"icon.png"}})";
    if (!json.open(QIODevice::WriteOnly) || json.write(data) != data.size())
        qFatal("Could not write the text meter fixture");
    json.close();
    if (!theme.load(QStringLiteral("text-meter-test")) || !QDir::setCurrent(originalPath))
        qFatal("Could not load the text meter fixture");
    Meter meter(&theme, QStringLiteral("text_primary"));
    meter.resize(120, 20);
    meter.setText(QStringLiteral("5d 12:00"));
    if (meter.grab().toImage().pixelColor(3, 3) != theme.color(QStringLiteral("chart_bg")))
        qFatal("Text-only background affected a regular meter");
    meter.setFillVisible(false);
    if (meter.grab().toImage().pixelColor(3, 3) != QColor("#eeeeec"))
        qFatal("Text-only meter did not use its themed background");

    auto *title = network.findChild<Decal *>();
    if (!title || title->text() != QStringLiteral("Uplink")) qFatal("Network title missing");
    title->resize(120, title->sizeHint().height());
    const auto titled = title->grab().toImage();
    if (titled.pixelColor(4, titled.height() / 2) != QColor("#ff5500"))
        qFatal("The themed icon did not render beside the panel title");
    Panel unthemed(&theme);
    unthemed.setTitle(QStringLiteral("Unchanged"));
    auto *ordinaryTitle = unthemed.findChild<Decal *>();
    ordinaryTitle->resize(120, ordinaryTitle->sizeHint().height());
    const auto ordinaryTitlePixels = ordinaryTitle->grab().toImage();
    if (ordinaryTitlePixels.pixelColor(4, ordinaryTitlePixels.height() / 2) == QColor("#ff5500"))
        qFatal("The network icon leaked into other panel titles");

    const auto bars = network.findChildren<Krell *>();
    if (bars.size() != 2) qFatal("Network panel lost its RX/TX bars");
    const auto checkBar = [](Krell *bar, const QColor &track, const QColor &indicator) {
        bar->resize(120, 8);
        bar->setValue(0.5);
        const auto image = bar->grab().toImage();
        const int y = image.height() / 2;
        if (image.pixelColor(image.width() / 4, y) != track
            || image.pixelColor(image.width() / 2, y) != indicator)
            qFatal("Krell track or moving indicator used the wrong color");
    };
    for (auto *bar : bars) checkBar(bar, QColor("#a9a9a9"), QColor("#3e3e3e"));
    Krell ordinary(&theme);
    checkBar(&ordinary, theme.color(QStringLiteral("krell_track")),
             theme.color(QStringLiteral("krell_indicator")));
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir config;
    if (!config.isValid()) qFatal("Temporary config unavailable");
    QCoreApplication::setOrganizationName(QStringLiteral("chart-panel-layout-test"));
    QCoreApplication::setApplicationName(QStringLiteral("chart-panel-layout-test"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.path());
    QSettings settings;
    settings.setValue(QStringLiteral("monitors/cpu/label_format"), QStringLiteral("Core {index}"));
    settings.setValue(QStringLiteral("monitors/net/eth0"), true);

    Theme theme;
    CpuStat::setReadOverride(cpuSamples);
    NetStat::setReadOverride(netSamples);
    DiskStat::setReadOverride(diskSamples);
    for (const QString &mode : {QStringLiteral("per-core"), QStringLiteral("aggregate"),
                                QStringLiteral("combined")}) {
        settings.setValue(QStringLiteral("monitors/cpu/mode"), mode);
        CpuMonitor cpu(&theme);
        std::unique_ptr<QWidget> panel(cpu.createWidget(nullptr));
        auto *chart = checkPanel(*panel, mode == QStringLiteral("per-core")
                                 ? QStringLiteral("Core 0") : QStringLiteral("CPU"), 1);
        ++ticks;
        cpu.tick();
        if (!chart->accessibleName().startsWith(QStringLiteral("50%")))
            qFatal("CPU chart readout does not show the current percentage first");
    }

    NetMonitor net(&theme);
    std::unique_ptr<QWidget> link(net.createWidget(nullptr));
    auto *netChart = checkPanel(*link, QStringLiteral("Uplink"), 2);
    DiskMonitor disk(&theme);
    std::unique_ptr<QWidget> device(disk.createWidget(nullptr));
    // Let Windows initialize its disk layout signature before obtaining the chart.
    disk.tick();
    auto *diskChart = checkPanel(*device, QStringLiteral("testdisk"), 2);
    ++ticks;
    net.tick();
    disk.tick();
    if (!netChart->accessibleName().startsWith(QStringLiteral("RX "))
        || !netChart->accessibleName().contains(QStringLiteral(" TX "))
        || !diskChart->accessibleName().startsWith(QStringLiteral("R "))
        || !diskChart->accessibleName().contains(QStringLiteral(" W ")))
        qFatal("The throughput readouts lost their direction labels");
    checkOverlay(theme);
    checkChartStyling(theme, config.path(), *link);
    checkTitleFont(theme, config.path());
    checkMemoryMeters();
    checkCombinedCpuFill(config.path());
    checkSavedAggregateCpu();
    checkOptInSurfaces(config.path());
    qInfo("PASS: chart layout, live readouts, moving tracks, and compact Memory and Swap meters");
}
