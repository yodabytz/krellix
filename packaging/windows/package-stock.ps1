param([Parameter(Mandatory = $true)][string]$Build,
      [Parameter(Mandatory = $true)][string]$HostDirectory,
      [Parameter(Mandatory = $true)][string]$Destination)
$ErrorActionPreference = 'Stop'
$source = [IO.Path]::GetFullPath("$PSScriptRoot\..\..")
if (Test-Path -LiteralPath $Destination) { throw 'Use a fresh output directory.' }
$hostHash = (Get-FileHash -LiteralPath "$HostDirectory\krellix.exe" -Algorithm SHA256).Hash
$preview5 = 'cc71488cd5dbc951236f778cf2de71de732dffb86ffc1e548b70ed67f52ae860'
if ($hostHash -ne $preview5) { throw 'Use the published Windows preview.5 host.' }
$version = '0.1.2-windows.1'
$folder = Join-Path $Destination 'krellstock'
New-Item -ItemType Directory -Path $folder -Force | Out-Null
Copy-Item -LiteralPath "$Build\plugins\krellstock.dll", "$source\LICENSE",
    "$source\plugins\krellstock\windows\README.md", "$PSScriptRoot\install-krellstock.ps1" -Destination $folder
@{
    host_version = '0.1.2-windows-preview.5'
    plugin_version = $version
    host_sha256 = $hostHash.ToLowerInvariant()
    plugin_sha256 = (Get-FileHash -LiteralPath "$folder\krellstock.dll" -Algorithm SHA256).Hash.ToLowerInvariant()
} | ConvertTo-Json | Set-Content -LiteralPath "$folder\manifest.json" -Encoding ASCII
$zip = Join-Path $Destination "krellix-krellstock-$version-x86_64.zip"
Compress-Archive -Path $folder -DestinationPath $zip -CompressionLevel Optimal
Get-FileHash -LiteralPath $zip -Algorithm SHA256
