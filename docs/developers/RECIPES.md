# Practical recipes

Use the generated starter as the entry point. These recipes use the public C
API only; they do not require KenshiLib or engine object layouts. Every example
still needs an in-game test of its intended behavior before release.

## Read settings next to your DLL

The starter locates its own module with `GetModuleHandleExW`, gets that module's
filename, and reads `plugin.cfg` beside it. This works independently of the
game's working directory and the folder name the player chose.

```ini
[Plugin]
log_first_tick=1
```

`GetPrivateProfileIntW` reads this deliberately simple ASCII INI setting at
startup. Add your own defaults and range checks; do not use this recipe as a
general UTF-8 configuration parser. For richer settings, keep a parser inside
your own DLL and publish the accepted types, limits and restart requirements.

TPL's Config button is a bounded text editor, not a schema editor or reload
notification. It discovers supported `.cfg` files beside the mod. Editing a
file does not magically reload it in your plugin. The starter needs a restart.
Deployment deliberately preserves the installed config rather than new defaults.

## Queue work onto the GUI thread

Copy `examples/sdk/examples.hpp` and `jobs.cpp` into your project, then add the
`.cpp` to the Visual Studio project using **Add > Existing Item**. After a
successful `plugin.connect` in `TPL_Start`:

```cpp
#include "examples.hpp"
// Inside your startup body:
TPLLib_Token job=0;
if(!plugin.check(tpl_examples::queueHello(plugin,&job),"MyMod: queue hello"))
    return 1;
```

On a later GUI frame it logs `SDK example: queued GUI callback` once. This is a
small dispatch example, not a reason to queue logging in production: logging
already permits workers. Replace the callback with short, reviewed GUI work.
The example uses the borrowed process-lifetime API table as context; no stack
address escapes. If called from a worker, connect first and keep the plugin
helper/owner unchanged until that worker finishes.

For real worker results, use plugin-owned persistent storage and synchronization.
Keep it alive until completion is known, and tag results with a world epoch so
results for a previous save/reset can be discarded. `cancel_job` is not a join.

## Offer a service to another plugin

Copy `examples.hpp` and `services.cpp` and add the `.cpp` to the project. Call
`tpl_examples::publishCounter(plugin)` once on the GUI thread. The service is
named `example.counter`, version 1. Change that name to your own namespace in
both provider and consumer before using it in a real mod.

The compiled example demonstrates a small `CounterAPI` with fixed-width
`size`/`version` and a function returning a status through C-compatible values.
The provider uses static storage and checks the GUI thread. It is not a game
statistic or a persistent counter. It resets when the process restarts.

In a consumer with the same header and `services.cpp`:

```cpp
const tpl_examples::CounterAPI* counter=0;
TPLLib_Status status=tpl_examples::findCounter(plugin,&counter);
if(status==TPLLIB_OK) {
    uint64_t value=0;
    plugin.check(counter->next(&value),"MyMod: counter call");
} else if(status!=TPLLIB_NOT_FOUND) {
    plugin.check(status,"MyMod: counter service");
}
```

An absent optional provider is normal. Retry at a bounded later checkpoint or
disable that feature; do not flood logs every frame. Never assume alphabetical
load order resolves all dependencies. Services cannot be replaced/unpublished
in ABI 1, and an owner with a published service cannot close.

## Request a built-in service

```cpp
#include "tpllib_ui.h"
const TPLLib_UI_API* ui=0;
TPLLib_Status status=plugin.service(TPLLIB_UI_SERVICE,TPLLIB_UI_VERSION,&ui);
if(!plugin.check(status,"MyMod: native UI service")) return 1;
if(ui->size<sizeof(*ui) || ui->version!=TPLLIB_UI_VERSION) return 1;
```

Run this inside a GUI-thread function after connecting. Follow each service's
own size/version checks even though the registry checks the published size.
Table negotiation does not make every desired UI feature available.

For a native button: open a UI session, uniquely locate a verified parent and
style button, inspect their geometry, create the button, and retain its token
and callback context. On native destruction the token becomes stale. Reacquire
on the GUI thread and avoid repeated creation in every tick. Close sessions
explicitly outside callbacks. Check every result and preserve other mods' edits.

Do not guess widget names from visible captions or synthesize arbitrary key
events. The save-menu route is limited to the reviewed two-column GamesList
shape and still requires live confirmation/deletion acceptance before release.
See the exact [UI contract](API_REFERENCE.md#ui-service-tplui-version-1).

## Install a reviewed shared hook

This is an advanced process, not a generic character-hook snippet:

1. Establish the exact supported game hash, native signature, valid call phase
   and lifetime. Raw native C++ contracts require the legacy ABI.
2. Resolve **all** required addresses and reviewed original 32-byte entries
   before patching anything. Never use current unknown bytes as a fingerprint.
3. Check `TPLLIB_HAS_SHARED_HOOKS(plugin.api())`.
4. Create disabled hooks with `hook_create_shared`. Store tokens and typed
   continuations in process-lifetime storage.
5. Enable on the GUI thread and check every status. Roll back all attempted
   enables on failure, observing the documented `BUSY` recovery contract.
6. In a detour, normally call its continuation once. Catch your own exceptions;
   do not call the hooked address recursively or unload callback code.

TPL chains do not merge with unknown patches. For a reviewed MinHook-style
foreign chain, check `TPLLIB_HAS_FOREIGN_HOOK_CHAINS` and use
`hook_create_shared_foreign` with the exact module hash, detour RVA and detour
fingerprint. A mismatch is a reason to decline the feature, never to overwrite
the other writer. A valid address alone does not prove that calling it is safe.

## Release checklist

- Test the exact deployed DLL, not merely the newest file in your build folder.
- Confirm disabled-at-launch behavior and preserve the player's config.
- Test save/load/reset where your feature retains native state.
- Check dependencies, required game build and optional-provider behavior.
- Keep the exact PDB privately; remove secrets and verbose per-frame logs.
- Choose your plugin license, bump its version, run `package.ps1`, and test an
  extraction of that ZIP in a temporary directory before sharing.
- Link players to TPL's release. Do not bundle the loader or game binaries.
