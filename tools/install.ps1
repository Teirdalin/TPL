param([Parameter(Mandatory=$true)][string]$GameDir)
$ErrorActionPreference='Stop'
$GameDir=(Resolve-Path -LiteralPath $GameDir).Path
if (Get-Process -Name kenshi_x64,kenshi_GOG_x64 -ErrorAction SilentlyContinue) { throw 'Close Kenshi before installing.' }
if (!(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'TPL.dll'))) { throw 'Run this script from the built dist folder.' }
$cfg=Join-Path $GameDir 'Plugins_x64.cfg'
$text=[IO.File]::ReadAllText($cfg)
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss-fff'
Copy-Item -LiteralPath $cfg -Destination ($cfg+'.tpl-'+$stamp+'.bak')
$target=Join-Path $GameDir 'TPL.dll'
if (Test-Path -LiteralPath $target) { Copy-Item -LiteralPath $target -Destination ($target+'.'+$stamp+'.bak') }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'TPL.dll') -Destination $target
$homePath=Join-Path $GameDir 'TPL'
New-Item -ItemType Directory -Path $homePath -Force | Out-Null
$versionsPath=Join-Path $homePath 'versions'
New-Item -ItemType Directory -Path $versionsPath -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'TPL\versions') -Directory | ForEach-Object {
    $existing=Join-Path $versionsPath $_.Name
    if(Test-Path -LiteralPath $existing) {
        $changed=@(Get-ChildItem -LiteralPath $_.FullName -File | Where-Object {
            $peer=Join-Path $existing $_.Name
            !(Test-Path -LiteralPath $peer) -or (Get-FileHash -LiteralPath $_.FullName).Hash -ne (Get-FileHash -LiteralPath $peer).Hash
        }).Count -gt 0
        if($changed) { Copy-Item -LiteralPath $existing -Destination ($existing+'.backup-'+$stamp) -Recurse }
    }
    Copy-Item -LiteralPath $_.FullName -Destination $versionsPath -Recurse -Force
}
foreach ($name in @('current.txt','automatic-updates.txt')) {
    if (!(Test-Path -LiteralPath (Join-Path $homePath $name))) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot ('TPL\'+$name)) -Destination (Join-Path $homePath $name) }
}
$lines=@($text -split '\r?\n' | Where-Object { $_ -notmatch '^\s*Plugin\s*=\s*TPL\s*$' })
$result=New-Object 'Collections.Generic.List[string]'
$inserted=$false
$hasRe=@($lines | Where-Object { $_ -match '^\s*Plugin\s*=\s*RE_Kenshi\s*$' }).Count -gt 0
foreach ($line in $lines) {
    if (!$hasRe -and !$inserted -and $line -match '^\s*Plugin\s*=') { $result.Add('Plugin=TPL'); $inserted=$true }
    $result.Add($line)
    if ($hasRe -and $line -match '^\s*Plugin\s*=\s*RE_Kenshi\s*$') { $result.Add('Plugin=TPL'); $inserted=$true }
}
if (!$inserted) { $result.Add('Plugin=TPL') }
$temp=$cfg+'.tpl-install.tmp'
[IO.File]::WriteAllText($temp,($result -join "`r`n"),(New-Object Text.UTF8Encoding($false)))
[IO.File]::Replace($temp,$cfg,($cfg+'.tpl-'+$stamp+'.bak'))
Write-Host 'TPL installed. The previous plugin configuration was backed up.'
