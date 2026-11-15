param([Parameter(Mandatory=$true)][string]$Output, [switch]$Test)
$ErrorActionPreference='Stop'
$framework="$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319"
$references=@("$framework\System.Web.Extensions.dll", "$framework\System.Windows.Forms.dll")
$facades=@('System.Runtime','System.Runtime.InteropServices.WindowsRuntime','System.ObjectModel')
foreach($name in $facades){
 $references+=@(Get-ChildItem "$env:WINDIR\Microsoft.NET\assembly\GAC_MSIL\$name" -Recurse -Filter "$name.dll" | Select-Object -First 1 -ExpandProperty FullName)
}
$references+=@(Get-ChildItem "$env:WINDIR\System32\WinMetadata\*.winmd" | ForEach-Object FullName)
$arguments=@('/nologo','/target:exe','/platform:x64','/optimize+',"/out:$Output")
$arguments+=@($references | ForEach-Object {"/reference:$_"})
if($Test){$arguments+=@('/main:ProbeTest',"$PSScriptRoot\ProbeTest.cs")}
& "$framework\csc.exe" @arguments "$PSScriptRoot\Probe.cs"
if($LASTEXITCODE){throw 'Bluetooth helper compilation failed'}
