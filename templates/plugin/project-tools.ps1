# TPL scaffold helper. Reuse permitted by sdk/PLUGIN_API_PERMISSION.md.
$ErrorActionPreference='Stop'
function Get-PluginSha256([string]$Path) {
    $stream=[IO.File]::OpenRead($Path)
    $algorithm=[Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-','') }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}
function Read-PluginProject {
    $project=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'plugin-project.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if($project.schema -ne 1 -or $project.name -notmatch '^[A-Za-z][A-Za-z0-9_]{0,47}$' -or
       $project.name -match '^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])$' -or $project.version -notmatch '^\d+\.\d+\.\d+$') {
        throw 'Invalid plugin-project.json: use a simple identifier and X.Y.Z version.'
    }
    return $project
}
function Get-PluginPayload($project,[switch]$Symbols) {
    $build=Join-Path $PSScriptRoot 'build\x64\Release'
    $files=[ordered]@{
        (Join-Path $build ($project.name+'.dll'))=($project.name+'.dll')
        (Join-Path $PSScriptRoot 'TPL.json')='TPL.json'
        (Join-Path $PSScriptRoot 'plugin.cfg')='plugin.cfg'
        (Join-Path $PSScriptRoot 'sdk\PLUGIN_API_PERMISSION.md')='TPL_API_PERMISSION.md'
        (Join-Path $PSScriptRoot 'sdk\Licenses\JDL-1.txt')='Licenses\TPL-JDL-1.txt'
    }
    if($Symbols){$files[(Join-Path $build ($project.name+'.pdb'))]=($project.name+'.pdb')}
    foreach($path in $files.Keys){if(!(Test-Path -LiteralPath $path -PathType Leaf)){throw "Required file missing: $path. Run build.ps1 first."}}
    $manifest=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'TPL.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if(@($manifest.plugins).Count -ne 1 -or $manifest.plugins[0].dll -cne ($project.name+'.dll')){throw 'TPL.json does not match the built plugin.'}
    $stream=[IO.File]::OpenRead((Join-Path $build ($project.name+'.dll')))
    $reader=New-Object IO.BinaryReader($stream)
    try {
        if($reader.ReadUInt16() -ne 0x5a4d){throw 'Plugin is not a PE image.'}
        $stream.Position=0x3c; $pe=$reader.ReadInt32()
        if($pe -lt 64 -or $pe -gt $stream.Length-6){throw 'Invalid plugin PE header.'}
        $stream.Position=$pe
        if($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664){throw 'Plugin must be Windows x64, not Win32.'}
    } finally {$reader.Dispose()}
    return $files
}
