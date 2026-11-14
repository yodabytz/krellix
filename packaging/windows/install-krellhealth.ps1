param([Parameter(Mandatory=$true)][string]$KrellixDirectory)
$ErrorActionPreference = 'Stop'
$manifest = Get-Content "$PSScriptRoot\manifest.json" -Raw | ConvertFrom-Json
$hostFile = Join-Path $KrellixDirectory 'krellix.exe'
if ((Get-FileHash $hostFile).Hash -ne $manifest.host_sha256) { throw 'Install the matching Krellix Windows preview before this plugin.' }
foreach ($entry in @(@{Name='krellhealth.dll';Hash=$manifest.plugin_sha256},@{Name='krellhealth-probe.exe';Hash=$manifest.probe_sha256})) {
    if ((Get-FileHash (Join-Path $PSScriptRoot $entry.Name)).Hash -ne $entry.Hash) { throw "Checksum mismatch: $($entry.Name)" }
}
if (Get-Process krellix -ErrorAction SilentlyContinue) { throw 'Close Krellix before installing.' }
$plugins = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'krellix\plugins'
$probe = Join-Path $KrellixDirectory 'health'
$installed = $null
$savedManifest = Join-Path $plugins 'krellhealth-manifest.json'
if (Test-Path $savedManifest) { $installed = Get-Content $savedManifest -Raw | ConvertFrom-Json }
if ($installed) {
    $format = '^\d+\.\d+\.\d+-windows\.\d+$'
    if ($installed.plugin_version -notmatch $format -or $manifest.plugin_version -notmatch $format) { throw 'Unknown installed version; refusing replacement.' }
    $oldVersion = [version]($installed.plugin_version -replace '-windows\.', '.')
    $newVersion = [version]($manifest.plugin_version -replace '-windows\.', '.')
    if ($newVersion -lt $oldVersion) { throw 'Downgrade refused.' }
}
foreach ($entry in @(@{Path="$plugins\krellhealth.dll";Hash=$manifest.plugin_sha256},@{Path="$probe\krellhealth-probe.exe";Hash=$manifest.probe_sha256})) {
    if ((Test-Path $entry.Path) -and (Get-FileHash $entry.Path).Hash -ne $entry.Hash) {
        if (-not $installed) { throw 'Unknown installed version; refusing replacement.' }
        if ($newVersion -le $oldVersion) { throw 'Downgrade or same-version replacement refused.' }
        $expected = if ($entry.Path.EndsWith('.dll')) { $installed.plugin_sha256 } else { $installed.probe_sha256 }
        if ((Get-FileHash $entry.Path).Hash -ne $expected) { throw 'Installed file does not match its manifest; inspect before replacing.' }
    }
}
if ($installed -and $installed.plugin_version -ne $manifest.plugin_version) {
    $backup = Join-Path $plugins "backups\krellhealth-$($installed.plugin_version)"
    New-Item -ItemType Directory $backup -Force | Out-Null
    Copy-Item "$plugins\krellhealth.dll",$savedManifest,"$probe\krellhealth-probe.exe" $backup
}
New-Item -ItemType Directory $plugins,$probe -Force | Out-Null
Copy-Item "$PSScriptRoot\krellhealth-probe.exe" "$probe\krellhealth-probe.exe" -Force
Copy-Item "$PSScriptRoot\krellhealth.dll" "$plugins\krellhealth.dll" -Force
Copy-Item "$PSScriptRoot\manifest.json" "$plugins\krellhealth-manifest.json" -Force
Write-Output "Installed KrellHealth $($manifest.plugin_version). Enable it in Settings / Plugins for each instance."
