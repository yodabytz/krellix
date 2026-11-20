param([Parameter(Mandatory = $true)][string]$KrellixDirectory)
$ErrorActionPreference = 'Stop'
$manifest = Get-Content "$PSScriptRoot\manifest.json" -Raw | ConvertFrom-Json
$hostFile = Join-Path $KrellixDirectory 'krellix.exe'
if ((Get-FileHash -LiteralPath $hostFile -Algorithm SHA256).Hash -ne $manifest.host_sha256) {
    throw 'Use the published Windows Krellix preview.6 directory for verification.'
}
$dll = Join-Path $PSScriptRoot 'krellwire.dll'
if ((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash -ne $manifest.plugin_sha256) {
    throw 'KrellWire DLL checksum mismatch.'
}
$plugins = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'krellix\plugins'
$target = Join-Path $plugins 'krellwire.dll'
$manifestPath = Join-Path $plugins 'krellwire-manifest.json'
if (Test-Path -LiteralPath $target) {
    $installedHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    if ($installedHash -eq $manifest.plugin_sha256) {
        'This KrellWire build is already installed.'
        return
    }
    if (-not (Test-Path -LiteralPath $manifestPath)) { throw 'An unknown KrellWire DLL exists.' }
    $installed = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $format = '^\d+\.\d+\.\d+-windows\.\d+$'
    if ($installedHash -ne $installed.plugin_sha256 -or
        $installed.plugin_version -notmatch $format -or $manifest.plugin_version -notmatch $format) {
        throw 'An unrecognized KrellWire build exists. Refusing to overwrite it.'
    }
    $old = [version]($installed.plugin_version -replace '-windows\.', '.')
    $new = [version]($manifest.plugin_version -replace '-windows\.', '.')
    if ($new -le $old) { throw 'Refusing to overwrite an equal or newer KrellWire build.' }
    $backup = Join-Path $plugins "backups\krellwire-$($installed.plugin_version)"
    if (-not (Test-Path -LiteralPath $backup)) {
        New-Item -ItemType Directory -Path $backup -Force | Out-Null
        Copy-Item -LiteralPath $target,$manifestPath -Destination $backup
    }
}
New-Item -ItemType Directory -Path $plugins -Force | Out-Null
Copy-Item -LiteralPath $dll -Destination $target
Copy-Item -LiteralPath "$PSScriptRoot\manifest.json" -Destination $manifestPath
"Installed KrellWire $($manifest.plugin_version). Enable it in Settings > Plugins > Krellwire."
