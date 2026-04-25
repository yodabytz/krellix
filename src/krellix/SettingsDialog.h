#pragma once

#include <QDialog>

class PluginLoader;
class Theme;

class QCheckBox;
class QComboBox;
class QListWidget;
class QSpinBox;

// Single-pane settings dialog covering the basic GKrellM-style toggles:
// theme picker, always-on-top, clock-at-top, monitor enable/disable,
// panel/krell/chart sizing, update interval, and a (read-only for now)
// list of discovered plugins.
//
// Theme switch and Always-on-top apply immediately; everything else is
// persisted but takes effect at the next launch (a hint label says so).
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(Theme *theme, QWidget *parent = nullptr);
    ~SettingsDialog() override;

signals:
    void themeNameChanged(const QString &name);
    void alwaysOnTopChanged(bool on);
    // Generic settings-changed notification — MainWindow listens to rebuild
    // the panel stack so changes apply live.
    void settingsApplied();

private slots:
    void onAccept();

private:
    void loadFromSettings();
    void saveToSettings();
    void populatePlugins();

    Theme *m_theme;

    QComboBox   *m_themeCombo    = nullptr;
    QCheckBox   *m_alwaysOnTop   = nullptr;
    QCheckBox   *m_clockAtTop    = nullptr;
    QCheckBox   *m_militaryTime  = nullptr;
    QCheckBox   *m_showFqdn      = nullptr;
    QSpinBox    *m_panelWidth    = nullptr;
    QSpinBox    *m_krellHeight   = nullptr;
    QSpinBox    *m_chartHeight   = nullptr;
    QSpinBox    *m_updateMs      = nullptr;
    QSpinBox    *m_scrollSpeed   = nullptr;
    QCheckBox   *m_hostEnabled   = nullptr;
    QCheckBox   *m_cpuEnabled    = nullptr;
    QCheckBox   *m_memEnabled    = nullptr;
    QCheckBox   *m_clockEnabled  = nullptr;
    QCheckBox   *m_uptimeEnabled = nullptr;
    QCheckBox   *m_netEnabled    = nullptr;
    QCheckBox   *m_diskEnabled   = nullptr;
    QListWidget *m_pluginList    = nullptr;

    Q_DISABLE_COPY_MOVE(SettingsDialog)
};
