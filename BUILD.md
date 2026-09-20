# Source builds

Target: Visual C++ 2010 x64, C++03, `/MD`, Windows SDK 7.1, and the supported
Steam Kenshi 1.0.65 executable. Game binaries and Microsoft tools are not
redistributed. Python 3.13 with the pinned analysis packages is used at build
time only; it is not required to run TPL.

Set `TPL_TOOLCHAIN_ROOT` to your extracted toolchain folder (the default is
`../toolchain`). Existing scripts expect these subdirectories:

```text
vc100-extract/Program Files(64)/Microsoft Visual Studio 10.0/VC
vc100-x86-extract/Program Files/Microsoft Visual Studio 10.0/VC/include
sdk71-build-extract/Program Files/Microsoft SDKs/Windows/v7.1
```

Fetch dependencies and build from the repository root:

```powershell
git clone --depth 1 --branch MyGUI3.2.2 --filter=blob:none --sparse https://github.com/MyGUI/mygui.git .deps/mygui
git -C .deps/mygui sparse-checkout set MyGUIEngine/include
git clone --depth 1 --branch v1.1.0 https://github.com/Tencent/rapidjson.git .deps/rapidjson
git clone --depth 1 --branch v1.3.4 https://github.com/TsudaKageyu/minhook.git .deps/minhook
python -m pip install -r tools/requirements-analysis.txt
$game = 'YOUR KENSHI FOLDER'
python tools/index-game.py --game "$game/kenshi_x64.exe"
./tools/build.ps1 -GameDir $game
./tools/package.ps1
```

MyGUI is pinned to `8a05127d7c5bb6772df88185b1f99d8052c379a4`, RapidJSON to
`f54b0e47a08782a6131cc3d60f94d038fa6e0a51`, and MinHook to
`c3fcafdc10146beb5919319d0683e44e3c30d537`. The checked-in MinHook patch is applied
to a generated copy. MyGUI import metadata comes from your installed DLL.

The indexer checks executable identity and resumes interrupted analysis.
Reviewed recipes produce seven native bindings; unreviewed discoveries are
not usable APIs. Full ABI evidence is retained with the world recipe. References
in that evidence to the maintainer's full parity/test pipeline describe the
private workbench; this source distribution uses the direct commands above.

Output is generated under `build/` and `dist/`. Tests, test runners, diagnostics,
and binary-analysis caches are deliberately excluded from the published source.
