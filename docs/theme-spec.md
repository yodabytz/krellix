# krellix Theme Format

A krellix theme is a directory containing a `theme.json` file plus any image
assets it references. Themes live in:

- `~/.config/krellix/themes/<name>/` (user, takes precedence)
- `/usr/share/krellix/themes/<name>/` (system, installed)
- `./themes/<name>/` (relative to working dir, for development)

Theme names must match `[A-Za-z0-9._-]{1,64}` and may not start with `.`.

While krellix is running, edits to `theme.json` are picked up automatically;
panels repaint on save. Asset paths are resolved relative to the theme
directory and may not escape it.

## `theme.json`

A single JSON object with up to five sections: `colors`, `fonts`, `metrics`,
`images`, plus optional `name` / `version` / `author` / `description`
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
  "images":  { ... }
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
| `text_primary`     | Primary text + chart line color          |
| `text_secondary`   | Secondary/label text                     |
| `krell_track`      | Krell unfilled track color               |
| `krell_indicator`  | Krell moving indicator color             |

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

Sizes are clamped to `4..72` points.

### `metrics`

Map of name → integer (clamped to `0..4096`).

| Key                | Default | Meaning                                 |
| ------------------ | ------- | --------------------------------------- |
| `panel_padding`    | 4       | Inner padding around each panel         |
| `panel_spacing`    | 2       | Gap between widgets within a panel      |
| `panel_border`     | 1       | Panel border line width (0 = none)      |
| `panel_min_width`  | 200     | Window/panel fixed width in pixels      |
| `krell_height`     | 8       | Krell strip height                      |
| `chart_height`     | 32      | Chart strip height                      |
| `chart_grid_lines` | 4       | Number of horizontal grid divisions     |

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

Image-themed widgets gracefully fall back to color-only rendering for any
key that isn't supplied — so a theme can mix images and colors freely.

## Limits

- `theme.json` must be ≤ 256 KiB.
- File and asset paths are confirmed to canonicalize inside the theme
  directory before being loaded.
- Invalid JSON or unsafe paths leave the previously loaded theme in place.
