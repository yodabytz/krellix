param([Parameter(Mandatory = $true)][string]$KrellixDirectory)
$ErrorActionPreference = 'Stop'
$manifest = Get-Content "$PSScriptRoot\manifest.json" -Raw | ConvertFrom-Json
$hostFile = Join-Path $KrellixDirectory 'krellix.exe'
if ((Get-FileHash $hostFile -Algorithm SHA256).Hash -ne $manifest.host_sha256) {
    throw 'KrellMail requires the matching Windows Krellix host.'
}
$dll = Join-Path $PSScriptRoot 'krellmail.dll'
if ((Get-FileHash $dll -Algorithm SHA256).Hash -ne $manifest.plugin_sha256) {
    throw 'KrellMail DLL checksum mismatch.'
}
$plugins = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'krellix\plugins'
$target = Join-Path $plugins 'krellmail.dll'
if (Test-Path $target) {
    if ((Get-FileHash $target -Algorithm SHA256).Hash -ne $manifest.plugin_sha256) {
        throw 'A different KrellMail build already exists. Refusing to overwrite or downgrade it.'
    }
    Write-Output 'This KrellMail build is already installed.'
    return
}
New-Item -ItemType Directory $plugins -Force | Out-Null
Copy-Item $dll $target
Copy-Item "$PSScriptRoot\manifest.json" (Join-Path $plugins 'krellmail-manifest.json')
Write-Output "Installed KrellMail $($manifest.plugin_version). Reopen the local instance and enable it in Settings / Plugins."
