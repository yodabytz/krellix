# KrellStock for Windows

This optional Windows x86-64 plugin uses the published Krellix Windows
preview.6 host for verification. It does not replace the host, your profile
or other plugins.
It is active only in a local Windows Krellix window.

1. Extract the plugin ZIP and run
   `./install-krellstock.ps1 -KrellixDirectory C:/path/to/Krellix` in PowerShell.
2. Reopen local Krellix. Right-click Krellix and open **Settings > Plugins >
   KrellStock**.
3. Enter the Yahoo Finance symbols you want, then save. No API key is needed.
   The same page controls the ticker and quote panels.

No watchlist is bundled. Quotes are requested from Yahoo Finance over HTTPS
without credentials, and requests do not follow redirects.

Both panels share one fetcher, preserve the configured order, and use a
15-minute minimum refresh. Cached quotes remain available during failures.
Known closed equity markets are rechecked after one hour; crypto pairs do
not use the closed-market delay. The fetcher limits bursts to eight symbol
requests per minute and backs off on provider rate limits. Not every
listed instrument is guaranteed to return a quote.

Use Yahoo Finance symbols such as `AAPL`, `BTC-USD`, `BP.L` or `SAP.DE`.
Pairs entered as `BTC/USD` are converted to Yahoo's `BTC-USD` form.

Linux and macOS builds use their existing code paths and packages.
