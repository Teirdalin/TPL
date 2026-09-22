# KEP compatibility investigation

Status: implemented in the 0.1.4 test build; offline validation and local live
startup passed. Options/save/gameplay acceptance remains pending. No KEP profile
is included in TPL 0.1.3.

## Supported capture

The local read-only capture is `build/kep-all-live-hooks.json`, collected on
2026-09-21 from 407 bindings with no read errors. It identifies:

- Alternate Kenshi SHA-256:
  `504B362CDE850D56AFB1CEA6F5B7B0EE014D9DD7B47E188599D91C804502CD3E`.
- `kep-core-preload-x64.dll` SHA-256:
  `324A5C2626A813C03D95A507F770F1DFC1AEC62C5DDE6B08CFB494E582F25A9A`.
- `KenshiExtensionPlugin.dll` SHA-256:
  `74C028AE13C1D9584857E1F5C0945782C5C40989F9BBB4CAA07541A1724C2E59`.

Six build-pair profiles cover OptionsWindow create/saveOptions, SaveManager
importGame, MedicalSystem addWound, CharStats _NV_init, and RootObjectFactory
createRandomCharacter. Only the exact module/target identities and entry bytes
are accepted; KEP is neither bundled nor started by these profiles.

The observed Options chain is KenshiZoneOpt -> KEP core -> original native code.
The existing foreign backend retains a 14-byte absolute jump to the preceding
relay immediately after its outer relay. Validation now follows up to four
distinct, individually reviewed destinations, rejects cycles and unknown links,
and requires the terminal trampoline to return to the correct original body.
TPL still wraps only the outer detour entry; existing native patches and foreign
trampolines remain untouched. Every recorded link and inner detour is rechecked
before routing changes and verified-state reports. Nested modules are pinned.

## Validation

- 176 new native-call checks across both terminal layouts and both foreign
  orders: two TPL owners, two foreign hooks, original body exactly once, rollback,
  absent modules, wrong hashes/bytes, corrupted continuations, cycles, depth cap,
  and mutations before and after activation.
- Existing core/shared/host and MinHook rollback suites: 489 checks passed.
- All 13 compiled profiles matched read-only captures and exact disk binaries.
- No installed plugin DLL, manifest, setting, or save was modified for capture.

Local live startup passed on 2026-09-21: KCA registered 37 shared hooks,
Nonlethal 6, Flowing Hair 6, and Freeloader 3. The running process contained both
KEP modules and KenshiZoneOpt. The log and module evidence are retained alongside
the first installed 0.1.4 test package.

Remaining live checklist: KEP and KCA Options controls render and save; a
disposable save loads/imports normally. Startup alone does not establish those
gameplay outcomes.

## Observed failure

The supplied `TPL (4).log` confirms TPL 0.1.3 and KCA's first rejected hook:

- Target: alternate Kenshi executable, RVA `0x3f0120` (OptionsWindow::create).
- Observed chain: private relay, then `kep-core-x64.dll+0x1260`.
- Result: TPLLIB_CONFLICT; KCA initialization rolls back.

This identifies an observed destination, not the binary fingerprint, original
trampoline layout, or the complete set of overlapping hooks.

## Independent interoperability work

KEP v0.17.2's public config_manager.cpp registers hooks for OptionsWindow::create,
OptionsWindow::saveOptions, and InputHandler::loadConfig. This is a reference
for targets to inspect, not evidence that the player runs v0.17.2.
Source: https://github.com/Lucius64/KenshiExtensionPlugin/blob/v0.17.2/kep-core/src/config_manager.cpp

Do not copy KEP implementation into TPL. Extend the existing independently
implemented foreign-profile mechanism using exact binary identity and observed
hook-chain evidence. Preserve KEP's detour and original-call path.

## Older Player Build

1. Obtain Hobbit's installed kep-core-x64.dll and identify its SHA-256, or retest
   with the captured current Workshop build.
2. Capture all changed KCA hook targets using inspect-live-shared-hooks.py while
   Kenshi is at the main menu. Record executable identity, detour RVAs, disk/live
   entry bytes, relay bytes, and original trampoline return addresses.
3. Review all overlaps, including Options create/save, not just the first failure.
4. Add only exact-build profiles whose layouts pass existing validation. Keep
   unknown builds, altered detours, and unsupported chains rejected.
5. Test original-call routing, both owners' behavior, rollback, and rejection of
   mismatched hashes, bytes, destinations, and trampoline layouts.
6. Live-test KCA initialization and both plugins' Options controls/save behavior.
   Offline tests alone do not establish coexistence.

The locally installed Workshop core is named kep-core-preload-x64.dll, unlike
the player's logged kep-core-x64.dll. Do not substitute it without evidence.
