param([string]$OutputDirectory,[string]$PayloadDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if(!$PayloadDirectory){$PayloadDirectory=Join-Path $root 'dist'}
$PayloadDirectory=(Resolve-Path -LiteralPath $PayloadDirectory).Path
if(!$OutputDirectory){$OutputDirectory=Join-Path $root 'build\player-installers'}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$manual=Join-Path $OutputDirectory 'TPL-0.1.0-Manual.zip'
$installer=Join-Path $OutputDirectory 'TPL-0.1.0-Installer.exe'
foreach($path in @($manual,$installer,($manual+'.sha256'),($installer+'.sha256'))){if(Test-Path -LiteralPath $path){throw "Output exists; retain it and choose a new directory: $path"}}
$payload=@(
    'TPL.dll','TPL\versions\0.1.0\TPL.Runtime.dll','TPL\versions\0.1.0\TPL.Update.ps1','TPL\versions\0.1.0\runtime.json',
    'TPL\current.txt','TPL\automatic-updates.txt','README.md','LICENSE','PLUGIN_API_PERMISSION.md','THIRD_PARTY_NOTICES.md',
    'Licenses\JDL-1.txt','Licenses\MyGUI.LICENSE.txt','Licenses\RapidJSON.LICENSE.txt','Licenses\MinHook.LICENSE.txt'
)
foreach($relative in $payload){if(!(Test-Path -LiteralPath (Join-Path $PayloadDirectory $relative) -PathType Leaf)){throw "Player payload missing: $relative"}}
$stage=Join-Path $OutputDirectory ('manual-stage-'+[guid]::NewGuid().ToString('N'))
$files=Join-Path $stage 'TPL Installer Files'
$stagedPayload=Join-Path $files 'payload'
New-Item -ItemType Directory -Force -Path $stagedPayload | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'tools\player-installer\Install TPL.bat') -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'tools\install.ps1') -Destination $files
Copy-Item -LiteralPath (Join-Path $root 'tools\uninstall.ps1') -Destination $files
foreach($relative in $payload){
    $target=Join-Path $stagedPayload $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Copy-Item -LiteralPath (Join-Path $PayloadDirectory $relative) -Destination $target
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
Add-Type -AssemblyName System.IO.Compression,System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::Open($manual,[IO.Compression.ZipArchiveMode]::Create)
try {
    Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object {
        $entry=$_.FullName.Substring($stage.Length+1).Replace('\','/')
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$_.FullName,$entry)
    }
} finally {$zip.Dispose()}
& (Join-Path $root 'tools\build-player-installer.ps1') -PayloadDirectory $PayloadDirectory -OutputPath $installer
foreach($path in @($manual,$installer)){[IO.File]::WriteAllText(($path+'.sha256'),(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant())}
Get-FileHash -LiteralPath $manual,$installer
