param(
    [Parameter(Mandatory=$true)][string]$PayloadDirectory,
    [Parameter(Mandatory=$true)][string]$OutputPath
)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$PayloadDirectory=(Resolve-Path -LiteralPath $PayloadDirectory).Path
$OutputPath=[IO.Path]::GetFullPath($OutputPath)
$csc=Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe'
if(!(Test-Path -LiteralPath $csc)){throw '.NET Framework C# compiler not found.'}
$map=@(
    'TPL.dll','TPL\versions\0.1.1\TPL.Runtime.dll','TPL\versions\0.1.1\TPL.Update.ps1','TPL\versions\0.1.1\runtime.json',
    'TPL\current.txt','TPL\automatic-updates.txt','README.md','LICENSE','PLUGIN_API_PERMISSION.md','THIRD_PARTY_NOTICES.md',
    'Licenses\JDL-1.txt','Licenses\MyGUI.LICENSE.txt','Licenses\RapidJSON.LICENSE.txt','Licenses\MinHook.LICENSE.txt'
)
$arguments=@('/nologo','/target:winexe','/platform:x64','/optimize+','/debug-','/r:System.Windows.Forms.dll','/r:System.Drawing.dll',
    ('/win32manifest:'+(Join-Path $root 'tools\player-installer\TPL.Installer.manifest')),('/out:'+$OutputPath),(Join-Path $root 'tools\player-installer\Program.cs'))
for($i=0;$i -lt $map.Count;$i++){
    $source=Join-Path $PayloadDirectory $map[$i]
    if(!(Test-Path -LiteralPath $source -PathType Leaf)){throw "Installer payload missing: $($map[$i])"}
    $arguments+=('/resource:'+$source+',TPL.Payload.'+$i)
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath) | Out-Null
& $csc @arguments
if($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $OutputPath -PathType Leaf)){throw 'Player installer build failed.'}
Write-Output "Built clickable installer: $OutputPath"
