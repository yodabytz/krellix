param([string]$Build='C:\krellix-work\build',
      [Parameter(Mandatory=$true)][string]$HostDirectory,
      [Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference='Stop'
$source=[IO.Path]::GetFullPath("$PSScriptRoot\..\..")
if(Test-Path $Destination){throw 'Use a new empty destination.'}
$hostHash=(Get-FileHash "$HostDirectory\krellix.exe").Hash
if($hostHash -ne (Get-FileHash "$Build\sensors-candidate\krellix.exe").Hash){throw 'Host differs from tested build.'}
$folder=Join-Path $Destination 'krellwifi'
New-Item -ItemType Directory $folder -Force | Out-Null
Copy-Item "$Build\plugins\krellwifi.dll","$Build\wifi\krellwifi-probe.exe","$source\LICENSE","$source\plugins\krellwifi\windows\README.md","$PSScriptRoot\install-krellwifi.ps1" $folder
@{
    host_version='0.1.2-windows-preview.2'
    plugin_version='0.1.3-windows.1'
    host_sha256=$hostHash.ToLowerInvariant()
    plugin_sha256=(Get-FileHash "$folder\krellwifi.dll").Hash.ToLowerInvariant()
    probe_sha256=(Get-FileHash "$folder\krellwifi-probe.exe").Hash.ToLowerInvariant()
} | ConvertTo-Json | Set-Content "$folder\manifest.json" -Encoding ASCII
$zip=Join-Path $Destination 'krellix-krellwifi-0.1.3-windows.1-x86_64.zip'
Compress-Archive -Path $folder -DestinationPath $zip -CompressionLevel Optimal
Get-FileHash $zip
