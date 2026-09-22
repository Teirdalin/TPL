# Your first TPL plugin

## 1. Install the essentials

- Install the [TPL runtime](https://github.com/Teirdalin/TPL/releases/tag/v0.1.4)
  in Steam Kenshi 1.0.65 x64. Start it once and confirm Mods appears.
- Install Visual Studio 2022 or its Build Tools with **Desktop development
  with C++**, the **v143 x64/x86 tools**, and a **Windows 10/11 SDK**.
- Extract `TPL-SDK-0.1.4.zip` or clone the TPL source repository.

Visual Studio Community can edit/build the generated project. Build Tools is
enough for the PowerShell workflow. The SDK does not bundle Microsoft compilers,
game binaries, the loader or dependency libraries.

## 2. Create a project

From the extracted SDK or repository root:

```powershell
./tools/New-TPLPlugin.ps1 -Name MyFirstMod -Destination C:\KenshiMods\MyFirstMod
```

Use a short identifier for `Name`: letters, digits and underscores, starting
with a letter. For a player-facing name use `-DisplayName "My First Mod"`.
Paths containing spaces must be quoted. Existing output folders are refused.

The generated project is independent of the SDK's location: its public headers
and notices are copied into `sdk/`. Moving the original SDK later is fine.

```text
MyFirstMod/
  src/plugin.cpp          Your implementation
  MyFirstMod.vcxproj       Visual Studio project, Release x64
  TPL.json                Loader entry and display name
  plugin-project.json     Packaging name/version, not a loader dependency rule
  plugin.cfg              Working example setting
  build.ps1               Finds VS2022 and compiles
  deploy.ps1              Installs only this plugin, with guarded replacement
  package.ps1             Creates a distributable ZIP
  sdk/                    Public headers and their notices
```

## 3. Build

```powershell
cd C:\KenshiMods\MyFirstMod
./build.ps1
```

Or open the `.vcxproj` and select **Release / x64**. You should get
`build/x64/Release/MyFirstMod.dll` and its matching PDB. No import libraries
need adding. Keep the PDB with the exact DLL when diagnosing a crash.

If PowerShell blocks a downloaded script, first review/trust its source.
You can use a process-scoped invocation without changing machine policy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

Do not install random compiler DLLs into Kenshi to solve a build problem.
See [Troubleshooting](TROUBLESHOOTING.md) for missing tools and wrong architecture.

## 4. Install and verify

Close Kenshi, then:

```powershell
./deploy.ps1 -GameDir "E:\SteamLibrary\steamapps\common\Kenshi"
```

Substitute your actual game path. Deployment requires an installed TPL, copies
the plugin and matching PDB into `TPL/plugins/MyFirstMod`, and never starts
the game. It does not alter saves, mod order or loader settings.

Start Kenshi. In Mods, look for your display name and a running status. In the
game directory's `TPL.log`, expect these two lines:

```text
MyFirstMod: initialized
MyFirstMod: first GUI tick
```

The starter only logs. No new in-game window is expected. It runs without a
`.mod` and does not need a save loaded. `plugin.cfg` contains a real setting:
set `log_first_tick=0`, restart, and the second line is suppressed.

## 5. Edit and repeat

Change `src/plugin.cpp`. Rebuild, close Kenshi and run:

```powershell
./deploy.ps1 -GameDir "YOUR KENSHI FOLDER" -Replace
```

The existing plugin directory is backed up under your project's `backups/`.
Its installed `plugin.cfg` is preserved, so rebuilding does not reset player
settings. To test a new default, edit that installed config deliberately.
Always restart; loaded native DLLs and hooks cannot be unloaded safely.

If the plugin was disabled in Mods, deployment does not silently enable it.
Enable it there and restart. Check the deployed DLL hash when changes seem
absent; do not assume the newest build is the one the game loaded.

## 6. Share your mod

Add a `LICENSE` for **your** plugin and set `version` in `plugin-project.json`.
Then run `./package.ps1`. The ZIP contains a single `MyFirstMod/` directory with
the DLL, manifest, config and notices. It omits source, PDBs, SDK headers and
the TPL loader. Existing versioned ZIPs are never overwritten.

Players extract that folder directly into `Kenshi/TPL/plugins`. Include a TPL
download link in your release instructions, not a bundled loader. The package
tool does not upload anything or update your mod through TPL's runtime updater.

Read [Lifecycle](LIFECYCLE.md) before adding native hooks or worker threads.
