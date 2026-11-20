# KrellWeather for Windows

This optional Windows x86-64 plugin uses the published Krellix preview.6
host for installer verification. It does not replace Krellix, your profile,
or other plugins.

Close local Krellix, extract the plugin ZIP, and run this in PowerShell:

    ./install-krellweather.ps1 -KrellixDirectory C:/path/to/preview.6/Krellix

Open local Krellix. Right-click and choose **Settings > Plugins >
Krellweather**. Enable the panel and set the four-character ICAO station
(for example, **KMIA**, **KJFK**, or **EGLL**), units, and update interval.

Observations come from the Aviation Weather Center's HTTPS METAR API.
No API key is needed. The plugin displays current reported conditions,
not forecasts. The default refresh interval is ten minutes. The Windows
plugin does not run in remote Krellix windows. Linux and macOS builds keep
their existing behavior.
