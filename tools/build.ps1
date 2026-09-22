param([string]$GameDir = 'E:\SteamLibrary\steamapps\common\Kenshi')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$family = Split-Path -Parent $root
$toolchain = if($env:TPL_TOOLCHAIN_ROOT){$env:TPL_TOOLCHAIN_ROOT}else{Join-Path $family 'toolchain'}
$vc = Join-Path $toolchain 'vc100-extract\Program Files(64)\Microsoft Visual Studio 10.0\VC'
$headers = Join-Path $toolchain 'vc100-x86-extract\Program Files\Microsoft Visual Studio 10.0\VC\include'
$sdk = Join-Path $toolchain 'sdk71-build-extract\Program Files\Microsoft SDKs\Windows\v7.1'
$bin = Join-Path $vc 'bin\amd64'
$cl = Join-Path $bin 'cl.exe'
$build = Join-Path $root 'build'
$dist = Join-Path $root 'dist'
$runtime = Join-Path $dist 'TPL\versions\0.1.3'
$mygui = Join-Path $root '.deps\mygui'
$json = Join-Path $root '.deps\rapidjson'
if (!(Test-Path $cl)) { throw 'The VC100 x64 toolchain is missing.' }
if (!(Test-Path "$mygui\MyGUIEngine\include\MyGUI.h") -or !(Test-Path "$json\include\rapidjson\document.h")) {
    throw 'Fetch the pinned MIT dependencies as described in README.md.'
}
New-Item -ItemType Directory -Force -Path $build,$runtime | Out-Null
& "$PSScriptRoot\build-engine-profile.ps1" -GameDir $GameDir
& "$PSScriptRoot\build-tpllib.ps1"
& "$PSScriptRoot\build-compat.ps1"
$env:PATH = "$bin;$env:PATH"
$env:INCLUDE = "$root\include;$root\src;$build\engine;$build\compat;$mygui\MyGUIEngine\include;$json\include;$headers;$sdk\Include"
$env:LIB = "$vc\lib\amd64;$sdk\Lib\x64;$build"

# Generate an import library from the user's installed MIT-licensed MyGUI binary.
$dll = Join-Path $GameDir 'MyGUIEngine_x64.dll'
$stream = [IO.File]::OpenRead($dll)
$reader = New-Object IO.BinaryReader($stream)
try {
    $stream.Position = 0x3c
    $pe = $reader.ReadInt32()
    $stream.Position = $pe + 6
    $sectionCount = $reader.ReadUInt16()
    $stream.Position = $pe + 20
    $optionalSize = $reader.ReadUInt16()
    $sections = @()
    for ($i = 0; $i -lt $sectionCount; $i++) {
        $stream.Position = $pe + 24 + $optionalSize + 40*$i + 8
        $size = $reader.ReadUInt32()
        $rva = $reader.ReadUInt32()
        $stream.Position = $pe + 24 + $optionalSize + 40*$i + 36
        $flags = $reader.ReadUInt32()
        $sections += [pscustomobject]@{ Start=$rva; End=([long]$rva+$size); Executable=($flags -band 0x20000000) -ne 0 }
    }
} finally { $reader.Dispose() }
$exports = @('LIBRARY MyGUIEngine_x64.dll','EXPORTS')
& "$bin\dumpbin.exe" /nologo /exports $dll | ForEach-Object {
    if ($_ -match '^\s+\d+\s+[0-9A-F]+\s+([0-9A-F]{8})\s+(\S+)') {
        $address = [Convert]::ToInt64($Matches[1],16)
        $name = $Matches[2]
        $section = $sections | Where-Object { $address -ge $_.Start -and $address -lt $_.End } | Select-Object -First 1
        $exports += '    ' + $name + $(if ($section -and !$section.Executable) { ' DATA' } else { '' })
    }
}
if ($LASTEXITCODE -ne 0 -or $exports.Count -lt 100) { throw 'Cannot enumerate MyGUI exports.' }
[IO.File]::WriteAllLines("$build\MyGUI.def",$exports,[Text.Encoding]::ASCII)
& "$bin\lib.exe" /nologo /machine:x64 "/def:$build\MyGUI.def" "/out:$build\MyGUIEngine_x64.lib"
if ($LASTEXITCODE) { throw 'Import library generation failed.' }
$flags = @('/nologo','/c','/O2','/MD','/EHsc','/W3','/Zi','/DNDEBUG','/DUNICODE','/D_UNICODE','/D_CRT_SECURE_NO_WARNINGS',"/Fo$build\", "/Fd$build\TPL.pdb")
Push-Location "$root\src"
try {
    & $cl @flags common.cpp legacy.cpp compat.cpp catalog.cpp tpllib_engine.cpp tpllib_ui_core.cpp tpllib_ui_mygui.cpp ui.cpp runtime.cpp bootstrap.cpp
    if ($LASTEXITCODE) { throw 'Compilation failed.' }
    & "$bin\link.exe" /nologo /DLL /MACHINE:X64 /DEBUG /INCREMENTAL:NO "/OUT:$dist\TPL.dll" "/IMPLIB:$build\TPL.lib" "/PDB:$build\TPL.pdb" "$build\bootstrap.obj" "$build\common.obj" kernel32.lib user32.lib advapi32.lib
    if ($LASTEXITCODE) { throw 'Bootstrap link failed.' }
    & "$bin\link.exe" /nologo /DLL /MACHINE:X64 /DEBUG /INCREMENTAL:NO "/OUT:$runtime\TPL.Runtime.dll" "/IMPLIB:$build\TPL.Runtime.lib" "/PDB:$build\TPL.Runtime.pdb" "$build\common.obj" "$build\legacy.obj" "$build\compat.obj" "$build\catalog.obj" "$build\tpllib_engine.obj" "$build\tpllib_ui_core.obj" "$build\tpllib_ui_mygui.obj" "$build\ui.obj" "$build\runtime.obj" "$build\tpllib\TPLLib.lib" MyGUIEngine_x64.lib kernel32.lib user32.lib advapi32.lib
    if ($LASTEXITCODE) { throw 'Runtime link failed.' }
} finally { Pop-Location }
Copy-Item "$root\tools\TPL.Update.ps1" $runtime -Force
Copy-Item "$root\tools\install.ps1" $dist -Force
Copy-Item "$root\tools\uninstall.ps1" $dist -Force
[IO.File]::WriteAllText("$dist\TPL\current.txt",'0.1.3')
[IO.File]::WriteAllText("$dist\TPL\automatic-updates.txt",'on')
& "$PSScriptRoot\stage-notices.ps1"
$imports = & "$bin\dumpbin.exe" /nologo /imports "$runtime\TPL.Runtime.dll"
if ($imports -match '^\s+(RE_Kenshi|KenshiLib)\.dll\s*$') { throw 'Forbidden GPL dependency in runtime.' }
Get-FileHash "$dist\TPL.dll","$runtime\TPL.Runtime.dll"
Write-Host 'Build passed. No files have been installed into Kenshi.'
