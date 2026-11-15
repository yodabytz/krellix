param([Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
$framework="$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319"
$package=Get-AppxPackage Microsoft.DesktopAppInstaller | Select-Object -First 1
if (!$package) { throw 'WinGet App Installer is required to build this helper.' }
$directory=Split-Path $Output
New-Item -ItemType Directory $directory -Force | Out-Null
$metadata=Join-Path $directory 'Microsoft.Management.Deployment.winmd'
Copy-Item "$($package.InstallLocation)\Microsoft.Management.Deployment.winmd" $metadata -Force
$references=@($metadata,"$framework\System.Web.Extensions.dll")
foreach($name in @('System.Runtime','System.Runtime.InteropServices.WindowsRuntime','System.ObjectModel')) {
 $references+=@(Get-ChildItem "$env:WINDIR\Microsoft.NET\assembly\GAC_MSIL\$name" -Recurse -Filter "$name.dll" | Select-Object -First 1 -ExpandProperty FullName)
}
$references+=@(Get-ChildItem "$env:WINDIR\System32\WinMetadata\*.winmd" | ForEach-Object FullName)
$arguments=@('/nologo','/target:exe','/platform:x64','/optimize+',"/out:$Output")
$arguments+=@($references | ForEach-Object {"/reference:$_"})
& "$framework\csc.exe" @arguments "$PSScriptRoot\Probe.cs"
if($LASTEXITCODE){throw 'Package helper compilation failed'}
