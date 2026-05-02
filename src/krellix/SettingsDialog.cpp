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
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int kMinPanelWidth   = 80;
constexpr int kMaxPanelWidth   = 600;
constexpr int kMinKrellHeight  = 4;
constexpr int kMaxKrellHeight  = 64;
constexpr int kMinChartHeight  = 12;
constexpr int kMaxChartHeight  = 200;
constexpr int kMinUpdateMs     = 100;
constexpr int kMaxUpdateMs     = 10000;
constexpr int kMinPluginUpdateMs = 1000;
constexpr int kMaxPluginUpdateMs = 300000;
constexpr int kMinScrollPps    = 5;
constexpr int kMaxScrollPps    = 200;
constexpr int kDefaultScrollPps = 30;

const QList<QPair<QString, QString>> kMonitorOrderItems = {
    {QStringLiteral("host"),    QStringLiteral("Host")},
    {QStringLiteral("cpu"),     QStringLiteral("CPU")},
    {QStringLiteral("proc"),    QStringLiteral("Proc")},
    {QStringLiteral("mem"),     QStringLiteral("Memory + Swap")},
    {QStringLiteral("uptime"),  QStringLiteral("Uptime")},
    {QStringLiteral("net"),     QStringLiteral("Net")},
    {QStringLiteral("krellkam"), QStringLiteral("Krellkam")},
    {QStringLiteral("krelldacious"), QStringLiteral("Krelldacious")},
    {QStringLiteral("krellweather"), QStringLiteral("Krellweather")},
    {QStringLiteral("krellwire"), QStringLiteral("Krellwire")},
    {QStringLiteral("disk"),    QStringLiteral("Disk I/O")},
    {QStringLiteral("sensors"), QStringLiteral("Sensors")},
    {QStringLiteral("battery"), QStringLiteral("Battery")},
};

