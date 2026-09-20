# Troubleshooting

## Script will not run

Use Windows PowerShell 5.1 or PowerShell 7. Review the downloaded script and
trust its source before using a process-scoped `-ExecutionPolicy Bypass`
invocation. Do not permanently lower machine-wide policy. Quote paths with
spaces and run scripts from a normal writable development folder.

## MSBuild, v143 or Windows SDK missing

In Visual Studio Installer, add **Desktop development with C++**, v143 x64/x86
tools and a Windows 10/11 SDK to Visual Studio 2022 or Build Tools. Restart the
shell after installation. `build.ps1 -MSBuildPath "...\MSBuild.exe"` can select
an installation explicitly. Fix the first compiler error, not the final
summary saying the build failed. The generated configuration is Release x64.

## Error about Win32, architecture or DLL dependencies

Kenshi/TPL require x64. The starter uses a private static CRT for its C API
boundary; it must not import KenshiLib or a private TPLLib implementation.
From a Developer Command Prompt, inspect:

```text
dumpbin /headers build\x64\Release\MyFirstMod.dll
dumpbin /exports build\x64\Release\MyFirstMod.dll
dumpbin /dependents build\x64\Release\MyFirstMod.dll
```

Expect x64 and undecorated `TPL_Start`/`TPL_Tick`. Diagnose dependency load
errors before entry-point errors. Do not download unrelated DLLs into Kenshi.
TPL's own legacy runtime requirement is separate from your modern plugin.

## No Mods menu

Verify the runtime install first, without your plugin. Check the actual game
folder, supported Steam 1.0.65 x64 executable, startup configuration and
`TPL.log`. Rebuilding the plugin cannot fix a loader that never starts.

## Plugin missing or not running

Check `TPL/plugins/MyFirstMod/TPL.json` and the matching DLL in the same folder.
Do not leave the ZIP's folder nested twice. JSON uses lowercase `plugins` and
`dll`; the path must stay inside that mod folder. If it belongs to an FCS mod,
enable the owner and avoid ambiguous multiple `.mod` files. Enable the plugin
in Mods and restart. Refreshing the list does not hot-load a disabled DLL.

Check `TPL.log` for the startup failure reason and the expected initialized
line. A `.mod` being enabled or a DLL merely loaded is not proof of successful
initialization. The starter does not add a separate window or gameplay effect.

## Changed code/config appears ignored

Close Kenshi, build, then deploy with `-Replace`. Compare the deployed DLL's
SHA256 with the build output using `Get-FileHash`. Deployment preserves existing
`plugin.cfg`; edit that installed file when testing a new setting. Restart to
reload it. Keep the matching PDB for the installed DLL, not an older build.

## Wrong thread, missing service or version mismatch

`WRONG_THREAD`: queue work using `post`; do not access UI/game objects from a
worker. `NOT_FOUND` on an optional service: the provider may be absent or not
started yet. `VERSION_MISMATCH`: check the exact requested version/table size,
capabilities and supported game hash. Do not bypass those checks by claiming
the running binary matches a known build.

## Hook conflict or busy owner

Other plugins may already patch the target. Use an agreed TPL shared chain
where possible; never overwrite an unknown writer. Read `hook_state` and the
documented enable/disable recovery rules. A service or any retained hook keeps
its owner alive. Disabling a hook does not destroy it or make unloading safe.

## Crash around load/reset or UI closing

Reproduce with the smallest feature enabled. Inspect stale callback contexts,
game pointers retained across reset, native widget destruction, repeated hook
installation, and mismatched C++ signatures/compilers. A pointer retaining the
same value across reset does not mean its old contents are still valid.

For native debugging, attach the matching Visual Studio debugger to the game,
load the exact plugin PDB, capture the failing thread's call stack and exception
address, and note the DLL base. A successful build or host fixture does not
prove the gameplay path. Do not use an exception catch as permission to keep
executing after arbitrary native memory corruption.

## Useful bug report

Include TPL/game/plugin versions, the DLL SHA256, compiler/toolset, relevant
`TPL.log` excerpt, minimal steps, enabled-plugin list and whether it reproduces
without RE_Kenshi. For crashes add the matching stack/symbol information.
Remove credentials, private paths and save contents before publishing logs.
