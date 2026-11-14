param([Parameter(Mandatory=$true)][string]$KrellixDirectory)
$ErrorActionPreference='Stop'
$manifest=Get-Content "$PSScriptRoot\manifest.json" -Raw | ConvertFrom-Json
if((Get-FileHash (Join-Path $KrellixDirectory 'krellix.exe')).Hash -ne $manifest.host_sha256){throw 'Install the matching Windows Krellix preview before this plugin.'}
foreach($item in @(@{Name='krellwifi.dll';Hash=$manifest.plugin_sha256},@{Name='krellwifi-probe.exe';Hash=$manifest.probe_sha256})){
    if((Get-FileHash (Join-Path $PSScriptRoot $item.Name)).Hash -ne $item.Hash){throw "Checksum mismatch: $($item.Name)"}
}
if(Get-Process krellix -ErrorAction SilentlyContinue){throw 'Close Krellix before installing.'}
$plugins=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'krellix\plugins'
$probe=Join-Path $KrellixDirectory 'wifi'
foreach($item in @(@{Path="$plugins\krellwifi.dll";Hash=$manifest.plugin_sha256},@{Path="$probe\krellwifi-probe.exe";Hash=$manifest.probe_sha256})){
    if((Test-Path $item.Path) -and (Get-FileHash $item.Path).Hash -ne $item.Hash){throw 'A different Wi-Fi build exists; inspect its version before replacing. Downgrades are refused.'}
}
New-Item -ItemType Directory $plugins,$probe -Force | Out-Null
Copy-Item "$PSScriptRoot\krellwifi-probe.exe" "$probe\krellwifi-probe.exe" -Force
Copy-Item "$PSScriptRoot\krellwifi.dll" "$plugins\krellwifi.dll" -Force
Copy-Item "$PSScriptRoot\manifest.json" "$plugins\krellwifi-manifest.json" -Force
Write-Output "Installed KrellWiFi $($manifest.plugin_version). Enable it in the local instance's Settings / Plugins."
