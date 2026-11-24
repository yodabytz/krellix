# macOS theme overrides

`gorilla/` restores the classic Gorilla banner on macOS. It uses white and
gray chart lines, thin dark grid lines, light panels (including Uptime), and
gray Memory and Swap meters. Panel text has a soft shadow; the chart readout
has its own independent style. Use the current Mac build for the grayscale
palette and light Uptime background.
Install the directory under `~/.config/krellix/themes/` (back up any existing
Gorilla directory), then select `gorilla` in Appearance. If you customized its
fonts or colors, merge those settings from your backup into `theme.json`.

The macOS packager installs these overrides after copying the shared themes
into `Krellix.app/Contents/Resources/themes/`. Linux and Windows packages
continue to use the shared themes only.
