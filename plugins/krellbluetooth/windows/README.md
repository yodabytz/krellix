# KrellBluetooth Windows Preview

Version 0.1.2-windows.1, for Krellix 0.1.2-windows-preview.2 x86-64.
Close Krellix, extract the ZIP, then run in PowerShell:

```powershell
./install-krellbluetooth.ps1 -KrellixDirectory C:/path/to/Krellix
```

Reopen the local instance and enable Bluetooth in Settings / Plugins.
The installer checks host/plugin/helper hashes, refuses replacing a different
build without inspection, and changes no settings or other plugins.

## Behavior

- Compact non-scrolling rows with elided names, theme fonts/colors, full escaped
  tooltips and accessibility names. Unknown battery is a dash, never a fake 0%.
- Connected classic and LE devices from Windows WinRT APIs, refreshed every
  15 seconds. Duplicate addresses are merged; names are never used as identity.
- Uses the documented Windows System.Devices.BatteryLife property when provided
  for a connected classic or LE device; its refresh age is controlled by Windows.
  Values outside 0-100 (including the documented unknown marker 101) stay unknown.
- If Windows supplies no level for a connected LE device, reads standard Battery Service 180F / Level 2A19.
  Only a single unambiguous battery characteristic is shown. Multiple batteries
  (earbuds/case) and proprietary protocols are not queried. Classic devices with
  no Windows-provided battery property remain unknown.
- Battery support depends on the device, drivers, OS permissions and VMware's
  Bluetooth capabilities. A shared virtual adapter may not expose LE features.
- Queries run in a separate .NET Framework helper, not the GUI thread. Each
  asynchronous operation is bounded to 1.8 seconds, the helper to 10 seconds,
  and the parent deadline to 12 seconds. At most 64 connected rows are accepted.
- Failed queries clear stale rows. Denied access, missing radio, radio-off and
  timeout states are explicit. No elevation, pairing, scanning, radio changes,
  service changes, driver installation or credential access is performed.
- BLE battery queries read an already-connected device and release their own
  references. They do not request pairing or set MaintainConnection. Windows
  controls access; unavailable/denied battery reads remain unknown.
- Local-only: the daemon has no Bluetooth telemetry. Remote windows do not
  substitute this Windows VM's or the Mac's devices.

## Validation and Limitations

Tested with the installed Windows host, VM radio enumeration and no-connected-
devices state, plus fixtures covering zero/unknown/full batteries, escaped names,
120px layout, malformed/oversized output, timeouts, shutdown and remote isolation.
Live connected-device battery readings still need a compatible device attached
to Windows; fixtures are not a substitute for that hardware validation.
Existing Linux/macOS implementations and packages are unchanged.

The helper compiles using Windows' .NET Framework compiler, installed WinRT
metadata and framework assemblies. No additional runtime/driver is bundled.

## Official API References

- https://learn.microsoft.com/en-us/uwp/api/windows.devices.bluetooth.bluetoothdevice.getdeviceselectorfromconnectionstatus
- https://learn.microsoft.com/en-us/uwp/api/windows.devices.bluetooth.bluetoothledevice.getdeviceselectorfromconnectionstatus
- https://learn.microsoft.com/en-us/windows/apps/develop/devices-sensors/gatt-client
- https://learn.microsoft.com/en-us/windows/uwp/devices-sensors/device-information-properties
- https://learn.microsoft.com/en-us/windows/win32/properties/props-system-devices-batterylife
