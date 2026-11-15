param([Parameter(Mandatory=$true)][string]$KrellixDirectory)
$ErrorActionPreference='Stop'
$manifest=Get-Content "$PSScriptRoot\manifest.json" -Raw | ConvertFrom-Json
if((Get-FileHash (Join-Path $KrellixDirectory 'krellix.exe')).Hash -ne $manifest.host_sha256){throw 'Install the matching Windows Krellix preview before this plugin.'}
$items=@(@{Name='krellpkg.dll';Hash=$manifest.plugin_sha256},@{Name='krellpkg-probe.exe';Hash=$manifest.probe_sha256},@{Name='Microsoft.Management.Deployment.winmd';Hash=$manifest.metadata_sha256})
foreach($item in $items){
 if((Get-FileHash (Join-Path $PSScriptRoot $item.Name)).Hash -ne $item.Hash){throw "Checksum mismatch: $($item.Name)"}
}
if(Get-Process krellix -ErrorAction SilentlyContinue){throw 'Close Krellix before installing.'}
$plugins=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'krellix\plugins'
$probe=Join-Path $KrellixDirectory 'packages'
foreach($item in $items){
 $destination=if($item.Name -eq 'krellpkg.dll'){Join-Path $plugins $item.Name}else{Join-Path $probe $item.Name}
 if((Test-Path $destination) -and (Get-FileHash $destination).Hash -ne $item.Hash){throw 'A different KrellPkg build exists; inspect its version before replacing. Downgrades are refused.'}
}
New-Item -ItemType Directory $plugins,$probe -Force | Out-Null
Copy-Item "$PSScriptRoot\krellpkg-probe.exe","$PSScriptRoot\Microsoft.Management.Deployment.winmd" $probe -Force
Copy-Item "$PSScriptRoot\krellpkg.dll" "$plugins\krellpkg.dll" -Force
Copy-Item "$PSScriptRoot\manifest.json" "$plugins\krellpkg-manifest.json" -Force
Write-Output "Installed KrellPkg $($manifest.plugin_version). Enable it in Settings / Plugins."
