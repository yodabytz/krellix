#include "WindowsStockFetcher.h"
#include "remote/RemoteSource.h"
#include "widgets/Panel.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QVBoxLayout>
#include <utility>
#include <windows.h>
#include <wincrypt.h>

namespace {
const QString kPrefix = QStringLiteral("plugins/krellstock/");
const QString kKey = kPrefix + QStringLiteral("twelvedata_key_dpapi");
const QString kLegacyKey = kPrefix + QStringLiteral("twelvedata_api_key");

QString symbolKey(int i) { return kPrefix + QStringLiteral("symbol%1").arg(i + 1); }

QByteArray transformKey(const QByteArray &input, bool protect)
{
    if (input.isEmpty() || input.size() > 8192) return {};
    DATA_BLOB data{DWORD(input.size()),
        reinterpret_cast<BYTE *>(const_cast<char *>(input.constData()))};
    DATA_BLOB output{};
    const BOOL ok = protect
        ? CryptProtectData(&data, L"KrellStock", nullptr, nullptr, nullptr,
                           CRYPTPROTECT_UI_FORBIDDEN, &output)
        : CryptUnprotectData(&data, nullptr, nullptr, nullptr, nullptr,
                             CRYPTPROTECT_UI_FORBIDDEN, &output);
    if (!ok) return {};
    QByteArray result(reinterpret_cast<const char *>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return result;
}

QByteArray storedKey(const QSettings &s)
{
    return s.contains(kKey) ? transformKey(s.value(kKey).toByteArray(), false)
                           : s.value(kLegacyKey).toByteArray().trimmed();
}

QStringList symbols(const QSettings &s)
{
    static const QRegularExpression split(QStringLiteral("[,;\\s]+"));
    QStringList out;
    for (int i = 0; i < 10; ++i)
        out.append(s.value(symbolKey(i)).toString().split(split, Qt::SkipEmptyParts));
    return out;
}

void configureDialog(QWidget *parent, const QWeakPointer<WindowsStockFetcher> &fetcher)
{
    QWidget *window = parent->window();
    if (auto *existing = window->findChild<QDialog *>(QStringLiteral("krellstockWindowsSettings"))) {
        existing->raise();
        existing->activateWindow();
        return;
    }
    auto *dialog = new QDialog(window);
    dialog->setObjectName(QStringLiteral("krellstockWindowsSettings"));
    dialog->setWindowTitle(QStringLiteral("KrellStock Settings"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto *layout = new QVBoxLayout(dialog);
    auto *instructions = new QLabel(QStringLiteral(
        "Paste your Twelve Data API key and enter up to ten symbols. "
        "Only the symbols you enter will be requested. The key is protected by Windows for this account."), dialog);
    instructions->setWordWrap(true);
    layout->addWidget(instructions);
    auto *keyLabel = new QLabel(QStringLiteral("Twelve Data API key"), dialog);
    auto *key = new QLineEdit(dialog);
    key->setObjectName(QStringLiteral("krellstockApiKey"));
    key->setAccessibleName(keyLabel->text());
    key->setEchoMode(QLineEdit::Password);
    key->setMaxLength(128);
    keyLabel->setBuddy(key);
    QSettings s;
    key->setText(QString::fromLatin1(storedKey(s)));
    layout->addWidget(keyLabel);
    layout->addWidget(key);
    auto *grid = new QGridLayout;
    layout->addLayout(grid);
    QList<QLineEdit *> edits;
    for (int i = 0; i < 10; ++i) {
        auto *label = new QLabel(QStringLiteral("Symbol %1").arg(i + 1), dialog);
        auto *edit = new QLineEdit(s.value(symbolKey(i)).toString(), dialog);
        edit->setAccessibleName(label->text());
        edit->setMaxLength(20);
        if (i == 0) edit->setPlaceholderText(QStringLiteral("AAPL"));
        if (i == 1) edit->setPlaceholderText(QStringLiteral("BTC/USD"));
        label->setBuddy(edit);
        grid->addWidget(label, i / 2, (i % 2) * 2);
        grid->addWidget(edit, i / 2, (i % 2) * 2 + 1);
        edits.append(edit);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
        const QByteArray entered = key->text().trimmed().toLatin1();
        if (!entered.isEmpty() && !WindowsStockFetcher::validKey(entered)) {
            QMessageBox::warning(dialog, dialog->windowTitle(), QStringLiteral("Enter a valid Twelve Data API key."));
            return;
        }
        QStringList sanitized;
        for (const auto *edit : edits) {
            const auto clean = WindowsStockFetcher::cleanSymbol(edit->text());
            if (clean.isEmpty() && !edit->text().trimmed().isEmpty()) {
                QMessageBox::warning(dialog, dialog->windowTitle(),
                                     QStringLiteral("Enter one valid symbol or pair per field."));
                return;
            }
            sanitized.append(clean);
        }
        const auto protectedKey = transformKey(entered, true);
        if (!entered.isEmpty() && protectedKey.isEmpty()) {
            QMessageBox::warning(dialog, dialog->windowTitle(), QStringLiteral("Windows could not protect the API key."));
            return;
        }
        QSettings updated;
        updated.setValue(kKey, protectedKey);
        updated.remove(kLegacyKey);
        for (int i = 0; i < sanitized.size(); ++i)
            updated.setValue(symbolKey(i), sanitized.at(i));
        updated.sync();
        if (updated.status() != QSettings::NoError) {
            QMessageBox::warning(dialog, dialog->windowTitle(), QStringLiteral("KrellStock settings could not be saved."));
            return;
        }
        if (const auto active = fetcher.toStrongRef())
            active->configure(sanitized, entered, updated.value(kPrefix + QStringLiteral("interval_ms"), 900000).toInt());
        dialog->accept();
    });
    dialog->resize(490, dialog->sizeHint().height());
    dialog->show();
}

class WindowsStockMonitor final : public MonitorBase
{
public:
    WindowsStockMonitor(Theme *theme, QObject *parent, bool ticker,
                        QSharedPointer<WindowsStockFetcher> fetcher)
        : MonitorBase(theme, parent), m_fetcher(std::move(fetcher)), m_isTicker(ticker)
    {
        connect(m_fetcher.get(), &WindowsStockFetcher::changed, this, [this] { render(); });
    }

    QString id() const override
    {
        return m_isTicker ? QStringLiteral("krellstock_ticker") : QStringLiteral("krellstock");
    }
    QString displayName() const override
    {
        return m_isTicker ? QStringLiteral("KrellStock Ticker") : QStringLiteral("KrellStock");
    }
    QWidget *createWidget(QWidget *parent) override
    {
        auto *panel = new Panel(theme(), parent);
        panel->setObjectName(id() + QStringLiteral("Panel"));
        panel->setSurfaceKey(QStringLiteral("panel_bg_") + id());
        panel->setTitle(QStringLiteral("Stocks"));
        if (m_isTicker) {
            m_ticker = new KrellstockTicker(theme(), panel);
            m_ticker->setAccessibleName(QStringLiteral("KrellStock ticker"));
            panel->addWidget(m_ticker);
        } else {
            m_quotes = new KrellstockQuoteWidget(theme(), panel);
            m_quotes->setAccessibleName(QStringLiteral("KrellStock quotes"));
            panel->addWidget(m_quotes);
        }
        auto *settings = new QPushButton(QStringLiteral("KrellStock settings"), panel);
        settings->setObjectName(QStringLiteral("krellstockOpenSettings"));
        panel->addWidget(settings);
        connect(settings, &QPushButton::clicked, this, [this, panel] {
            if (m_fetcher) configureDialog(panel, m_fetcher.toWeakRef());
        });
        render();
        QTimer::singleShot(0, this, &WindowsStockMonitor::tick);
        return panel;
    }

    void tick() override
    {
        if (!m_fetcher) return;
        QSettings s;
        const QByteArray stored = s.value(kKey, s.value(kLegacyKey)).toByteArray();
        const bool protectedKey = s.contains(kKey);
        if (stored != m_lastKey || protectedKey != m_lastProtected) {
            m_lastKey = stored;
            m_lastProtected = protectedKey;
            m_clearKey = storedKey(s);
        }
        m_fetcher->configure(symbols(s), m_clearKey, s.value(kPrefix + QStringLiteral("interval_ms"), 900000).toInt());
    }

    void shutdown() override
    {
        if (!m_fetcher) return;
        disconnect(m_fetcher.get(), nullptr, this, nullptr);
        m_fetcher.reset();
    }

private:
    void render()
    {
        const auto quotes = m_fetcher->quotes();
        if (m_ticker) {
            m_ticker->setPlaceholder(QStringLiteral("Add symbols in settings"));
            m_ticker->setQuotes(quotes);
        }
        if (m_quotes) {
            m_quotes->setPlaceholder(QStringLiteral("Add symbols in settings"));
            m_quotes->setQuotes(quotes);
        }
    }

    QSharedPointer<WindowsStockFetcher> m_fetcher;
    QPointer<KrellstockTicker> m_ticker;
    QPointer<KrellstockQuoteWidget> m_quotes;
    QByteArray m_lastKey;
    QByteArray m_clearKey;
    bool m_lastProtected = false;
    bool m_isTicker;
};
}

class KrellstockWindowsPlugin final : public QObject, public IKrellixPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID KrellixPlugin_iid)
    Q_INTERFACES(IKrellixPlugin)
public:
    QString pluginId() const override { return QStringLiteral("io.krellix.krellstock"); }
    QString pluginName() const override { return QStringLiteral("KrellStock"); }
    QString pluginVersion() const override { return QStringLiteral("0.1.2-windows.1"); }
    QList<MonitorBase *> createMonitors(Theme *theme, QObject *parent) override
    {
        if (RemoteSource::instance()) return {};
        QSettings s;
        const bool ticker = s.value(kPrefix + QStringLiteral("ticker_enabled"), false).toBool();
        const bool quotes = s.value(kPrefix + QStringLiteral("quotes_enabled"), true).toBool();
        if (!ticker && !quotes) return {};
        auto fetcher = m_fetcher.toStrongRef();
        if (!fetcher) {
            fetcher = QSharedPointer<WindowsStockFetcher>::create();
            m_fetcher = fetcher;
        }
        QList<MonitorBase *> result;
        if (ticker) result.append(new WindowsStockMonitor(theme, parent, true, fetcher));
        if (quotes) result.append(new WindowsStockMonitor(theme, parent, false, fetcher));
        return result;
    }

private:
    QWeakPointer<WindowsStockFetcher> m_fetcher;
};

#include "KrellstockWindows.moc"
