# RE_Kenshi to TPL migration compendium

Consolidated 2026-09-20 from the Project Cars, Freeloader, Nonlethal, Flowing
Hair, and Kenshi Comes Alive migration records. This is the canonical working
note for future ports. Recheck current TPL headers, installed binaries, source,
and build reports before reusing any address, count, or conclusion.

## What a migration actually is

An RE_Kenshi-to-TPL migration changes runtime ownership, not just filenames.
A real TPL-native plugin must have all of the following:

- a C `TPL_Start(const TPL_Host*)` boundary and a lowercase-schema `TPL.json`;
- no unresolved runtime dependency on `RE_Kenshi.dll` or `KenshiLib.dll`;
- an explicit replacement for every old logging, address, hook, lifecycle, and
  native game API dependency;
- reviewed ABI bindings for the exact game and engine images it supports;
- TPL-owned hook transactions with correct continuation and rollback behavior;
- packaging, deployment, and live acceptance evidence distinct from build tests.

Changing `RE_Kenshi.json` to `TPL.json`, adding `TPL_Start`, or replacing only
`AddHook` and `GetRealAddress` is not sufficient. KCA demonstrated the failure
directly: its first manifest-level port still imported 216 symbols from
`KenshiLib.dll` and failed to load with Windows error 126.

TPL also has an experimental legacy fallback for narrowly eligible unchanged
DLLs. That path is useful for testing self-contained ordinary plugins, but it is
not a universal converter. Preload plugins cannot be silently run later, and
general KenshiLib-backed plugins still require either a native port or newly
implemented and validated compatibility coverage.

## Preserve the known-good state first

Before editing anything, retain a recoverable baseline:

- the accepted plugin DLL and its exact matching PDB;
- `RE_Kenshi.json`, loader configuration, mod descriptor, settings, and assets;
- hashes for the game executable, engine DLLs, plugin DLLs, and reference data;
- the compiler/toolset, link inputs, old RVA data, and relevant build logs;
- a disposable save and a feature-specific gameplay checklist.

Do not use migration as an excuse to redesign working gameplay. Keep policy,
configuration keys, content IDs, save keys, and FCS data stable so regressions
can be attributed to the runtime transition. Nonlethal kept its damage policy
and 20,150 policy checks unchanged; Flowing Hair retained its solver, rendering,
weather, wetness, and visibility behavior; Project Cars retained its FCS and
vehicle-save identities.

Inventory before deciding on an architecture. Inspect:

- ordinary and delay-load PE imports;
- dynamic `LoadLibrary` and `GetProcAddress` paths;
- every game method, global, constructor, vtable call, allocator boundary, and
  compiler thunk;
- global/static objects whose constructors may call game functions;
- hook targets, detour signatures, original/continuation usage, and expected
  target bytes;
- native layouts, `sizeof`/`offsetof` assumptions, object lifetimes, and thread
  or world-phase assumptions;
- manifest load phase, third-party dependencies, and startup order.

An import table is essential evidence, but not complete evidence. Flowing Hair
also resolved Ogre and ParticleUniverse functions dynamically. Removing named
imports does not repair a wrong object layout, ownership rule, or lifecycle.

## Choose the least complex valid route

Use a normal TPL plugin when the feature can live entirely behind TPL's public
C/POD contracts. Start from the generated TPL starter, connect to the host, and
move independent logic across one feature at a time.

Use TPL's experimental legacy fallback only when an unchanged ordinary x64 DLL
is self-contained under the documented inspection rules. Treat successful
`startPlugin` return as loader evidence, not proof that hooks or gameplay work.
Keep the known-good installation until the exact binary has been exercised.

Use a bootstrap/native pair when retained code depends on Kenshi's exact VC++
2010 x64 C++ ABI or calls game functions during CRT/global construction:

1. `Mod.dll` is the small TPL-facing bootstrap.
2. `Mod.Native.dll` contains the retained native implementation.
3. The bootstrap validates the host, thread, capabilities, module identities,
   address ranges, and complete binding profile.
4. It prepares a versioned POD table containing callbacks, binding identity,
   and resolved targets before loading the native DLL.
5. A narrowly scoped custom native entry validates and copies that table before
   invoking `_DllMainCRTStartup`.
6. After `LoadLibrary` returns, a separate native-start export prepares features
   and hooks; the bootstrap commits hooks only after everything succeeds.

The pre-CRT path must not allocate, mutate the engine, install hooks, or load
additional providers. Direct or mismatched loading of the inner DLL must fail
before native constructors execute. This pattern was necessary in Project Cars,
Nonlethal, Flowing Hair, and KCA; it is not necessary for every plugin.

