param([string]$KrellixDirectory = [IO.Path]::GetFullPath("$PSScriptRoot\..\.."))
$ErrorActionPreference = 'Stop'
$manifest = Get-Content "$PSScriptRoot\manifest.json" -Raw | ConvertFrom-Json
$hostFile = Join-Path $KrellixDirectory 'krellix.exe'
if (-not (Test-Path $hostFile)) { throw 'Pass -KrellixDirectory pointing to the matching Krellix Windows preview.' }
if ((Get-FileHash $hostFile -Algorithm SHA256).Hash -ne $manifest.host_sha256) {
    throw 'This plugin requires its matching Windows host build. Install the complete matching preview first.'
}
$dll = Join-Path $PSScriptRoot 'krelltop.dll'
if ((Get-FileHash $dll -Algorithm SHA256).Hash -ne $manifest.plugin_sha256) { throw 'Plugin checksum mismatch.' }
if (Get-Process krellix -ErrorAction SilentlyContinue) { throw 'Close Krellix before installing the plugin.' }
$destination = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'krellix\plugins'
New-Item -ItemType Directory $destination -Force | Out-Null
$target = Join-Path $destination 'krelltop.dll'
if (Test-Path $target) {
    if ((Get-FileHash $target -Algorithm SHA256).Hash -eq $manifest.plugin_sha256) {
        Write-Output 'The matching KrellTop is already installed.'
        exit 0
    }
    throw 'A different KrellTop DLL is installed. Preserve it and verify its version before replacing it; automatic downgrades are not allowed.'
}
Copy-Item $dll $target
Copy-Item "$PSScriptRoot\manifest.json" (Join-Path $destination 'krelltop-manifest.json')
Write-Output "Installed KrellTop $($manifest.plugin_version). Enable it in Settings / Plugins for each instance."
