param([Parameter(Mandatory = $true)][string]$PreviousZip,
      [Parameter(Mandatory = $true)][string]$CandidateExe,
      [Parameter(Mandatory = $true)][string]$Destination)
$ErrorActionPreference = 'Stop'
$previousHash = '322d01221005cb6d728ab36584fecc9ce2c2906e78958587994095065822c679'
if ((Get-FileHash $PreviousZip -Algorithm SHA256).Hash -ne $previousHash) {
    throw 'Use the published Windows preview.2 archive as the runtime baseline.'
}
if (Test-Path $Destination) { throw 'Use a fresh output directory.' }
$version = '0.1.2-windows-preview.3'
$stage = Join-Path $Destination 'stage'
Expand-Archive -LiteralPath $PreviousZip -DestinationPath $stage
$hostDirectory = Join-Path $stage 'Krellix'
if (-not (Test-Path "$hostDirectory\krellix.exe")) { throw 'Missing portable host in baseline package.' }
Copy-Item $CandidateExe "$hostDirectory\krellix.exe" -Force
$manifestPath = Join-Path $hostDirectory 'extras\krelltop\manifest.json'
$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
$manifest.host_version = $version
$manifest.host_sha256 = (Get-FileHash "$hostDirectory\krellix.exe" -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest | ConvertTo-Json | Set-Content $manifestPath -Encoding ASCII
$zip = Join-Path $Destination "krellix-$version-x86_64.zip"
Compress-Archive -Path $hostDirectory -DestinationPath $zip -CompressionLevel Optimal
Get-FileHash $zip -Algorithm SHA256