Keep the two ABI boundaries distinct. The public TPL edge is C linkage with
versioned POD structures. Retained game-facing code may still require VC100 x64,
`/MD`, C++03-compatible layouts, and exact allocator/calling conventions. A
modern compiler consuming `tpl.h` does not make Kenshi STL objects portable.
Do not pass C++ containers, exceptions, or ownership across the public boundary.

## Build-specific binding correspondence

The old loader environment and a TPL-only launch may execute different images.
The recorded migrations found:

- old build-only Steam 1.0.65 SHA-256:
  `504b362cde850d56afb1cea6f5b7b0ee014d9dd7b47e188599d91c804502cd3e`;
- installed Steam 1.0.68 SHA-256:
  `a596ab4e407c67b58599c54ffb32dc1bf2b64510cdebd3fa9359ef05a576aeb1`.

These values are historical evidence, not a permanent compatibility promise.
Some earlier documentation mislabeled the newer image. Hash the executable that
will actually run. Never reuse the old RVA table by changing only the accepted
hash, and never patch the installed executable merely to fit old addresses.

Keep old images, import-library metadata, and RVA tables as build-only reference
inputs. For each required native symbol:

1. establish the old symbol/RVA and exact ABI;
2. compare complete function bodies with relocations normalized;
3. preserve object offsets, constants, instruction semantics, and internal flow;
4. use callers, neighboring functions, strings, RTTI, vtables, or global access
   patterns to disambiguate small or common functions;
5. review changed bodies and switch/jump-table functions explicitly;
6. classify data exports as embedded objects versus pointer variables;
7. reject missing, ambiguous, or unsupported matches;
8. emit the current module hash, RVA, symbol, ABI, and expected entry bytes;
9. give the profile a deterministic identity and verify it at runtime.

Short byte-prefix matching is not enough. Nonlethal's five-byte tooltip getter
had many identical candidates and required neighbor evidence. KCA's switch
functions required complete body and jump-table review. Changed OptionsWindow
creation was handled as an explicit ABI review, not declared byte-identical.

Generated local import slots and tail-call thunks can satisfy retained native
symbol names without importing the old runtime. Local adapters can route logging
and hook creation into TPL and resolve only known profiled compiler thunks. An
unknown thunk or target must fail rather than be guessed.

Removing an old runtime dependency does not erase provenance. Plugin-derived
headers, export metadata, and RVA-derived profiles retain their applicable
licenses and notices. Keep them separate from TPLLib's independently developed
engine service; do not present a plugin-local bridge as general SDK parity.

## Hook transactions and lifecycle

TPL plugin startup occurs at the main-menu checkpoint on the GUI thread.
`TPL_Tick(float)` and TPL frame subscriptions are GUI callbacks, not substitutes
for simulation updates, world creation, save-loaded events, or renderer timing.
Preserve the verified native phase for each feature and clear/reacquire state at
the correct world transitions.

For hooks:

1. resolve and validate every target before changing code;
2. preserve the exact native signature and call phase;
3. open one named TPL owner for the plugin;
4. create every hook disabled with reviewed expected bytes;
5. retain every token and continuation, including partial-failure handles;
6. enable the complete batch only after native initialization succeeds;
7. activate gameplay only after all hooks are enabled;
8. on failure, deactivate gameplay and disable the batch in reverse order;
9. retain owners, callback code, continuations, and DLLs for process lifetime.

A shared-hook continuation calls the next enabled chain member or original. A
detour calling its own target can recurse. Disabling a hook does not join calls
already in flight, so failure is not permission to unload code immediately.
TPL shared hooks coordinate with other TPL owners, not with an unrelated hook
manager. A plugin may deliberately reject mixed-loader launches until that exact
combination has been validated.

Test hooks both individually and as one combined layout. KCA exposed why: two
distinct targets were only 32 bytes apart while TPL's verification reservation
covered a wider window. The first conflict rolled back the whole 35-hook batch;
later errors were a cascade, not 35 independent failures. The repair moved the
event hook to a verified callee and changed the receiver/signature accordingly.
Never move a detour to another address without revalidating its ABI.

## Packaging and deployment

The TPL manifest beside the outer DLL uses lowercase keys:

```json
{"plugins":[{"name":"Example Mod","dll":"ExampleMod.dll"}]}
```

Ship every runtime component together: outer and native DLLs where applicable,
`TPL.json`, configuration, existing `.mod`/assets, and relevant notices. Keep
matching PDBs internally for crash investigation. Update build, deploy, and ZIP
lists together; an omitted inner DLL makes a correct bootstrap unusable.

Deployment must be recoverable:

- require TPL installed/enabled and Kenshi closed;
- require a complete passing build record and supported installed hashes;
- stage and hash-check the payload before replacement;
- back up every exact file to be replaced or retired;
- retire only this mod's `RE_Kenshi.json`, never other mods or loaders;
- preserve settings, FCS data, assets, saves, and enabled-mod order;
- verify installed files against the tested build;
- restart the process after replacement or failed initialization.

