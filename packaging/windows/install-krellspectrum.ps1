param([Parameter(Mandatory = $true)][string]$KrellixDirectory)
$ErrorActionPreference = 'Stop'
$manifest = Get-Content "$PSScriptRoot\manifest.json" -Raw | ConvertFrom-Json
$hostFile = Join-Path $KrellixDirectory 'krellix.exe'
if ((Get-FileHash -LiteralPath $hostFile -Algorithm SHA256).Hash -ne $manifest.host_sha256) {
    throw 'Use the published Windows Krellix preview.6 directory for verification.'
}
$dll = Join-Path $PSScriptRoot 'krellspectrum.dll'
if ((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash -ne $manifest.plugin_sha256) {
    throw 'KrellSpectrum DLL checksum mismatch.'
}
$helper = Join-Path $PSScriptRoot 'krellspectrum-wasapi.exe'
if ((Get-FileHash -LiteralPath $helper -Algorithm SHA256).Hash -ne $manifest.helper_sha256) {
    throw 'KrellSpectrum playback helper checksum mismatch.'
}
$plugins = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'krellix\plugins'
$target = Join-Path $plugins 'krellspectrum.dll'
$helperTarget = Join-Path $plugins 'krellspectrum-wasapi.exe'
$manifestPath = Join-Path $plugins 'krellspectrum-manifest.json'
if ((Test-Path -LiteralPath $target) -ne (Test-Path -LiteralPath $helperTarget)) {
    throw 'An incomplete KrellSpectrum installation exists. Refusing to overwrite it.'
}
if (Test-Path -LiteralPath $target) {
    $installedHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    $helperHash = (Get-FileHash -LiteralPath $helperTarget -Algorithm SHA256).Hash
    if ($installedHash -eq $manifest.plugin_sha256 -and $helperHash -eq $manifest.helper_sha256) {
        'This KrellSpectrum build is already installed.'
        return
    }
    if (-not (Test-Path -LiteralPath $manifestPath)) { throw 'An unknown KrellSpectrum DLL exists.' }
    $installed = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $format = '^\d+\.\d+\.\d+-windows\.\d+$'
    if ($installedHash -ne $installed.plugin_sha256 -or $helperHash -ne $installed.helper_sha256 -or
        $installed.plugin_version -notmatch $format -or $manifest.plugin_version -notmatch $format) {
        throw 'An unrecognized KrellSpectrum build exists. Refusing to overwrite it.'
    }
    $old = [version]($installed.plugin_version -replace '-windows\.', '.')
    $new = [version]($manifest.plugin_version -replace '-windows\.', '.')
    if ($new -le $old) { throw 'Refusing to overwrite an equal or newer KrellSpectrum build.' }
    $backup = Join-Path $plugins "backups\krellspectrum-$($installed.plugin_version)"
    if (-not (Test-Path -LiteralPath $backup)) {
        New-Item -ItemType Directory -Path $backup -Force | Out-Null
        Copy-Item -LiteralPath $target,$helperTarget,$manifestPath -Destination $backup
    }
}
New-Item -ItemType Directory -Path $plugins -Force | Out-Null
Copy-Item -LiteralPath $helper -Destination $helperTarget
Copy-Item -LiteralPath $dll -Destination $target
Copy-Item -LiteralPath "$PSScriptRoot\manifest.json" -Destination $manifestPath
"Installed KrellSpectrum $($manifest.plugin_version). Enable it in Settings > Plugins > KrellSpectrum."
