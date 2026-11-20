param([Parameter(Mandatory = $true)][string]$PreviousZip,
      [Parameter(Mandatory = $true)][string]$CandidateExe,
      [Parameter(Mandatory = $true)][string]$Destination,
      [string]$Objcopy = 'C:\msys64\ucrt64\bin\objcopy.exe')
$ErrorActionPreference = 'Stop'
$previousHash = 'fb940ec5e19bf5eb3afc8380e44c15f2d4219bbd76209e40feb37c7b92b9bf7c'
if ((Get-FileHash $PreviousZip -Algorithm SHA256).Hash -ne $previousHash) {
    throw 'Use the published preview.3 archive as the runtime baseline.'
}
if (Test-Path $Destination) { throw 'Use a fresh output directory.' }
$version = '0.1.2-windows-preview.4'
$stage = Join-Path $Destination 'stage'
Expand-Archive -LiteralPath $PreviousZip -DestinationPath $stage
$hostDirectory = Join-Path $stage 'Krellix'
$hostExe = Join-Path $hostDirectory 'krellix.exe'
if (-not (Test-Path $hostExe)) { throw 'Missing portable host in baseline package.' }
# Keep the matching host symbols outside the user-facing ZIP.
$symbols = Join-Path $Destination 'krellix.exe.debug'
& $Objcopy --only-keep-debug $CandidateExe $symbols
if ($LASTEXITCODE) { throw 'Could not preserve Windows host symbols.' }
& $Objcopy --strip-debug "--add-gnu-debuglink=$symbols" $CandidateExe $hostExe
if ($LASTEXITCODE) { throw 'Could not prepare Windows host binary.' }
Copy-Item "$PSScriptRoot\crash-report.ps1" $hostDirectory
Copy-Item "$PSScriptRoot\README.md" "$hostDirectory\README-Windows.md" -Force
$manifestPath = Join-Path $hostDirectory 'extras\krelltop\manifest.json'
$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
$manifest.host_version = $version
$manifest.host_sha256 = (Get-FileHash $hostExe -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest | ConvertTo-Json | Set-Content $manifestPath -Encoding ASCII
$zip = Join-Path $Destination "krellix-$version-x86_64.zip"
Compress-Archive -Path $hostDirectory -DestinationPath $zip -CompressionLevel Optimal
Get-FileHash $zip,$symbols -Algorithm SHA256
