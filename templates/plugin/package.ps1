param([string]$LicensePath)
. "$PSScriptRoot\project-tools.ps1"
$project=Read-PluginProject
$files=Get-PluginPayload $project
if(!$LicensePath){$LicensePath=Join-Path $PSScriptRoot 'LICENSE'}
if(!(Test-Path -LiteralPath $LicensePath -PathType Leaf)){throw 'Choose a license for YOUR plugin: add LICENSE or pass -LicensePath. TPL does not choose one for you.'}
$license=(Resolve-Path -LiteralPath $LicensePath).Path
if($files.Contains($license)){throw 'Use a separate plugin license file, not an existing payload file.'}
$files[$license]='LICENSE'
$output=Join-Path $PSScriptRoot ('dist\'+$project.name+'-'+$project.version+'.zip')
if((Test-Path -LiteralPath $output) -or (Test-Path -LiteralPath ($output+'.sha256'))){throw 'Package exists. Keep it and increase version in plugin-project.json.'}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $output) | Out-Null
Add-Type -AssemblyName System.IO.Compression,System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::Open($output,[IO.Compression.ZipArchiveMode]::Create)
try {
    foreach($path in $files.Keys){[void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$path,($project.name+'/'+($files[$path] -replace '\\','/')))}
} finally {$zip.Dispose()}
[IO.File]::WriteAllText(($output+'.sha256'),(Get-PluginSha256 $output).ToLowerInvariant())
Write-Output "Packaged: $output (DLL, manifest, config and notices; no source, PDB, SDK implementation or loader)."
