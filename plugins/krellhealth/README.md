# KrellHealth theme artwork

To replace the built-in healthy heart, add this entry to the existing
`images` object in the theme's `theme.json`:

```json
{
  "metrics": {
    "panel_icon_krellhealth_heart_size": 26
  },
  "images": {
    "panel_icon_krellhealth_heart": "assets/health-heart.png"
  }
}
```

Place the PNG inside the theme folder. A square transparent 32 or 64 px
source works well; keep the silhouette simple and avoid large empty margins.
The optional size metric sets the longest side, from 8 to 32 logical pixels.
With no metric, the art fits the original heart slot. An explicit larger
icon can make the OK row taller, without changing the label or other states.
The original heart and row size return if the PNG is missing or unreadable.

`panel_icon_krellhealth` is separate: it decorates the **Health** panel title.
The healthy heart override is never drawn in an informational, warning,
critical, or checking state. Hover over the Health panel for the full details
of reported checks and findings.

During theme development, save `theme.json` or reselect the theme after
changing a PNG. The same key works on the Linux, macOS, and Windows plugin
builds that support this feature.
