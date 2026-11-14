# KrellWiFi for Windows

Requires Krellix 0.1.2-windows-preview.2 x86-64. Close Krellix, extract the
plugin ZIP, then run in PowerShell:

```powershell
./install-krellwifi.ps1 -KrellixDirectory C:/path/to/Krellix
```

Reopen the local Krellix instance and enable KrellWiFi in Settings / Plugins.
The installer verifies host, plugin and helper hashes. It changes no settings
and requests no administrator privileges.

## What It Shows

- Access-point count, strongest signal/SSID, busiest band/channel and counts
  of open versus privacy-enabled APs. Privacy-enabled does not mean strong
  encryption: WEP also sets the privacy bit.
- Duplicate BSSIDs on the same frequency are counted once across adapters.
- Channel labels distinguish 2.4, 5 and 6 GHz. SSIDs and tooltip text are
  sanitized/escaped. Raw information-element blobs are never parsed.
- Scans use the native Windows WLAN API every 60 seconds in a separate
  helper. Each scan waits up to four seconds; the helper has a 20-second
  deadline. At most four adapters and 4096 AP records are processed.
- No-adapter, stopped service, denied permission, radio-off, timeout and
  malformed-data states clear previous AP readings rather than showing stale
  or fabricated information.

Scanning can briefly affect wireless latency, so it is deliberately not run
on each UI tick. The plugin does not connect/disconnect networks, read saved
profiles/passwords, enable services or change privacy/security settings.
Windows may require location consent for Wi-Fi scanning; denial is respected.

## VM and Remote Limitations

VMware's virtual Ethernet adapter is not a Wi-Fi radio, even when the Mac
uses Wi-Fi. A Wi-Fi adapter exposed to Windows (for example USB passthrough)
is needed for actual scans. This VM's no-adapter state and synthetic scan
fixtures were tested; live RF scanning still needs physical-adapter validation.

KrellWiFi remains local-only. The current daemon protocol has no Wi-Fi scan
telemetry. Remote instances never substitute Windows or Mac wireless data.
The original Linux/macOS implementation is unchanged.

## API References

- https://learn.microsoft.com/en-us/windows/win32/api/wlanapi/nf-wlanapi-wlanscan
- https://learn.microsoft.com/en-us/windows/win32/api/wlanapi/nf-wlanapi-wlanregisternotification
- https://learn.microsoft.com/en-us/windows/win32/nativewifi/wi-fi-access-location-changes
- https://learn.microsoft.com/en-us/windows/win32/api/wlanapi/ns-wlanapi-wlan_bss_entry
