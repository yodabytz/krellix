# Krelldacious macOS Audacious bridge

The macOS Krelldacious plugin controls Audacious through a small Audacious-side
General plugin instead of shelling out to `audacious --play-pause` or relying on
DBus/MPRIS. The bridge keeps Linux behavior unchanged and uses Audacious' native
control API on macOS.

## Files

- `plugins/krelldacious/KrelldaciousPlugin.mm`: macOS-only Krelldacious UI/control implementation.
- `plugins/krelldacious/KrelldaciousPlugin.cpp`: existing Linux DBus implementation.
- `packaging/macos/krelldacious-remote.cc`: Audacious General plugin bridge.

## Runtime layout

Install the Krellix plugin into:

```text
/Applications/Krellix.app/Contents/PlugIns/krellix/libkrelldacious.so
```

Install the Audacious bridge into:

```text
/usr/local/lib/audacious/General/krelldacious-remote.dylib
```

Enable `Krelldacious Remote` in Audacious' plugin registry. The bridge listens on
`/tmp/krelldacious-audacious-<uid>.sock` and accepts `status`, `play`, `pause`,
`playpause`, `next`, `previous`, and `stop`.

The verified artifact is published as:

```text
macos/x86_64/krelldacious-macos-0.1.2-remote-bridge.tar.gz
```
