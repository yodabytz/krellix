param([string]$Build = 'C:\krellix-work\build',
      [Parameter(Mandatory=$true)][string]$HostDirectory,
      [Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference = 'Stop'
$source = [IO.Path]::GetFullPath("$PSScriptRoot\..\..")
$folder = Join-Path $Destination 'krellhealth'
if (Test-Path $Destination) { throw 'Use a new empty package destination.' }
$hostHash = (Get-FileHash "$HostDirectory\krellix.exe").Hash
if ($hostHash -ne (Get-FileHash "$Build\sensors-candidate\krellix.exe").Hash) { throw 'Host does not match the tested build.' }
New-Item -ItemType Directory $folder -Force | Out-Null
Copy-Item "$Build\plugins\krellhealth.dll" $folder
Copy-Item "$Build\health\krellhealth-probe.exe" $folder
Copy-Item "$PSScriptRoot\install-krellhealth.ps1" $folder
Copy-Item "$source\plugins\krellhealth\windows\README.md" $folder
Copy-Item "$source\LICENSE" $folder
@{
    host_version='0.1.2-windows-preview.2'
    plugin_version='0.1.1-windows.2'
    host_sha256=$hostHash.ToLowerInvariant()
    plugin_sha256=(Get-FileHash "$folder\krellhealth.dll").Hash.ToLowerInvariant()
    probe_sha256=(Get-FileHash "$folder\krellhealth-probe.exe").Hash.ToLowerInvariant()
} | ConvertTo-Json | Set-Content "$folder\manifest.json" -Encoding ASCII
$zip = Join-Path $Destination 'krellix-krellhealth-0.1.1-windows.2-x86_64.zip'
Compress-Archive -Path $folder -DestinationPath $zip -CompressionLevel Optimal
Get-FileHash $zip
