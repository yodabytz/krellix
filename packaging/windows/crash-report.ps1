[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [ValidateSet('Report', 'Enable', 'Disable')]
    [string]$Action = 'Report',
    [ValidateRange(1, 30)]
    [int]$Days = 7
)

$ErrorActionPreference = 'Stop'
$baseKey = 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps'
$key = "$baseKey\krellix.exe"
$dumpFolder = '%LOCALAPPDATA%\krellix\crash-dumps'
$capture = Get-Item -LiteralPath $key -ErrorAction SilentlyContinue
$owned = $capture -and $capture.ValueCount -eq 4 -and $capture.SubKeyCount -eq 0 -and
    $capture.GetValue('KrellixCrashReportSetup', 0) -eq 1 -and
    $capture.GetValue('DumpFolder', '', 'DoNotExpandEnvironmentNames') -eq $dumpFolder -and
    $capture.GetValue('DumpType', 0) -eq 1 -and $capture.GetValue('DumpCount', 0) -eq 5

if ($Action -ne 'Report') {
    if (-not [Environment]::Is64BitProcess) {
        throw 'Run the 64-bit Windows PowerShell.'
    }
    if ($capture -and -not $owned) {
        throw 'Krellix already has externally managed dump settings; leaving them unchanged.'
    }
    if ($Action -eq 'Enable' -and -not $capture -and
        $PSCmdlet.ShouldProcess('krellix.exe', 'Enable Windows Error Reporting local minidumps')) {
        New-Item -Path $key -Force | Out-Null
        New-ItemProperty -LiteralPath $key -Name DumpFolder -Value $dumpFolder -PropertyType ExpandString | Out-Null
        New-ItemProperty -LiteralPath $key -Name DumpType -Value 1 -PropertyType DWord | Out-Null
        New-ItemProperty -LiteralPath $key -Name DumpCount -Value 5 -PropertyType DWord | Out-Null
        New-ItemProperty -LiteralPath $key -Name KrellixCrashReportSetup -Value 1 -PropertyType DWord | Out-Null
    } elseif ($Action -eq 'Disable' -and $owned -and
              $PSCmdlet.ShouldProcess('krellix.exe', 'Disable minidump settings created by this script')) {
        Remove-Item -LiteralPath $key
    }
    return
}

$configuredFolder = if ($capture) { $capture.GetValue('DumpFolder') }
if (-not $configuredFolder) {
    $globalCapture = Get-Item -LiteralPath $baseKey -ErrorAction SilentlyContinue
    $configuredFolder = if ($globalCapture) { $globalCapture.GetValue('DumpFolder') }
}
if (-not $configuredFolder) { $configuredFolder = "$env:LOCALAPPDATA\CrashDumps" }
$dumps = @()
# Do not access network shares or include the dump directory in the report.
if ($configuredFolder -is [string] -and $configuredFolder -match '^[A-Za-z]:\\') {
    $dumps = @(Get-ChildItem -LiteralPath $configuredFolder -Filter 'krellix.exe.*.dmp' `
        -File -ErrorAction SilentlyContinue)
}
$since = (Get-Date).AddDays(-$Days)
$filter = @{
    LogName = 'Application'
    ProviderName = 'Application Error'
    Id = 1000
    StartTime = $since
}
$rows = [System.Collections.Generic.List[object]]::new()
try {
    Get-WinEvent -FilterHashtable $filter | ForEach-Object {
        $event = $_
        $data = @{}
        foreach ($field in ([xml]$event.ToXml()).Event.EventData.Data) {
            $data[[string]$field.Name] = [string]$field.'#text'
        }
        if ($data.AppName -ieq 'krellix.exe') {
            $eventPid = [uint32]0
            $dumpName = $null
            if ([uint32]::TryParse(($data.ProcessId -replace '^0x', ''), 'HexNumber',
                    [Globalization.CultureInfo]::InvariantCulture, [ref]$eventPid)) {
                $dump = $dumps | Where-Object {
                    $_.Name -eq "krellix.exe.$eventPid.dmp" -and
                    [Math]::Abs(($_.LastWriteTimeUtc - $event.TimeCreated.ToUniversalTime()).TotalMinutes) -lt 5
                } | Select-Object -First 1
                if ($dump) { $dumpName = $dump.Name }
            }
            $rows.Add([pscustomobject][ordered]@{
                timeUtc = $event.TimeCreated.ToUniversalTime().ToString('o')
                eventRecordId = $event.RecordId
                reportId = $data.IntegratorReportId
                processId = $eventPid
                processStartFileTime = $data.ProcessCreationTime
                executableVersion = $data.AppVersion
                executableStamp = $data.AppTimeStamp
                module = $data.ModuleName
                moduleVersion = $data.ModuleVersion
                moduleStamp = $data.ModuleTimeStamp
                exceptionCode = $data.ExceptionCode
                moduleOffset = $data.FaultingOffset
                localMinidump = $dumpName
            })
        }
    }
} catch {
    if ($_.FullyQualifiedErrorId -notlike 'NoMatchingEventsFound,*') { throw }
}

[pscustomobject][ordered]@{
    schema = 1
    sinceUtc = $since.ToUniversalTime().ToString('o')
    appDumpOverridePresent = [bool]$capture
    krellixMinidumpSetup = [bool]$owned
    crashes = $rows.ToArray()
} | ConvertTo-Json -Depth 4
