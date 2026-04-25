#include "SettingsDialog.h"

#include "krellix/PluginLoader.h"
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
constexpr int kMinScrollPps    = 5;         // pixels/sec
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

    auto *generalBox = new QGroupBox(QStringLiteral("General"), this);
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

    auto *appearanceBox = new QGroupBox(QStringLiteral("Appearance"), this);
    auto *appearanceForm = new QFormLayout(appearanceBox);

    m_panelWidth = new QSpinBox(appearanceBox);
    m_panelWidth->setRange(kMinPanelWidth, kMaxPanelWidth);
    m_panelWidth->setSingleStep(10);
    m_panelWidth->setSuffix(QStringLiteral(" px"));
    appearanceForm->addRow(QStringLiteral("Panel width:"), m_panelWidth);

    m_krellHeight = new QSpinBox(appearanceBox);
    m_krellHeight->setRange(kMinKrellHeight, kMaxKrellHeight);
    m_krellHeight->setSuffix(QStringLiteral(" px"));
    appearanceForm->addRow(QStringLiteral("Krell height:"), m_krellHeight);

    m_chartHeight = new QSpinBox(appearanceBox);
    m_chartHeight->setRange(kMinChartHeight, kMaxChartHeight);
    m_chartHeight->setSuffix(QStringLiteral(" px"));
    appearanceForm->addRow(QStringLiteral("Chart height:"), m_chartHeight);

    auto *monitorsBox = new QGroupBox(QStringLiteral("Built-in monitors"), this);
    auto *monitorsForm = new QVBoxLayout(monitorsBox);
    m_hostEnabled  = new QCheckBox(QStringLiteral("Host (hostname + kernel)"),  monitorsBox);
    m_cpuEnabled   = new QCheckBox(QStringLiteral("CPU (per-core krell + chart)"), monitorsBox);
    m_memEnabled   = new QCheckBox(QStringLiteral("Memory + Swap"),               monitorsBox);
    m_clockEnabled = new QCheckBox(QStringLiteral("Clock + date"),                monitorsBox);
    monitorsForm->addWidget(m_hostEnabled);
    monitorsForm->addWidget(m_cpuEnabled);
    monitorsForm->addWidget(m_memEnabled);
    monitorsForm->addWidget(m_clockEnabled);

    auto *pluginsBox = new QGroupBox(QStringLiteral("Plugins"), this);
    auto *pluginsLayout = new QVBoxLayout(pluginsBox);
    m_pluginList = new QListWidget(pluginsBox);
    m_pluginList->setMaximumHeight(120);
    pluginsLayout->addWidget(m_pluginList);
    auto *pluginNote = new QLabel(
        QStringLiteral("Drop plugin .so files in:\n"
                       "  ~/.local/share/krellix/plugins/\n"
                       "Restart krellix to pick up new plugins."),
        pluginsBox);
    pluginNote->setWordWrap(true);
    pluginsLayout->addWidget(pluginNote);

    root->addWidget(generalBox);
    root->addWidget(appearanceBox);
    root->addWidget(monitorsBox);
    root->addWidget(pluginsBox);

    auto *liveHint = new QLabel(
        QStringLiteral("Settings apply immediately when you click OK."),
        this);
    liveHint->setWordWrap(true);
    QFont hintFont = liveHint->font();
    hintFont.setItalic(true);
    liveHint->setFont(hintFont);
    root->addWidget(liveHint);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    loadFromSettings();
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
    m_clockAtTop   ->setChecked(s.value(QStringLiteral("window/clock_at_top"),  true).toBool());
    m_militaryTime ->setChecked(s.value(QStringLiteral("clock/military"),       true).toBool());
    m_showFqdn     ->setChecked(s.value(QStringLiteral("host/show_fqdn"),       false).toBool());

    m_panelWidth ->setValue(s.value(QStringLiteral("appearance/panel_width"),
                                    m_theme->metric(QStringLiteral("panel_min_width"), 100)).toInt());
    m_krellHeight->setValue(s.value(QStringLiteral("appearance/krell_height"),
                                    m_theme->metric(QStringLiteral("krell_height"), 8)).toInt());
    m_chartHeight->setValue(s.value(QStringLiteral("appearance/chart_height"),
                                    m_theme->metric(QStringLiteral("chart_height"), 32)).toInt());

    m_updateMs   ->setValue(s.value(QStringLiteral("update/interval_ms"),  1000).toInt());
    m_scrollSpeed->setValue(s.value(QStringLiteral("appearance/scroll_pps"),
                                    kDefaultScrollPps).toInt());

    m_hostEnabled ->setChecked(s.value(QStringLiteral("monitors/host"),  true).toBool());
    m_cpuEnabled  ->setChecked(s.value(QStringLiteral("monitors/cpu"),   true).toBool());
    m_memEnabled  ->setChecked(s.value(QStringLiteral("monitors/mem"),   true).toBool());
    m_clockEnabled->setChecked(s.value(QStringLiteral("monitors/clock"), true).toBool());
}

void SettingsDialog::saveToSettings()
{
    QSettings s;

    const QString themeName   = m_themeCombo->currentText();
    const bool    alwaysOnTop = m_alwaysOnTop->isChecked();

    s.setValue(QStringLiteral("theme/name"),                themeName);
    s.setValue(QStringLiteral("window/always_on_top"),      alwaysOnTop);
    s.setValue(QStringLiteral("window/clock_at_top"),       m_clockAtTop->isChecked());
    s.setValue(QStringLiteral("clock/military"),            m_militaryTime->isChecked());
    s.setValue(QStringLiteral("host/show_fqdn"),            m_showFqdn->isChecked());
    s.setValue(QStringLiteral("appearance/panel_width"),    m_panelWidth->value());
    s.setValue(QStringLiteral("appearance/krell_height"),   m_krellHeight->value());
    s.setValue(QStringLiteral("appearance/chart_height"),   m_chartHeight->value());
    s.setValue(QStringLiteral("update/interval_ms"),        m_updateMs->value());
    s.setValue(QStringLiteral("appearance/scroll_pps"),     m_scrollSpeed->value());
    s.setValue(QStringLiteral("monitors/host"),  m_hostEnabled ->isChecked());
    s.setValue(QStringLiteral("monitors/cpu"),   m_cpuEnabled  ->isChecked());
    s.setValue(QStringLiteral("monitors/mem"),   m_memEnabled  ->isChecked());
    s.setValue(QStringLiteral("monitors/clock"), m_clockEnabled->isChecked());

    emit themeNameChanged(themeName);
    emit alwaysOnTopChanged(alwaysOnTop);
    emit settingsApplied();
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
    saveToSettings();
    accept();
}
