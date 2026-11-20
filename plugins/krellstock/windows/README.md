# KrellStock for Windows

This optional Windows x86-64 plugin uses the published Krellix Windows
preview.5 host. It does not replace the host, your profile or other plugins.
It is active only in a local Windows Krellix window.

1. Extract the plugin ZIP and run
   `./install-krellstock.ps1 -KrellixDirectory C:/path/to/Krellix` in PowerShell.
2. Reopen local Krellix. In the Stocks panel, click **KrellStock settings**.
3. Paste your own Twelve Data API key and enter the symbols you want. Save.
   The older host's built-in stock page can still toggle the ticker and quote
   panels; use the panel's settings button for credentials.

No key or watchlist is bundled. New keys are protected with Windows DPAPI for
the current Windows account. A copied encrypted key must be entered again
under a different account. Requests send the key in the HTTPS Authorization
header, not in the URL, and do not follow redirects.

Both panels share one fetcher, preserve the configured order, and use a
15-minute minimum refresh. Cached quotes remain available during failures.
Known closed equity markets are rechecked after one hour; crypto pairs do
not use the closed-market delay. The fetcher limits bursts to eight symbol
requests per minute and backs off on provider rate limits. A subscription
may not cover every symbol or exchange.

Use Twelve Data symbols such as `AAPL` or `BTC/USD`. Common older forms
including `BTC-USD`, `BP.L`, and `SAP.DE` are also accepted. An explicitly
selected exchange must match the quote returned by the provider.

Linux and macOS builds use their existing code paths and packages.
