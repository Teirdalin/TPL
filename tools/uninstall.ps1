param([Parameter(Mandatory=$true)][string]$GameDir)
$ErrorActionPreference='Stop'
if (Get-Process -Name kenshi_x64,kenshi_GOG_x64 -ErrorAction SilentlyContinue) { throw 'Close Kenshi first.' }
$cfg=Join-Path (Resolve-Path -LiteralPath $GameDir).Path 'Plugins_x64.cfg'
$text=[IO.File]::ReadAllText($cfg)
Copy-Item -LiteralPath $cfg -Destination ($cfg+'.tpl-uninstall-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')+'.bak')
$lines=@($text -split '\r?\n' | Where-Object { $_ -notmatch '^\s*Plugin\s*=\s*TPL\s*$' })
[IO.File]::WriteAllText($cfg,($lines -join "`r`n"),(New-Object Text.UTF8Encoding($false)))
Write-Host 'TPL disabled. Runtime versions, settings, and backups were retained.'
