# Windows core preview

Build with MSYS2 UCRT64 Qt 6, GCC, CMake and Ninja. Plugins and the daemon
are not part of this Windows preview. Linux/macOS builds are unchanged.

## Sensors

The bundled `sensors/krellix-sensors.exe` reads Libre Hardware Monitor 0.9.6
directly, without its GUI, WMI provider, HTTP listener, or a background service.
It uses .NET Framework 4.7.2 or later (included with Windows 11).
The helper stays at the user's privilege level. Krellix does not install a
kernel driver, request elevation, or disable Windows security protections.
Some physical CPU/mainboard readings require the official PawnIO driver and
administrator permissions; unsupported hardware remains unavailable.

VMware does not necessarily expose physical host sensors. Known virtual-disk
placeholders and NVMe warning/critical thresholds are not live temperatures
and are excluded. Zero fan RPM remains a valid reading.

The helper refreshes every five seconds. Its output is bounded and validated.
It is isolated from Qt's UI thread, killed after a stalled read, and retried no
more than once a minute. Stale readings are cleared. It exits when its parent
exits. Remote instances bypass the helper entirely, even when the server is
disconnected or sends no sensor readings.

Download the official upstream archive:
https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases/download/v0.9.6/LibreHardwareMonitor.zip

SHA256: `086d9f1b5a99e643edc2cfaaac16051685b551e4c5ac0b32a57c58c0e529c001`

Before configuring CMake, run from PowerShell:

```powershell
./packaging/windows/build-sensors.ps1 -Archive C:/downloads/LibreHardwareMonitor.zip -Output C:/build/krellix/sensors
```

Set `KRELLIX_SENSOR_DIR` if using a different helper output directory. Ship
the helper, its DLLs/config, upstream license/third-party notices, and the
unmodified upstream source archive alongside the portable application.
Upstream source: https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/tree/v0.9.6

## Launching

```text
krellix.exe
krellix.exe --host mail.quantumbytz.com
krellix.exe --host mail.vibrixmedia.com
```

Each host has independent settings. Remote CPU, memory, disks, interfaces,
ports, processes, uptime, battery, sensors, clock and host identity come only
from the selected daemon. Missing remote readings are not replaced locally.

## Verification

Run CTest for settings lifecycle, native core settings, remote reconnect,
Windows sensor parsing and remote source isolation. `krellix-sensors.exe
--once` provides a diagnostic JSON sample without starting Krellix. Physical
temperature accuracy still requires comparison on supported physical hardware;
VM and synthetic tests cannot establish that.
