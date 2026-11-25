# macOS theme overrides

`gorilla/` restores the classic Gorilla banner on macOS. It uses white and
gray chart lines, thin dark grid lines, light panels (including Uptime), and
gray Memory, Swap, and Network bars. Panel text has a soft shadow; the chart readout
has its own independent style. Use the current Mac build for the grayscale
palette and light Uptime background.

`egan/` uses the supplied slate panel, chart, and frame artwork with white
shadowed labels and gold readouts. It follows the
[original GKrellM Egan preview](http://www.muhri.net/gkrellm/egan.jpg).

Install the directory under `~/.config/krellix/themes/` (back up any existing
directory of the same name), then select the theme in Appearance. If you
customized its fonts or colors, merge those settings from your backup into
`theme.json`.

The macOS packager installs these overrides after copying the shared themes
into `Krellix.app/Contents/Resources/themes/`. Linux and Windows packages
continue to use the shared themes only. The updated Cheese is shared and
bundled. Download-only themes are kept under [packaging/themes](../../themes/)
and are not copied into any app package.
