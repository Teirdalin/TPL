# TPL mod developer guide

Start with a working plugin, then add one capability at a time. You do not need
to build the loader, install Python, link KenshiLib, or reconstruct engine
addresses to use TPL's public C API.

## Start here

1. [Quickstart](QUICKSTART.md): install the tools, generate a project, build,
   deploy, verify the first log, and package a mod.
2. [Lifecycle and ownership](LIFECYCLE.md): where work runs, what survives a
   load, and what the library does not promise.
3. [API reference](API_REFERENCE.md): every public foundation/UI/engine entry,
   required thread, errors, capacities and ownership.
4. [Recipes](RECIPES.md): settings, queued work, services, shared hooks and UI.
5. [Porting from RE_Kenshi](MIGRATION.md): source adaptation and missing APIs.
6. [Troubleshooting](TROUBLESHOOTING.md): symptoms, checks and actionable fixes.

Download [TPL-SDK-0.1.1.zip](https://github.com/Teirdalin/TPL/releases/download/v0.1.1/TPL-SDK-0.1.1.zip)
for a self-contained kit. The public repository contains the same authoring
tools and docs. Tests and private research logs stay out of the source release.

## Choose the right path

**Public C API plugins:** the starter uses Visual Studio 2022/v143, Release x64
and a private static C runtime. It talks through C function tables, fixed-width
values, opaque tokens and borrowed pointers. This path does not require the old
compiler or TPL import libraries. Its cross-compiler boundary is covered by a
VC100 host fixture, not a blanket approval for arbitrary game C++ calls.

**Native engine work:** raw Kenshi/MyGUI/Ogre C++ calls retain their exact-build
and compiler/layout/lifetime requirements. Use VC100 x64 `/MD` for reviewed
native C++ contracts. Do not cast a C API token to a game object or treat the
modern starter as a rebuilt KenshiLib SDK.

**FCS data only:** a `.mod` contains game data, not executable installer code.
Use the FCS normally; a native DLL is only needed for native behavior.

## What is ready

Logging, owner tokens, checked module/address tools, queued GUI jobs, frame
subscriptions, services, exclusive hooks and opt-in TPL shared hooks exist.
The UI service exposes a limited native-button/list surface. Seven reviewed
native addresses are present; world update/reset observation has passed one
scoped user playtest. These are different evidence levels, not full parity.

There is no general character/squad/inventory/navigation API, automatic save
serialization, native object-layout SDK, pre-menu startup or hot unloading.
The save-menu example now recognizes the reviewed two-column GamesList input
route, but confirmation and deletion still require a disposable-save live
test. Unchanged DLLs with unresolved native imports remain blocked rather than
guessed into use.

## Your license

Your plugin can be closed-source or use a license of your choice. JDL-1 is not
automatically applied to your work. The separate `PLUGIN_API_PERMISSION.md`
covers reuse of the API headers, starter and generator; keep its notices for
those portions. Choose your own plugin license before distributing a package.
