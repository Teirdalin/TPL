param([string]$OutputDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$dist=Join-Path $root 'dist'
if(!$OutputDirectory){$OutputDirectory=Join-Path $root 'build\release'}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$full=Join-Path $OutputDirectory 'TPL-0.1.5.zip'
$runtime=Join-Path $OutputDirectory 'TPL-runtime.zip'
foreach($path in @($full,$runtime,($full+'.sha256'),($runtime+'.sha256'))) {
    if(Test-Path -LiteralPath $path){throw 'Release output exists; retain it and choose a new directory.'}
}
& "$PSScriptRoot\stage-notices.ps1"
$files=@('TPL.dll','install.ps1','uninstall.ps1','README.md','LICENSE','PLUGIN_API_PERMISSION.md','THIRD_PARTY_NOTICES.md',
    'Licenses\JDL-1.txt','Licenses\MyGUI.LICENSE.txt','Licenses\RapidJSON.LICENSE.txt','Licenses\MinHook.LICENSE.txt',
    'TPL\current.txt','TPL\automatic-updates.txt','TPL\versions\0.1.5\TPL.Runtime.dll',
    'TPL\versions\0.1.5\TPL.Update.ps1','TPL\versions\0.1.5\runtime.json')
foreach($name in $files){if(!(Test-Path -LiteralPath (Join-Path $dist $name))){throw "Build output missing: $name"}}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
Add-Type -AssemblyName System.IO.Compression,System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::Open($full,[IO.Compression.ZipArchiveMode]::Create)
try {
    foreach($name in $files){[void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,(Join-Path $dist $name),($name -replace '\\','/'))}
} finally {$zip.Dispose()}
$zip=[IO.Compression.ZipFile]::Open($runtime,[IO.Compression.ZipArchiveMode]::Create)
try {
    foreach($name in @('TPL.Runtime.dll','TPL.Update.ps1','runtime.json')) {
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,(Join-Path $dist ('TPL\versions\0.1.5\'+$name)),$name)
    }
} finally {$zip.Dispose()}
foreach($path in @($full,$runtime)) {
    [IO.File]::WriteAllText(($path+'.sha256'),(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant())
}
Get-FileHash -LiteralPath $full,$runtime
