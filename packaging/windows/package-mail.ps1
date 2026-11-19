param([string]$Build = 'C:\krellix-work\build',
      [Parameter(Mandatory = $true)][string]$HostDirectory,
      [Parameter(Mandatory = $true)][string]$HostVersion,
      [Parameter(Mandatory = $true)][string]$Destination)
$ErrorActionPreference = 'Stop'
$source = [IO.Path]::GetFullPath("$PSScriptRoot\..\..")
if (Test-Path $Destination) { throw 'Use a fresh output directory.' }
$hostHash = (Get-FileHash "$HostDirectory\krellix.exe" -Algorithm SHA256).Hash
if ($hostHash -ne (Get-FileHash "$Build\sensors-candidate\krellix.exe" -Algorithm SHA256).Hash) {
    throw 'The host is not the one built with this plugin.'
}
$version = '0.1.0-windows.1'
$folder = Join-Path $Destination 'krellmail'
New-Item -ItemType Directory $folder -Force | Out-Null
Copy-Item "$Build\plugins\krellmail.dll", "$source\LICENSE", "$source\plugins\krellmail\windows\README.md", "$PSScriptRoot\install-krellmail.ps1" $folder
@{
    host_version = $HostVersion
    plugin_version = $version
    host_sha256 = $hostHash.ToLowerInvariant()
    plugin_sha256 = (Get-FileHash "$folder\krellmail.dll" -Algorithm SHA256).Hash.ToLowerInvariant()
} | ConvertTo-Json | Set-Content "$folder\manifest.json" -Encoding ASCII
$zip = Join-Path $Destination "krellix-krellmail-$version-x86_64.zip"
Compress-Archive -Path $folder -DestinationPath $zip -CompressionLevel Optimal
Get-FileHash $zip -Algorithm SHA256
