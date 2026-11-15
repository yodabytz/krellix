param([Parameter(Mandatory=$true)][string]$KrellixDirectory)
$ErrorActionPreference='Stop'
$manifest=Get-Content "$PSScriptRoot\manifest.json" -Raw | ConvertFrom-Json
if((Get-FileHash (Join-Path $KrellixDirectory 'krellix.exe')).Hash -ne $manifest.host_sha256){throw 'Install the matching Windows Krellix preview before this plugin.'}
foreach($item in @(@{Name='krellbluetooth.dll';Hash=$manifest.plugin_sha256},@{Name='krellbt-probe.exe';Hash=$manifest.probe_sha256})){
 if((Get-FileHash (Join-Path $PSScriptRoot $item.Name)).Hash -ne $item.Hash){throw "Checksum mismatch: $($item.Name)"}
}
if(Get-Process krellix -ErrorAction SilentlyContinue){throw 'Close Krellix before installing.'}
$plugins=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'krellix\plugins'
$probe=Join-Path $KrellixDirectory 'bluetooth'
foreach($item in @(@{Path="$plugins\krellbluetooth.dll";Hash=$manifest.plugin_sha256},@{Path="$probe\krellbt-probe.exe";Hash=$manifest.probe_sha256})){
 if((Test-Path $item.Path) -and (Get-FileHash $item.Path).Hash -ne $item.Hash){throw 'A different Bluetooth build exists; inspect its version before replacing. Downgrades are refused.'}
}
New-Item -ItemType Directory $plugins,$probe -Force | Out-Null
Copy-Item "$PSScriptRoot\krellbt-probe.exe" "$probe\krellbt-probe.exe" -Force
Copy-Item "$PSScriptRoot\krellbluetooth.dll" "$plugins\krellbluetooth.dll" -Force
Copy-Item "$PSScriptRoot\manifest.json" "$plugins\krellbluetooth-manifest.json" -Force
Write-Output "Installed KrellBluetooth $($manifest.plugin_version). Enable it in the local instance's Settings / Plugins."
