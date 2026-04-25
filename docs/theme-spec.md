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

A single JSON object with up to four keys: `colors`, `fonts`, `metrics`,
plus optional `name` / `version` / `author` / `description` metadata. Every
key is optional — missing values fall back to built-in defaults.

```json
{
  "name":        "default",
  "version":     "1.0",
  "author":      "you",
  "description": "...",

  "colors":  { ... },
  "fonts":   { ... },
  "metrics": { ... }
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

## Limits

- `theme.json` must be ≤ 256 KiB.
- File and asset paths are confirmed to canonicalize inside the theme
  directory before being loaded.
- Invalid JSON or unsafe paths leave the previously loaded theme in place.
