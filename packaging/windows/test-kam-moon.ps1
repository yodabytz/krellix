param([Parameter(Mandatory = $true)][string]$PackageDirectory,
      [Parameter(Mandatory = $true)][string]$KrellixDirectory,
      [Parameter(Mandatory = $true)][string]$WorkDirectory,
      [string]$LaunchDirectory = $KrellixDirectory)
$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath $WorkDirectory) { throw 'Use a fresh test directory.' }
$work = (New-Item -ItemType Directory -Path $WorkDirectory).FullName
$stage = (New-Item -ItemType Directory -Path "$work\plugins").FullName
$hostExe = Join-Path $LaunchDirectory 'krellix.exe'
$hostHash = (Get-FileHash -LiteralPath "$KrellixDirectory\krellix.exe").Hash
foreach ($name in 'krellkam','krellmoon') {
    $installed = Join-Path $env:LOCALAPPDATA "krellix\plugins\$name.dll"
    if (Test-Path -LiteralPath $installed) { throw "An installed $name could shadow this test DLL." }
    Expand-Archive -LiteralPath "$PackageDirectory\krellix-$name-0.1.0-windows.1-x86_64.zip" -DestinationPath $work
    $manifest = Get-Content -LiteralPath "$work\$name\manifest.json" -Raw | ConvertFrom-Json
    $dll = "$work\$name\$name.dll"
    if ($hostHash -ne $manifest.host_sha256 -or (Get-FileHash -LiteralPath $dll).Hash -ne $manifest.plugin_sha256) {
        throw "$name package hash or host mismatch."
    }
    Copy-Item -LiteralPath $dll -Destination $stage
}

Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Drawing
$env:KRELLIX_DEV_PLUGINS = '1'
$env:QT_FORCE_STDERR_LOGGING = '1'
$env:QT_LOGGING_RULES = 'krellix.plugin.info=true'
$env:QT_QPA_PLATFORM = 'windows:fontengine=freetype'
$ae = [Windows.Automation.AutomationElement]
$root = $ae::RootElement
$children = [Windows.Automation.TreeScope]::Children
$desc = [Windows.Automation.TreeScope]::Descendants
$phase = '^(NEW MOON|FULL MOON|FIRST QUARTER|LAST QUARTER|(?:WAXING|WANING) (?:CRESCENT|GIBBOUS))$'
$image = "$work\Windows camera sample.png"
$bitmap = [System.Drawing.Bitmap]::new(120,80)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.Clear([System.Drawing.Color]::DeepPink)
$graphics.Dispose()
$bitmap.Save($image,[System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()
$source = ([uri]$image).AbsoluteUri
foreach ($remote in $false,$true) {
    $id = 'qa-kam-moon-' + [guid]::NewGuid().ToString('N').Substring(0,8)
    $profile = Join-Path $env:APPDATA "krellix\krellix-$id.ini"
    if (Test-Path -LiteralPath $profile) { throw 'Test profile collision.' }
    $log = "$work\$id.log"
    $p = $null
    try {
        @"
[migration]
base_defaults_imported_v2=true
legacy_conf_imported=true
[window]
pos=@Point(100 100)
[plugins]
krellkam\enabled=true
krellkam\title1=Windows image fixture
krellkam\type1=auto
krellkam\source1=$source
krellmoon\enabled=true
krellstock\quotes_enabled=false
krellstock\ticker_enabled=false
"@ | Set-Content -LiteralPath $profile -Encoding ASCII
        $arguments = "--instance $id --monitors clock"
        if ($remote) { $arguments += ' --host 127.0.0.1:19997' }
        $p = Start-Process -FilePath $hostExe -ArgumentList $arguments -WorkingDirectory $work -WindowStyle Normal `
            -RedirectStandardError $log -PassThru
        $null = $p.Handle
        $owner = [Windows.Automation.PropertyCondition]::new($ae::ProcessIdProperty,$p.Id)
        $names = @()
        $window = $null
        for ($n = 0; $n -lt 60; $n++) {
            if ($p.HasExited) { throw "Krellix exited early; see $log." }
            $p.Refresh()
            if ($p.MainWindowHandle -ne [IntPtr]::Zero) {
                $window = $ae::FromHandle($p.MainWindowHandle)
            } else {
                $window = $root.FindAll($children,$owner) | Where-Object { -not $_.Current.IsOffscreen } | Select-Object -First 1
            }
            if ($window) {
                $names = @($window.FindAll($desc,[Windows.Automation.Condition]::TrueCondition) |
                    ForEach-Object { $_.Current.Name })
                if ($remote -or ('KrellKam image: Windows image fixture' -in $names -and ($names -match $phase))) { break }
            }
            Start-Sleep -Milliseconds 300
        }
        if (-not $window) { throw "No desktop window found for PID $($p.Id) on session $($p.SessionId)." }
        foreach ($name in 'krellkam','krellmoon') {
            $loaded = $p.Modules | Where-Object ModuleName -eq "$name.dll"
            if (-not $loaded -or $loaded.FileName -ne "$stage\$name.dll") { throw "$name did not load from the extracted ZIP." }
        }
        if ($remote) {
            if (($names -match '^KrellKam image') -or ($names -match $phase)) { throw 'A remote panel displayed local data.' }
            'Remote isolation passed.'
        } else {
            if ('KrellKam image: Windows image fixture' -notin $names -or -not ($names -match $phase)) {
                throw "Expected an image and a moon phase; found $($names -join ', ')."
            }
            "File-URL image decoded; KrellMoon displayed $($names -match $phase)."
        }
        if (Select-String -LiteralPath $log -Pattern 'connect\(Theme, (KrellkamField|KrellmoonMonitor)\).*signal not found' -Quiet) {
            throw 'The host theme signal was not connected.'
        }
        $null = $p.CloseMainWindow()
        if (-not $p.WaitForExit(10000) -or $p.ExitCode -ne 0) { throw "Krellix did not exit cleanly; see $log." }
    } finally {
        if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -ErrorAction SilentlyContinue }
        Remove-Item -LiteralPath $profile -ErrorAction SilentlyContinue
    }
}
