param([string]$OutputDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$family=Split-Path -Parent $root
$toolchain=if($env:TPL_TOOLCHAIN_ROOT){$env:TPL_TOOLCHAIN_ROOT}else{Join-Path $family 'toolchain'}
$vc=Join-Path $toolchain 'vc100-extract\Program Files(64)\Microsoft Visual Studio 10.0\VC'
$headers=Join-Path $toolchain 'vc100-x86-extract\Program Files\Microsoft Visual Studio 10.0\VC\include'
$sdk=Join-Path $toolchain 'sdk71-build-extract\Program Files\Microsoft SDKs\Windows\v7.1'
$bin=Join-Path $vc 'bin\amd64'
$dependency=Join-Path $root '.deps\minhook'
$commit='c3fcafdc10146beb5919319d0683e44e3c30d537'
if(!(Test-Path -LiteralPath "$dependency\include\MinHook.h")) {
    throw 'Fetch MinHook v1.3.4 as described in docs/TPLLIB.md.'
}
$actual=& git -C $dependency rev-parse HEAD
if($LASTEXITCODE -ne 0 -or $actual -ne $commit){throw 'MinHook revision differs from the reviewed pin.'}
$changes=& git -C $dependency status --porcelain --untracked-files=no
if($LASTEXITCODE -ne 0 -or $changes){throw 'MinHook tracked files were modified; review dependency provenance before building.'}
if(!$OutputDirectory){$OutputDirectory=Join-Path $root 'build\tpllib'}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$patch=Join-Path $root 'patches\minhook-1.3.4-context-rollback.patch'
if(!(Test-Path -LiteralPath $patch)){throw 'Reviewed MinHook context-ordering patch is missing.'}
$patchHash=(Get-FileHash -LiteralPath $patch).Hash
$source=Join-Path $OutputDirectory ('minhook-source-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $source | Out-Null
Copy-Item -LiteralPath "$dependency\src","$dependency\include" -Destination $source -Recurse
& git -C $source apply --check $patch
if($LASTEXITCODE){throw 'MinHook patch does not apply to pinned upstream source.'}
& git -C $source apply $patch
if($LASTEXITCODE){throw 'MinHook patch application failed.'}
$provenance=[ordered]@{upstreamCommit=$commit;patchSha256=$patchHash;sourceDirectory=$source}
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'minhook-build.json'),($provenance | ConvertTo-Json))
$oldPath=$env:PATH
$oldInclude=$env:INCLUDE
$oldLib=$env:LIB
try {
    $env:PATH="$bin;$env:PATH"
    $env:INCLUDE="$root\include;$root\src;$dependency\include;$headers;$sdk\Include"
    $env:LIB="$vc\lib\amd64;$sdk\Lib\x64"
    Push-Location $OutputDirectory
    try {
        & "$bin\cl.exe" /nologo /c /O2 /MD /W3 /Zi /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_WIN32_WINNT=0x0601 "$source\src\buffer.c" "$source\src\hook.c" "$source\src\trampoline.c" "$source\src\hde\hde64.c"
        if($LASTEXITCODE){throw 'MinHook compilation failed.'}
        & "$bin\cl.exe" /nologo /c /O2 /MD /EHsc /W4 /Zi /DNDEBUG /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 "$root\src\tpllib.cpp"
        if($LASTEXITCODE){throw 'TPLLib compilation failed.'}
        & "$bin\lib.exe" /nologo /machine:x64 /OUT:TPLLib.lib tpllib.obj buffer.obj hook.obj trampoline.obj hde64.obj
        if($LASTEXITCODE){throw 'TPLLib archive failed.'}
    } finally { Pop-Location }
} finally {
    $env:PATH=$oldPath
    $env:INCLUDE=$oldInclude
    $env:LIB=$oldLib
}
Write-Output "TPLLib static runtime built: $OutputDirectory\TPLLib.lib"
