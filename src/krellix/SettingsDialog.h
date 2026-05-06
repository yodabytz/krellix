#pragma once

#include <QList>
#include <QDialog>
#include <QString>

class PluginLoader;
class Theme;

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLineEdit;
class QLabel;
class QListWidget;
class QDoubleSpinBox;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTcpServer;
class QVBoxLayout;

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
    void panelStackChanged();

private slots:
    void onAccept();

private:
    void loadFromSettings();
    void saveToSettings();
    void populatePlugins();
    void saveMonitorOrder();
    bool hasPlugin(const QString &id) const;
    bool hasKrellkamPlugin() const;
    bool hasKrelldaciousPlugin() const;
    bool hasKrellweatherPlugin() const;
    bool hasKrellwirePlugin() const;
    bool hasKrellmailPlugin() const;
    bool hasKrellSpectrumPlugin() const;
    int krellmailAccountCount() const;
    void rebuildKrellmailAccounts();
    void saveKrellmailAccount(int index);
    void addKrellmailAccount();
    void removeKrellmailAccount(int index);
    void beginKrellmailOAuth(int index);
    void handleKrellmailOAuthCallback();
    void finishKrellmailOAuth(QNetworkReply *reply);

    struct KrellmailAccountWidgets {
        QGroupBox *group = nullptr;
        QComboBox *protocol = nullptr;
        QComboBox *auth = nullptr;
        QLineEdit *host = nullptr;
        QSpinBox *port = nullptr;
        QCheckBox *ssl = nullptr;
        QLineEdit *user = nullptr;
        QLineEdit *password = nullptr;
        QLineEdit *oauthClientId = nullptr;
        QPushButton *authorize = nullptr;
        QPushButton *remove = nullptr;
        QLabel *status = nullptr;
    };

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
    QCheckBox   *m_procEnabled   = nullptr;
    QCheckBox   *m_clockEnabled  = nullptr;
    QCheckBox   *m_uptimeEnabled  = nullptr;
    QCheckBox   *m_netEnabled     = nullptr;
    QCheckBox   *m_netPortsEnabled = nullptr;
    QCheckBox   *m_diskEnabled    = nullptr;
    QCheckBox   *m_sensorsEnabled = nullptr;
    QCheckBox   *m_batteryEnabled = nullptr;
    QListWidget *m_pluginList    = nullptr;
    QStackedWidget *m_pluginStack = nullptr;
    QListWidget *m_orderList     = nullptr;
    QCheckBox   *m_krellkamEnabled = nullptr;
    QCheckBox   *m_krellkamAllowCommands = nullptr;
    QSpinBox    *m_krellkamUpdateMs = nullptr;
    QSpinBox    *m_krellkamFieldHeight = nullptr;
    QList<QLineEdit *> m_krellkamTitles;
    QList<QComboBox *> m_krellkamTypes;
    QList<QLineEdit *> m_krellkamSources;
    QCheckBox   *m_krelldaciousEnabled = nullptr;
    QCheckBox   *m_krellweatherEnabled = nullptr;
    QLineEdit   *m_krellweatherStation = nullptr;
    QComboBox   *m_krellweatherUnits = nullptr;
    QSpinBox    *m_krellweatherUpdateMs = nullptr;
    QCheckBox   *m_krellwireEnabled = nullptr;
    QSpinBox    *m_krellwireItems = nullptr;
    QSpinBox    *m_krellwireUpdateMs = nullptr;
    QSpinBox    *m_krellwireScrollPps = nullptr;
    QList<QLineEdit *> m_krellwireFeeds;
    QCheckBox   *m_krellmailEnabled = nullptr;
    QSpinBox    *m_krellmailUpdateMs = nullptr;
    QWidget     *m_krellmailAccountsContainer = nullptr;
    QVBoxLayout *m_krellmailAccountsLayout = nullptr;
    QPushButton *m_krellmailAddAccount = nullptr;
    QList<KrellmailAccountWidgets> m_krellmailAccounts;
    QTcpServer *m_krellmailOAuthServer = nullptr;
    QNetworkAccessManager *m_krellmailOAuthNetwork = nullptr;
    int m_krellmailOAuthAccount = -1;
    QString m_krellmailOAuthVerifier;
    QString m_krellmailOAuthState;
    QCheckBox   *m_krellspectrumEnabled = nullptr;
    QComboBox   *m_krellspectrumVisualMode = nullptr;
    QComboBox   *m_krellspectrumBandCount = nullptr;
    QDoubleSpinBox *m_krellspectrumSensitivity = nullptr;
    QDoubleSpinBox *m_krellspectrumSmoothing = nullptr;
    QComboBox   *m_krellspectrumColorMode = nullptr;
    QComboBox   *m_krellspectrumBackend = nullptr;
    QLineEdit   *m_krellspectrumDevice = nullptr;
    QSpinBox    *m_krellspectrumFps = nullptr;
    QSpinBox    *m_krellspectrumHeight = nullptr;
    QCheckBox   *m_krellspectrumPeakHold = nullptr;
    QCheckBox   *m_krellspectrumStereoSplit = nullptr;

    Q_DISABLE_COPY_MOVE(SettingsDialog)
};
