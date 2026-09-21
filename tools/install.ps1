param(
    [Parameter(Mandatory=$true)][string]$GameDir,
    [string]$PayloadDir=$PSScriptRoot
)
$ErrorActionPreference='Stop'
function Get-TPLHash([string]$Path){
    $sha=[Security.Cryptography.SHA256]::Create()
    $stream=[IO.File]::OpenRead($Path)
    try{return -join @($sha.ComputeHash($stream) | ForEach-Object {$_.ToString('x2')})}
    finally{$stream.Dispose();$sha.Dispose()}
}
$GameDir=(Resolve-Path -LiteralPath $GameDir).Path.TrimEnd('\')
$PayloadDir=(Resolve-Path -LiteralPath $PayloadDir).Path.TrimEnd('\')
if(Get-Process -Name kenshi_x64,kenshi_GOG_x64 -ErrorAction SilentlyContinue){throw 'Close Kenshi before installing TPL.'}
foreach($required in @('kenshi_x64.exe','Plugins_x64.cfg')){
    if(!(Test-Path -LiteralPath (Join-Path $GameDir $required) -PathType Leaf)){throw "The selected folder is not a Steam Kenshi installation: missing $required"}
}
$payload=@(
    'TPL.dll','TPL\versions\0.1.2\TPL.Runtime.dll','TPL\versions\0.1.2\TPL.Update.ps1',
    'TPL\versions\0.1.2\runtime.json','TPL\current.txt','TPL\automatic-updates.txt'
)
foreach($relative in $payload){if(!(Test-Path -LiteralPath (Join-Path $PayloadDir $relative) -PathType Leaf)){throw "Installer payload is incomplete: $relative"}}
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$cfg=Join-Path $GameDir 'Plugins_x64.cfg'
$cfgBackup=$cfg+'.tpl-'+$stamp+'.bak'
Copy-Item -LiteralPath $cfg -Destination $cfgBackup
$bootstrap=Join-Path $GameDir 'TPL.dll'
if(Test-Path -LiteralPath $bootstrap -PathType Leaf){Copy-Item -LiteralPath $bootstrap -Destination ($bootstrap+'.'+$stamp+'.bak')}
$versionSource=Join-Path $PayloadDir 'TPL\versions\0.1.2'
$versionTarget=Join-Path $GameDir 'TPL\versions\0.1.2'
if(Test-Path -LiteralPath $versionTarget -PathType Container){
    $changed=@(Get-ChildItem -LiteralPath $versionSource | Where-Object {!$_.PSIsContainer} | Where-Object {
        $peer=Join-Path $versionTarget $_.Name
        !(Test-Path -LiteralPath $peer -PathType Leaf) -or (Get-TPLHash $_.FullName) -ne (Get-TPLHash $peer)
    }).Count -gt 0
    if($changed){Copy-Item -LiteralPath $versionTarget -Destination ($versionTarget+'.backup-'+$stamp) -Recurse}
}
foreach($relative in $payload){
    $source=Join-Path $PayloadDir $relative
    $target=Join-Path $GameDir $relative
    if(@('TPL\current.txt','TPL\automatic-updates.txt') -contains $relative -and (Test-Path -LiteralPath $target)){continue}
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    $temp=$target+'.tpl-install-'+[guid]::NewGuid().ToString('N')+'.tmp'
    Copy-Item -LiteralPath $source -Destination $temp
    if(Test-Path -LiteralPath $target){
        $replaceBackup=$target+'.tpl-replace-'+[guid]::NewGuid().ToString('N')+'.bak'
        [IO.File]::Replace($temp,$target,$replaceBackup)
        Remove-Item -LiteralPath $replaceBackup
    }else{[IO.File]::Move($temp,$target)}
    if((Get-TPLHash $source) -ne (Get-TPLHash $target)){throw "Installed file checksum mismatch: $relative"}
}
$text=[IO.File]::ReadAllText($cfg)
$lines=@($text -split '\r?\n' | Where-Object {$_ -notmatch '^\s*Plugin\s*=\s*TPL\s*$'})
$result=New-Object 'Collections.Generic.List[string]'
$inserted=$false
$preferred=@($lines | Where-Object {$_ -match '^\s*Plugin\s*=\s*RE_Kenshi\s*$'}).Count -gt 0
foreach($line in $lines){
    if(!$preferred -and !$inserted -and $line -match '^\s*Plugin\s*='){$result.Add('Plugin=TPL');$inserted=$true}
    $result.Add($line)
    if($preferred -and $line -match '^\s*Plugin\s*=\s*RE_Kenshi\s*$'){$result.Add('Plugin=TPL');$inserted=$true}
}
if(!$inserted){$result.Add('Plugin=TPL')}
$tempCfg=$cfg+'.tpl-install-'+[guid]::NewGuid().ToString('N')+'.tmp'
[IO.File]::WriteAllText($tempCfg,($result -join "`r`n"),(New-Object Text.UTF8Encoding($false)))
$replaceBackup=$cfg+'.tpl-replace-'+[guid]::NewGuid().ToString('N')+'.bak'
[IO.File]::Replace($tempCfg,$cfg,$replaceBackup)
Remove-Item -LiteralPath $replaceBackup
Write-Output "TPL installed in $GameDir"
Write-Output "Plugin configuration backup: $cfgBackup"
