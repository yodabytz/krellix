# krellix Theme Format

A krellix theme is a directory containing a `theme.json` file plus any image
assets it references. Themes live in:

- `~/.config/krellix/themes/<name>/` (Linux and macOS user install);
  on Linux use `$XDG_CONFIG_HOME/krellix/themes/<name>/` when set
- `%LOCALAPPDATA%\krellix\themes\<name>\` (Windows user install)
- `themes\<name>\` next to `krellix.exe` (Windows portable)
- `/usr/share/krellix/themes/<name>/` (system, installed)
- `./themes/<name>/` (relative to working dir, for development)

Theme names must match `[A-Za-z0-9._-]{1,64}` and may not start with `.`.

While krellix is running, edits to `theme.json` are picked up automatically;
panels repaint on save. After replacing only artwork or the animation
manifest, re-save `theme.json` or reselect the theme to refresh its caches.
Asset paths are resolved relative to the theme
directory and may not escape it.

## `theme.json`

A single JSON object with up to seven sections: `colors`, `fonts`, `metrics`,
`images`, `surfaces`, `text_styles`, plus optional `name` / `version` / `author` / `description`
metadata. Every key is optional — missing values fall back to built-in
defaults (or a flat-color render if no image is supplied).

```json
{
  "name":        "default",
  "version":     "1.0",
  "author":      "you",
  "description": "...",

  "colors":  { ... },
  "fonts":   { ... },
  "metrics": { ... },
  "images":  { ... },
  "surfaces": { ... },
  "text_styles": { ... }
}
```

### `colors`

Map of semantic name → CSS-style color string (`"#rrggbb"`, `"#rgb"`, named
colors like `"red"`). Recognized keys:

| Key                | Used by                                  |
| ------------------ | ---------------------------------------- |
| `panel_bg`         | Panel fill                               |
| `panel_border`     | Panel outer border                       |
| `panel_highlight`  | Reserved for future hover/selection      |
| `chart_bg`         | Chart background                         |
| `chart_grid`       | Chart horizontal grid lines              |
| `text_meter_bg`   | Optional background for text-only meters such as Uptime |
| `text_primary`     | Primary text + chart line color          |
| `text_secondary`   | Secondary/label text                     |
| `text_clock`      | Optional clock text color; falls back to `text_primary` |
| `krell_track`      | Krell unfilled track color               |
| `krell_indicator`  | Krell moving indicator color             |
| `net_krell_track`  | Optional track color or gradient for Network RX/TX bars |
| `net_krell_indicator` | Optional moving indicator color or gradient for Network RX/TX bars |

When set, the `net_krell_*` colors replace the corresponding general Krell
color or image on Network bars only.

Any unrecognized key is loaded but unused — safe for theme authors to add
custom tokens for use by future plugins.

### `fonts`

Map of semantic name → font descriptor object:

```json
{
  "family": "Monospace",
  "size":   10,
  "bold":   true,
  "italic": false
}
```

Recognized keys:

| Key     | Used by                          |
| ------- | -------------------------------- |
| `label` | Decals showing labels / units    |
| `value` | Decals showing values / titles   |
| `panel_title` | Optional font override for panel headings such as CPU, Proc, and Memory |
| `netports` | Optional Linux Net Ports row font override |

Sizes are clamped to `4..72` points.

Panel headings inherit `label` with 2 additional points (up to 72) and
semibold weight on all platforms. Set `fonts.panel_title` to customize
headings without changing chart readouts or other labels. Unspecified
properties inherit the heading defaults.

On Linux, Net Ports rows default to 75% of the `label` font size (minimum
4 points), retaining the theme's family, weight and style. An explicit
`netports` font is used at its specified size. macOS continues to use `label`.

### `metrics`

Map of name → integer (clamped to `0..4096`).

| Key                | Default | Meaning                                 |
| ------------------ | ------- | --------------------------------------- |
| `panel_padding`    | 4       | Inner padding around each panel         |
| `panel_spacing`    | 2       | Gap between widgets within a panel      |
| `netports_row_spacing` | 0   | Linux Net Ports row gap; updates live   |
| `panel_border`     | 1       | Panel border line width (0 = none)      |
| `panel_min_width`  | 100     | Legacy metric; the current client uses the user's panel width instead |
| `krell_height`     | 8       | Base slider track height; a themed sprite can set it instead |
| `meter_height`     | 0       | Base meter height; 0 falls back to Krell height for meters without text |
| `chart_height`     | 32      | Legacy chart strip height. In the desktop app, the profile's Settings → Appearance chart height (32 if unset) always wins. |
| `chart_grid_lines` | 5       | Number of horizontal grid divisions     |
| `chart_grid_width` | 1       | Horizontal grid line width (1 to 4 logical pixels) |
| `chart_series_grayscale` | 0  | Use white and light gray lines in combined charts when set to 1 |
| `chart_overlay_right` | 0 | Put the live chart readout at the top right when set to 1 |
| `panel_icon_size` | 16 | Maximum panel title icon width or height (8 to 32 logical pixels) |
| `top_strip_keep_aspect` | 0 | Scale an enabled top strip proportionally to the panel width |
| `window_shadow` | 1 | Set to `0` to suppress the desktop window shadow on supported platforms. It does not affect the artwork or panel opacity. |

**Optional sculpted panel metrics (new):**

| Key | Default | Effect |
| --- | --- | --- |
| `panel_padding_left`, `panel_padding_top`, `panel_padding_right`, `panel_padding_bottom` | `panel_padding` | Content margins in logical pixels. |
| `panel_bg_<monitor>_padding`, `panel_bg_<monitor>_padding_<edge>` | Inherited | A panel can override all margins or one edge, e.g. `panel_bg_cpu_padding_top`. Resolution is scoped edge, scoped uniform, global edge, global uniform. |
| `chart_glass` | 0 | At 1, skip the chart's independent backdrop and overlays; show the parent panel behind the traces, grid and readout. |
| `chart_corner_radius` | 0 | Clip the complete plot to a rounded rectangle. Clamped to half the shorter plot dimension. |
| `chart_padding_left`, `chart_padding_top`, `chart_padding_right`, `chart_padding_bottom` | 0 | Inset the plot within its existing widget. Excess insets leave at least one logical pixel. No change to chart height or samples. |
| `panel_icon_offset_x`, `panel_icon_offset_y` | 0 | Offset the heading icon rightward and bias it downward; 0–32 logical pixels. The label reserves the horizontal offset and the row reserves vertical space. |
| `panel_icon_spacing` | 4 | Gap in logical pixels between icon and title, 0–32. |
| `host_clock_shared_casing` | 0 | At 1, adjacent Host and Clock panels (in that order) share `surfaces.panel_bg_host_clock` or the base panel surface. Both monitors keep their own rows, data, settings and order. If one is disabled or another panel separates them, use the normal independent surfaces. |

Charts and Krells are already separate widgets. Use the existing
`panel_spacing` to keep a gap between them; the opt-in chart clip does not
change a track's size, position, sprite frame or animation.

For combined charts, a color such as `chart_line_cpu_series` or
`chart_line_net_rx_series` opts into several shades of that color. A grayscale
series palette takes precedence if both options are configured.
CPU charts always use a fixed 0-100% scale. In combined mode, the fill follows
average core usage using a translucent gradient derived from `colors.chart_line_cpu`.
Override it with a color or gradient in `colors.chart_fill_cpu_series`.
Use `"#00000000"` (transparent ARGB) to keep only the individual core lines.
Individual core lines remain visible, and other multi-series charts stay outline-only.
In current builds, sliders and the Memory and Swap indicators render at
two-thirds of their base height, rounded to logical pixels. Text-only
meters and charts are not reduced.

### `images`

Map of semantic key → either a bare string filename (relative to the theme
directory) or an object with `image` plus integer sub-fields like `frames`.
Image paths must canonicalize inside the theme directory; symlinks pointing
outside are rejected.

```json
"images": {
  "panel_bg":    "panel.png",
  "krell_track": "track.png",
  "krell":       { "image": "krell.png", "frames": 32 }
}
```

Recognized keys:

| Key            | Used by                                                       |
| -------------- | ------------------------------------------------------------- |
| `panel_bg`     | Tiled across each Panel as the background; falls back to color |
| `krell_track`  | Tiled across the Krell strip as the unfilled track; falls back |
| `krell`        | Indicator sprite. With `frames > 1` the image is treated as a horizontal strip of N equal-width frames; the frame painted is `frame = value * frames` |
| `panel_icon_*` | Optional icon beside a panel title, e.g. `panel_icon_cpu` for `panel_bg_cpu` |
| `frame_bottom` | Bottom frame strip; use `{"image":"assets/footer.png","mode":"stretch"}` for a single proportional footer between the side frames |
| `window_bg` | Optional chassis backing. Scales across the existing window width and tiles vertically behind the monitor stack, between the header and footer. It adds no margins and is layered below the frame strips, panels and shared Host/Clock casing. Omit it for the existing solid backing. |
| `window_bg_cap_top`, `window_bg_cap_bottom` | Optional transparent end caps for the existing side strips. The left and right bands of each PNG must match the frame strip widths in source pixels. Only those bands are painted: rounded alpha replaces the corresponding rail ends without resizing the window or the header/footer. A missing cap leaves that end unchanged. |

Image-themed widgets gracefully fall back to color-only rendering for any
key that isn't supplied — so a theme can mix images and colors freely.

KrellWeather also accepts [per-condition artwork](../plugins/krellweather/README.md#theme-artwork)
in Theme mode, such as `panel_icon_krellweather_clear` and
`panel_icon_krellweather_clear_night`. These are static assets; missing
keys keep the built-in vector icons.

KrellHealth accepts `panel_icon_krellhealth_heart` in `images` to override the
healthy heart without changing the OK label, other states, or check details.
An optional `metrics.panel_icon_krellhealth_heart_size` sets the longest edge
of the custom artwork in logical pixels (8 to 32). Omit it for the old slot.
See the [KrellHealth artwork example](../plugins/krellhealth/README.md).

The updated KrellHealth plugin also accepts
`panel_icon_krellhealth_status_healthy`, `_status_info`, `_status_warning`,
`_status_critical` and `_status_check` (with the same full prefix). These
map only to OK, INFO, WARN, CRIT and CHECK when the platform reports them.
Missing art falls back to the built-in indicator, or to the original
`panel_icon_krellhealth_heart` for OK. The opt-in
`panel_icon_krellhealth_status_size` takes precedence over the old size
metric, accepts 8–80 logical pixels, and shrinks the artwork if the label
needs room. Use a tightly trimmed, transparent PNG, such as 128 × 128
pixels or larger, and preserve the state label and tooltip.

### Optional animation manifest

A theme can include `animation.json` next to `theme.json`. The original
static images still determine layout and remain the fallback. [Reactor's
manifest](../packaging/themes/reactor/animation.json) shows a complete
example. Only horizontal, looping PNG sheets are supported. Frame size
must match the static asset; the full PNG must be exactly
`frame_width * frames` by `frame_height` pixels. Paths must stay inside
the theme directory. Invalid entries do not replace the static artwork.

Supported targets: `surfaces.panel_bg_top`, `surfaces.panel_bg`,
`surfaces.panel_bg_host`, `surfaces.panel_bg_host_clock`, `images.frame_left`, `images.frame_right`,
`images.frame_bottom`, `images.krell`, and `mail.new`. Mail uses
`panel_icon_krellmail_new` as its static counterpart and only runs with
unread mail and Follow Theme. The `images.krell` animation replaces only
single-frame artwork. Position still follows the measured value.

### `surfaces`

Surfaces are the preferred way to theme panel and chart backgrounds. They
support scalable base art plus optional decorative overlay images:

```json
"surfaces": {
  "panel_bg": {
    "image": "assets/panel.png",
    "overlays": ["assets/logo.png"],
    "slice": 4,
    "opacity": 1.0,
    "tint": "#20000000"
  }
}
```

`slice` enables 9-slice scaling for framed panel art. `overlays` are drawn on
top without 9-slice scaling, so logos or badges do not stretch with the panel.
Qt eight-digit colors use `#AARRGGBB` (alpha first). For a hands-on walkthrough,
see the [theme author's guide](https://cerberix.org/extras/krellix/themes/#build-a-theme).

#### Opt-in slice geometry

`surfaces.<key>.mode` accepts two new values:

- `nine-slice-insets`: Set `slice_insets` in source PNG pixels and optional
  `destination_insets` in logical pixels, with `left`, `top`, `right` and
  `bottom` keys. Source edges default to zero. Each destination edge
  defaults to the corresponding source edge.
- `vertical-caps`: Set `cap_top` and `cap_bottom` in source PNG pixels.
  Omitted caps default to zero. The full-width top, middle and bottom use
  one horizontal scale; the middle takes the variable vertical space.

```json
{
  "surfaces": {
    "panel_bg": {"image":"assets/framed.png","mode":"nine-slice-insets",
      "slice_insets":{"left":12,"right":10,"top":18,"bottom":20},
      "destination_insets":{"left":6,"right":6,"top":12,"bottom":12}},
    "panel_bg_host": {"image":"assets/gloss.png","mode":"vertical-caps",
      "cap_top":20,"cap_bottom":20}
  }
}
```

Keep at least one source pixel between opposite caps. Overlarge values
are reduced proportionally, preserving a center pixel. Short or narrow
destinations compress the destination caps proportionally without
stretching the entire image as a fallback. An absent mode, an unknown
mode, or no positive source edges keeps legacy rendering and the numeric
`slice` fallback exactly as before. Geometry follows the resolved image,
not a missing override. Older Krellix builds ignore these optional fields;
keep a numeric `slice` as a fallback for them. Use full-width, alpha
transparent artwork for sculpted caps, not crops of a single middle column.

## Limits

- `theme.json` must be ≤ 256 KiB.
- File and asset paths are confirmed to canonicalize inside the theme
  directory before being loaded.
- Invalid JSON can fall back to default styling. Missing or unsafe asset
  paths do not load; keep a backup of the last working theme.
