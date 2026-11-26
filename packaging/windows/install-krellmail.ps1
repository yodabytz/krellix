param([Parameter(Mandatory = $true)][string]$KrellixDirectory)
$ErrorActionPreference = 'Stop'
$manifest = Get-Content "$PSScriptRoot\manifest.json" -Raw | ConvertFrom-Json
$hostFile = Join-Path $KrellixDirectory 'krellix.exe'
if ((Get-FileHash $hostFile -Algorithm SHA256).Hash -ne $manifest.host_sha256) {
    throw 'KrellMail requires the matching Windows Krellix host.'
}
$dll = Join-Path $PSScriptRoot 'krellmail.dll'
if ((Get-FileHash $dll -Algorithm SHA256).Hash -ne $manifest.plugin_sha256) {
    throw 'KrellMail DLL checksum mismatch.'
}
$plugins = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'krellix\plugins'
$target = Join-Path $plugins 'krellmail.dll'
$replace = $false
if (Test-Path $target) {
    $installedHash = (Get-FileHash $target -Algorithm SHA256).Hash
    if ($installedHash -eq $manifest.plugin_sha256) {
        Write-Output 'This KrellMail build is already installed.'
        return
    }
    # Only replace known previous public DLLs, never an unknown or newer build.
    $knownPrevious = @(
        '065cb8cc96ace6425b912bb5c00e48fbb53589b480bd3ac5023fb5e6341df30d'
        '57670311238914cc8edc662c5c1887cdf0e0c2ffffc5cd47c1038307833360c4'
        '83f600dbfa052d4c43c5bc44b5120cfa16c8c78012d8a6979d5451d9e5bf40b9'
    )
    if ($installedHash -notin $knownPrevious -or $manifest.plugin_version -ne '0.1.4-windows.1') {
        throw 'A different KrellMail build already exists. Refusing to overwrite or downgrade it.'
    }
    $replace = $true
}
New-Item -ItemType Directory $plugins -Force | Out-Null
if ($replace) {
    $temporary = Join-Path $plugins ([Guid]::NewGuid().ToString('N') + '.tmp')
    try {
        Copy-Item $dll $temporary
        [IO.File]::Replace($temporary, $target, "$target.before-0.1.4")
    } finally {
        if (Test-Path $temporary) { Remove-Item -LiteralPath $temporary }
    }
} else {
    Copy-Item $dll $target
}
Copy-Item "$PSScriptRoot\manifest.json" (Join-Path $plugins 'krellmail-manifest.json')
Write-Output "Installed KrellMail $($manifest.plugin_version). Reopen the local instance and enable it in Settings / Plugins."
