# Teirdalin's Plugin Loader

An independent native plugin loader and in-game Mods manager for Kenshi.

## Install

1. Download `TPL-0.1.0.zip` from [Releases](https://github.com/Teirdalin/TPL/releases).
2. Close Kenshi and extract the archive into a temporary folder.
3. In PowerShell, run `./install.ps1 -GameDir "YOUR KENSHI FOLDER"` from that folder.

Requires Windows x64, Steam Kenshi 1.0.65 and the Visual C++ 2010 x64 runtime.
The installer backs up the startup configuration and preserves existing mods,
settings and saves. `uninstall.ps1` disables TPL startup and retains its files.

## Features

- Main-menu Mods list for FCS mods and native plugins.
- Enable/disable selections applied at the next launch.
- In-game editing of supported bundled text `.cfg` files, with backups.
- TPLLib plugin API: logging, checked addresses, hooks, shared TPL hook chains,
  main-thread dispatch and versioned services.
- Optional automatic updates from this repository's releases.

This is an **experimental foundation release**, not a complete KenshiLib
replacement. Existing RE_Kenshi plugins that require missing KenshiLib APIs
remain blocked. TPL neither bundles nor initializes RE_Kenshi or KenshiLib.
Optional RE_Kenshi management targets 0.3.5; shared native hook targets across
the two loaders remain unverified. Do not remove a working dependency merely
because a DLL appears in the Mods list.

The tested TPL-only world observer completed 10,407 graphics updates and one
reset without thread/identity warnings. That is a scoped smoke test, not
acceptance of every menu, game API, mod or save/load scenario. Diagnostic
plugins are not included in the distribution.

## Plugin development

Place a DLL and `TPL.json` in a mod folder or `TPL/plugins/<name>`:

```json
{"plugins":[{"name":"My Plugin","dll":"MyPlugin.dll"}]}
```

Export `extern "C" int TPL_Start(const TPL_Host*)`, returning zero on success.
Optional `TPL_Tick(float)` runs on the GUI thread. Initialization is at the
main menu; early startup and live DLL unloading are unsupported. Public
interfaces are in `include/`; native game bindings require the reviewed build
and ABI. Resolve all hook targets before modifying their entries.

## Updates

The installer enables automatic checks; users can turn them off in Mods.
Checks use the latest non-prerelease `vX.Y.Z` release on `Teirdalin/TPL`.
`TPL-runtime.zip` and its `.sha256` file stage newer runtimes for the next
launch. The bootstrap is updated manually. Existing 0.1.0 test installations
are already at this version; automatic upgrades start with a higher version.

## Source and license

See [BUILD.md](BUILD.md) for source builds. Build output, tests, local research
artifacts and diagnostic plugins are intentionally omitted from this repository.

TPL uses [JDL-1](LICENSE), a custom restricted license, not MIT or an open-source
license. Personal-use modifications are permitted; redistribution or other
modifications require the creator's permission. Plugin authors may choose
their own licenses and need not publish their source. See the separate
[plugin API permission](PLUGIN_API_PERMISSION.md) and
[third-party notices](THIRD_PARTY_NOTICES.md).
