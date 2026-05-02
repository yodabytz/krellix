# Krellkam

Krellkam is an installable Krellix plugin that displays up to five periodically
refreshed images in a compact chart-like panel. It is intended for webcam
snapshots, camera still URLs, and other image endpoints.

Install the built plugin shared library into one of Krellix's plugin paths:

- `~/.local/share/krellix/plugins/`
- `/usr/lib/krellix/plugins/`
- `./plugins/` when running from a build directory

Configure sources with QSettings keys:

```ini
[plugins/krellkam]
enabled=true
interval_ms=5000
field_height=48
source1=/path/to/image.jpg
source2=https://example.com/webcam.jpg
source3=
source4=
source5=
```

You can also set `sources` to a comma, semicolon, or newline separated list.

Click a loaded image field to open the current frame in a small closeable
viewer window.
