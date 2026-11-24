# macOS theme overrides

`gorilla/` restores the classic Gorilla banner on macOS. It uses white and
gray chart lines, dark grid lines, light panels (including Uptime), and a
subtle shadow on panel labels. The chart readout has its own independent style.
Use the current Mac build for the grayscale palette, thicker grid, and light
Uptime background.
Install the directory under `~/.config/krellix/themes/` (back up any existing
Gorilla directory), then select `gorilla` in Appearance. If you customized its
fonts or colors, merge those settings from your backup into `theme.json`.

When assembling a macOS bundle, use this version for
`Krellix.app/Contents/Resources/themes/gorilla/`. Do not use this override in
Linux or Windows packages.
