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
    $installed = Get-Content (Join-Path $destination 'krelltop-manifest.json') -Raw | ConvertFrom-Json
    if ((Get-FileHash $target -Algorithm SHA256).Hash -ne $installed.plugin_sha256) { throw 'Existing plugin does not match its manifest; inspect before replacing.' }
    $format = '^(\d+)\.(\d+)\.(\d+)-windows\.(\d+)$'
    if ($installed.plugin_version -notmatch $format -or $manifest.plugin_version -notmatch $format) { throw 'Unknown plugin version; automatic replacement refused.' }
    $oldVersion = [version]($installed.plugin_version -replace '-windows\.', '.')
    $newVersion = [version]($manifest.plugin_version -replace '-windows\.', '.')
    if ($newVersion -le $oldVersion) { throw 'Downgrade or same-version replacement refused.' }
    $backup = Join-Path $destination "backups\$($installed.plugin_version)"
    New-Item -ItemType Directory $backup -Force | Out-Null
    Copy-Item $target $backup
    Copy-Item (Join-Path $destination 'krelltop-manifest.json') $backup
}
Copy-Item $dll $target -Force
Copy-Item "$PSScriptRoot\manifest.json" (Join-Path $destination 'krelltop-manifest.json') -Force
Write-Output "Installed KrellTop $($manifest.plugin_version). Enable it in Settings / Plugins for each instance."
