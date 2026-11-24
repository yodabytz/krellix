# macOS theme overrides

`gorilla/` restores the classic Gorilla banner, light panel texture, and dark
chart texture on macOS. Panel labels have a subtle shadow; the chart readout
has its own independent style. It is a theme-only update and does not replace
the app.
Install the directory under `~/.config/krellix/themes/` (back up any existing
Gorilla directory), then select `gorilla` in Appearance. If you customized its
fonts or colors, merge those settings from your backup into `theme.json`.

When assembling a macOS bundle, use this version for
`Krellix.app/Contents/Resources/themes/gorilla/`. Do not use this override in
Linux or Windows packages.
