# Bluetooth (KrellBluetooth plugin)

Optional, disabled by default. Enable under Settings → Plugins → Bluetooth.
Each connected device gets one compact, non-scrolling row: an elided name and
battery percentage. Hover for the full name, address and battery details.
Device names use the current theme's text accent and update when the theme changes.
An em dash means the device/OS did not report a battery; it does not mean 0%.
Separate earbud/case readings appear in the tooltip without an invented average.

The monitor refreshes every 15 seconds. Queries are asynchronous and bounded;
failed queries clear stale devices. No pairing, scanning, forced disconnection,
root access or radio power changes are performed. Settings are instance-specific.
Remote instances deliberately omit this local-only plugin.

## Platforms

- **Linux:** BlueZ system D-Bus ObjectManager, Device1.Connected and Battery1.Percentage.
  Requires Qt6 DBus and a running BlueZ service; works without systemd-specific commands.
  Adapter off, no adapters, no connected devices and service errors have distinct states.
- **macOS:** Apple's system_profiler Bluetooth JSON, supplemented with matching
  Apple HID battery readings from IOKit and a native CoreBluetooth helper reading
  standard BLE Battery Service (180F / 2A19), plus `pmset -g accps -xml` for
  headset/accessory power readings matched by hardware address. Tested on Intel macOS 12.7.6.
  The helper queries already-connected peripherals only, with a six-second limit.
  macOS may ask for Bluetooth access. It obtains and releases its own logical
  connection reference; existing system connections are preserved.
  BLE names must be unique in both lists to merge readings safely; renamed devices
  are supported when macOS reports the new name. Missing/ambiguous values stay unknown.
  Live tests: renamed Smart KBD reports 60%; Apple mouse reports its IOKit percentage;
  JBL Tune reports 50% through accessory power. No private framework selectors are used.
  Battery availability depends on the device and OS; no private Bluetooth API is used.
- **FreeBSD:** hccontrol -N read_connection_list, using the first HCI adapter.
  Shows established connections by address; battery data is unavailable through this
  interface. Parser tested against the FreeBSD Handbook format; hardware validation
  on FreeBSD is still required.
- Other Unix variants show an explicit unsupported status. There is no universal
  Bluetooth API across Unix implementations.

## Build

Use the normal Krellix Qt6 build, then `cmake --build build --target krellbluetooth`.
On macOS also build `krellbt-battery` and install it in `Contents/Helpers/`.
The plugin is `build/plugins/libkrellbluetooth.so` (including on macOS).
Linux optional installation: `/usr/lib/krellix/plugins/`; macOS packages include it
in `Krellix.app/Contents/PlugIns/krellix/`. Use the matching host build so Settings
contains its enable control. The Mac packaging pipeline includes it, disabled.

Tests: `bluetooth-data` covers parsing, malformed output, missing/invalid/zero
batteries, earbud levels and disconnected devices. `bluetooth-monitor` checks
120-pixel layout, full tooltips, stale-row clearing, default-disabled behavior,
remote isolation and destruction during an asynchronous query.

API references:
- https://github.com/bluez/bluez/blob/master/doc/org.bluez.Device.rst
- https://github.com/bluez/bluez/blob/master/doc/org.bluez.Battery.rst
- https://docs.freebsd.org/en/books/handbook/advanced-networking/#network-bluetooth
