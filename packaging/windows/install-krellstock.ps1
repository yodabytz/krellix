param([Parameter(Mandatory = $true)][string]$KrellixDirectory)
$ErrorActionPreference = 'Stop'
$manifest = Get-Content "$PSScriptRoot\manifest.json" -Raw | ConvertFrom-Json
$hostFile = Join-Path $KrellixDirectory 'krellix.exe'
if ((Get-FileHash -LiteralPath $hostFile -Algorithm SHA256).Hash -ne $manifest.host_sha256) {
    throw 'KrellStock requires the matching Windows Krellix preview.5 host.'
}
$dll = Join-Path $PSScriptRoot 'krellstock.dll'
if ((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash -ne $manifest.plugin_sha256) {
    throw 'KrellStock DLL checksum mismatch.'
}
$plugins = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'krellix\plugins'
$target = Join-Path $plugins 'krellstock.dll'
if (Test-Path -LiteralPath $target) {
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $manifest.plugin_sha256) {
        throw 'A different KrellStock build exists. Refusing to overwrite or downgrade it.'
    }
    'This KrellStock build is already installed.'
    return
}
New-Item -ItemType Directory -Path $plugins -Force | Out-Null
Copy-Item -LiteralPath $dll -Destination $target
Copy-Item -LiteralPath "$PSScriptRoot\manifest.json" -Destination (Join-Path $plugins 'krellstock-manifest.json')
"Installed KrellStock $($manifest.plugin_version). Reopen local Krellix and click KrellStock settings."
