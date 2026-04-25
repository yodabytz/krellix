# krellix

A themeable Qt 6 desktop system monitor in the spirit of GKrellM.

A skinny vertical panel of stacked "krell" monitors — CPU, memory, disk, net, sensors,
and more — driven by a clean-sheet native theme format that any user can author.

> Status: very early scaffolding. Phase 0/1 only — host panel and clock render against
> a hot-reloadable theme. Real monitors land next.

## Build

Requires Qt 6.2+ and a C++17 compiler.

```bash
sudo apt install qt6-base-dev cmake g++   # Debian/Ubuntu
cmake -S . -B build
cmake --build build -j
./build/bin/krellix
```

Run with a specific theme:

```bash
./build/bin/krellix --theme default
```

## Themes

Themes live in:

- `~/.config/krellix/themes/<name>/` (user, takes precedence)
- `/usr/share/krellix/themes/<name>/` (system)

A theme is a directory containing `theme.json` plus any PNG sprites it references.
See [`docs/theme-spec.md`](docs/theme-spec.md) for the full format.

While krellix is running, edits to `theme.json` are picked up automatically — the
panel repaints on save, so theme authors can iterate without restarting the app.

## License

GPL-3.0-or-later. See `COPYING` (TODO).
