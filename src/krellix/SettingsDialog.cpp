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
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringList>
#include <QVBoxLayout>

namespace {

constexpr int kMinPanelWidth   = 80;
constexpr int kMaxPanelWidth   = 600;
constexpr int kMinKrellHeight  = 4;
constexpr int kMaxKrellHeight  = 64;
constexpr int kMinChartHeight  = 12;
constexpr int kMaxChartHeight  = 200;
constexpr int kMinUpdateMs     = 100;
constexpr int kMaxUpdateMs     = 10000;
constexpr int kMinScrollPps    = 5;
constexpr int kMaxScrollPps    = 200;
constexpr int kDefaultScrollPps = 30;

} // namespace

// Build a category page that's a QWidget the caller adds to the stack.
// Each "section" inside the page is a QGroupBox so labels keep their grouping
// even though there's typically just one section per page now.

SettingsDialog::SettingsDialog(Theme *theme, QWidget *parent)
    : QDialog(parent)
    , m_theme(theme)
{
    Q_ASSERT(m_theme);
    setWindowTitle(QStringLiteral("krellix — Settings"));
    setModal(true);
    resize(640, 460);

    // ---------------- Outer layout: sidebar + stack + buttons ----------------
    auto *root = new QVBoxLayout(this);

    auto *splitRow = new QHBoxLayout;
    splitRow->setSpacing(8);
    auto *sidebar = new QListWidget(this);
    sidebar->setMaximumWidth(160);
    sidebar->setMinimumWidth(140);
    auto *stack   = new QStackedWidget(this);
    splitRow->addWidget(sidebar);
    splitRow->addWidget(stack, 1);
    root->addLayout(splitRow, 1);

    auto addPage = [sidebar, stack](const QString &title, QWidget *page) {
        sidebar->addItem(title);
        stack->addWidget(page);
    };

    // ---------------- General page ----------------
    {
        auto *page = new QWidget(stack);
        auto *form = new QFormLayout(page);

        m_themeCombo = new QComboBox(page);
        m_themeCombo->addItems(Theme::availableThemes());
        form->addRow(QStringLiteral("Theme:"), m_themeCombo);

        m_alwaysOnTop = new QCheckBox(QStringLiteral("Keep window above other windows"), page);
        form->addRow(QString(), m_alwaysOnTop);

        m_clockAtTop = new QCheckBox(QStringLiteral("Clock right under hostname (vs. at bottom)"), page);
        form->addRow(QString(), m_clockAtTop);

        m_militaryTime = new QCheckBox(QStringLiteral("24-hour time (uncheck for 12-hour AM/PM)"), page);
        form->addRow(QString(), m_militaryTime);

        m_showFqdn = new QCheckBox(QStringLiteral("Show fully-qualified hostname"), page);
        form->addRow(QString(), m_showFqdn);

        m_updateMs = new QSpinBox(page);
        m_updateMs->setRange(kMinUpdateMs, kMaxUpdateMs);
        m_updateMs->setSingleStep(100);
        m_updateMs->setSuffix(QStringLiteral(" ms"));
        form->addRow(QStringLiteral("Update interval:"), m_updateMs);

        m_scrollSpeed = new QSpinBox(page);
        m_scrollSpeed->setRange(kMinScrollPps, kMaxScrollPps);
        m_scrollSpeed->setSingleStep(5);
        m_scrollSpeed->setSuffix(QStringLiteral(" px/s"));
        form->addRow(QStringLiteral("Ticker scroll speed:"), m_scrollSpeed);

        addPage(QStringLiteral("General"), page);
    }

    // ---------------- Appearance page ----------------
    {
        auto *page = new QWidget(stack);
        auto *form = new QFormLayout(page);

        m_panelWidth = new QSpinBox(page);
        m_panelWidth->setRange(kMinPanelWidth, kMaxPanelWidth);
        m_panelWidth->setSingleStep(10);
        m_panelWidth->setSuffix(QStringLiteral(" px"));
        form->addRow(QStringLiteral("Window width:"), m_panelWidth);

        m_krellHeight = new QSpinBox(page);
        m_krellHeight->setRange(kMinKrellHeight, kMaxKrellHeight);
        m_krellHeight->setSuffix(QStringLiteral(" px"));
        form->addRow(QStringLiteral("Krell height:"), m_krellHeight);

        m_chartHeight = new QSpinBox(page);
        m_chartHeight->setRange(kMinChartHeight, kMaxChartHeight);
        m_chartHeight->setSuffix(QStringLiteral(" px"));
        form->addRow(QStringLiteral("Chart height:"), m_chartHeight);

        addPage(QStringLiteral("Appearance"), page);
    }

    // ---------------- Monitors page ----------------
    {
        auto *page = new QWidget(stack);
        auto *layout = new QVBoxLayout(page);
        m_hostEnabled    = new QCheckBox(QStringLiteral("Host (hostname + kernel)"),       page);
        m_clockEnabled   = new QCheckBox(QStringLiteral("Clock + date"),                   page);
        m_cpuEnabled     = new QCheckBox(QStringLiteral("CPU"),                            page);
        m_memEnabled     = new QCheckBox(QStringLiteral("Memory + Swap"),                  page);
        m_uptimeEnabled  = new QCheckBox(QStringLiteral("Uptime"),                         page);
        m_netEnabled     = new QCheckBox(QStringLiteral("Net (per-interface RX/TX)"),      page);
        m_diskEnabled    = new QCheckBox(QStringLiteral("Disk I/O (per-disk read/write)"), page);
        m_sensorsEnabled = new QCheckBox(QStringLiteral("Sensors (temps via /sys/class/hwmon)"), page);
        m_batteryEnabled = new QCheckBox(QStringLiteral("Battery (laptops)"),              page);
        layout->addWidget(m_hostEnabled);
        layout->addWidget(m_clockEnabled);
        layout->addWidget(m_cpuEnabled);
        layout->addWidget(m_memEnabled);
        layout->addWidget(m_uptimeEnabled);
        layout->addWidget(m_netEnabled);
        layout->addWidget(m_diskEnabled);
        layout->addWidget(m_sensorsEnabled);
        layout->addWidget(m_batteryEnabled);
        layout->addStretch(1);
        addPage(QStringLiteral("Monitors"), page);
    }

    // ---------------- CPU page (mode + per-core) ----------------
    QComboBox *cpuModeCombo = nullptr;
    {
        auto *page = new QWidget(stack);
        auto *layout = new QVBoxLayout(page);
        auto *modeForm = new QFormLayout;
        cpuModeCombo = new QComboBox(page);
        cpuModeCombo->addItem(QStringLiteral("Per-core (one panel each)"),
                              QStringLiteral("per-core"));
        cpuModeCombo->addItem(QStringLiteral("Aggregate (single panel for all cores)"),
                              QStringLiteral("aggregate"));
        cpuModeCombo->addItem(QStringLiteral("Combined (one chart, one line per core)"),
                              QStringLiteral("combined"));
        {
            QSettings cs;
            const QString cur = cs.value(QStringLiteral("monitors/cpu/mode"),
                                         QStringLiteral("per-core")).toString();
            const int idx = cpuModeCombo->findData(cur);
            if (idx >= 0) cpuModeCombo->setCurrentIndex(idx);
        }
        modeForm->addRow(QStringLiteral("Mode:"), cpuModeCombo);
        layout->addLayout(modeForm);

        auto *coresLabel = new QLabel(QStringLiteral("Cores (per-core mode only):"), page);
        layout->addWidget(coresLabel);

        const QList<CpuSample> coreList = CpuStat::read();
        if (coreList.size() < 2) {
            layout->addWidget(new QLabel(
                QStringLiteral("(no per-core data — connect to a host first)"), page));
        } else {
            QSettings cs;
            for (int i = 1; i < coreList.size(); ++i) {
                const CpuSample &smp = coreList[i];
                const QString key = QStringLiteral("monitors/cpu/")
                                    + QString::number(smp.index);
                const bool checked = cs.value(key, true).toBool();
                auto *cb = new QCheckBox(smp.name, page);
                cb->setChecked(checked);
                layout->addWidget(cb);
                const int cpuIdx = smp.index;
                connect(cb, &QCheckBox::toggled, this, [this, cpuIdx](bool v) {
                    QSettings().setValue(QStringLiteral("monitors/cpu/")
                                         + QString::number(cpuIdx), v);
                    emit settingsApplied();
                });
            }
        }
        layout->addStretch(1);
        addPage(QStringLiteral("CPU"), page);
    }

    // ---------------- Network page (per-iface checkboxes) ----------------
    {
        auto *page = new QWidget(stack);
        auto *layout = new QVBoxLayout(page);

        const QList<NetSample> ifaces = NetStat::read();
        if (ifaces.isEmpty()) {
            layout->addWidget(new QLabel(
                QStringLiteral("(no interfaces detected)"), page));
        } else {
            layout->addWidget(new QLabel(
                QStringLiteral("Interfaces (toggle to show/hide their panels):"), page));
            QSettings cs;
            for (const NetSample &smp : ifaces) {
                const bool defEnabled = NetStat::isMainInterface(smp.name);
                const bool checked = cs.value(
                    QStringLiteral("monitors/net/") + smp.name, defEnabled).toBool();
                auto *cb = new QCheckBox(smp.name, page);
                cb->setChecked(checked);
                layout->addWidget(cb);
                const QString name = smp.name;
                connect(cb, &QCheckBox::toggled, this, [this, name](bool v) {
                    QSettings().setValue(QStringLiteral("monitors/net/") + name, v);
                    emit settingsApplied();
                });
            }
        }
        layout->addStretch(1);
        addPage(QStringLiteral("Network"), page);
    }

    // ---------------- Plugins page (read-only) ----------------
    {
        auto *page = new QWidget(stack);
        auto *layout = new QVBoxLayout(page);
        m_pluginList = new QListWidget(page);
        layout->addWidget(m_pluginList);
        layout->addWidget(new QLabel(
            QStringLiteral("Drop plugin .so files in ~/.local/share/krellix/plugins/.\n"
                           "Restart krellix to pick up new plugins."), page));
        addPage(QStringLiteral("Plugins"), page);
    }

    sidebar->setCurrentRow(0);
    connect(sidebar, &QListWidget::currentRowChanged,
            stack, &QStackedWidget::setCurrentIndex);

    // ---------------- Footer: hint + close button ----------------
    auto *liveHint = new QLabel(
        QStringLiteral("Every change applies immediately."), this);
    liveHint->setWordWrap(true);
    QFont hintFont = liveHint->font();
    hintFont.setItalic(true);
    liveHint->setFont(hintFont);
    root->addWidget(liveHint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    root->addWidget(buttons);

    // ---------------- Initial values + live wiring ----------------
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
        QSignalBlocker b18(m_sensorsEnabled);
        QSignalBlocker b19(m_batteryEnabled);
        loadFromSettings();
    }

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
        QSettings().setValue(QStringLiteral("clock/military"), v);
    });
    connect(m_showFqdn, &QCheckBox::toggled, this, [](bool v) {
        QSettings().setValue(QStringLiteral("host/show_fqdn"), v);
    });
    connect(m_updateMs, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        QSettings().setValue(QStringLiteral("update/interval_ms"), v);
        emit settingsApplied();
    });
    connect(m_scrollSpeed, QOverload<int>::of(&QSpinBox::valueChanged), this, [](int v) {
        QSettings().setValue(QStringLiteral("appearance/scroll_pps"), v);
    });
    connect(m_panelWidth, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        QSettings().setValue(QStringLiteral("appearance/panel_width"), v);
        emit settingsApplied();
    });
    connect(m_krellHeight, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        QSettings().setValue(QStringLiteral("appearance/krell_height"), v);
        emit settingsApplied();
    });
    connect(m_chartHeight, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
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
    wireMonitorToggle(m_hostEnabled,    "host");
    wireMonitorToggle(m_clockEnabled,   "clock");
    wireMonitorToggle(m_cpuEnabled,     "cpu");
    wireMonitorToggle(m_memEnabled,     "mem");
    wireMonitorToggle(m_uptimeEnabled,  "uptime");
    wireMonitorToggle(m_netEnabled,     "net");
    wireMonitorToggle(m_diskEnabled,    "disk");
    wireMonitorToggle(m_sensorsEnabled, "sensors");
    wireMonitorToggle(m_batteryEnabled, "battery");

    if (cpuModeCombo) {
        connect(cpuModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, cpuModeCombo](int) {
                    QSettings().setValue(QStringLiteral("monitors/cpu/mode"),
                                         cpuModeCombo->currentData().toString());
                    emit settingsApplied();
                });
    }

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

    m_hostEnabled   ->setChecked(s.value(QStringLiteral("monitors/host"),    true).toBool());
    m_cpuEnabled    ->setChecked(s.value(QStringLiteral("monitors/cpu"),     true).toBool());
    m_memEnabled    ->setChecked(s.value(QStringLiteral("monitors/mem"),     true).toBool());
    m_clockEnabled  ->setChecked(s.value(QStringLiteral("monitors/clock"),   true).toBool());
    m_uptimeEnabled ->setChecked(s.value(QStringLiteral("monitors/uptime"),  true).toBool());
    m_netEnabled    ->setChecked(s.value(QStringLiteral("monitors/net"),     true).toBool());
    m_diskEnabled   ->setChecked(s.value(QStringLiteral("monitors/disk"),    true).toBool());
    m_sensorsEnabled->setChecked(s.value(QStringLiteral("monitors/sensors"), true).toBool());
    m_batteryEnabled->setChecked(s.value(QStringLiteral("monitors/battery"), true).toBool());
}

void SettingsDialog::saveToSettings()
{
    // No-op: live signals do per-control saves inline. Method kept for ABI.
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
    accept();
}
