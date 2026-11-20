param([Parameter(Mandatory = $true)][string]$Build,
      [Parameter(Mandatory = $true)][string]$HostDirectory,
      [Parameter(Mandatory = $true)][string]$Destination)
$ErrorActionPreference = 'Stop'
$source = [IO.Path]::GetFullPath("$PSScriptRoot\..\..")
if (Test-Path -LiteralPath $Destination) { throw 'Use a fresh output directory.' }
$hostHash = (Get-FileHash -LiteralPath "$HostDirectory\krellix.exe" -Algorithm SHA256).Hash
$preview6 = '432fc75dfb4879f82395603697b0c05cc6aa8cdba960acc4df0f90d14adf480f'
if ($hostHash -ne $preview6) { throw 'Use the published Windows preview.6 host.' }
$version = '0.1.0-windows.1'
$folder = Join-Path $Destination 'krellspectrum'
New-Item -ItemType Directory -Path $folder -Force | Out-Null
Copy-Item -LiteralPath "$Build\plugins\krellspectrum.dll", "$Build\plugins\krellspectrum-wasapi.exe", "$source\LICENSE",
    "$source\plugins\krellspectrum\windows\README.md", "$PSScriptRoot\install-krellspectrum.ps1" -Destination $folder
@{
    host_version = '0.1.2-windows-preview.6'
    plugin_version = $version
    host_sha256 = $hostHash.ToLowerInvariant()
    plugin_sha256 = (Get-FileHash -LiteralPath "$folder\krellspectrum.dll" -Algorithm SHA256).Hash.ToLowerInvariant()
    helper_sha256 = (Get-FileHash -LiteralPath "$folder\krellspectrum-wasapi.exe" -Algorithm SHA256).Hash.ToLowerInvariant()
} | ConvertTo-Json | Set-Content -LiteralPath "$folder\manifest.json" -Encoding ASCII
$zip = Join-Path $Destination "krellix-krellspectrum-$version-x86_64.zip"
Compress-Archive -Path $folder -DestinationPath $zip -CompressionLevel Optimal
Get-FileHash -LiteralPath $zip -Algorithm SHA256
