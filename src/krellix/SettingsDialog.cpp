#include "SettingsDialog.h"

#include "krellix/PluginLoader.h"
#include "sysdep/CpuStat.h"
#include "sysdep/NetStat.h"
#include "theme/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>

namespace {

constexpr int kMinPanelWidth   = 80;
constexpr int kMaxPanelWidth   = 600;
constexpr int kMinKrellHeight  = 4;
constexpr int kMaxKrellHeight  = 64;
constexpr int kMinChartHeight  = 12;
constexpr int kMaxChartHeight  = 200;
constexpr int kMinUpdateMs     = 100;       // 10 Hz
constexpr int kMaxUpdateMs     = 10000;     // 0.1 Hz
constexpr int kMinScrollPps    = 5;
constexpr int kMaxScrollPps    = 200;
constexpr int kDefaultScrollPps = 30;

} // namespace

SettingsDialog::SettingsDialog(Theme *theme, QWidget *parent)
    : QDialog(parent)
    , m_theme(theme)
{
    Q_ASSERT(m_theme);
    setWindowTitle(QStringLiteral("krellix — Settings"));
    setModal(true);

    auto *root = new QVBoxLayout(this);

    // ---------- General ----------
    auto *generalBox  = new QGroupBox(QStringLiteral("General"), this);
    auto *generalForm = new QFormLayout(generalBox);

    m_themeCombo = new QComboBox(generalBox);
    m_themeCombo->addItems(Theme::availableThemes());
    generalForm->addRow(QStringLiteral("Theme:"), m_themeCombo);

    m_alwaysOnTop = new QCheckBox(QStringLiteral("Keep window above other windows"),
                                  generalBox);
    generalForm->addRow(QString(), m_alwaysOnTop);

    m_clockAtTop = new QCheckBox(QStringLiteral("Clock right under hostname (vs. at bottom)"),
                                 generalBox);
    generalForm->addRow(QString(), m_clockAtTop);

    m_militaryTime = new QCheckBox(QStringLiteral("24-hour time (uncheck for 12-hour AM/PM)"),
                                   generalBox);
    generalForm->addRow(QString(), m_militaryTime);

    m_showFqdn = new QCheckBox(QStringLiteral("Show fully-qualified hostname"),
                               generalBox);
    generalForm->addRow(QString(), m_showFqdn);

    m_updateMs = new QSpinBox(generalBox);
    m_updateMs->setRange(kMinUpdateMs, kMaxUpdateMs);
    m_updateMs->setSingleStep(100);
    m_updateMs->setSuffix(QStringLiteral(" ms"));
    generalForm->addRow(QStringLiteral("Update interval:"), m_updateMs);

    m_scrollSpeed = new QSpinBox(generalBox);
    m_scrollSpeed->setRange(kMinScrollPps, kMaxScrollPps);
    m_scrollSpeed->setSingleStep(5);
    m_scrollSpeed->setSuffix(QStringLiteral(" px/s"));
    generalForm->addRow(QStringLiteral("Ticker scroll speed:"), m_scrollSpeed);

    // ---------- Appearance ----------
    auto *appearanceBox  = new QGroupBox(QStringLiteral("Appearance"), this);
    auto *appearanceForm = new QFormLayout(appearanceBox);

    m_panelWidth = new QSpinBox(appearanceBox);
    m_panelWidth->setRange(kMinPanelWidth, kMaxPanelWidth);
    m_panelWidth->setSingleStep(10);
    m_panelWidth->setSuffix(QStringLiteral(" px"));
    appearanceForm->addRow(QStringLiteral("Window width:"), m_panelWidth);

    m_krellHeight = new QSpinBox(appearanceBox);
    m_krellHeight->setRange(kMinKrellHeight, kMaxKrellHeight);
    m_krellHeight->setSuffix(QStringLiteral(" px"));
    appearanceForm->addRow(QStringLiteral("Krell height:"), m_krellHeight);

    m_chartHeight = new QSpinBox(appearanceBox);
    m_chartHeight->setRange(kMinChartHeight, kMaxChartHeight);
    m_chartHeight->setSuffix(QStringLiteral(" px"));
    appearanceForm->addRow(QStringLiteral("Chart height:"), m_chartHeight);

    // ---------- Built-in monitors ----------
    auto *monitorsBox  = new QGroupBox(QStringLiteral("Built-in monitors"), this);
    auto *monitorsForm = new QVBoxLayout(monitorsBox);
    m_hostEnabled   = new QCheckBox(QStringLiteral("Host (hostname + kernel)"),       monitorsBox);
    m_clockEnabled  = new QCheckBox(QStringLiteral("Clock + date"),                   monitorsBox);
    m_cpuEnabled    = new QCheckBox(QStringLiteral("CPU (per-core krell + chart)"),   monitorsBox);
    m_memEnabled    = new QCheckBox(QStringLiteral("Memory + Swap"),                  monitorsBox);
    m_uptimeEnabled = new QCheckBox(QStringLiteral("Uptime"),                         monitorsBox);
    m_netEnabled    = new QCheckBox(QStringLiteral("Net (per-interface RX/TX)"),      monitorsBox);
    m_diskEnabled   = new QCheckBox(QStringLiteral("Disk I/O (per-disk read/write)"), monitorsBox);
    monitorsForm->addWidget(m_hostEnabled);
    monitorsForm->addWidget(m_clockEnabled);
    monitorsForm->addWidget(m_cpuEnabled);
    monitorsForm->addWidget(m_memEnabled);
    monitorsForm->addWidget(m_uptimeEnabled);
    monitorsForm->addWidget(m_netEnabled);
    monitorsForm->addWidget(m_diskEnabled);

    // ---------- CPU display (mode + per-core enable) ----------
    auto *cpuBox    = new QGroupBox(QStringLiteral("CPU display"), this);
    auto *cpuLayout = new QVBoxLayout(cpuBox);

    auto *cpuModeRow = new QFormLayout;
    auto *cpuModeCombo = new QComboBox(cpuBox);
    cpuModeCombo->addItem(QStringLiteral("Per-core (one panel each)"),
                          QStringLiteral("per-core"));
    cpuModeCombo->addItem(QStringLiteral("Aggregate (single panel for all cores)"),
                          QStringLiteral("aggregate"));
    {
        QSettings cs;
        const QString cur = cs.value(QStringLiteral("monitors/cpu/mode"),
                                     QStringLiteral("per-core")).toString();
        const int idx = cpuModeCombo->findData(cur);
        if (idx >= 0) cpuModeCombo->setCurrentIndex(idx);
    }
    cpuModeRow->addRow(QStringLiteral("Mode:"), cpuModeCombo);
    cpuLayout->addLayout(cpuModeRow);

    auto *coresLabel = new QLabel(QStringLiteral("Cores (per-core mode only):"),
                                  cpuBox);
    cpuLayout->addWidget(coresLabel);

    const QList<CpuSample> coreList = CpuStat::read();
    if (coreList.size() < 2) {
        auto *lbl = new QLabel(QStringLiteral("(no per-core data — connect to a host first)"),
                               cpuBox);
        cpuLayout->addWidget(lbl);
    } else {
        QSettings cs;
        for (int i = 1; i < coreList.size(); ++i) {
            const CpuSample &smp = coreList[i];
            const QString key = QStringLiteral("monitors/cpu/")
                                + QString::number(smp.index);
            const bool checked = cs.value(key, true).toBool();
            auto *cb = new QCheckBox(smp.name, cpuBox);
            cb->setChecked(checked);
            cpuLayout->addWidget(cb);

            const int cpuIdx = smp.index;
            connect(cb, &QCheckBox::toggled, this,
                    [this, cpuIdx](bool v) {
                        QSettings().setValue(QStringLiteral("monitors/cpu/")
                                             + QString::number(cpuIdx), v);
                        emit settingsApplied();
                    });
        }
    }

    connect(cpuModeCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, cpuModeCombo](int) {
                QSettings().setValue(QStringLiteral("monitors/cpu/mode"),
                                     cpuModeCombo->currentData().toString());
                emit settingsApplied();
            });

    // ---------- Network interfaces (per-iface checkboxes) ----------
    auto *netIfaceBox    = new QGroupBox(QStringLiteral("Network interfaces"), this);
    auto *netIfaceLayout = new QVBoxLayout(netIfaceBox);
    const QList<NetSample> ifaces = NetStat::read();
    if (ifaces.isEmpty()) {
        auto *lbl = new QLabel(QStringLiteral("(no interfaces detected)"), netIfaceBox);
        netIfaceLayout->addWidget(lbl);
    } else {
        QSettings s;
        for (const NetSample &smp : ifaces) {
            const bool defEnabled = NetStat::isMainInterface(smp.name);
            const bool checked = s.value(QStringLiteral("monitors/net/") + smp.name,
                                         defEnabled).toBool();
            auto *cb = new QCheckBox(smp.name, netIfaceBox);
            cb->setChecked(checked);
            netIfaceLayout->addWidget(cb);

            const QString name = smp.name;
            connect(cb, &QCheckBox::toggled, this,
                    [this, name](bool v) {
                        QSettings().setValue(QStringLiteral("monitors/net/") + name, v);
                        emit settingsApplied();
                    });
        }
    }

    // ---------- Plugins (read-only list) ----------
    auto *pluginsBox    = new QGroupBox(QStringLiteral("Plugins"), this);
    auto *pluginsLayout = new QVBoxLayout(pluginsBox);
    m_pluginList = new QListWidget(pluginsBox);
    m_pluginList->setMaximumHeight(120);
    pluginsLayout->addWidget(m_pluginList);
    auto *pluginNote = new QLabel(
        QStringLiteral("Drop plugin .so files in ~/.local/share/krellix/plugins/.\n"
                       "Restart krellix to pick up new plugins."),
        pluginsBox);
    pluginNote->setWordWrap(true);
    pluginsLayout->addWidget(pluginNote);

    root->addWidget(generalBox);
    root->addWidget(appearanceBox);
    root->addWidget(monitorsBox);
    root->addWidget(cpuBox);
    root->addWidget(netIfaceBox);
    root->addWidget(pluginsBox);

    auto *liveHint = new QLabel(
        QStringLiteral("Every change applies immediately."),
        this);
    liveHint->setWordWrap(true);
    QFont hintFont = liveHint->font();
    hintFont.setItalic(true);
    liveHint->setFont(hintFont);
    root->addWidget(liveHint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    root->addWidget(buttons);

    // Load values FIRST (with signal-blocking so we don't fire spurious
    // change events during initial population), then wire up live signals.
    {
        QSignalBlocker b1(m_themeCombo);
        QSignalBlocker b2(m_alwaysOnTop);
        QSignalBlocker b3(m_clockAtTop);
        QSignalBlocker b4(m_militaryTime);
        QSignalBlocker b5(m_showFqdn);
        QSignalBlocker b6(m_updateMs);
        QSignalBlocker b7(m_scrollSpeed);
        QSignalBlocker b8(m_panelWidth);
        QSignalBlocker b9(m_krellHeight);
        QSignalBlocker b10(m_chartHeight);
        QSignalBlocker b11(m_hostEnabled);
        QSignalBlocker b12(m_clockEnabled);
        QSignalBlocker b13(m_cpuEnabled);
        QSignalBlocker b14(m_memEnabled);
        QSignalBlocker b15(m_uptimeEnabled);
        QSignalBlocker b16(m_netEnabled);
        QSignalBlocker b17(m_diskEnabled);
        loadFromSettings();
    }

    // ---------- Live wiring ----------
    connect(m_themeCombo,
            QOverload<const QString &>::of(&QComboBox::currentTextChanged),
            this, [this](const QString &name) {
                QSettings().setValue(QStringLiteral("theme/name"), name);
                emit themeNameChanged(name);
            });
    connect(m_alwaysOnTop, &QCheckBox::toggled, this, [this](bool v) {
        QSettings().setValue(QStringLiteral("window/always_on_top"), v);
        emit alwaysOnTopChanged(v);
    });
    connect(m_clockAtTop, &QCheckBox::toggled, this, [this](bool v) {
        QSettings().setValue(QStringLiteral("window/clock_at_top"), v);
        emit settingsApplied();
    });
    connect(m_militaryTime, &QCheckBox::toggled, this, [](bool v) {
        // Live read by ClockMonitor; no rebuild needed.
        QSettings().setValue(QStringLiteral("clock/military"), v);
    });
    connect(m_showFqdn, &QCheckBox::toggled, this, [](bool v) {
        // Live read by HostMonitor; no rebuild needed.
        QSettings().setValue(QStringLiteral("host/show_fqdn"), v);
    });
    connect(m_updateMs, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v) {
                QSettings().setValue(QStringLiteral("update/interval_ms"), v);
                emit settingsApplied();
            });
    connect(m_scrollSpeed, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [](int v) {
                // Live read by Decal each tick.
                QSettings().setValue(QStringLiteral("appearance/scroll_pps"), v);
            });
    connect(m_panelWidth, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v) {
                QSettings().setValue(QStringLiteral("appearance/panel_width"), v);
                emit settingsApplied();
            });
    connect(m_krellHeight, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v) {
                QSettings().setValue(QStringLiteral("appearance/krell_height"), v);
                emit settingsApplied();
            });
    connect(m_chartHeight, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v) {
                QSettings().setValue(QStringLiteral("appearance/chart_height"), v);
                emit settingsApplied();
            });

    auto wireMonitorToggle = [this](QCheckBox *cb, const char *key) {
        const QString k = QStringLiteral("monitors/") + QString::fromLatin1(key);
        connect(cb, &QCheckBox::toggled, this, [this, k](bool v) {
            QSettings().setValue(k, v);
            emit settingsApplied();
        });
    };
    wireMonitorToggle(m_hostEnabled,   "host");
    wireMonitorToggle(m_clockEnabled,  "clock");
    wireMonitorToggle(m_cpuEnabled,    "cpu");
    wireMonitorToggle(m_memEnabled,    "mem");
    wireMonitorToggle(m_uptimeEnabled, "uptime");
    wireMonitorToggle(m_netEnabled,    "net");
    wireMonitorToggle(m_diskEnabled,   "disk");

    populatePlugins();
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::loadFromSettings()
{
    QSettings s;

    const QString currentTheme = s.value(QStringLiteral("theme/name"),
                                         QStringLiteral("default")).toString();
    const int themeIdx = m_themeCombo->findText(currentTheme);
    if (themeIdx >= 0) m_themeCombo->setCurrentIndex(themeIdx);

    m_alwaysOnTop  ->setChecked(s.value(QStringLiteral("window/always_on_top"), false).toBool());
    m_clockAtTop   ->setChecked(s.value(QStringLiteral("window/clock_at_top"),  true ).toBool());
    m_militaryTime ->setChecked(s.value(QStringLiteral("clock/military"),       true ).toBool());
    m_showFqdn     ->setChecked(s.value(QStringLiteral("host/show_fqdn"),       false).toBool());

    m_panelWidth ->setValue(s.value(QStringLiteral("appearance/panel_width"),  100).toInt());
    m_krellHeight->setValue(s.value(QStringLiteral("appearance/krell_height"),
                                    m_theme->metric(QStringLiteral("krell_height"), 8)).toInt());
    m_chartHeight->setValue(s.value(QStringLiteral("appearance/chart_height"),
                                    m_theme->metric(QStringLiteral("chart_height"), 32)).toInt());

    m_updateMs   ->setValue(s.value(QStringLiteral("update/interval_ms"),  1000).toInt());
    m_scrollSpeed->setValue(s.value(QStringLiteral("appearance/scroll_pps"),
                                    kDefaultScrollPps).toInt());

    m_hostEnabled  ->setChecked(s.value(QStringLiteral("monitors/host"),   true).toBool());
    m_cpuEnabled   ->setChecked(s.value(QStringLiteral("monitors/cpu"),    true).toBool());
    m_memEnabled   ->setChecked(s.value(QStringLiteral("monitors/mem"),    true).toBool());
    m_clockEnabled ->setChecked(s.value(QStringLiteral("monitors/clock"),  true).toBool());
    m_uptimeEnabled->setChecked(s.value(QStringLiteral("monitors/uptime"), true).toBool());
    m_netEnabled   ->setChecked(s.value(QStringLiteral("monitors/net"),    true).toBool());
    m_diskEnabled  ->setChecked(s.value(QStringLiteral("monitors/disk"),   true).toBool());
}

void SettingsDialog::saveToSettings()
{
    // Retained for backwards compatibility; live signal handlers now do
    // each save inline. Unused — but harmless to keep the symbol.
}

void SettingsDialog::populatePlugins()
{
    m_pluginList->clear();
    QStringList found;
    for (const QString &dir : PluginLoader::searchPaths()) {
        QDir d(dir);
        if (!d.exists()) continue;
        const QStringList files = d.entryList(
            QStringList{QStringLiteral("*.so"),
                        QStringLiteral("*.dylib"),
                        QStringLiteral("*.dll")},
            QDir::Files);
        for (const QString &f : files)
            found << QDir(dir).filePath(f);
    }
    if (found.isEmpty()) {
        auto *item = new QListWidgetItem(QStringLiteral("(no plugins installed)"));
        item->setFlags(Qt::NoItemFlags);
        m_pluginList->addItem(item);
    } else {
        for (const QString &p : found) m_pluginList->addItem(p);
    }
}

void SettingsDialog::onAccept()
{
    // No-op now — live signals handle saves; Close button just dismisses.
    accept();
}
