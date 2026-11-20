param([Parameter(Mandatory = $true)][string]$KrellixDirectory)
$ErrorActionPreference = 'Stop'
$manifestFile = Join-Path $PSScriptRoot 'manifest.json'
$manifest = Get-Content -LiteralPath $manifestFile -Raw | ConvertFrom-Json
$name = $manifest.plugin
if ($name -notin 'krellkam','krellmoon' -or $manifest.plugin_version -ne '0.1.0-windows.1') {
    throw 'Unexpected KrellKam or KrellMoon manifest.'
}
$hostFile = Join-Path $KrellixDirectory 'krellix.exe'
if ((Get-FileHash -LiteralPath $hostFile -Algorithm SHA256).Hash -ne $manifest.host_sha256) {
    throw 'Use the published Krellix Windows preview.6.'
}
$dll = Join-Path $PSScriptRoot "$name.dll"
if ((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash -ne $manifest.plugin_sha256) {
    throw "$name DLL checksum mismatch."
}
$plugins = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'krellix\plugins'
$target = Join-Path $plugins "$name.dll"
$targetManifest = Join-Path $plugins "$name-manifest.json"
if (Test-Path -LiteralPath $target) {
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -eq $manifest.plugin_sha256) {
        "$name $($manifest.plugin_version) is already installed."
        return
    }
    throw "A different $name DLL is installed. Refusing to overwrite it."
}
if (Test-Path -LiteralPath $targetManifest) { throw "An existing $name manifest needs review." }
New-Item -ItemType Directory -Path $plugins -Force | Out-Null
Copy-Item -LiteralPath $dll -Destination $target
Copy-Item -LiteralPath $manifestFile -Destination $targetManifest
"Installed $name $($manifest.plugin_version). Enable it under Settings > Plugins."
