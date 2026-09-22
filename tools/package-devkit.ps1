param([string]$OutputDirectory,[string]$Version='0.1.4')
$ErrorActionPreference='Stop'
function Get-Sha256([string]$Path) {
    $stream=[IO.File]::OpenRead($Path)
    $algorithm=[Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-','') }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}
if($Version -notmatch '^\d+\.\d+\.\d+$'){throw 'Version must be X.Y.Z.'}
$root=Split-Path -Parent $PSScriptRoot
if(!$OutputDirectory){$OutputDirectory=Join-Path $root 'build\developer-release'}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$archive=Join-Path $OutputDirectory ('TPL-SDK-'+$Version+'.zip')
if((Test-Path -LiteralPath $archive) -or (Test-Path -LiteralPath ($archive+'.sha256'))){throw 'SDK output exists. Keep it and choose another output directory/version.'}
$paths=@(
    'include\tpl.h','include\tpllib.h','include\tpl_plugin.hpp','include\tpllib_ui.h',
    'include\tpllib_engine.h','include\tpllib_engine_save.hpp','include\tpllib_engine_generated.hpp',
    'PLUGIN_API_PERMISSION.md','LICENSE','Licenses\JDL-1.txt','Licenses\JDL.png',
    'tools\New-TPLPlugin.ps1','tools\convert-rekenshi-project.py','tools\requirements-analysis.txt',
    'templates\plugin\plugin.cpp.in','templates\plugin\plugin.vcxproj.in',
    'templates\plugin\README.md.in','templates\plugin\plugin.cfg',
    'templates\plugin\build.ps1','templates\plugin\deploy.ps1',
    'templates\plugin\package.ps1','templates\plugin\project-tools.ps1',
    'examples\sdk\examples.hpp','examples\sdk\jobs.cpp','examples\sdk\services.cpp',
    'docs\developers\README.md','docs\developers\QUICKSTART.md',
    'docs\developers\API_REFERENCE.md','docs\developers\LIFECYCLE.md',
    'docs\developers\RECIPES.md','docs\developers\MIGRATION.md',
    'docs\developers\REKENSHI_TO_TPL_COMPENDIUM.md',
    'docs\developers\TROUBLESHOOTING.md'
)
$files=[ordered]@{'README.md'=(Join-Path $root 'docs\SDK_README.md')}
foreach($path in $paths){$files[($path -replace '\\','/')]=Join-Path $root $path}
foreach($source in $files.Values){if(!(Test-Path -LiteralPath $source -PathType Leaf)){throw "SDK input missing: $source"}}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
Add-Type -AssemblyName System.IO.Compression,System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::Open($archive,[IO.Compression.ZipArchiveMode]::Create)
try {
    foreach($entry in $files.Keys){[void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$files[$entry],$entry)}
} finally {$zip.Dispose()}
[IO.File]::WriteAllText(($archive+'.sha256'),(Get-Sha256 $archive).ToLowerInvariant())
Write-Output "SDK packaged: $archive ($($files.Count) files; no game/runtime binaries or tests)."
