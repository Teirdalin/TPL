param(
    [Parameter(Mandatory=$true)][string]$Name,
    [string]$Destination,
    [string]$DisplayName
)
$ErrorActionPreference='Stop'
if($Name -notmatch '^[A-Za-z][A-Za-z0-9_]{0,47}$' -or $Name -match '^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])$') {
    throw 'Name must be 1-48 ASCII letters/digits/underscores, starting with a letter; Windows reserved names are not allowed.'
}
if(!$DisplayName){$DisplayName=$Name}
if($DisplayName.Length -gt 100 -or $DisplayName -match '[\x00-\x1f]'){throw 'DisplayName must be a single line of at most 100 characters.'}
if(!$Destination){$Destination=Join-Path $PWD $Name}
$Destination=[IO.Path]::GetFullPath($Destination)
if(Test-Path -LiteralPath $Destination){throw 'Destination exists. Choose a new folder; existing work is never overwritten.'}
$root=Split-Path -Parent $PSScriptRoot
$template=Join-Path $root 'templates\plugin'
$mapping=[ordered]@{'plugin.cpp.in'='src\plugin.cpp';'plugin.vcxproj.in'=($Name+'.vcxproj');'README.md.in'='README.md';'plugin.cfg'='plugin.cfg';'build.ps1'='build.ps1';'package.ps1'='package.ps1';'deploy.ps1'='deploy.ps1';'project-tools.ps1'='project-tools.ps1'}
foreach($file in $mapping.Keys){if(!(Test-Path -LiteralPath (Join-Path $template $file))){throw "SDK template missing: $file"}}
foreach($file in @('include\tpl.h','include\tpllib.h','include\tpl_plugin.hpp','PLUGIN_API_PERMISSION.md','Licenses\JDL-1.txt')) {
    if(!(Test-Path -LiteralPath (Join-Path $root $file))){throw "SDK file missing: $file"}
}
$utf8=New-Object Text.UTF8Encoding($false)
$guid=[guid]::NewGuid().ToString().ToUpperInvariant()
New-Item -ItemType Directory -Path $Destination | Out-Null
foreach($file in $mapping.Keys) {
    $target=Join-Path $Destination $mapping[$file]
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    $text=[IO.File]::ReadAllText((Join-Path $template $file)).Replace('__TPL_PLUGIN_NAME__',$Name).Replace('__TPL_PROJECT_GUID__',$guid)
    [IO.File]::WriteAllText($target,$text,$utf8)
}
$sdk=Join-Path $Destination 'sdk'
New-Item -ItemType Directory -Path (Join-Path $sdk 'include'),(Join-Path $sdk 'Licenses') | Out-Null
Get-ChildItem -LiteralPath (Join-Path $root 'include') -File | Where-Object {$_.Extension -in @('.h','.hpp')} | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $sdk 'include')
}
Copy-Item -LiteralPath (Join-Path $root 'PLUGIN_API_PERMISSION.md') -Destination $sdk
Copy-Item -LiteralPath (Join-Path $root 'Licenses\JDL-1.txt') -Destination (Join-Path $sdk 'Licenses')
$manifest=[ordered]@{plugins=@([ordered]@{name=$DisplayName;dll=($Name+'.dll')})}
$project=[ordered]@{schema=1;name=$Name;displayName=$DisplayName;version='0.1.0';tplMinimum='0.1.0'}
[IO.File]::WriteAllText((Join-Path $Destination 'TPL.json'),($manifest | ConvertTo-Json -Depth 4),$utf8)
[IO.File]::WriteAllText((Join-Path $Destination 'plugin-project.json'),($project | ConvertTo-Json),$utf8)
[IO.File]::WriteAllText((Join-Path $Destination '.gitignore'),"build/`ndist/`nbackups/`n.vs/`n*.user`n",$utf8)
Write-Output "Created $Destination. Run its build.ps1, then deploy.ps1 with Kenshi closed. No build or installation was performed."
