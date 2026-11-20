#include "WindowsStockFetcher.h"
#include "remote/RemoteSource.h"
#include "widgets/Panel.h"

#include <QRegularExpression>
#include <QSettings>
#include <utility>

namespace {
const QString kPrefix = QStringLiteral("plugins/krellstock/");

QString symbolKey(int i) { return kPrefix + QStringLiteral("symbol%1").arg(i + 1); }

QStringList symbols(const QSettings &s)
{
    static const QRegularExpression split(QStringLiteral("[,;\\s]+"));
    QStringList out;
    for (int i = 0; i < 10; ++i)
        out.append(s.value(symbolKey(i)).toString().split(split, Qt::SkipEmptyParts));
    return out;
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
        render();
        QTimer::singleShot(0, this, &WindowsStockMonitor::tick);
        return panel;
    }

    void tick() override
    {
        if (!m_fetcher) return;
        QSettings s;
        m_fetcher->configure(symbols(s), s.value(kPrefix + QStringLiteral("interval_ms"), 900000).toInt());
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
    QString pluginVersion() const override { return QStringLiteral("0.1.2-windows.3"); }
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
