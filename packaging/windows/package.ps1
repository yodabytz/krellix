param([string]$Build = 'C:\krellix-work\build',
      [string]$QtBin = 'C:\msys64\ucrt64\bin',
      [string]$Destination = 'C:\krellix-work\release\Krellix',
      [Parameter(Mandatory=$true)][string]$UpstreamSourceArchive,
      [Parameter(Mandatory=$true)][string]$UpstreamSourceDirectory)
$ErrorActionPreference = 'Stop'
$env:PATH = "$QtBin;" + $env:PATH
if (Test-Path $Destination) { throw 'Use a new empty destination to avoid stale release files.' }
New-Item -ItemType Directory $Destination -Force | Out-Null
$source = [IO.Path]::GetFullPath("$PSScriptRoot\..\..")
Copy-Item "$Build\sensors-candidate\krellix.exe" $Destination
Copy-Item "$source\themes" $Destination -Recurse
& "$QtBin\windeployqt6.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw --dir $Destination "$Destination\krellix.exe"
if ($LASTEXITCODE) { throw 'Qt deployment failed' }
& "$QtBin\cmake.exe" "-DBUNDLE=$($Destination.Replace('\','/'))" "-DQT_BIN=$($QtBin.Replace('\','/'))" '-DCMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM=windows+pe' '-DCMAKE_GET_RUNTIME_DEPENDENCIES_TOOL=objdump' "-DCMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND=$($QtBin.Replace('\','/'))/objdump.exe" -P "$PSScriptRoot\bundle.cmake"
if ($LASTEXITCODE) { throw 'Dependency scan failed' }
New-Item -ItemType Directory "$Destination\sensors" | Out-Null
$pending = [Collections.Generic.Queue[string]]::new()
$seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$pending.Enqueue("$Build\sensors\krellix-sensors.exe")
while ($pending.Count) {
    $file = $pending.Dequeue()
    if (-not $seen.Add($file)) { continue }
    Copy-Item $file "$Destination\sensors"
    $assembly = [Reflection.Assembly]::ReflectionOnlyLoadFrom($file)
    foreach ($reference in $assembly.GetReferencedAssemblies()) {
        $dependency = "$Build\sensors\$($reference.Name).dll"
        if (Test-Path $dependency) { $pending.Enqueue($dependency) }
    }
}
Copy-Item "$Build\sensors\krellix-sensors.exe.config" "$Destination\sensors"
New-Item -ItemType Directory "$Destination\licenses\LibreHardwareMonitor" -Force | Out-Null
Copy-Item "$QtBin\..\share\licenses\*" "$Destination\licenses" -Recurse -Force
Copy-Item "$source\LICENSE" "$Destination\licenses\Krellix-LICENSE"
Copy-Item "$UpstreamSourceDirectory\LICENSE" "$Destination\licenses\LibreHardwareMonitor"
Copy-Item "$UpstreamSourceDirectory\THIRD-PARTY-NOTICES.txt" "$Destination\licenses\LibreHardwareMonitor"
Copy-Item "$PSScriptRoot\licenses\*" "$Destination\licenses\LibreHardwareMonitor"
Copy-Item $UpstreamSourceArchive "$Destination\licenses\LibreHardwareMonitor\upstream-source-0.9.6.tar.gz"
Copy-Item "$PSScriptRoot\README.md" "$Destination\README-Windows.md"
$plugin = "$Destination\extras\krelltop"
New-Item -ItemType Directory $plugin -Force | Out-Null
Copy-Item "$Build\plugins\krelltop.dll" $plugin
Copy-Item "$PSScriptRoot\install-krelltop.ps1" $plugin
@{
    host_version = '0.1.2-windows-preview.2'
    plugin_version = '0.1.4-windows.2'
    host_sha256 = (Get-FileHash "$Destination\krellix.exe" -Algorithm SHA256).Hash.ToLowerInvariant()
    plugin_sha256 = (Get-FileHash "$plugin\krelltop.dll" -Algorithm SHA256).Hash.ToLowerInvariant()
} | ConvertTo-Json | Set-Content "$plugin\manifest.json" -Encoding ASCII
