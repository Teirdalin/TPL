$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$family=Split-Path -Parent $root
$toolchain=if($env:TPL_TOOLCHAIN_ROOT){$env:TPL_TOOLCHAIN_ROOT}else{Join-Path $family 'toolchain'}
$vc=Join-Path $toolchain 'vc100-extract\Program Files(64)\Microsoft Visual Studio 10.0\VC'
$headers=Join-Path $toolchain 'vc100-x86-extract\Program Files\Microsoft Visual Studio 10.0\VC\include'
$sdk=Join-Path $toolchain 'sdk71-build-extract\Program Files\Microsoft SDKs\Windows\v7.1'
$bin=Join-Path $vc 'bin\amd64'
$build=Join-Path $root 'build\compat'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$definitions=New-Object 'Collections.Generic.List[string]'
$definitions.Add('LIBRARY TPL.KL.dll')
$definitions.Add('EXPORTS')
foreach($line in Get-Content -LiteralPath "$root\src\compat_aliases.inc") {
    if($line -match '^TPL_COMPAT_ALIAS\("([^"\r\n]+)", "[^"]+", ([A-Za-z_][A-Za-z0-9_]*)\)$') {
        $definitions.Add('    '+$Matches[1]+'='+$Matches[2])
    } elseif($line.Trim() -and !$line.StartsWith('//')) { throw 'Malformed compatibility alias registry.' }
}
if($definitions.Count -le 2){throw 'Compatibility alias registry is empty.'}
[IO.File]::WriteAllLines("$build\TPL.KL.def",$definitions,[Text.Encoding]::ASCII)
$oldPath=$env:PATH
$oldInclude=$env:INCLUDE
$oldLib=$env:LIB
try {
    $env:PATH="$bin;$env:PATH"
    $env:INCLUDE="$root\include;$headers;$sdk\Include"
    $env:LIB="$vc\lib\amd64;$sdk\Lib\x64"
    Push-Location $build
    try {
        & "$bin\cl.exe" /nologo /LD /MD /O2 /EHsc /W4 "$root\src\compat_shim.cpp" /link /INCREMENTAL:NO /DEF:TPL.KL.def /OUT:TPL.KL.dll /IMPLIB:TPL.KL.lib
        if($LASTEXITCODE){throw 'Compatibility adapter compilation failed.'}
        $imports=& "$bin\dumpbin.exe" /imports TPL.KL.dll
        if($LASTEXITCODE -ne 0 -or $imports -match '^\s*(KenshiLib|RE_Kenshi)\.dll\s*$'){throw 'Adapter has a forbidden dependency.'}
        $payload=[IO.File]::ReadAllBytes("$build\TPL.KL.dll")
        $rows=New-Object 'Collections.Generic.List[string]'
        $rows.Add('// Generated from the independently built adapter DLL. Do not edit.')
        $rows.Add('static const unsigned char compatibilityPayload[]={')
        for($i=0;$i -lt $payload.Length;$i+=32) {
            $end=[Math]::Min($payload.Length-1,$i+31)
            $rows.Add((($payload[$i..$end] | ForEach-Object { '0x'+$_.ToString('X2') }) -join ',')+',')
        }
        $rows.Add('};')
        [IO.File]::WriteAllLines("$build\TPL.CompatPayload.h",$rows,[Text.Encoding]::ASCII)
    } finally { Pop-Location }
} finally {
    $env:PATH=$oldPath
    $env:INCLUDE=$oldInclude
    $env:LIB=$oldLib
}
Write-Output "Built independent compatibility adapter: $build\TPL.KL.dll"
