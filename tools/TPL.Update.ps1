param([Parameter(Mandatory=$true)][string]$TplHomePath,[switch]$Force)
$ErrorActionPreference = 'Stop'
$TplHomePath = [IO.Path]::GetFullPath($TplHomePath)
function Atomic-Text([string]$Path,[string]$Text) {
    $temp = $Path + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
    [IO.File]::WriteAllText($temp,$Text,(New-Object Text.UTF8Encoding($false)))
    if (Test-Path -LiteralPath $Path) { [IO.File]::Replace($temp,$Path,($Path+'.previous')) }
    else { [IO.File]::Move($temp,$Path) }
}
$lock = $null
try {
    $lock = [IO.File]::Open((Join-Path $TplHomePath 'update.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
} catch { exit 0 }
try {
    $checkFile = Join-Path $TplHomePath 'last-check.txt'
    if (!$Force -and (Test-Path -LiteralPath $checkFile)) {
        $checked = [datetime]::Parse([IO.File]::ReadAllText($checkFile)).ToUniversalTime()
        if (([datetime]::UtcNow - $checked).TotalHours -lt 12) { exit 0 }
    }
    Atomic-Text $checkFile ([datetime]::UtcNow.ToString('o'))
    Atomic-Text (Join-Path $TplHomePath 'update-status.txt') 'Checking for updates...'
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $headers = @{ 'User-Agent'='Teirdalin-TPL/0.1.0'; 'Accept'='application/vnd.github+json' }
    $release = Invoke-RestMethod -Uri 'https://api.github.com/repos/Teirdalin/TPL/releases/latest' -Headers $headers -TimeoutSec 30
    if ($release.draft -or $release.prerelease) { throw 'No stable release available.' }
    $versionText = ([string]$release.tag_name) -replace '^v',''
    if ($versionText -notmatch '^\d+\.\d+\.\d+$') { throw 'Unsupported release version.' }
    $version = [version]$versionText
    $current = [version]([IO.File]::ReadAllText((Join-Path $TplHomePath 'current.txt')).Trim())
    if ($version -le $current) { Atomic-Text (Join-Path $TplHomePath 'update-status.txt') 'TPL is up to date.'; exit 0 }
    $archive = @($release.assets | Where-Object name -eq 'TPL-runtime.zip')
    $checksum = @($release.assets | Where-Object name -eq 'TPL-runtime.zip.sha256')
    if ($archive.Count -ne 1 -or $checksum.Count -ne 1 -or $archive[0].size -gt 64MB) { throw 'Release package missing or too large.' }
    foreach ($asset in @($archive[0],$checksum[0])) {
        $uri = [uri]$asset.browser_download_url
        if ($uri.Scheme -ne 'https' -or $uri.Host -ne 'github.com' -or !$uri.AbsolutePath.StartsWith('/Teirdalin/TPL/releases/download/',[StringComparison]::Ordinal)) { throw 'Unexpected release download URL.' }
    }
    $staging = Join-Path $TplHomePath ('staging\' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $staging -Force | Out-Null
    $zipPath = Join-Path $staging 'package.zip'
    Invoke-WebRequest -UseBasicParsing -Uri $archive[0].browser_download_url -Headers $headers -OutFile $zipPath -TimeoutSec 120
    $hashResponse = Invoke-WebRequest -UseBasicParsing -Uri $checksum[0].browser_download_url -Headers $headers -TimeoutSec 30
    $expected = ([string]$hashResponse.Content).Trim().Split(' ')[0]
    if ($expected -notmatch '^[0-9a-fA-F]{64}$' -or (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash -ne $expected) { throw 'Update checksum mismatch.' }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $package = [IO.Compression.ZipFile]::OpenRead($zipPath)
    $unpacked = Join-Path $staging 'runtime'
    New-Item -ItemType Directory -Path $unpacked | Out-Null
    try {
        $total = 0L
        $seen = @{}
        foreach ($entry in $package.Entries) {
            # Runtime packages are flat and contain only the independently updated runtime.
            if ($entry.FullName -notin @('TPL.Runtime.dll','TPL.Update.ps1','runtime.json')) { throw 'Unexpected file in runtime package.' }
            if ($seen.ContainsKey($entry.FullName)) { throw 'Duplicate update file.' }
            $seen[$entry.FullName] = $true
            $total += $entry.Length
            if ($total -gt 128MB) { throw 'Expanded package too large.' }
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,(Join-Path $unpacked $entry.FullName),$false)
        }
        if ($seen.Count -ne 3) { throw 'Incomplete runtime package.' }
    } finally { $package.Dispose() }
    $manifest = Get-Content -Raw -LiteralPath (Join-Path $unpacked 'runtime.json') | ConvertFrom-Json
    if ($manifest.bootstrapAbi -ne 1 -or $manifest.version -ne $versionText) { throw 'Update requires a newer manual installation.' }
    $destination = Join-Path $TplHomePath ('versions\' + $versionText)
    if (Test-Path -LiteralPath $destination) {
        foreach ($name in @('TPL.Runtime.dll','TPL.Update.ps1','runtime.json')) {
            if (!(Test-Path -LiteralPath (Join-Path $destination $name)) -or (Get-FileHash -LiteralPath (Join-Path $destination $name)).Hash -ne (Get-FileHash -LiteralPath (Join-Path $unpacked $name)).Hash) { throw 'Existing version differs from the release; retained for inspection.' }
        }
    } else { [IO.Directory]::Move($unpacked,$destination) }
    Atomic-Text (Join-Path $TplHomePath 'pending.txt') $versionText
    Atomic-Text (Join-Path $TplHomePath 'update-status.txt') ('TPL ' + $versionText + ' is ready. Restart Kenshi to apply.')
} catch {
    Atomic-Text (Join-Path $TplHomePath 'update-status.txt') ('Update failed: ' + $_.Exception.Message)
    exit 1
} finally { if ($lock) { $lock.Dispose() } }
