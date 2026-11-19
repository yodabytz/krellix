param([Parameter(Mandatory=$true)][string]$KrellixDirectory)
$ErrorActionPreference='Stop'
$manifest=Get-Content "$PSScriptRoot\manifest.json" -Raw | ConvertFrom-Json
if((Get-FileHash (Join-Path $KrellixDirectory 'krellix.exe')).Hash -ne $manifest.host_sha256){throw 'Install the matching Windows Krellix preview before this plugin.'}
$items=@(@{Name='krellstack.dll';Hash=$manifest.plugin_sha256},@{Name='krellstack-probe.exe';Hash=$manifest.probe_sha256})
foreach($item in $items){
 if((Get-FileHash (Join-Path $PSScriptRoot $item.Name)).Hash -ne $item.Hash){throw "Checksum mismatch: $($item.Name)"}
}
if(Get-Process krellix -ErrorAction SilentlyContinue){throw 'Close Krellix before installing.'}
$plugins=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'krellix\plugins'
$installedManifest=Join-Path $plugins 'krellstack-manifest.json'
$installed=$null
function ReleaseVersion([string]$value){
 if($value -notmatch '^(\d+)\.(\d+)\.(\d+)-windows\.(\d+)$'){throw 'Unrecognized KrellStack version; refusing replacement.'}
 return [version](($matches[1],$matches[2],$matches[3],$matches[4]) -join '.')
}
if(Test-Path $installedManifest){$installed=Get-Content $installedManifest -Raw | ConvertFrom-Json}
$incomingVersion=ReleaseVersion $manifest.plugin_version
if($installed -and (ReleaseVersion $installed.plugin_version) -gt $incomingVersion){throw 'KrellStack downgrade refused.'}
foreach($item in $items){
 $destination=if($item.Name -eq 'krellstack.dll'){Join-Path $plugins $item.Name}else{Join-Path $KrellixDirectory $item.Name}
 if((Test-Path $destination) -and (Get-FileHash $destination).Hash -ne $item.Hash){
  if(!$installed -or (ReleaseVersion $installed.plugin_version) -ge (ReleaseVersion $manifest.plugin_version)){throw 'Same-version replacement or downgrade refused.'}
  $oldHash=if($item.Name -eq 'krellstack.dll'){$installed.plugin_sha256}else{$installed.probe_sha256}
  if((Get-FileHash $destination).Hash -ne $oldHash){throw 'Existing KrellStack file differs from its manifest; refusing replacement.'}
 }
}
New-Item -ItemType Directory $plugins -Force | Out-Null
Copy-Item "$PSScriptRoot\krellstack-probe.exe" $KrellixDirectory -Force
Copy-Item "$PSScriptRoot\krellstack.dll" "$plugins\krellstack.dll" -Force
Copy-Item "$PSScriptRoot\manifest.json" "$plugins\krellstack-manifest.json" -Force
Write-Output "Installed KrellStack $($manifest.plugin_version). Enable it in Settings / Plugins."
