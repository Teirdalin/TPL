# KEP compatibility investigation

Status: the Workshop profiles shipped in 0.1.4; offline validation and local live
startup passed. Version 0.1.5 adds the separate KEP 0.17.1 manual-package builds
described below. Their offline validation passed; live acceptance remains pending.
Options/save/gameplay acceptance is still separate from startup validation.

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

## KEP 0.17.1 Manual Package

The supplied 0.17.1 archive contains different binaries, not renamed Workshop
DLLs. Version 0.1.5 retains the Workshop profiles and adds these exact identities:

- `kep-core-x64.dll`:
  `A387B5BDA59F7CDE7346526E72C6881F2CB82C0913F178AB0E2596F868E98CD0`.
- `KenshiExtensionPlugin.dll`:
  `01A41C8B9FD7A286FEBCEC8361C8929F388F0FC3F94EEF690739F4A38C1C0A0B`.

The same six native targets are covered for the alternate executable identified
above. Binary inspection verifies the imported target passed to GetRealAddress,
the detour and original-pointer slot passed to AddHook, and the exact entry bytes.
Core detours remain at 0x1260/0x12d0. Plugin detours are importGame 0x45030,
createRandomCharacter 0x46fb0, CharStats init 0x42950, and addWound 0xef30.

Profile selection now checks the complete binary identity before choosing among
releases with an identical module name and RVA. A nonmatching candidate cannot
shadow a later valid build; no matching build still fails without changing hooks.
This matters for addWound, whose name, RVA, and initial bytes match both releases.

Validation: six registration-site audits and 376 image-only checks across both
KEP releases and both supported trampoline layouts passed. The images were mapped
in isolated test processes without DLL initialization, import resolution, or
execution of game/KEP code. Tests create and roll back synthetic patches against
those images; they are not live captures. Shared/nested regression fixtures also
exercise same-name/RVA candidates, unknown hashes, hook ordering, and rollback.

Local evidence is in `build/KEP-0.17.1-compat/binary-evidence.json`,
`build/kep-0171-image-tests.log`, and `build/kep-workshop-image-tests.log`.
The supplied KEP binaries are never included in TPL packages.

Remaining player test: fully restart with TPL 0.1.5, confirm KCA initializes,
open and save both plugins' Options controls, then load a disposable save.
If rejected, collect the full new TPL.log and inspect the actual chain. Unknown
binary identities, altered entry bytes, and unreviewed chain layouts stay blocked.
