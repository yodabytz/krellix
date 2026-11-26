param([Parameter(Mandatory = $true)][string]$KrellixDirectory)
$ErrorActionPreference = 'Stop'
$manifest = Get-Content "$PSScriptRoot\manifest.json" -Raw | ConvertFrom-Json
$hostFile = Join-Path $KrellixDirectory 'krellix.exe'
if ((Get-FileHash -LiteralPath $hostFile -Algorithm SHA256).Hash -ne $manifest.host_sha256) {
    throw "Use the matching Windows Krellix directory for $($manifest.host_version)."
}
$dll = Join-Path $PSScriptRoot 'krellweather.dll'
if ((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash -ne $manifest.plugin_sha256) {
    throw 'KrellWeather DLL checksum mismatch.'
}
$plugins = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'krellix\plugins'
$target = Join-Path $plugins 'krellweather.dll'
$manifestPath = Join-Path $plugins 'krellweather-manifest.json'
if (Test-Path -LiteralPath $target) {
    $installedHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    if ($installedHash -eq $manifest.plugin_sha256) {
        'This KrellWeather build is already installed.'
        return
    }
    if (-not (Test-Path -LiteralPath $manifestPath)) { throw 'An unknown KrellWeather DLL exists.' }
    $installed = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $format = '^\d+\.\d+\.\d+-windows\.\d+$'
    if ($installedHash -ne $installed.plugin_sha256 -or
        $installed.plugin_version -notmatch $format -or $manifest.plugin_version -notmatch $format) {
        throw 'An unrecognized KrellWeather build exists. Refusing to overwrite it.'
    }
    $old = [version]($installed.plugin_version -replace '-windows\.', '.')
    $new = [version]($manifest.plugin_version -replace '-windows\.', '.')
    if ($new -le $old) { throw 'Refusing to overwrite an equal or newer KrellWeather build.' }
    $backup = Join-Path $plugins "backups\krellweather-$($installed.plugin_version)"
    if (-not (Test-Path -LiteralPath $backup)) {
        New-Item -ItemType Directory -Path $backup -Force | Out-Null
        Copy-Item -LiteralPath $target,$manifestPath -Destination $backup
    }
}
New-Item -ItemType Directory -Path $plugins -Force | Out-Null
Copy-Item -LiteralPath $dll -Destination $target
Copy-Item -LiteralPath "$PSScriptRoot\manifest.json" -Destination $manifestPath
"Installed KrellWeather $($manifest.plugin_version). Enable it in Settings > Plugins > Krellweather."
