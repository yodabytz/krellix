param([Parameter(Mandatory=$true)][string]$KrellixDirectory)
$ErrorActionPreference = 'Stop'
$manifest = Get-Content "$PSScriptRoot\manifest.json" -Raw | ConvertFrom-Json
$hostFile = Join-Path $KrellixDirectory 'krellix.exe'
if ((Get-FileHash $hostFile).Hash -ne $manifest.host_sha256) { throw 'Install the matching Krellix Windows preview before this plugin.' }
foreach ($entry in @(@{Name='krellhealth.dll';Hash=$manifest.plugin_sha256},@{Name='krellhealth-probe.exe';Hash=$manifest.probe_sha256})) {
    if ((Get-FileHash (Join-Path $PSScriptRoot $entry.Name)).Hash -ne $entry.Hash) { throw "Checksum mismatch: $($entry.Name)" }
}
if (Get-Process krellix -ErrorAction SilentlyContinue) { throw 'Close Krellix before installing.' }
$plugins = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'krellix\plugins'
$probe = Join-Path $KrellixDirectory 'health'
foreach ($entry in @(@{Path="$plugins\krellhealth.dll";Hash=$manifest.plugin_sha256},@{Path="$probe\krellhealth-probe.exe";Hash=$manifest.probe_sha256})) {
    if ((Test-Path $entry.Path) -and (Get-FileHash $entry.Path).Hash -ne $entry.Hash) {
        throw 'A different KrellHealth build is installed. Inspect its version before replacing; automatic downgrades are refused.'
    }
}
New-Item -ItemType Directory $plugins,$probe -Force | Out-Null
Copy-Item "$PSScriptRoot\krellhealth-probe.exe" "$probe\krellhealth-probe.exe" -Force
Copy-Item "$PSScriptRoot\krellhealth.dll" "$plugins\krellhealth.dll" -Force
Copy-Item "$PSScriptRoot\manifest.json" "$plugins\krellhealth-manifest.json" -Force
Write-Output "Installed KrellHealth $($manifest.plugin_version). Enable it in Settings / Plugins for each instance."