Rollback must restore a matched DLL/manifest pair. Restoring an old DLL while
leaving `TPL.json` active gives it the wrong entrypoint contract. If Kenshi opens
during deployment, stop rather than copying over potentially loaded DLLs.

## Evidence levels and the definition of done

Keep five evidence layers separate:

1. Source/ABI review establishes layouts, signatures, compiler boundaries,
   ownership, image correspondence, and provenance.
2. Build/offline tests establish independent policy behavior, binding gates,
   hook rollback, load rejection, and reproducible output.
3. Artifact inspection establishes architecture, imports, exports, manifests,
   and exact hashes.
4. Deployment evidence establishes backups and installed/tested hash identity.
5. Live acceptance establishes actual behavior in the game.

A clean build is not deployment. A mock-host start is not a game start. A DLL
load is not successful hook activation. A successful hook transaction is not
gameplay acceptance. The final checklist must exercise the plugin's real feature
paths, old saves, pause/resume where relevant, world/save transitions, return to
menu, dense or stressed scenarios, and coexistence with plugins sharing targets.

Recorded case boundaries:

- Project Cars: 214 native bindings in the latest cited build; extensive vehicle
  and bridge checks passed. The user reported the migration and gate repair
  working in game, but that does not validate every vehicle/system scenario.
- Freeloader: 21 reviewed bindings and 182,344 streaming-policy checks; deployed,
  with live travel, save/reload, RAM, and coexistence acceptance still separate.
- Nonlethal: 21 reviewed bindings, 20,150 policy checks, 240 hook-transaction
  checks, and 49 actual-DLL mock startup checks; live combat/UI acceptance was
  still pending in the recorded migration.
- Flowing Hair: 32 game bindings plus two exported hook targets, solver/mesh,
  3,078 hook, and 23 load checks; live rendering, motion, weather, wetness,
  transitions, and coexistence remained pending.
- KCA: final profile had 212 bindings and 35 non-overlapping hooks after the
  collision repair; build and deployment were confirmed, but the final NPC TALK
  and broader gameplay check was interrupted and must not be reported complete.

Counts are build-specific evidence, not general SDK coverage.

## Automated converter

TPL now includes `tools/convert-rekenshi-project.py`. It performs bounded,
read-only inspection of a source tree and writes a new migration kit:

```powershell
python tools/convert-rekenshi-project.py `
  C:\path\to\LegacyMod `
  --output C:\path\to\LegacyMod-TPL-Migration
```

The output contains:

- `migration-report.json` with manifests, source signals, declared DLLs, PE
  imports/exports when binaries are available, blockers, and evidence claims;
- `MIGRATION_PLAN.md` with the selected route and acceptance gates;
- `starter/`, a complete minimal TPL plugin project using the current SDK.

The converter never edits the source tree and refuses to overwrite its output.
It classifies projects as a simple source-port candidate, legacy-fallback
candidate, early-lifecycle requirement, native-bridge requirement, or manual
inventory requirement. It does not fabricate binding addresses, translate C++
layouts, copy gameplay code, deploy files, launch Kenshi, or claim validation.

For native-bridge projects, use the report as the front end to a reviewed binding
pipeline. The existing Project Cars, Nonlethal, Flowing Hair, and KCA generators
remain project-specific references, not universal address generators.

## Compact migration sequence

1. Freeze and hash the known-good state.
2. Run the converter and manually complete anything its bounded scan cannot see.
3. Build the generated starter and confirm its startup and first-tick logs.
4. Choose normal C API, eligible legacy fallback, or reviewed native bridge.
5. Pin the actual runtime images and complete zero-pending binding review.
6. Preserve behavior while replacing loader/address/hook ownership.
7. Test pre-CRT rejection, hook creation/activation failure, combined target
   spacing, lifecycle transitions, and final imports/exports.
8. Package both DLL layers and all data; create a verified recovery point.
9. Deploy with Kenshi closed and confirm exact installed hashes.
10. Complete feature-specific live acceptance before calling the port done.

## Source records

- `README.md`: Project Cars migration and general ABI/bootstrap lessons.
- `TPL_MIGRATION_NOTES.md`: general migration and Freeloader reference.
- `NONLETHAL_MIGRATION_NOTES.md`: small-function/global evidence and hook rollback.
- `FLOWING_HAIR_TPL_MIGRATION.md`: dynamic engine targets and render/simulation timing.
- `KENSHI_COMES_ALIVE_MIGRATION_NOTES.md`: dependency failure and adjacent-hook conflict.
- `MIGRATION.md`: current public TPL migration contract.
- The full TPL source tree also contains `docs/LEGACY_COMPATIBILITY.md` for the
  unchanged-DLL fallback and `docs/PARITY_AUTOMATION.md` for reviewed bindings.
