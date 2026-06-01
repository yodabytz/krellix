#pragma once

#include "monitors/MonitorBase.h"
#include "sdk/KrellixPlugin.h"

#include <QElapsedTimer>
#include <QList>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSharedPointer>
#include <QString>
#include <QTimer>
#include <QWidget>

class QNetworkReply;
class Theme;

// ── Quote data ────────────────────────────────────────────────────────────────

struct StockQuote {
    QString symbol;       // Yahoo Finance symbol, e.g. "AAPL", "BTC-USD", "BP.L"
    QString shortName;    // human-readable name from Yahoo
    double  price      = 0;
    double  change     = 0;
    double  changePct  = 0;  // percent, e.g. 1.25 means +1.25%
    QString currency;
    QString marketState;  // "REGULAR", "PRE", "POST", "CLOSED"
    bool    valid      = false;
};

// ── Shared fetcher ────────────────────────────────────────────────────────────
//
// One fetcher is shared between the ticker and quote monitors so a single
// network request serves both panels.  Held via QSharedPointer; destroyed
// when the last monitor referencing it is destroyed.

class KrellstockFetcher : public QObject
{
    Q_OBJECT

public:
    explicit KrellstockFetcher(QObject *parent = nullptr);
    ~KrellstockFetcher() override;

    // Trigger a fetch.  No-ops if a fetch is already in flight.
    void requestFetch(const QStringList &symbols);

    bool              isFetching()  const { return m_fetching;    }
    QList<StockQuote> lastQuotes()  const { return m_lastQuotes;  }

signals:
    void quotesReady(const QList<StockQuote> &quotes);

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    void startNext();

    QNetworkAccessManager          m_net;
    QList<StockQuote>              m_lastQuotes;
    QList<QString>                 m_pending;    // symbols still to fetch
    QHash<QNetworkReply*, QString> m_inFlight;   // reply → symbol
    QList<StockQuote>              m_building;   // accumulates this round
    bool                           m_fetching = false;

    Q_DISABLE_COPY_MOVE(KrellstockFetcher)
};

// ── Scrolling ticker widget ───────────────────────────────────────────────────

class KrellstockTicker : public QWidget
{
    Q_OBJECT

public:
    explicit KrellstockTicker(Theme *theme, QWidget *parent = nullptr);

    void setQuotes(const QList<StockQuote> &quotes);
    void setPlaceholder(const QString &text);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;

private slots:
    void onTick();
    void onThemeChanged();

private:
    struct Seg {
        QString text;
        QColor  color;
        bool    bold       = false;
        int     pixelWidth = 0;
    };

    void rebuildSegs();

    Theme            *m_theme;
    QTimer           *m_timer;
    QElapsedTimer     m_clock;
    QList<StockQuote> m_quotes;
    QString           m_placeholder;
    QList<Seg>        m_segs;
    double            m_offset = 0;
    int               m_totalW = 0;

    Q_DISABLE_COPY_MOVE(KrellstockTicker)
};

// ── Quote-panel widget ────────────────────────────────────────────────────────

class KrellstockQuoteWidget : public QWidget
{
    Q_OBJECT

public:
    explicit KrellstockQuoteWidget(Theme *theme, QWidget *parent = nullptr);

    void setQuotes(const QList<StockQuote> &quotes);
    void setPlaceholder(const QString &text);

    QSize sizeHint()        const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private slots:
    void onThemeChanged();

private:
    Theme            *m_theme;
    QList<StockQuote> m_quotes;
    QString           m_placeholder;
    int               m_rowH = 18;

    Q_DISABLE_COPY_MOVE(KrellstockQuoteWidget)
};

// ── Monitors ──────────────────────────────────────────────────────────────────

class KrellstockTickerMonitor : public MonitorBase
{
    Q_OBJECT

public:
    explicit KrellstockTickerMonitor(Theme *theme,
                                      QSharedPointer<KrellstockFetcher> fetcher,
                                      QObject *parent = nullptr);

    QString  id()            const override { return QStringLiteral("krellstock_ticker"); }
    QString  displayName()   const override { return QStringLiteral("KrellStock Ticker"); }
    int      tickIntervalMs() const override;
    QWidget *createWidget(QWidget *parent) override;
    void     tick() override;

private slots:
    void onQuotesReady(const QList<StockQuote> &quotes);

private:
    QSharedPointer<KrellstockFetcher> m_fetcher;
    QPointer<KrellstockTicker>        m_ticker;

    Q_DISABLE_COPY_MOVE(KrellstockTickerMonitor)
};

class KrellstockQuoteMonitor : public MonitorBase
{
    Q_OBJECT

public:
    explicit KrellstockQuoteMonitor(Theme *theme,
                                     QSharedPointer<KrellstockFetcher> fetcher,
                                     QObject *parent = nullptr);

    QString  id()            const override { return QStringLiteral("krellstock"); }
    QString  displayName()   const override { return QStringLiteral("KrellStock"); }
    int      tickIntervalMs() const override;
    QWidget *createWidget(QWidget *parent) override;
    void     tick() override;

private slots:
    void onQuotesReady(const QList<StockQuote> &quotes);

private:
    QSharedPointer<KrellstockFetcher>  m_fetcher;
    QPointer<KrellstockQuoteWidget>    m_quoteWidget;

    Q_DISABLE_COPY_MOVE(KrellstockQuoteMonitor)
};

// ── Plugin ────────────────────────────────────────────────────────────────────

class KrellstockPlugin : public QObject, public IKrellixPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID KrellixPlugin_iid)
    Q_INTERFACES(IKrellixPlugin)

public:
    QString pluginId()      const override { return QStringLiteral("io.krellix.krellstock"); }
    QString pluginName()    const override { return QStringLiteral("KrellStock"); }
    QString pluginVersion() const override { return QStringLiteral("0.1.0"); }
    QList<MonitorBase *> createMonitors(Theme *theme, QObject *parent) override;
};
