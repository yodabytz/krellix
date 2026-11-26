# KrellHealth theme artwork

To replace the built-in healthy heart, add this entry to the existing
`images` object in the theme's `theme.json`:

```json
{
  "images": {
    "panel_icon_krellhealth_heart": "assets/health-heart.png"
  }
}
```

Place the PNG inside the theme folder. A square transparent 32 or 64 px
source works well; keep the silhouette simple and avoid large empty margins.
The artwork scales proportionally inside the current heart slot. It does not
change row height, the OK label, or any health checks. It is static, and the
original heart is used if the asset is missing or unreadable.

`panel_icon_krellhealth` is separate: it decorates the **Health** panel title.
The healthy heart override is never drawn in an informational, warning,
critical, or checking state. Hover over the Health panel for the full details
of reported checks and findings.

During theme development, save `theme.json` or reselect the theme after
changing a PNG. The same key works on the Linux, macOS, and Windows plugin
builds that support this feature.
