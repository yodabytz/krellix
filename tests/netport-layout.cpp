#include "monitors/NetPortMonitor.h"
#include "sysdep/NetPortStat.h"
#include "theme/Theme.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFontMetrics>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLayout>
#include <QSettings>
#include <QTemporaryDir>
#include <QWidget>
#include <memory>

static QList<NetPortSample> samples()
{
    QList<NetPortSample> out;
    for (int i = 0; i < 137; ++i)
        out.append({QStringLiteral("tcp"), 443, quint16(10000 + i),
                    QStringLiteral("192.0.2.1"), QStringLiteral("01")});
    return out;
}

static QList<QWidget *> rows(QWidget *panel)
{
    QList<QWidget *> out;
    for (QWidget *widget : panel->findChildren<QWidget *>())
        if (widget->toolTip().startsWith(QStringLiteral("Watch"))) out.append(widget);
    return out;
}

int main(int argc, char **argv)
{
    QTemporaryDir config;
    if (!config.isValid()) qFatal("Temporary config unavailable");
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("netport-layout-test"));
    QCoreApplication::setApplicationName(QStringLiteral("netport-layout-test"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.path());
    QSettings settings;
    for (int i = 1; i <= 8; ++i) {
        const QString key = QStringLiteral("monitors/netports/watch%1/").arg(i);
        settings.setValue(key + QStringLiteral("enabled"), true);
        settings.setValue(key + QStringLiteral("label"), QStringLiteral("Watch%1").arg(i));
        settings.setValue(key + QStringLiteral("ports"), QStringLiteral("443"));
    }
    settings.sync();
    NetPortStat::setReadOverride(samples);
    Theme theme;
    NetPortMonitor monitor(&theme);
    std::unique_ptr<QWidget> panel(monitor.createWidget(nullptr));
    panel->resize(180, panel->sizeHint().height());
    panel->show();
    app.processEvents();
    const auto watchRows = rows(panel.get());
    if (watchRows.size() != 8) qFatal("Expected eight watch rows");
    const QFont label = theme.font(QStringLiteral("label"));
    for (QWidget *row : watchRows) {
#ifdef Q_OS_LINUX
        if (!qFuzzyCompare(row->font().pointSizeF(), qMax(4.0, label.pointSizeF() * 0.75)))
            qFatal("Linux watch font is not compact");
        if (row->parentWidget()->layout()->spacing() != 0) qFatal("Unexpected row gap");
#else
        if (row->font() != label) qFatal("Non-Linux font changed");
#endif
        if (row->sizeHint().height() != QFontMetrics(row->font()).height() + 2)
            qFatal("Row height differs from painted font metrics");
        if (row->height() != row->sizeHint().height()) qFatal("Row stretched vertically");
        if (!row->toolTip().contains(QStringLiteral("137 connections"))) qFatal("Count truncated");
    }
    // A theme's broad panel spacing must not add gaps between Linux watch rows.
    theme.setMetric(QStringLiteral("panel_spacing"), 12);
    app.processEvents();
#ifdef Q_OS_LINUX
    if (watchRows.first()->parentWidget()->layout()->spacing() != 0)
        qFatal("Panel spacing leaked into watch rows");
    theme.setMetric(QStringLiteral("netports_row_spacing"), 3);
    app.processEvents();
    if (watchRows.first()->parentWidget()->layout()->spacing() != 3)
        qFatal("Dedicated row spacing did not update live");
#endif
    const QString themeDir = config.path() + QStringLiteral("/krellix/themes/layout-test");
    QDir().mkpath(themeDir);
    QFile json(themeDir + QStringLiteral("/theme.json"));
    if (!json.open(QIODevice::WriteOnly)) qFatal("Cannot create theme fixture");
    json.write(QJsonDocument(QJsonObject{
        {QStringLiteral("fonts"), QJsonObject{
            {QStringLiteral("label"), QJsonObject{{QStringLiteral("size"), 16}, {QStringLiteral("bold"), true}}},
            {QStringLiteral("netports"), QJsonObject{{QStringLiteral("family"), QStringLiteral("Monospace")},
                {QStringLiteral("size"), 11}, {QStringLiteral("italic"), true}}}}},
        {QStringLiteral("metrics"), QJsonObject{{QStringLiteral("netports_row_spacing"), 1}}}
    }).toJson());
    json.close();
    if (!theme.load(QStringLiteral("layout-test"))) qFatal("Theme fixture did not load");
    panel->resize(180, panel->sizeHint().height());
    app.processEvents();
    const auto changedRows = rows(panel.get());
    if (changedRows != watchRows) qFatal("Theme change rebuilt configured watches");
    for (QWidget *row : changedRows) {
#ifdef Q_OS_LINUX
        if (row->font().pointSize() != 11 || !row->font().italic())
            qFatal("Explicit Net Ports theme font was not honored live");
        if (row->parentWidget()->layout()->spacing() != 1) qFatal("Theme spacing stale");
#else
        if (row->font().pointSize() != 16 || !row->font().bold()) qFatal("Non-Linux label font changed");
#endif
        if (row->sizeHint().height() != QFontMetrics(row->font()).height() + 2)
            qFatal("Live theme size hint stale");
    }
    if (qEnvironmentVariableIsSet("KRELLIX_LAYOUT_SCREENSHOT"))
        panel->grab().save(qEnvironmentVariable("KRELLIX_LAYOUT_SCREENSHOT"));
    panel.reset();
    NetPortStat::setReadOverride(nullptr);
    qInfo("PASS: compact font/row geometry, eight watches, 137-count tooltip, live theme overrides");
}
