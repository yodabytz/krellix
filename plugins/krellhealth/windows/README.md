# KrellHealth for Windows

Requires the matching Krellix 0.1.2-windows-preview.11 x86-64 host. The
installer verifies the host, DLL and helper hashes and refuses to overwrite
an unknown/different installed build. No administrator rights are needed.

Close Krellix. From the extracted plugin directory, run in PowerShell:

```powershell
./install-krellhealth.ps1 -KrellixDirectory C:/path/to/Krellix
```

Reopen Krellix and enable KrellHealth in Settings / Plugins independently
for each desired instance. Existing settings are not changed by the installer.

Local Windows checks run every 60 seconds in an isolated helper with a
15-second deadline. Checks are read-only and never install updates, start
services, request elevation, or change security settings:

- Space available to the current user on local fixed drive letters: warning
  at 85% full, critical at 95%.
- Physical memory pressure: warning at 90%, critical at 98%.
- Windows Update's read-only reboot-required property (no update scan).
- Automatic Windows services stopped with a reported error. Ordinary stopped
  services are not treated as failures.

Unavailable probes are shown as informational findings, not silently treated
as healthy. SMART, backup age, certificates, RAID/ZFS/Btrfs and event-log history
are not covered by this Windows probe. The tooltip documents actual coverage.

Remote instances use only the selected daemon's health findings. Missing or
invalid health data, timeouts, and disconnection show an unavailable/check
state, never a false OK or local Windows fallback. Incoming messages, finding
counts and text sizes are bounded; tooltip text is HTML-escaped.

Clear health is displayed as a heart followed by OK, matching the original
plugin. Missing data, unavailable checks and warnings never show a healthy heart.
Themes can replace the heart with `images.panel_icon_krellhealth_heart`.
This only changes the artwork; the OK text and detailed tooltip are unchanged.

## API References

- https://learn.microsoft.com/en-us/windows/win32/fileio/obtaining-volume-information
- https://learn.microsoft.com/en-us/windows/desktop/api/Wuapi/nn-wuapi-isysteminformation
- https://learn.microsoft.com/en-us/windows/desktop/api/winsvc/ns-winsvc-service_status_process
