param([Parameter(Mandatory=$true)][string]$GameDir)
$ErrorActionPreference='Stop'
$GameDir=(Resolve-Path -LiteralPath $GameDir).Path.TrimEnd('\')
if(Get-Process -Name kenshi_x64,kenshi_GOG_x64 -ErrorAction SilentlyContinue){throw 'Close Kenshi before uninstalling TPL.'}
$cfg=Join-Path $GameDir 'Plugins_x64.cfg'
if(!(Test-Path -LiteralPath $cfg -PathType Leaf)){throw 'The selected folder is not a Kenshi installation.'}
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$backup=$cfg+'.tpl-uninstall-'+$stamp+'.bak'
Copy-Item -LiteralPath $cfg -Destination $backup
$lines=@([IO.File]::ReadAllText($cfg) -split '\r?\n' | Where-Object {$_ -notmatch '^\s*Plugin\s*=\s*TPL\s*$'})
$temp=$cfg+'.tpl-uninstall-'+[guid]::NewGuid().ToString('N')+'.tmp'
[IO.File]::WriteAllText($temp,($lines -join "`r`n"),(New-Object Text.UTF8Encoding($false)))
$replaceBackup=$cfg+'.tpl-replace-'+[guid]::NewGuid().ToString('N')+'.bak'
[IO.File]::Replace($temp,$cfg,$replaceBackup)
Remove-Item -LiteralPath $replaceBackup
$bootstrap=Join-Path $GameDir 'TPL.dll'
if(Test-Path -LiteralPath $bootstrap -PathType Leaf){Move-Item -LiteralPath $bootstrap -Destination ($bootstrap+'.tpl-uninstalled-'+$stamp+'.bak')}
$version=Join-Path $GameDir 'TPL\versions\0.1.2'
if(Test-Path -LiteralPath $version -PathType Container){Move-Item -LiteralPath $version -Destination ($version+'.uninstalled-'+$stamp)}
Write-Output 'TPL uninstalled from startup. Plugins, settings, saves, and recovery backups were retained.'
Write-Output "Plugin configuration backup: $backup"
