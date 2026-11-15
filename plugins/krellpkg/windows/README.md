# KrellPkg for Windows 0.1.2-windows.1

Requires the matching Krellix 0.1.2 Windows preview.2 x86_64 host and Microsoft
App Installer (WinGet) registered for the signed-in desktop user. Tested with
WinGet 1.29.380 on Windows 11. No administrator rights are required.

Close Krellix, extract this package, and run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\install-krellpkg.ps1 -KrellixDirectory C:\path\to\Krellix
```

Enable KrellPkg in Settings / Plugins for each desired instance. Local Windows
instances automatically use the existing WinGet community source. The existing
Unix package-system checkboxes do not select Windows backends. Include/exclude
patterns match package IDs or display names, case-insensitively; wildcards work,
and exclusions win. Changes apply on the next one-second monitor tick without
another network query. Disabled plugins do not query anything.

Checks run in a separate process every five minutes, with a 60-second deadline.
Hover over the count for package IDs, names, installed/available versions, and
the check time. Tooltips list up to 100 packages; counts include every match.
WinGet's API determines whether an applicable newer version exists. This is not
a Windows Update or Microsoft Store monitor and does not claim to cover programs
unknown to WinGet. Offline, missing-source, consent and failed checks are shown
as unavailable, never as zero updates. A source may refresh its cached catalog.
No source is added/reset, no agreements are accepted automatically, and no
package is installed, removed or upgraded.

Remote instances consume only their selected daemon's package data using the
existing core connection. They never run the local WinGet helper. Server-side
package-list freshness and backend support still apply; local filters do not
alter the server's authoritative count. Unsupported servers display unavailable.

The installer verifies hashes and the exact host build, refuses a different
existing KrellPkg binary rather than downgrading it, and does not edit settings.
Linux/macOS implementations and host binaries are unchanged. The bundled WinGet
API metadata is covered by WinGet-LICENSE.txt; it is not a bundled WinGet server.
