#include "monitors/CpuMonitor.h"
#include "monitors/DiskMonitor.h"
#include "monitors/NetMonitor.h"
#include "theme/Theme.h"
#include "widgets/Chart.h"
#include "widgets/Decal.h"
#include "widgets/Krell.h"
#include "widgets/Panel.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontMetrics>
#include <QImage>
#include <QLayout>
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
    checkTitleFont(theme, config.path());
    qInfo("PASS: title, live chart readout and moving tracks on all charted monitors");
}
