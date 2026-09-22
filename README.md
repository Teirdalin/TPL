# Teirdalin's Plugin Loader

An independent native plugin loader and in-game Mods manager for Kenshi.

TPL is a lightweight option for players who want native plugin support without
extra bundled gameplay or engine changes, and for mod makers who want to ship
native mods without publishing all of their source files.

## Install

Download either the installer or the ZIP from
[Releases](https://github.com/Teirdalin/TPL/releases).

**Option 1: installer**

Download and run `TPL-0.1.3-Installer.exe`. It finds your Kenshi folder, copies
the loader files, and updates Kenshi's plugin configuration for you. If TPL is
already installed, it offers Reinstall and Uninstall.

**Option 2: manual ZIP**

1. Close Kenshi.
2. Extract the ZIP directly into your Kenshi folder.
3. Run `Install TPL.bat`. An existing installation offers Reinstall and
   Uninstall.

Requires Windows x64, Steam Kenshi 1.0.65, and the Visual C++ 2010 x64 runtime.
The installer backs up Kenshi's plugin configuration and preserves existing
mods, settings and saves. `uninstall.ps1` disables TPL startup and retains its
files.

`TPL.log` contains the current launch. At startup, the previous launch is
retained as `TPL.previous.log` instead of allowing one log file to grow
indefinitely.

TPL is designed to install alongside other native loaders when they are already
present. Do not remove a working dependency from an existing mod setup just
because a DLL appears in the Mods list.

## Features

- Main-menu Mods list for FCS mods and native plugins.
- Enable/disable selections applied at the next launch.
- In-game editing of supported bundled text `.cfg` files, with backups.
- TPLLib plugin API: logging, checked addresses, hooks, shared TPL hook chains,
  main-thread dispatch and versioned services.
- Optional automatic updates from this repository's releases.
- Plugin authors may keep their source private and choose JDL, another license,
  or their own terms for their own plugins.

This is an **experimental foundation release**. It is not acceptance of every
menu, game API, mod or save/load scenario. The tested TPL-only world observer
completed 10,407 graphics updates and one reset without thread/identity
warnings. Diagnostic plugins are not included in the distribution.

## Plugin development

Start with the [developer guide](docs/developers/README.md) for the standalone
plugin starter, build/deploy/package workflow, API reference, examples and
troubleshooting. Ordinary plugins do not need to rebuild TPL.

Place a DLL and `TPL.json` in a mod folder or `TPL/plugins/<name>`:

```json
{"plugins":[{"name":"My Plugin","dll":"MyPlugin.dll"}]}
```

Export `extern "C" int TPL_Start(const TPL_Host*)`, returning zero on success.
Optional `TPL_Tick(float)` runs on the GUI thread. Initialization is at the main
menu; early startup and live DLL unloading are unsupported. Public interfaces
are in `include/`; native game bindings require the reviewed build and ABI.
Resolve all hook targets before modifying their entries.

## Updates

TPL checks for a newer release at startup and shows a dismissible notice once
per launch. The installer enables automatic downloads; users can turn those
off in Mods and choose Update now from the notice instead. Checks use the
latest non-prerelease `vX.Y.Z` release on `Teirdalin/TPL`.
`TPL-runtime.zip` and its `.sha256` file stage newer runtimes for the next
launch. The bootstrap is updated manually when a release requires it.

## Source and license

Source builds use the existing extracted VC100 x64 toolchain in `../toolchain`.
Build output, tests, local research artifacts and diagnostic plugins are
intentionally omitted from release packages.

TPL uses [JDL-1](LICENSE), a custom restricted license, not MIT or an
open-source license. Personal-use modifications are permitted; redistribution
or other modifications require the creator's permission. Plugin authors may
choose their own licenses and need not publish their source. See the separate
[plugin API permission](PLUGIN_API_PERMISSION.md) and
[third-party notices](THIRD_PARTY_NOTICES.md).
