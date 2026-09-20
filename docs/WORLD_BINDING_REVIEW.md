# GameWorld binding review

Binary/ABI review, 2026-09-20. In-game acceptance is still pending.
Reviewer: Codex, independent analysis of the installed executable.

Steam 1.0.65 x64 SHA256:
`a596ab4e407c67b58599c54ffb32dc1bf2b64510cdebd3fa9359ef05a576aeb1`.
All addresses below are RVAs, not process addresses. Source: PE exception
records, RTTI, disassembly and callers in that executable. KenshiLib import
names supplied declaration hints only; no upstream implementation, class
headers or address tables were copied. This is not a formal clean-room claim.

## Identity and ABI

- RTTI `.?AVGameWorld@@`, complete-object vtable `0x1722608`, slot 0:
  thunk `0x26724` jumps to `0x788a00`. The constructor at `0x874e70` and
  destructor at `0x86d650` install this same vtable.
- The static initializer at `0x15d5bc0` passes executable-base + `0x2134110`
  to the constructor through thunk `0x26d37`.
- The frame caller at `0x82b8c1` passes that same object in RCX; its preceding
  instruction loads a float into XMM1. Call `0x82b8c8` targets thunk `0x26724`.
  Body `0x788a00` saves XMM1 into XMM6 and RCX into RSI, scales the timestep,
  and uses the rendering camera. The SDK declaration hint agrees:
  `void GameWorld::mainLoop_GPUSensitiveStuff(float)`.
- Reset body `0x36cb80` directly references `Reset game` at `0x36cbd1`,
  keeps RCX in RSI, sets and clears an internal reset flag, and returns void.
  Calls at `0x373fc9`, `0x378b3c` and `0x47c428` use thunk `0x41c59` and
  pass the same static GameWorld address in RCX. The last caller is the
  load/new/import controller, before its branch-specific work. The matching
  public declaration hint is `void GameWorld::resetGame()`.
- Destructor body `0x86d650` reinstalls the GameWorld vtable, tears down owned
  subobjects and tail-calls its Ogre base destructor. Its declaration hint is
  `GameWorld::~GameWorld()`, not a scalar/vector deleting destructor. The
  reviewed native signature is `void(GameWorld*)`, with no deletion flag.

The recipe pins complete function hashes for all three bodies and additionally
pins the reset string anchor. The vtable slot points to a thunk, not the body;
the generator's raw slot selector is intentionally not used for this family.
Generated names `updateGraphics` and `destruct` are TPL aliases for these bodies.
Signatures use MSVC x64 with an explicit opaque `this`; float is the second
argument in XMM1. No layout or allocation size is exposed.

## Lifetime and phase restrictions

These are observation hook targets, not permission to drive the game loop,
reset a world or destroy game objects from a plugin. Call the continuation
exactly once with the unchanged arguments. Do not suppress native exceptions.
Resolve all targets before enabling any hook. Shared TPL hooks may cooperate;
sharing the same target with RE_Kenshi remains unverified.

A pointer observed during a native call is borrowed for that call only. The
static object's identity can remain unchanged while all its contents are reset.
Invalidate any future game-object handles at reset entry, not after reset.
Destructor entry ends usable object lifetime. Never dereference retained
character, collection or world pointers across these boundaries.

The graphics update is not proof that a save is loaded or simulation is running.
Reset completion is not load completion, save completion or load success.
This review does not establish those events, pause-state getters or a general
thread-safe world API. The observer records unexpected callback threads and
does not touch GUI/game fields or log from native detours, including shutdown.

## Reproduction

Run `tools/run-parity.ps1` against the pinned executable. Reviewed recipes are
in `bindings/reviewed/world.json`. The binary index provides complete function
hashes and direct references; `tools/inspect-native-ui.py --function <rva>` can
emit bounded disassembly of the bodies and their callers. Tool-generated
wrappers remain marked binary-reviewed, never runtime-verified by generation.
