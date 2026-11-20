# Windows preview with KrellTop

Build with MSYS2 UCRT64 Qt 6, GCC, CMake and Ninja. This preview includes
KrellTop for Windows, but no Windows daemon. Linux/macOS builds are unchanged.

## KrellTop

Close Krellix, then run `extras/krelltop/install-krelltop.ps1` in PowerShell
(no administrator rights required). For a standalone plugin download, pass
`-KrellixDirectory C:/path/to/Krellix`. The installer verifies the matching
host and DLL before installing into your per-user plugin directory. Reopen
Krellix and enable KrellTop in Settings / Plugins for each desired instance.
Your existing settings are not changed by the installer.

The Windows DLL requires the matching Windows preview. It must not be used
with the older preview that did not export the plugin API. Keep the bundled
host and DLL together when upgrading; do not mix Linux/macOS plugin binaries.

Local Windows processes are sampled asynchronously once a second; the panel
refreshes every five seconds. CPU usage follows the Unix convention: 100%
means one busy logical core. Memory is the process working set. Protected or
inaccessible processes are omitted without requesting elevated privileges.
Tooltips show executable paths, not full Windows command-line arguments.

Remote instances read only the selected daemon's process stream, with bounded
messages and timeouts. Disconnection or missing data clears the display;
there is never a fallback to local Windows processes.

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
Windows sensor parsing, remote source isolation, native process counters,
and loading the actual KrellTop DLL with local/remote lifecycle tests.
`krellix-sensors.exe
--once` provides a diagnostic JSON sample without starting Krellix. Physical
temperature accuracy still requires comparison on supported physical hardware;
VM and synthetic tests cannot establish that.

## Windows Crash Reports

From a PowerShell window in the portable Krellix folder, run
`./crash-report.ps1` for a local JSON summary of recent Krellix application
errors. The summary includes event timestamps, process IDs, executable and
module build stamps, exception codes, and offsets. It does not include
command lines, profile names, settings, or full paths.

For future crashes, optionally run `./crash-report.ps1 -Action Enable` in
**64-bit PowerShell as administrator**. Windows Error Reporting keeps up to
five local minidumps for `krellix.exe` in the crashing user's
`%LOCALAPPDATA%\krellix\crash-dumps`. This does not require restarting
Krellix, does not change existing custom settings, and does not upload
anything. Use `-Action Disable` to remove only the override created by the
script. Existing dumps are not deleted. Treat dumps as private: they can
contain sensitive process memory.
