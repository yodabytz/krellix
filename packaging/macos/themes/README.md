# macOS theme overrides

`gorilla/` restores the classic Gorilla banner on macOS. It uses white and
gray chart lines, thin dark grid lines, light panels (including Uptime), and
gray Memory, Swap, and Network bars. Panel text has a soft shadow; the chart readout
has its own independent style. Use the current Mac build for the grayscale
palette and light Uptime background.

`egan/` uses the supplied slate panel, chart, and frame artwork with white
shadowed labels and gold readouts. It follows the
[original GKrellM Egan preview](http://www.muhri.net/gkrellm/egan.jpg).

`fenrir/` uses the supplied armor textures and wolf-and-moon artwork. Its
title icons, proportional banner, and right-aligned readouts require the
current Mac build. A panel width of about 240 gives the reference proportions;
the banner also scales to narrower profiles without changing saved settings.

`leviathan/` uses the supplied undersea banner and weathered bronze frame,
with cyan monitor icons and live readouts. A width of about 240 matches the
reference proportions.

`anubis/` uses the supplied Egyptian gold and lapis textures, a jackal banner,
gold monitor icons, and a winged scarab footer. Use the current Mac build to
scale the footer proportionally with the panel width.

Install the directory under `~/.config/krellix/themes/` (back up any existing
directory of the same name), then select the theme in Appearance. If you
customized its fonts or colors, merge those settings from your backup into
`theme.json`.

The macOS packager installs these overrides after copying the shared themes
into `Krellix.app/Contents/Resources/themes/`. Linux and Windows packages
continue to use the shared themes only.
