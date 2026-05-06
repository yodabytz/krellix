#include "SettingsDialog.h"

#include "krellix/PluginLoader.h"
#include "sysdep/CpuStat.h"
#include "sysdep/DiskStat.h"
#include "sysdep/NetStat.h"
#include "theme/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
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
constexpr int kMaxKrellmailAccounts = 10;

const QList<QPair<QString, QString>> kMonitorOrderItems = {
    {QStringLiteral("host"),    QStringLiteral("Host")},
    {QStringLiteral("cpu"),     QStringLiteral("CPU")},
    {QStringLiteral("proc"),    QStringLiteral("Proc")},
    {QStringLiteral("mem"),     QStringLiteral("Memory + Swap")},
    {QStringLiteral("uptime"),  QStringLiteral("Uptime")},
    {QStringLiteral("net"),     QStringLiteral("Net")},
    {QStringLiteral("netports"), QStringLiteral("Net Ports")},
    {QStringLiteral("krellkam"), QStringLiteral("Krellkam")},
    {QStringLiteral("krelldacious"), QStringLiteral("Krelldacious")},
    {QStringLiteral("krellweather"), QStringLiteral("Krellweather")},
    {QStringLiteral("krellwire"), QStringLiteral("Krellwire")},
    {QStringLiteral("krellmail"), QStringLiteral("Krellmail")},
    {QStringLiteral("krellspectrum"), QStringLiteral("KrellSpectrum")},
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

QString krellmailAccountKey(int index, const QString &name)
{
    return QStringLiteral("plugins/krellmail/account%1/%2").arg(index + 1).arg(name);
}

int defaultMailPort(const QString &protocol, bool ssl)
{
    if (protocol == QLatin1String("pop3"))
        return ssl ? 995 : 110;
    return ssl ? 993 : 143;
}

QString randomUrlToken(int bytes)
{
    QByteArray data;
    data.resize(bytes);
    for (int i = 0; i < bytes; ++i)
        data[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    return QString::fromLatin1(data.toBase64(QByteArray::Base64UrlEncoding
                                             | QByteArray::OmitTrailingEquals));
}

QString pkceChallenge(const QString &verifier)
{
    const QByteArray hash = QCryptographicHash::hash(verifier.toLatin1(),
                                                     QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toBase64(QByteArray::Base64UrlEncoding
                                             | QByteArray::OmitTrailingEquals));
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
        m_netPortsEnabled = new QCheckBox(QStringLiteral("Net Ports (connection counts)"),  page);
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
        layout->addWidget(m_netPortsEnabled);
        layout->addWidget(m_diskEnabled);
        const QList<DiskSample> disks = DiskStat::read();
        if (disks.isEmpty()) {
            layout->addWidget(new QLabel(QStringLiteral("    (no whole disks detected)"), page));
        } else {
            QSettings ds;
            layout->addWidget(new QLabel(QStringLiteral("Drives / SSDs:"), page));
            for (const DiskSample &disk : disks) {
                auto *cb = new QCheckBox(disk.name, page);
                cb->setChecked(ds.value(QStringLiteral("monitors/disk/devices/") + disk.name,
                                        true).toBool());
                cb->setEnabled(ds.value(QStringLiteral("monitors/disk"), true).toBool());
                layout->addWidget(cb);
                const QString name = disk.name;
                connect(cb, &QCheckBox::toggled, this, [this, name](bool v) {
                    QSettings().setValue(QStringLiteral("monitors/disk/devices/") + name, v);
                    emit panelStackChanged();
                });
                connect(m_diskEnabled, &QCheckBox::toggled, cb, &QCheckBox::setEnabled);
            }
        }
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
                auto *cb = new QCheckBox(QStringLiteral("cpu%1").arg(smp.index), page);
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

        auto *portsGroup = new QGroupBox(QStringLiteral("Port connection watches"), page);
        auto *portsGrid = new QGridLayout(portsGroup);
        portsGrid->addWidget(new QLabel(QStringLiteral("On"), portsGroup), 0, 0);
        portsGrid->addWidget(new QLabel(QStringLiteral("Label"), portsGroup), 0, 1);
        portsGrid->addWidget(new QLabel(QStringLiteral("Protocol"), portsGroup), 0, 2);
        portsGrid->addWidget(new QLabel(QStringLiteral("Ports / ranges"), portsGroup), 0, 3);

        QSettings ps;
        for (int i = 1; i <= 8; ++i) {
            const bool defEnabled = i == 1;
            const QString defLabel = i == 1 ? QStringLiteral("SSH") : QString();
            const QString defPorts = i == 1 ? QStringLiteral("22") : QString();
            const QString base = QStringLiteral("monitors/netports/watch%1/").arg(i);

            auto *enabled = new QCheckBox(portsGroup);
            enabled->setChecked(ps.value(base + QStringLiteral("enabled"), defEnabled).toBool());
            portsGrid->addWidget(enabled, i, 0);

            auto *label = new QLineEdit(portsGroup);
            label->setClearButtonEnabled(true);
            label->setPlaceholderText(QStringLiteral("SSH"));
            label->setText(ps.value(base + QStringLiteral("label"), defLabel).toString());
            portsGrid->addWidget(label, i, 1);

            auto *protocol = new QComboBox(portsGroup);
            protocol->addItem(QStringLiteral("TCP"), QStringLiteral("tcp"));
            protocol->addItem(QStringLiteral("UDP"), QStringLiteral("udp"));
            protocol->addItem(QStringLiteral("TCP + UDP"), QStringLiteral("all"));
            const QString proto = ps.value(base + QStringLiteral("protocol"),
                                           QStringLiteral("tcp")).toString().toLower();
            const int protoIdx = protocol->findData(proto);
            protocol->setCurrentIndex(protoIdx >= 0 ? protoIdx : 0);
            portsGrid->addWidget(protocol, i, 2);

            auto *ports = new QLineEdit(portsGroup);
            ports->setClearButtonEnabled(true);
            ports->setPlaceholderText(QStringLiteral("22, 80, 8000-8010"));
            ports->setText(ps.value(base + QStringLiteral("ports"), defPorts).toString());
            portsGrid->addWidget(ports, i, 3);

            connect(enabled, &QCheckBox::toggled, this, [this, base](bool v) {
                QSettings().setValue(base + QStringLiteral("enabled"), v);
                emit settingsApplied();
            });
            connect(label, &QLineEdit::editingFinished, this, [this, label, base]() {
                QSettings().setValue(base + QStringLiteral("label"), label->text().trimmed());
                emit settingsApplied();
            });
            connect(protocol, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this, protocol, base](int) {
                        QSettings().setValue(base + QStringLiteral("protocol"),
                                             protocol->currentData().toString());
                        emit settingsApplied();
                    });
            connect(ports, &QLineEdit::editingFinished, this, [this, ports, base]() {
                QSettings().setValue(base + QStringLiteral("ports"), ports->text().trimmed());
                emit settingsApplied();
            });
        }

        layout->addWidget(portsGroup);
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

            m_krellkamAllowCommands =
                new QCheckBox(QStringLiteral("Allow command sources"), group);
            m_krellkamAllowCommands->setToolTip(QStringLiteral(
                "Command sources run local shell commands. Leave this off unless the commands are trusted."));
            form->addRow(QString(), m_krellkamAllowCommands);

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
                type->addItem(QStringLiteral("YouTube live"), QStringLiteral("youtube"));
                type->addItem(QStringLiteral("Command"), QStringLiteral("command"));
                auto *edit = new QLineEdit(row);
                edit->setClearButtonEnabled(true);
                edit->setPlaceholderText(QStringLiteral("File path, image URL, MJPEG URL, YouTube URL, or command"));
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

        if (hasKrellmailPlugin()) {
            auto *pluginPage = new QWidget(m_pluginStack);
            auto *pluginLayout = new QVBoxLayout(pluginPage);
            auto *group = new QGroupBox(QStringLiteral("Krellmail"), pluginPage);
            auto *groupLayout = new QVBoxLayout(group);
            auto *form = new QFormLayout;

            m_krellmailEnabled =
                new QCheckBox(QStringLiteral("Show Krellmail mail monitor"), group);
            form->addRow(QString(), m_krellmailEnabled);

            m_krellmailUpdateMs = new QSpinBox(group);
            m_krellmailUpdateMs->setRange(30000, 3600000);
            m_krellmailUpdateMs->setSingleStep(30000);
            m_krellmailUpdateMs->setSuffix(QStringLiteral(" ms"));
            form->addRow(QStringLiteral("Update interval:"), m_krellmailUpdateMs);
            groupLayout->addLayout(form);

            m_krellmailAccountsContainer = new QWidget(group);
            m_krellmailAccountsLayout = new QVBoxLayout(m_krellmailAccountsContainer);
            m_krellmailAccountsLayout->setContentsMargins(0, 0, 0, 0);
            m_krellmailAccountsLayout->setSpacing(6);
            groupLayout->addWidget(m_krellmailAccountsContainer);

            auto *buttonRow = new QHBoxLayout;
            buttonRow->addStretch(1);
            m_krellmailAddAccount = new QPushButton(QStringLiteral("+"), group);
            m_krellmailAddAccount->setFixedWidth(32);
            m_krellmailAddAccount->setToolTip(QStringLiteral("Add mail account"));
            buttonRow->addWidget(m_krellmailAddAccount);
            groupLayout->addLayout(buttonRow);

            pluginLayout->addWidget(group);
            pluginLayout->addStretch(1);
            m_pluginList->addItem(QStringLiteral("Krellmail"));
            m_pluginStack->addWidget(pluginPage);
        }

        if (hasKrellSpectrumPlugin()) {
            auto *pluginPage = new QWidget(m_pluginStack);
            auto *pluginLayout = new QVBoxLayout(pluginPage);
            auto *group = new QGroupBox(QStringLiteral("KrellSpectrum"), pluginPage);
            auto *form = new QFormLayout(group);

            m_krellspectrumEnabled =
                new QCheckBox(QStringLiteral("Show KrellSpectrum visualizer"), group);
            form->addRow(QString(), m_krellspectrumEnabled);

            m_krellspectrumVisualMode = new QComboBox(group);
            m_krellspectrumVisualMode->addItem(QStringLiteral("Bars"), QStringLiteral("bars"));
            m_krellspectrumVisualMode->addItem(QStringLiteral("Smooth bars"), QStringLiteral("smooth_bars"));
            m_krellspectrumVisualMode->addItem(QStringLiteral("Waveform"), QStringLiteral("waveform"));
            m_krellspectrumVisualMode->addItem(QStringLiteral("Filled waveform"), QStringLiteral("filled_waveform"));
            m_krellspectrumVisualMode->addItem(QStringLiteral("Circular"), QStringLiteral("circular"));
            m_krellspectrumVisualMode->addItem(QStringLiteral("Particles"), QStringLiteral("particles"));
            m_krellspectrumVisualMode->addItem(QStringLiteral("Blurscope"), QStringLiteral("blur_scope"));
            m_krellspectrumVisualMode->addItem(QStringLiteral("Centered bars"), QStringLiteral("center_bars"));
            form->addRow(QStringLiteral("Visual mode:"), m_krellspectrumVisualMode);

            m_krellspectrumBandCount = new QComboBox(group);
            for (int bands : {16, 32, 64, 128})
                m_krellspectrumBandCount->addItem(QString::number(bands), bands);
            form->addRow(QStringLiteral("Bands:"), m_krellspectrumBandCount);

            m_krellspectrumSensitivity = new QDoubleSpinBox(group);
            m_krellspectrumSensitivity->setRange(0.2, 8.0);
            m_krellspectrumSensitivity->setSingleStep(0.05);
            m_krellspectrumSensitivity->setDecimals(2);
            form->addRow(QStringLiteral("Sensitivity:"), m_krellspectrumSensitivity);

            m_krellspectrumSmoothing = new QDoubleSpinBox(group);
            m_krellspectrumSmoothing->setRange(0.0, 0.95);
            m_krellspectrumSmoothing->setSingleStep(0.05);
            m_krellspectrumSmoothing->setDecimals(2);
            form->addRow(QStringLiteral("Smoothing:"), m_krellspectrumSmoothing);

            m_krellspectrumColorMode = new QComboBox(group);
            m_krellspectrumColorMode->addItem(QStringLiteral("Gradient"), QStringLiteral("gradient"));
            m_krellspectrumColorMode->addItem(QStringLiteral("Theme/static"), QStringLiteral("theme"));
            m_krellspectrumColorMode->addItem(QStringLiteral("Per-band"), QStringLiteral("per_band"));
            m_krellspectrumColorMode->addItem(QStringLiteral("Reactive"), QStringLiteral("reactive"));
            m_krellspectrumColorMode->addItem(QStringLiteral("Static color"), QStringLiteral("static"));
            form->addRow(QStringLiteral("Color mode:"), m_krellspectrumColorMode);

            m_krellspectrumBackend = new QComboBox(group);
            m_krellspectrumBackend->addItem(QStringLiteral("Auto"), QStringLiteral("auto"));
            m_krellspectrumBackend->addItem(QStringLiteral("PipeWire"), QStringLiteral("pipewire"));
            m_krellspectrumBackend->addItem(QStringLiteral("PulseAudio"), QStringLiteral("pulse"));
            form->addRow(QStringLiteral("Backend:"), m_krellspectrumBackend);

            m_krellspectrumDevice = new QLineEdit(group);
            m_krellspectrumDevice->setClearButtonEnabled(true);
            m_krellspectrumDevice->setPlaceholderText(QStringLiteral("blank = default; use monitor/source name if needed"));
            form->addRow(QStringLiteral("Device:"), m_krellspectrumDevice);

            m_krellspectrumFps = new QSpinBox(group);
            m_krellspectrumFps->setRange(8, 60);
            m_krellspectrumFps->setSuffix(QStringLiteral(" fps"));
            form->addRow(QStringLiteral("FPS cap:"), m_krellspectrumFps);

            m_krellspectrumHeight = new QSpinBox(group);
            m_krellspectrumHeight->setRange(24, 220);
            m_krellspectrumHeight->setSuffix(QStringLiteral(" px"));
            form->addRow(QStringLiteral("Height:"), m_krellspectrumHeight);

            m_krellspectrumPeakHold = new QCheckBox(QStringLiteral("Peak hold"), group);
            form->addRow(QString(), m_krellspectrumPeakHold);

            m_krellspectrumStereoSplit = new QCheckBox(QStringLiteral("Stereo split"), group);
            form->addRow(QString(), m_krellspectrumStereoSplit);

            pluginLayout->addWidget(group);
            pluginLayout->addStretch(1);
            m_pluginList->addItem(QStringLiteral("KrellSpectrum"));
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
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::onAccept);
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
        QSignalBlocker b18(m_netPortsEnabled);
        QSignalBlocker b19(m_diskEnabled);
        QSignalBlocker b20(m_sensorsEnabled);
        QSignalBlocker b21(m_batteryEnabled);
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
    wireMonitorToggle(m_netPortsEnabled, "netports");
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
    if (m_krellkamAllowCommands) {
        connect(m_krellkamAllowCommands, &QCheckBox::toggled, this, [this](bool v) {
            QSettings().setValue(QStringLiteral("plugins/krellkam/allow_command_sources"), v);
            emit settingsApplied();
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
    if (m_krellmailEnabled) {
        connect(m_krellmailEnabled, &QCheckBox::toggled, this, [this](bool v) {
            QSettings().setValue(QStringLiteral("plugins/krellmail/enabled"), v);
            emit panelStackChanged();
        });
    }
    if (m_krellmailUpdateMs) {
        connect(m_krellmailUpdateMs, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int v) {
                    QSettings().setValue(QStringLiteral("plugins/krellmail/update_ms"), v);
                    emit settingsApplied();
                });
    }
    if (m_krellmailAddAccount)
        connect(m_krellmailAddAccount, &QPushButton::clicked, this, &SettingsDialog::addKrellmailAccount);
    if (m_krellspectrumEnabled) {
        connect(m_krellspectrumEnabled, &QCheckBox::toggled, this, [this](bool v) {
            QSettings().setValue(QStringLiteral("plugins/krellspectrum/enabled"), v);
            emit panelStackChanged();
        });
    }
    if (m_krellspectrumVisualMode) {
        connect(m_krellspectrumVisualMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) {
                    QSettings().setValue(QStringLiteral("plugins/krellspectrum/visual_mode"),
                                         m_krellspectrumVisualMode->currentData().toString());
                    emit settingsApplied();
                });
    }
    if (m_krellspectrumBandCount) {
        connect(m_krellspectrumBandCount, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) {
                    QSettings().setValue(QStringLiteral("plugins/krellspectrum/bands"),
                                         m_krellspectrumBandCount->currentData().toInt());
                    emit settingsApplied();
                });
    }
    if (m_krellspectrumSensitivity) {
        connect(m_krellspectrumSensitivity, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this](double v) {
                    QSettings().setValue(QStringLiteral("plugins/krellspectrum/sensitivity"), v);
                    emit settingsApplied();
                });
    }
    if (m_krellspectrumSmoothing) {
        connect(m_krellspectrumSmoothing, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this](double v) {
                    QSettings().setValue(QStringLiteral("plugins/krellspectrum/smoothing"), v);
                    emit settingsApplied();
                });
    }
    if (m_krellspectrumColorMode) {
        connect(m_krellspectrumColorMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) {
                    QSettings().setValue(QStringLiteral("plugins/krellspectrum/color_mode"),
                                         m_krellspectrumColorMode->currentData().toString());
                    emit settingsApplied();
                });
    }
    if (m_krellspectrumBackend) {
        connect(m_krellspectrumBackend, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) {
                    QSettings().setValue(QStringLiteral("plugins/krellspectrum/backend"),
                                         m_krellspectrumBackend->currentData().toString());
                    emit settingsApplied();
                });
    }
    if (m_krellspectrumDevice) {
        connect(m_krellspectrumDevice, &QLineEdit::editingFinished, this, [this]() {
            QSettings().setValue(QStringLiteral("plugins/krellspectrum/device"),
                                 m_krellspectrumDevice->text().trimmed());
            emit settingsApplied();
        });
    }
    if (m_krellspectrumFps) {
        connect(m_krellspectrumFps, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int v) {
                    QSettings().setValue(QStringLiteral("plugins/krellspectrum/fps"), v);
                    emit settingsApplied();
                });
    }
    if (m_krellspectrumHeight) {
        connect(m_krellspectrumHeight, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int v) {
                    QSettings().setValue(QStringLiteral("plugins/krellspectrum/height"), v);
                    emit settingsApplied();
                });
    }
    if (m_krellspectrumPeakHold) {
        connect(m_krellspectrumPeakHold, &QCheckBox::toggled, this, [this](bool v) {
            QSettings().setValue(QStringLiteral("plugins/krellspectrum/peak_hold"), v);
            emit settingsApplied();
        });
    }
    if (m_krellspectrumStereoSplit) {
        connect(m_krellspectrumStereoSplit, &QCheckBox::toggled, this, [this](bool v) {
            QSettings().setValue(QStringLiteral("plugins/krellspectrum/stereo_split"), v);
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
    m_netPortsEnabled->setChecked(s.value(QStringLiteral("monitors/netports"), true).toBool());
    m_diskEnabled   ->setChecked(s.value(QStringLiteral("monitors/disk"),    true).toBool());
    m_sensorsEnabled->setChecked(s.value(QStringLiteral("monitors/sensors"), true).toBool());
    m_batteryEnabled->setChecked(s.value(QStringLiteral("monitors/battery"), true).toBool());

    if (m_krellkamEnabled) {
        m_krellkamEnabled->setChecked(
            s.value(QStringLiteral("plugins/krellkam/enabled"), true).toBool());
    }
    if (m_krellkamAllowCommands) {
        m_krellkamAllowCommands->setChecked(
            s.value(QStringLiteral("plugins/krellkam/allow_command_sources"), false).toBool());
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
    if (m_krellmailEnabled) {
        m_krellmailEnabled->setChecked(
            s.value(QStringLiteral("plugins/krellmail/enabled"), true).toBool());
    }
    if (m_krellmailUpdateMs) {
        m_krellmailUpdateMs->setValue(
            s.value(QStringLiteral("plugins/krellmail/update_ms"), 300000).toInt());
    }
    rebuildKrellmailAccounts();
    if (m_krellspectrumEnabled) {
        m_krellspectrumEnabled->setChecked(
            s.value(QStringLiteral("plugins/krellspectrum/enabled"), true).toBool());
    }
    if (m_krellspectrumVisualMode) {
        const QString mode = s.value(QStringLiteral("plugins/krellspectrum/visual_mode"),
                                     QStringLiteral("bars")).toString();
        const int idx = m_krellspectrumVisualMode->findData(mode);
        m_krellspectrumVisualMode->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    if (m_krellspectrumBandCount) {
        const int bands = s.value(QStringLiteral("plugins/krellspectrum/bands"), 32).toInt();
        const int idx = m_krellspectrumBandCount->findData(bands);
        m_krellspectrumBandCount->setCurrentIndex(idx >= 0 ? idx : 1);
    }
    if (m_krellspectrumSensitivity) {
        m_krellspectrumSensitivity->setValue(
            s.value(QStringLiteral("plugins/krellspectrum/sensitivity"), 1.35).toDouble());
    }
    if (m_krellspectrumSmoothing) {
        m_krellspectrumSmoothing->setValue(
            s.value(QStringLiteral("plugins/krellspectrum/smoothing"), 0.38).toDouble());
    }
    if (m_krellspectrumColorMode) {
        const QString mode = s.value(QStringLiteral("plugins/krellspectrum/color_mode"),
                                     QStringLiteral("gradient")).toString();
        const int idx = m_krellspectrumColorMode->findData(mode);
        m_krellspectrumColorMode->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    if (m_krellspectrumBackend) {
        const QString backend = s.value(QStringLiteral("plugins/krellspectrum/backend"),
                                        QStringLiteral("auto")).toString();
        const int idx = m_krellspectrumBackend->findData(backend);
        m_krellspectrumBackend->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    if (m_krellspectrumDevice) {
        m_krellspectrumDevice->setText(
            s.value(QStringLiteral("plugins/krellspectrum/device")).toString());
    }
    if (m_krellspectrumFps) {
        m_krellspectrumFps->setValue(
            s.value(QStringLiteral("plugins/krellspectrum/fps"), 45).toInt());
    }
    if (m_krellspectrumHeight) {
        m_krellspectrumHeight->setValue(
            s.value(QStringLiteral("plugins/krellspectrum/height"),
                    m_theme->metric(QStringLiteral("chart_height"), 44)).toInt());
    }
    if (m_krellspectrumPeakHold) {
        m_krellspectrumPeakHold->setChecked(
            s.value(QStringLiteral("plugins/krellspectrum/peak_hold"), true).toBool());
    }
    if (m_krellspectrumStereoSplit) {
        m_krellspectrumStereoSplit->setChecked(
            s.value(QStringLiteral("plugins/krellspectrum/stereo_split"), false).toBool());
    }
}

int SettingsDialog::krellmailAccountCount() const
{
    QSettings s;
    if (s.contains(QStringLiteral("plugins/krellmail/account_count")))
        return qBound(0, s.value(QStringLiteral("plugins/krellmail/account_count"), 1).toInt(),
                      kMaxKrellmailAccounts);

    int legacyCount = 0;
    for (int i = 0; i < 3; ++i) {
        if (!s.value(krellmailAccountKey(i, QStringLiteral("host"))).toString().trimmed().isEmpty()
            || !s.value(krellmailAccountKey(i, QStringLiteral("username"))).toString().trimmed().isEmpty())
            legacyCount = i + 1;
    }
    return qMax(1, legacyCount);
}

void SettingsDialog::rebuildKrellmailAccounts()
{
    if (!m_krellmailAccountsLayout) return;
    QSettings s;

    while (QLayoutItem *item = m_krellmailAccountsLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_krellmailAccounts.clear();

    const int count = krellmailAccountCount();
    s.setValue(QStringLiteral("plugins/krellmail/account_count"), count);

    for (int i = 0; i < count; ++i) {
        KrellmailAccountWidgets row;
        row.group = new QGroupBox(QStringLiteral("Account %1").arg(i + 1),
                                  m_krellmailAccountsContainer);
        auto *outer = new QVBoxLayout(row.group);
        auto *top = new QHBoxLayout;
        top->addStretch(1);
        row.remove = new QPushButton(QStringLiteral("-"), row.group);
        row.remove->setFixedWidth(32);
        row.remove->setToolTip(QStringLiteral("Delete mail account"));
        row.remove->setEnabled(count > 1);
        top->addWidget(row.remove);
        outer->addLayout(top);

        auto *form = new QFormLayout;
        row.protocol = new QComboBox(row.group);
        row.protocol->addItem(QStringLiteral("IMAP"), QStringLiteral("imap"));
        row.protocol->addItem(QStringLiteral("POP3"), QStringLiteral("pop3"));
        form->addRow(QStringLiteral("Protocol:"), row.protocol);

        row.auth = new QComboBox(row.group);
        row.auth->addItem(QStringLiteral("Password"), QStringLiteral("password"));
        row.auth->addItem(QStringLiteral("Gmail OAuth"), QStringLiteral("oauth"));
        form->addRow(QStringLiteral("Auth:"), row.auth);

        row.host = new QLineEdit(row.group);
        row.host->setClearButtonEnabled(true);
        row.host->setPlaceholderText(QStringLiteral("imap.gmail.com"));
        form->addRow(QStringLiteral("Server:"), row.host);

        row.port = new QSpinBox(row.group);
        row.port->setRange(1, 65535);
        form->addRow(QStringLiteral("Port:"), row.port);

        row.ssl = new QCheckBox(QStringLiteral("Use SSL/TLS"), row.group);
        form->addRow(QStringLiteral("Security:"), row.ssl);

        row.user = new QLineEdit(row.group);
        row.user->setClearButtonEnabled(true);
        form->addRow(QStringLiteral("User:"), row.user);

        row.password = new QLineEdit(row.group);
        row.password->setEchoMode(QLineEdit::Password);
        row.password->setClearButtonEnabled(true);
        form->addRow(QStringLiteral("Password:"), row.password);

        row.oauthClientId = new QLineEdit(row.group);
        row.oauthClientId->setClearButtonEnabled(true);
        row.oauthClientId->setPlaceholderText(QStringLiteral("Google OAuth desktop client ID"));
        form->addRow(QStringLiteral("OAuth client ID:"), row.oauthClientId);

        row.authorize = new QPushButton(QStringLiteral("Authorize Gmail"), row.group);
        form->addRow(QString(), row.authorize);

        row.status = new QLabel(row.group);
        row.status->setWordWrap(true);
        row.status->setTextInteractionFlags(Qt::TextSelectableByMouse
                                            | Qt::LinksAccessibleByMouse);
        form->addRow(QString(), row.status);
        outer->addLayout(form);
        m_krellmailAccountsLayout->addWidget(row.group);

        const QString proto = s.value(krellmailAccountKey(i, QStringLiteral("protocol")),
                                      QStringLiteral("imap")).toString();
        row.protocol->setCurrentIndex(qMax(0, row.protocol->findData(proto)));
        const QString auth = s.value(krellmailAccountKey(i, QStringLiteral("auth")),
                                     QStringLiteral("password")).toString();
        row.auth->setCurrentIndex(qMax(0, row.auth->findData(auth)));
        const bool ssl = s.value(krellmailAccountKey(i, QStringLiteral("ssl")), true).toBool();
        row.ssl->setChecked(ssl);
        row.port->setValue(s.value(krellmailAccountKey(i, QStringLiteral("port")),
                                   defaultMailPort(proto, ssl)).toInt());
        row.host->setText(s.value(krellmailAccountKey(i, QStringLiteral("host"))).toString());
        row.user->setText(s.value(krellmailAccountKey(i, QStringLiteral("username"))).toString());
        row.password->setText(s.value(krellmailAccountKey(i, QStringLiteral("password"))).toString());
        row.oauthClientId->setText(s.value(krellmailAccountKey(i, QStringLiteral("oauth_client_id"))).toString());
        row.status->setText(s.value(krellmailAccountKey(i, QStringLiteral("oauth_refresh_token"))).toString().isEmpty()
                            ? QStringLiteral("Not authorized")
                            : QStringLiteral("Gmail authorized"));

        m_krellmailAccounts.append(row);
        const int index = i;
        auto updateAuthUi = [this, index]() {
            if (index < 0 || index >= m_krellmailAccounts.size()) return;
            KrellmailAccountWidgets &widgets = m_krellmailAccounts[index];
            const bool oauth = widgets.auth->currentData().toString() == QLatin1String("oauth");
            widgets.password->setEnabled(!oauth);
            widgets.oauthClientId->setEnabled(oauth);
            widgets.authorize->setEnabled(oauth);
            if (oauth) {
                widgets.protocol->setCurrentIndex(qMax(0, widgets.protocol->findData(QStringLiteral("imap"))));
                widgets.host->setText(QStringLiteral("imap.gmail.com"));
                widgets.ssl->setChecked(true);
                widgets.port->setValue(993);
            }
        };
        updateAuthUi();
        auto save = [this, index]() { saveKrellmailAccount(index); };
        connect(row.protocol, QOverload<int>::of(&QComboBox::currentIndexChanged), this, save);
        connect(row.auth, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, index, updateAuthUi](int) {
            updateAuthUi();
            saveKrellmailAccount(index);
        });
        connect(row.host, &QLineEdit::textChanged, this, save);
        connect(row.port, QOverload<int>::of(&QSpinBox::valueChanged), this, save);
        connect(row.ssl, &QCheckBox::toggled, this, save);
        connect(row.user, &QLineEdit::textChanged, this, save);
        connect(row.password, &QLineEdit::textChanged, this, save);
        connect(row.oauthClientId, &QLineEdit::textChanged, this, save);
        connect(row.authorize, &QPushButton::clicked, this, [this, index]() {
            saveKrellmailAccount(index);
            beginKrellmailOAuth(index);
        });
        connect(row.remove, &QPushButton::clicked, this, [this, index]() { removeKrellmailAccount(index); });
    }
    m_krellmailAccountsLayout->addStretch(1);
}

void SettingsDialog::saveKrellmailAccount(int index)
{
    if (index < 0 || index >= m_krellmailAccounts.size()) return;
    const KrellmailAccountWidgets &row = m_krellmailAccounts.at(index);
    QSettings s;
    s.setValue(krellmailAccountKey(index, QStringLiteral("protocol")), row.protocol->currentData().toString());
    s.setValue(krellmailAccountKey(index, QStringLiteral("auth")), row.auth->currentData().toString());
    s.setValue(krellmailAccountKey(index, QStringLiteral("host")), row.host->text().trimmed());
    s.setValue(krellmailAccountKey(index, QStringLiteral("port")), row.port->value());
    s.setValue(krellmailAccountKey(index, QStringLiteral("ssl")), row.ssl->isChecked());
    s.setValue(krellmailAccountKey(index, QStringLiteral("username")), row.user->text().trimmed());
    s.setValue(krellmailAccountKey(index, QStringLiteral("password")), row.password->text());
    s.setValue(krellmailAccountKey(index, QStringLiteral("oauth_client_id")), row.oauthClientId->text().trimmed());
    emit settingsApplied();
}

void SettingsDialog::addKrellmailAccount()
{
    QSettings s;
    const int count = krellmailAccountCount();
    if (count >= kMaxKrellmailAccounts) return;
    s.setValue(QStringLiteral("plugins/krellmail/account_count"), count + 1);
    s.setValue(krellmailAccountKey(count, QStringLiteral("protocol")), QStringLiteral("imap"));
    s.setValue(krellmailAccountKey(count, QStringLiteral("auth")), QStringLiteral("password"));
    s.setValue(krellmailAccountKey(count, QStringLiteral("ssl")), true);
    s.setValue(krellmailAccountKey(count, QStringLiteral("port")), 993);
    rebuildKrellmailAccounts();
    emit settingsApplied();
}

void SettingsDialog::removeKrellmailAccount(int index)
{
    QSettings s;
    const int count = krellmailAccountCount();
    if (index < 0 || index >= count || count <= 1) return;

    for (int dst = index; dst < count - 1; ++dst) {
        const int src = dst + 1;
        const QStringList keys = {QStringLiteral("protocol"), QStringLiteral("auth"), QStringLiteral("host"),
                                  QStringLiteral("port"), QStringLiteral("ssl"), QStringLiteral("username"),
                                  QStringLiteral("password"), QStringLiteral("oauth_client_id"),
                                  QStringLiteral("oauth_refresh_token")};
        for (const QString &key : keys)
            s.setValue(krellmailAccountKey(dst, key), s.value(krellmailAccountKey(src, key)));
    }
    s.remove(QStringLiteral("plugins/krellmail/account%1").arg(count));
    s.setValue(QStringLiteral("plugins/krellmail/account_count"), count - 1);
    rebuildKrellmailAccounts();
    emit settingsApplied();
}

void SettingsDialog::beginKrellmailOAuth(int index)
{
    if (index < 0 || index >= m_krellmailAccounts.size()) return;
    KrellmailAccountWidgets &row = m_krellmailAccounts[index];
    const QString clientId = row.oauthClientId->text().trimmed();
    const QString user = row.user->text().trimmed();
    if (clientId.isEmpty() || user.isEmpty()) {
        row.auth->setCurrentIndex(qMax(0, row.auth->findData(QStringLiteral("oauth"))));
        row.status->setText(QStringLiteral(
            "OAuth needs the full Gmail address and a Google desktop OAuth client ID."));
        return;
    }

    delete m_krellmailOAuthServer;
    m_krellmailOAuthServer = new QTcpServer(this);
    if (!m_krellmailOAuthServer->listen(QHostAddress::LocalHost, 0)) {
        row.status->setText(QStringLiteral("Could not start local OAuth callback"));
        return;
    }
    m_krellmailOAuthAccount = index;
    m_krellmailOAuthVerifier = randomUrlToken(48);
    m_krellmailOAuthState = randomUrlToken(24);

    connect(m_krellmailOAuthServer, &QTcpServer::newConnection,
            this, &SettingsDialog::handleKrellmailOAuthCallback);

    QUrl authUrl(QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), clientId);
    query.addQueryItem(QStringLiteral("redirect_uri"),
                       QStringLiteral("http://127.0.0.1:%1/").arg(m_krellmailOAuthServer->serverPort()));
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("scope"), QStringLiteral("https://mail.google.com/"));
    query.addQueryItem(QStringLiteral("access_type"), QStringLiteral("offline"));
    query.addQueryItem(QStringLiteral("prompt"), QStringLiteral("consent"));
    query.addQueryItem(QStringLiteral("login_hint"), user);
    query.addQueryItem(QStringLiteral("code_challenge"), pkceChallenge(m_krellmailOAuthVerifier));
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    query.addQueryItem(QStringLiteral("state"), m_krellmailOAuthState);
    authUrl.setQuery(query);

    const QString authUrlText = authUrl.toString(QUrl::FullyEncoded);
    row.status->setText(QStringLiteral("Waiting for Google authorization..."));
    bool opened = QDesktopServices::openUrl(authUrl);
    if (!opened)
        opened = QProcess::startDetached(QStringLiteral("xdg-open"), {authUrlText});
    if (!opened) {
        row.status->setText(QStringLiteral(
            "Could not open a browser. Copy this URL:\n%1").arg(authUrlText));
    }
}

void SettingsDialog::handleKrellmailOAuthCallback()
{
    if (!m_krellmailOAuthServer) return;
    QTcpSocket *socket = m_krellmailOAuthServer->nextPendingConnection();
    if (!socket) return;
    socket->waitForReadyRead(3000);
    const QByteArray request = socket->readAll();
    const QList<QByteArray> lines = request.split('\n');
    const QByteArray first = lines.isEmpty() ? QByteArray() : lines.first();
    const QList<QByteArray> parts = first.split(' ');
    QUrl url(QStringLiteral("http://127.0.0.1") + QString::fromUtf8(parts.size() > 1 ? parts.at(1) : QByteArray("/")));
    QUrlQuery query(url);

    const QString code = query.queryItemValue(QStringLiteral("code"));
    const QString state = query.queryItemValue(QStringLiteral("state"));
    const QString redirectUri =
        QStringLiteral("http://127.0.0.1:%1/").arg(m_krellmailOAuthServer->serverPort());
    const QString response = code.isEmpty()
        ? QStringLiteral("HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nKrellmail authorization failed.")
        : QStringLiteral("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nKrellmail is authorized. You can close this window.");
    socket->write(response.toUtf8());
    socket->disconnectFromHost();
    socket->deleteLater();
    m_krellmailOAuthServer->close();

    if (m_krellmailOAuthAccount < 0 || m_krellmailOAuthAccount >= m_krellmailAccounts.size())
        return;
    if (code.isEmpty() || state != m_krellmailOAuthState) {
        m_krellmailAccounts[m_krellmailOAuthAccount].status->setText(QStringLiteral("OAuth failed"));
        return;
    }

    if (!m_krellmailOAuthNetwork)
        m_krellmailOAuthNetwork = new QNetworkAccessManager(this);
    const KrellmailAccountWidgets &row = m_krellmailAccounts.at(m_krellmailOAuthAccount);
    QNetworkRequest tokenRequest(QUrl(QStringLiteral("https://oauth2.googleapis.com/token")));
    tokenRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                           QStringLiteral("application/x-www-form-urlencoded"));
    QUrlQuery body;
    body.addQueryItem(QStringLiteral("client_id"), row.oauthClientId->text().trimmed());
    body.addQueryItem(QStringLiteral("code"), code);
    body.addQueryItem(QStringLiteral("code_verifier"), m_krellmailOAuthVerifier);
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    body.addQueryItem(QStringLiteral("redirect_uri"), redirectUri);
    QNetworkReply *reply = m_krellmailOAuthNetwork->post(tokenRequest, body.query(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { finishKrellmailOAuth(reply); });
}

void SettingsDialog::finishKrellmailOAuth(QNetworkReply *reply)
{
    if (!reply) return;
    reply->deleteLater();
    if (m_krellmailOAuthAccount < 0 || m_krellmailOAuthAccount >= m_krellmailAccounts.size())
        return;
    KrellmailAccountWidgets &row = m_krellmailAccounts[m_krellmailOAuthAccount];
    const QByteArray payload = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    const QString refreshToken = doc.object().value(QStringLiteral("refresh_token")).toString();
    if (reply->error() != QNetworkReply::NoError || refreshToken.isEmpty()) {
        row.status->setText(QStringLiteral("OAuth token failed"));
        return;
    }
    QSettings s;
    s.setValue(krellmailAccountKey(m_krellmailOAuthAccount, QStringLiteral("oauth_refresh_token")),
               refreshToken);
    s.setValue(krellmailAccountKey(m_krellmailOAuthAccount, QStringLiteral("auth")),
               QStringLiteral("oauth"));
    row.auth->setCurrentIndex(qMax(0, row.auth->findData(QStringLiteral("oauth"))));
    row.status->setText(QStringLiteral("Gmail authorized"));
    emit settingsApplied();
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

bool SettingsDialog::hasKrellmailPlugin() const
{
    return hasPlugin(QStringLiteral("krellmail"));
}

bool SettingsDialog::hasKrellSpectrumPlugin() const
{
    return hasPlugin(QStringLiteral("krellspectrum"));
}

void SettingsDialog::onAccept()
{
    if (m_krellmailEnabled)
        emit settingsApplied();
    accept();
}
