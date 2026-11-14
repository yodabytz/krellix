param([Parameter(Mandatory=$true)][string]$Archive,
      [Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference = 'Stop'
$expected = '086d9f1b5a99e643edc2cfaaac16051685b551e4c5ac0b32a57c58c0e529c001'
if ((Get-FileHash $Archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw 'Libre Hardware Monitor 0.9.6 archive checksum mismatch'
}
$Output = [IO.Path]::GetFullPath($Output)
New-Item -ItemType Directory -Path $Output -Force | Out-Null
$expanded = Join-Path $Output 'upstream'
Expand-Archive -LiteralPath $Archive -DestinationPath $expanded -Force
Copy-Item "$expanded\*.dll" $Output -Force
Copy-Item "$expanded\LibreHardwareMonitor.exe.config" "$Output\krellix-sensors.exe.config" -Force
$compiler = "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
& $compiler /nologo /target:exe /platform:x64 /optimize+ "/out:$Output\krellix-sensors.exe" "/reference:$Output\LibreHardwareMonitorLib.dll" /reference:System.Management.dll /reference:System.Web.Extensions.dll "$PSScriptRoot\SensorHelper.cs"
if ($LASTEXITCODE) { throw 'Sensor helper compilation failed' }
