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
        'a723a65f5843e84cb8b0a640577bd27b1b56afa415ca30fe7076380b47cb354d'
        '504da22bc2341e1de2c799421534692943e44d6e16bf331b401d4abb75cfc018'
        '5a1bd603e900ba9d4c072e37482c8f5962cbfaa5d2e7f91e800f7edff38e4205'
        '065cb8cc96ace6425b912bb5c00e48fbb53589b480bd3ac5023fb5e6341df30d'
        '57670311238914cc8edc662c5c1887cdf0e0c2ffffc5cd47c1038307833360c4'
        '83f600dbfa052d4c43c5bc44b5120cfa16c8c78012d8a6979d5451d9e5bf40b9'
        '501a090d07459fab3015d081c19e445183dcd2721e374013fd9b6c3fdd402f9c'
        'bc7bff89ec061955d79e6c30c26c197c89229ef693b26ca2eaa58f591f29d767'
        '9a6592ed29f9281d1e72d706113bf7f63fddc59d8d1cc3b75c0912c7043110c5'
    )
    if ($installedHash -notin $knownPrevious -or $manifest.plugin_version -ne '0.1.7-windows.4') {
        throw 'A different KrellMail build already exists. Refusing to overwrite or downgrade it.'
    }
    $replace = $true
}
New-Item -ItemType Directory $plugins -Force | Out-Null
if ($replace) {
    $temporary = Join-Path $plugins ([Guid]::NewGuid().ToString('N') + '.tmp')
    try {
        Copy-Item $dll $temporary
        [IO.File]::Replace($temporary, $target, "$target.before-0.1.7-windows.4")
    } finally {
        if (Test-Path $temporary) { Remove-Item -LiteralPath $temporary }
    }
} else {
    Copy-Item $dll $target
}
Copy-Item "$PSScriptRoot\manifest.json" (Join-Path $plugins 'krellmail-manifest.json')
Write-Output "Installed KrellMail $($manifest.plugin_version). Reopen the local instance and enable it in Settings / Plugins."