QList<CpuSample> sortedCoreSamples(const QList<CpuSample> &samples)
{
    QList<CpuSample> cores;
    for (const CpuSample &s : samples) {
        if (s.index >= 0)
            cores.append(s);
    }
    std::sort(cores.begin(), cores.end(), [](const CpuSample &a, const CpuSample &b) {
        return a.index < b.index;
    });
    return cores;
}

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
        m_procEnabled    = new QCheckBox(QStringLiteral("Proc (process/user count)"),       page);
        m_uptimeEnabled  = new QCheckBox(QStringLiteral("Uptime"),                         page);
        m_netEnabled     = new QCheckBox(QStringLiteral("Net (per-interface RX/TX)"),      page);
        m_diskEnabled    = new QCheckBox(QStringLiteral("Disk I/O (per-disk read/write)"), page);
        m_sensorsEnabled = new QCheckBox(QStringLiteral("Sensors (temps via /sys/class/hwmon)"), page);
        m_batteryEnabled = new QCheckBox(QStringLiteral("Battery (laptops)"),              page);
        layout->addWidget(m_hostEnabled);
        layout->addWidget(m_clockEnabled);
        layout->addWidget(m_cpuEnabled);
        layout->addWidget(m_procEnabled);
        layout->addWidget(m_memEnabled);
        layout->addWidget(m_uptimeEnabled);
        layout->addWidget(m_netEnabled);
        layout->addWidget(m_diskEnabled);
        layout->addWidget(m_sensorsEnabled);
        layout->addWidget(m_batteryEnabled);
        layout->addStretch(1);
        addPage(QStringLiteral("Monitors"), page);
    }

    // ---------------- Order page ----------------
    {
        auto *page = new QWidget(stack);
        auto *layout = new QVBoxLayout(page);
        m_orderList = new QListWidget(page);

        QStringList order = QSettings().value(QStringLiteral("monitors/order")).toString()
                                .split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const auto &item : kMonitorOrderItems) {
            if (!order.contains(item.first))
                order.append(item.first);
        }
        for (const QString &id : order) {
            auto it = std::find_if(kMonitorOrderItems.cbegin(), kMonitorOrderItems.cend(),
                                   [&id](const auto &item) { return item.first == id; });
            if (it == kMonitorOrderItems.cend()) continue;
            auto *row = new QListWidgetItem(it->second, m_orderList);
            row->setData(Qt::UserRole, it->first);
        }

        auto *buttons = new QHBoxLayout;
        auto *up = new QPushButton(QStringLiteral("Up"), page);
        auto *down = new QPushButton(QStringLiteral("Down"), page);
        buttons->addWidget(up);
        buttons->addWidget(down);
        buttons->addStretch(1);
        layout->addWidget(m_orderList);
        layout->addLayout(buttons);

        connect(up, &QPushButton::clicked, this, [this]() {
            const int row = m_orderList ? m_orderList->currentRow() : -1;
            if (row <= 0) return;
            QListWidgetItem *item = m_orderList->takeItem(row);
            m_orderList->insertItem(row - 1, item);
            m_orderList->setCurrentRow(row - 1);
            saveMonitorOrder();
            emit panelStackChanged();
        });
        connect(down, &QPushButton::clicked, this, [this]() {
            const int row = m_orderList ? m_orderList->currentRow() : -1;
            if (row < 0 || row >= m_orderList->count() - 1) return;
            QListWidgetItem *item = m_orderList->takeItem(row);
            m_orderList->insertItem(row + 1, item);
            m_orderList->setCurrentRow(row + 1);
            saveMonitorOrder();
            emit panelStackChanged();
        });

        addPage(QStringLiteral("Order"), page);
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

        const QList<CpuSample> coreList = sortedCoreSamples(CpuStat::read());
        if (coreList.isEmpty()) {
            layout->addWidget(new QLabel(
                QStringLiteral("(no per-core data — connect to a host first)"), page));
        } else {
            QSettings cs;
            for (int slot = 0; slot < coreList.size(); ++slot) {
                const CpuSample &smp = coreList[slot];
                const QString key = QStringLiteral("monitors/cpu/")
                                    + QString::number(smp.index);
                const bool checked = cs.value(key, true).toBool();
                auto *cb = new QCheckBox(QStringLiteral("cpu%1").arg(slot), page);
                cb->setChecked(checked);
                layout->addWidget(cb);
                const int cpuIdx = smp.index;
                connect(cb, &QCheckBox::toggled, this, [this, cpuIdx](bool v) {
                    QSettings().setValue(QStringLiteral("monitors/cpu/")
                                         + QString::number(cpuIdx), v);
                    emit panelStackChanged();
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
            QSettings cs;
            auto *showAll = new QCheckBox(QStringLiteral("Show all detected networks"), page);
            showAll->setChecked(cs.value(QStringLiteral("monitors/net/show_all"), false).toBool());
            layout->addWidget(showAll);
            connect(showAll, &QCheckBox::toggled, this, [this](bool v) {
                QSettings().setValue(QStringLiteral("monitors/net/show_all"), v);
                emit panelStackChanged();
            });

            layout->addWidget(new QLabel(
                QStringLiteral("Interfaces (toggle to show/hide their panels):"), page));
            for (const NetSample &smp : ifaces) {
                const bool defEnabled =
                    (smp.name == QLatin1String("docker0"))
                    || NetStat::isMainInterface(smp.name);
                const bool checked = cs.value(
                    QStringLiteral("monitors/net/") + smp.name, defEnabled).toBool();
                const QString label = smp.alias.isEmpty()
                    ? smp.name
                    : QStringLiteral("%1 (%2)").arg(smp.alias, smp.name);
                auto *cb = new QCheckBox(label, page);
                cb->setChecked(checked);
                cb->setEnabled(!showAll->isChecked());
                layout->addWidget(cb);
                const QString name = smp.name;
                connect(cb, &QCheckBox::toggled, this, [this, name](bool v) {
                    QSettings().setValue(QStringLiteral("monitors/net/") + name, v);
                    emit panelStackChanged();
                });
                connect(showAll, &QCheckBox::toggled, cb, &QCheckBox::setDisabled);
            }
        }
        layout->addStretch(1);
        addPage(QStringLiteral("Network"), page);
    }

    // ---------------- Plugins page ----------------
    {
        auto *page = new QWidget(stack);
        auto *layout = new QVBoxLayout(page);
        auto *split = new QHBoxLayout;
        m_pluginList = new QListWidget(page);
        m_pluginList->setMinimumWidth(190);
        m_pluginList->setMaximumWidth(260);
        m_pluginStack = new QStackedWidget(page);
        split->addWidget(m_pluginList);
        split->addWidget(m_pluginStack, 1);
        layout->addLayout(split, 1);

        if (hasKrellkamPlugin()) {
            auto *pluginPage = new QWidget(m_pluginStack);
            auto *pluginLayout = new QVBoxLayout(pluginPage);
            auto *group = new QGroupBox(QStringLiteral("Krellkam cameras"), pluginPage);
            auto *form = new QFormLayout(group);

            m_krellkamEnabled = new QCheckBox(QStringLiteral("Show Krellkam panel"), group);
            form->addRow(QString(), m_krellkamEnabled);

            m_krellkamUpdateMs = new QSpinBox(group);
            m_krellkamUpdateMs->setRange(kMinPluginUpdateMs, kMaxPluginUpdateMs);
            m_krellkamUpdateMs->setSingleStep(1000);
            m_krellkamUpdateMs->setSuffix(QStringLiteral(" ms"));
            form->addRow(QStringLiteral("Update interval:"), m_krellkamUpdateMs);

            m_krellkamFieldHeight = new QSpinBox(group);
            m_krellkamFieldHeight->setRange(24, 240);
            m_krellkamFieldHeight->setSuffix(QStringLiteral(" px"));
            form->addRow(QStringLiteral("Image height:"), m_krellkamFieldHeight);

            for (int i = 1; i <= 5; ++i) {
                auto *row = new QWidget(group);
                auto *rowLayout = new QHBoxLayout(row);
                rowLayout->setContentsMargins(0, 0, 0, 0);
                rowLayout->setSpacing(6);
                auto *title = new QLineEdit(row);
                title->setClearButtonEnabled(true);
                title->setPlaceholderText(QStringLiteral("Title"));
                title->setMaximumWidth(120);
                auto *type = new QComboBox(row);
                type->addItem(QStringLiteral("Auto/Image"), QStringLiteral("auto"));
                type->addItem(QStringLiteral("MJPEG"), QStringLiteral("mjpeg"));
                type->addItem(QStringLiteral("Command"), QStringLiteral("command"));
                auto *edit = new QLineEdit(row);
                edit->setClearButtonEnabled(true);
                edit->setPlaceholderText(QStringLiteral("File path, image URL, MJPEG URL, or command"));
                rowLayout->addWidget(title);
                rowLayout->addWidget(type);
                rowLayout->addWidget(edit, 1);
                m_krellkamTitles.append(title);
                m_krellkamTypes.append(type);
                m_krellkamSources.append(edit);
                form->addRow(QStringLiteral("Camera %1:").arg(i), row);
            }

            pluginLayout->addWidget(group);
            pluginLayout->addStretch(1);
            m_pluginList->addItem(QStringLiteral("Krellkam"));
            m_pluginStack->addWidget(pluginPage);
        }

        if (hasKrelldaciousPlugin()) {
            auto *pluginPage = new QWidget(m_pluginStack);
            auto *pluginLayout = new QVBoxLayout(pluginPage);
            auto *group = new QGroupBox(QStringLiteral("Krelldacious"), pluginPage);
            auto *form = new QFormLayout(group);
            m_krelldaciousEnabled =
                new QCheckBox(QStringLiteral("Show Krelldacious panel"), group);
            form->addRow(QString(), m_krelldaciousEnabled);
            form->addRow(QStringLiteral("Controls:"),
                         new QLabel(QStringLiteral("Audacious playback and volume"), group));
            pluginLayout->addWidget(group);
            pluginLayout->addStretch(1);
            m_pluginList->addItem(QStringLiteral("Krelldacious"));
            m_pluginStack->addWidget(pluginPage);
        }

        if (hasKrellweatherPlugin()) {
            auto *pluginPage = new QWidget(m_pluginStack);
            auto *pluginLayout = new QVBoxLayout(pluginPage);
            auto *group = new QGroupBox(QStringLiteral("Krellweather"), pluginPage);
            auto *form = new QFormLayout(group);

            m_krellweatherEnabled =
                new QCheckBox(QStringLiteral("Show Krellweather panel"), group);
            form->addRow(QString(), m_krellweatherEnabled);

            m_krellweatherStation = new QLineEdit(group);
            m_krellweatherStation->setMaxLength(8);
            m_krellweatherStation->setClearButtonEnabled(true);
            m_krellweatherStation->setPlaceholderText(QStringLiteral("KMIA"));
            form->addRow(QStringLiteral("Station ID:"), m_krellweatherStation);

            auto *stationHelp = new QLabel(group);
            stationHelp->setTextFormat(Qt::RichText);
            stationHelp->setOpenExternalLinks(true);
            stationHelp->setTextInteractionFlags(Qt::TextBrowserInteraction);
            stationHelp->setWordWrap(true);
            stationHelp->setText(QStringLiteral(
                "<a href=\"https://www.cnrfc.noaa.gov/metar.php\">Find station IDs</a>"));
            form->addRow(QString(), stationHelp);

            m_krellweatherUnits = new QComboBox(group);
            m_krellweatherUnits->addItem(QStringLiteral("Fahrenheit"), QStringLiteral("F"));
            m_krellweatherUnits->addItem(QStringLiteral("Celsius"), QStringLiteral("C"));
            form->addRow(QStringLiteral("Temperature:"), m_krellweatherUnits);

            m_krellweatherUpdateMs = new QSpinBox(group);
            m_krellweatherUpdateMs->setRange(60000, 3600000);
            m_krellweatherUpdateMs->setSingleStep(60000);
            m_krellweatherUpdateMs->setSuffix(QStringLiteral(" ms"));
            form->addRow(QStringLiteral("Update interval:"), m_krellweatherUpdateMs);

            pluginLayout->addWidget(group);
            pluginLayout->addStretch(1);
            m_pluginList->addItem(QStringLiteral("Krellweather"));
            m_pluginStack->addWidget(pluginPage);
        }

        if (hasKrellwirePlugin()) {
            auto *pluginPage = new QWidget(m_pluginStack);
            auto *pluginLayout = new QVBoxLayout(pluginPage);
            auto *group = new QGroupBox(QStringLiteral("Krellwire"), pluginPage);
            auto *form = new QFormLayout(group);

            m_krellwireEnabled =
                new QCheckBox(QStringLiteral("Show Krellwire ticker"), group);
            form->addRow(QString(), m_krellwireEnabled);

            m_krellwireItems = new QSpinBox(group);
            m_krellwireItems->setRange(1, 3);
            form->addRow(QStringLiteral("Items per feed:"), m_krellwireItems);

            m_krellwireUpdateMs = new QSpinBox(group);
            m_krellwireUpdateMs->setRange(60000, 3600000);
            m_krellwireUpdateMs->setSingleStep(60000);
            m_krellwireUpdateMs->setSuffix(QStringLiteral(" ms"));
            form->addRow(QStringLiteral("Refresh interval:"), m_krellwireUpdateMs);

            m_krellwireScrollPps = new QSpinBox(group);
            m_krellwireScrollPps->setRange(10, 160);
            m_krellwireScrollPps->setSingleStep(5);
            m_krellwireScrollPps->setSuffix(QStringLiteral(" px/s"));
            form->addRow(QStringLiteral("Scroll speed:"), m_krellwireScrollPps);

            const QStringList defaults = {
                QStringLiteral("https://feeds.bbci.co.uk/news/rss.xml"),
                QStringLiteral("https://feeds.npr.org/1001/rss.xml"),
                QStringLiteral("https://rss.nytimes.com/services/xml/rss/nyt/HomePage.xml"),
            };
            for (int i = 0; i < 3; ++i) {
                auto *feed = new QLineEdit(group);
                feed->setClearButtonEnabled(true);
                feed->setPlaceholderText(defaults.at(i));
                m_krellwireFeeds.append(feed);
                form->addRow(QStringLiteral("Feed %1:").arg(i + 1), feed);
            }

            pluginLayout->addWidget(group);
            pluginLayout->addStretch(1);
            m_pluginList->addItem(QStringLiteral("Krellwire"));
            m_pluginStack->addWidget(pluginPage);
        }

        if (m_pluginList->count() == 0) {
            auto *empty = new QLabel(QStringLiteral("No editable plugins installed."), m_pluginStack);
            empty->setAlignment(Qt::AlignCenter);
            m_pluginList->addItem(QStringLiteral("(none)"));
            m_pluginStack->addWidget(empty);
        }

        m_pluginList->setCurrentRow(0);
        connect(m_pluginList, &QListWidget::currentRowChanged,
                m_pluginStack, &QStackedWidget::setCurrentIndex);
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
        QSignalBlocker b15(m_procEnabled);
        QSignalBlocker b16(m_uptimeEnabled);
        QSignalBlocker b17(m_netEnabled);
        QSignalBlocker b18(m_diskEnabled);
        QSignalBlocker b19(m_sensorsEnabled);
        QSignalBlocker b20(m_batteryEnabled);
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
        emit panelStackChanged();
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
            emit panelStackChanged();
        });
    };
    wireMonitorToggle(m_hostEnabled,    "host");
    wireMonitorToggle(m_clockEnabled,   "clock");
    wireMonitorToggle(m_cpuEnabled,     "cpu");
    wireMonitorToggle(m_procEnabled,    "proc");
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
                    emit panelStackChanged();
                });
    }

    if (m_krellkamEnabled) {
        connect(m_krellkamEnabled, &QCheckBox::toggled, this, [this](bool v) {
            QSettings().setValue(QStringLiteral("plugins/krellkam/enabled"), v);
            emit panelStackChanged();
        });
    }
    if (m_krellkamUpdateMs) {
        connect(m_krellkamUpdateMs, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int v) {
                    QSettings().setValue(QStringLiteral("plugins/krellkam/interval_ms"), v);
                    emit settingsApplied();
                });
    }
    if (m_krellkamFieldHeight) {
        connect(m_krellkamFieldHeight, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int v) {
                    QSettings().setValue(QStringLiteral("plugins/krellkam/field_height"), v);
                    emit settingsApplied();
                });
    }
    for (int i = 0; i < m_krellkamTitles.size(); ++i) {
        QLineEdit *edit = m_krellkamTitles.at(i);
        const QString key = QStringLiteral("plugins/krellkam/title%1").arg(i + 1);
        connect(edit, &QLineEdit::editingFinished, this, [this, edit, key]() {
            QSettings().setValue(key, edit->text().trimmed());
            emit settingsApplied();
        });
    }
    for (int i = 0; i < m_krellkamTypes.size(); ++i) {
        QComboBox *type = m_krellkamTypes.at(i);
        const QString key = QStringLiteral("plugins/krellkam/type%1").arg(i + 1);
        connect(type, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, type, key](int) {
                    QSettings().setValue(key, type->currentData().toString());
                    emit settingsApplied();
                });
    }
    for (int i = 0; i < m_krellkamSources.size(); ++i) {
        QLineEdit *edit = m_krellkamSources.at(i);
        const QString key = QStringLiteral("plugins/krellkam/source%1").arg(i + 1);
        connect(edit, &QLineEdit::editingFinished, this, [this, edit, key]() {
            QSettings().setValue(key, edit->text().trimmed());
            emit settingsApplied();
        });
    }
    if (m_krelldaciousEnabled) {
        connect(m_krelldaciousEnabled, &QCheckBox::toggled, this, [this](bool v) {
            QSettings().setValue(QStringLiteral("plugins/krelldacious/enabled"), v);
            emit panelStackChanged();
        });
    }
    if (m_krellweatherEnabled) {
        connect(m_krellweatherEnabled, &QCheckBox::toggled, this, [this](bool v) {
            QSettings().setValue(QStringLiteral("plugins/krellweather/enabled"), v);
            emit panelStackChanged();
        });
    }
    if (m_krellweatherStation) {
        connect(m_krellweatherStation, &QLineEdit::editingFinished, this, [this]() {
            const QString code = m_krellweatherStation->text().trimmed().toUpper();
            m_krellweatherStation->setText(code);
            QSettings().setValue(QStringLiteral("plugins/krellweather/station"), code);
            emit settingsApplied();
        });
    }
    if (m_krellweatherUnits) {
        connect(m_krellweatherUnits, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) {
                    QSettings().setValue(QStringLiteral("plugins/krellweather/units"),
                                         m_krellweatherUnits->currentData().toString());
                    emit settingsApplied();
                });
    }
    if (m_krellweatherUpdateMs) {
        connect(m_krellweatherUpdateMs, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int v) {
                    QSettings().setValue(QStringLiteral("plugins/krellweather/interval_ms"), v);
                    emit settingsApplied();
                });
    }
    if (m_krellwireEnabled) {
        connect(m_krellwireEnabled, &QCheckBox::toggled, this, [this](bool v) {
            QSettings().setValue(QStringLiteral("plugins/krellwire/enabled"), v);
            emit panelStackChanged();
        });
    }
    if (m_krellwireItems) {
        connect(m_krellwireItems, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int v) {
                    QSettings().setValue(QStringLiteral("plugins/krellwire/items"), v);
                    emit settingsApplied();
                });
    }
    if (m_krellwireUpdateMs) {
        connect(m_krellwireUpdateMs, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int v) {
                    QSettings().setValue(QStringLiteral("plugins/krellwire/interval_ms"), v);
                    emit settingsApplied();
                });
    }
    if (m_krellwireScrollPps) {
        connect(m_krellwireScrollPps, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int v) {
                    QSettings().setValue(QStringLiteral("plugins/krellwire/scroll_pps"), v);
                    emit settingsApplied();
                });
    }
    for (int i = 0; i < m_krellwireFeeds.size(); ++i) {
        QLineEdit *feed = m_krellwireFeeds.at(i);
        const QString key = QStringLiteral("plugins/krellwire/feed%1").arg(i + 1);
        connect(feed, &QLineEdit::editingFinished, this, [this, feed, key]() {
            QSettings().setValue(key, feed->text().trimmed());
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
    m_procEnabled   ->setChecked(s.value(QStringLiteral("monitors/proc"),    true).toBool());
    m_clockEnabled  ->setChecked(s.value(QStringLiteral("monitors/clock"),   true).toBool());
    m_uptimeEnabled ->setChecked(s.value(QStringLiteral("monitors/uptime"),  true).toBool());
    m_netEnabled    ->setChecked(s.value(QStringLiteral("monitors/net"),     true).toBool());
    m_diskEnabled   ->setChecked(s.value(QStringLiteral("monitors/disk"),    true).toBool());
    m_sensorsEnabled->setChecked(s.value(QStringLiteral("monitors/sensors"), true).toBool());
    m_batteryEnabled->setChecked(s.value(QStringLiteral("monitors/battery"), true).toBool());

    if (m_krellkamEnabled) {
        m_krellkamEnabled->setChecked(
            s.value(QStringLiteral("plugins/krellkam/enabled"), true).toBool());
    }
    if (m_krellkamUpdateMs) {
        m_krellkamUpdateMs->setValue(
            s.value(QStringLiteral("plugins/krellkam/interval_ms"), 5000).toInt());
    }
    if (m_krellkamFieldHeight) {
        m_krellkamFieldHeight->setValue(
            s.value(QStringLiteral("plugins/krellkam/field_height"), 48).toInt());
    }
    for (int i = 0; i < m_krellkamTitles.size(); ++i) {
        m_krellkamTitles.at(i)->setText(
            s.value(QStringLiteral("plugins/krellkam/title%1").arg(i + 1)).toString());
    }
    for (int i = 0; i < m_krellkamTypes.size(); ++i) {
        QComboBox *type = m_krellkamTypes.at(i);
        const QString value =
            s.value(QStringLiteral("plugins/krellkam/type%1").arg(i + 1),
                    QStringLiteral("auto")).toString();
        const int idx = type->findData(value);
        type->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    for (int i = 0; i < m_krellkamSources.size(); ++i) {
        m_krellkamSources.at(i)->setText(
            s.value(QStringLiteral("plugins/krellkam/source%1").arg(i + 1)).toString());
    }
    if (m_krelldaciousEnabled) {
        m_krelldaciousEnabled->setChecked(
            s.value(QStringLiteral("plugins/krelldacious/enabled"), true).toBool());
    }
    if (m_krellweatherEnabled) {
        m_krellweatherEnabled->setChecked(
            s.value(QStringLiteral("plugins/krellweather/enabled"), true).toBool());
    }
    if (m_krellweatherStation) {
        const QString code = s.value(QStringLiteral("plugins/krellweather/station"),
                                     QStringLiteral("KMIA")).toString().toUpper();
        m_krellweatherStation->setText(code);
    }
    if (m_krellweatherUnits) {
        const QString units = s.value(QStringLiteral("plugins/krellweather/units"),
                                      QStringLiteral("F")).toString().toUpper();
        const int idx = m_krellweatherUnits->findData(units);
        m_krellweatherUnits->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    if (m_krellweatherUpdateMs) {
        m_krellweatherUpdateMs->setValue(
            s.value(QStringLiteral("plugins/krellweather/interval_ms"), 600000).toInt());
    }
    if (m_krellwireEnabled) {
        m_krellwireEnabled->setChecked(
            s.value(QStringLiteral("plugins/krellwire/enabled"), true).toBool());
    }
    if (m_krellwireItems) {
        m_krellwireItems->setValue(
            s.value(QStringLiteral("plugins/krellwire/items"), 3).toInt());
    }
    if (m_krellwireUpdateMs) {
        m_krellwireUpdateMs->setValue(
            s.value(QStringLiteral("plugins/krellwire/interval_ms"), 600000).toInt());
    }
    if (m_krellwireScrollPps) {
        m_krellwireScrollPps->setValue(
            s.value(QStringLiteral("plugins/krellwire/scroll_pps"), 28).toInt());
    }
    const QStringList wireDefaults = {
        QStringLiteral("https://feeds.bbci.co.uk/news/rss.xml"),
        QStringLiteral("https://feeds.npr.org/1001/rss.xml"),
        QStringLiteral("https://rss.nytimes.com/services/xml/rss/nyt/HomePage.xml"),
    };
    for (int i = 0; i < m_krellwireFeeds.size(); ++i) {
        m_krellwireFeeds.at(i)->setText(
            s.value(QStringLiteral("plugins/krellwire/feed%1").arg(i + 1),
                    i < wireDefaults.size() ? wireDefaults.at(i) : QString()).toString());
    }
}

void SettingsDialog::saveToSettings()
{
    // No-op: live signals do per-control saves inline. Method kept for ABI.
}

void SettingsDialog::saveMonitorOrder()
{
    if (!m_orderList) return;
    QStringList ids;
    for (int i = 0; i < m_orderList->count(); ++i)
        ids << m_orderList->item(i)->data(Qt::UserRole).toString();
    QSettings().setValue(QStringLiteral("monitors/order"), ids.join(QLatin1Char(',')));
}

void SettingsDialog::populatePlugins()
{
    if (!m_pluginList) return;
    if (m_pluginList->count() > 0) return;

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

bool SettingsDialog::hasPlugin(const QString &id) const
{
    const QString needle = QStringLiteral("*") + id + QStringLiteral("*");
    for (const QString &dir : PluginLoader::searchPaths()) {
        QDir d(dir);
        if (!d.exists()) continue;
        const QStringList files = d.entryList(
            QStringList{needle + QStringLiteral(".so"),
                        needle + QStringLiteral(".dylib"),
                        needle + QStringLiteral(".dll")},
            QDir::Files | QDir::Readable);
        if (!files.isEmpty()) return true;
    }
    return false;
}

bool SettingsDialog::hasKrellkamPlugin() const
{
    return hasPlugin(QStringLiteral("krellkam"));
}

bool SettingsDialog::hasKrelldaciousPlugin() const
{
    return hasPlugin(QStringLiteral("krelldacious"));
}

bool SettingsDialog::hasKrellweatherPlugin() const
{
    return hasPlugin(QStringLiteral("krellweather"));
}

bool SettingsDialog::hasKrellwirePlugin() const
{
    return hasPlugin(QStringLiteral("krellwire"));
}

void SettingsDialog::onAccept()
{
    accept();
}
