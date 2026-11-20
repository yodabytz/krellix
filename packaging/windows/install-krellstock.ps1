param([Parameter(Mandatory = $true)][string]$KrellixDirectory)
$ErrorActionPreference = 'Stop'
$manifest = Get-Content "$PSScriptRoot\manifest.json" -Raw | ConvertFrom-Json
$hostFile = Join-Path $KrellixDirectory 'krellix.exe'
if ((Get-FileHash -LiteralPath $hostFile -Algorithm SHA256).Hash -ne $manifest.host_sha256) {
    throw 'Use the matching Windows Krellix preview.6 folder for verification.'
}
$dll = Join-Path $PSScriptRoot 'krellstock.dll'
if ((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash -ne $manifest.plugin_sha256) {
    throw 'KrellStock DLL checksum mismatch.'
}
$plugins = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'krellix\plugins'
$target = Join-Path $plugins 'krellstock.dll'
if (Test-Path -LiteralPath $target) {
    $installedHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    if ($installedHash -eq $manifest.plugin_sha256) {
        'This KrellStock build is already installed.'
        return
    }
    $installedManifestPath = Join-Path $plugins 'krellstock-manifest.json'
    $installed = Get-Content -LiteralPath $installedManifestPath -Raw | ConvertFrom-Json
    $format = '^\d+\.\d+\.\d+-windows\.\d+$'
    if ($installedHash -ne $installed.plugin_sha256 -or
        $installed.plugin_version -notmatch $format -or $manifest.plugin_version -notmatch $format) {
        throw 'An unrecognized KrellStock build exists. Refusing to overwrite it.'
    }
    $old = [version]($installed.plugin_version -replace '-windows\.', '.')
    $new = [version]($manifest.plugin_version -replace '-windows\.', '.')
    if ($new -le $old) { throw 'Refusing to overwrite an equal or newer KrellStock build.' }
    $backup = Join-Path $plugins "backups\krellstock-$($installed.plugin_version)"
    if (-not (Test-Path -LiteralPath $backup)) {
        New-Item -ItemType Directory -Path $backup -Force | Out-Null
        Copy-Item -LiteralPath $target,$installedManifestPath -Destination $backup
    }
}
New-Item -ItemType Directory -Path $plugins -Force | Out-Null
Copy-Item -LiteralPath $dll -Destination $target
Copy-Item -LiteralPath "$PSScriptRoot\manifest.json" -Destination (Join-Path $plugins 'krellstock-manifest.json')
"Installed KrellStock $($manifest.plugin_version). Reopen local Krellix and edit symbols in Settings > Plugins > KrellStock."
