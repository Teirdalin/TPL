# Public API reference (ABI 1)

Headers are the source of truth: `tpl.h`, `tpllib.h`, `tpllib_ui.h` and
`tpllib_engine.h`. All public tables/structs use packing 8. Initialize output
structs to zero and set `size=sizeof(value)` before calls that accept a size.
Use `TPL_HOST_HAS_TPLLIB` and `TPLLIB_HAS_SHARED_HOOKS` for optional tails.
Never index past the host's reported table size.

## Host and C++ helper

`TPL_Host` supplies `size`, `abi_version`, a borrowed wide `game_directory`,
`log(const char*)`, and optional `get_tpllib(abi_version)`. Request ABI 1; an
unsupported version returns null. Do not link a private TPLLib implementation.

`tpl_plugin.hpp` is an optional C++03 header-only helper, not another runtime:

- `Plugin::connect(host, name, requiredCapabilities)` checks host/API size,
  versions, capabilities and GUI thread, then opens an owner. Default required
  capabilities are logging, dispatch and services. A second connection returns
  `CONFLICT`; failed connection leaves no owner.
- `api()` returns the borrowed foundation table, or null before connection.
- `owner()` gives the owner token; `game_directory()` gives the borrowed path.
- `has(bits)` requires every requested capability bit.
- `log(text)` writes through the host. Include your plugin name in messages.
- `check(status, operation)` returns true for `OK`; otherwise logs the operation
  and status description when available. It does not recover or retry.
- `service<Table>(name, version, &table)` performs a size-checked service query
  and clears the result on failure.
- `close()` explicitly closes the owner and clears the helper only on success.
  It must run on the GUI thread. No destructor closes anything automatically.

## Status codes

- `OK` (0): operation succeeded under its documented contract.
- `INVALID` (1): null/bad argument, size, range or malformed input.
- `NOT_READY` (2): foundation not initialized.
- `WRONG_THREAD` (3): call requires the GUI thread.
- `NOT_FOUND` (4): owner/module/job/service/target not found, as appropriate.
- `VERSION_MISMATCH` (5): requested ABI, service version or binary hash differs.
- `AMBIGUOUS` (6): signature/widget lookup found multiple valid candidates.
- `UNREADABLE` (7): requested memory could not be read completely.
- `CONFLICT` (8): ownership, duplicate identity, incompatible patch or other writer.
- `LIMIT` (9): bounded registry/queue capacity reached.
- `BACKEND_ERROR` (10): native backend operation failed.
- `CALLBACK_ERROR` (11): callback/log operation failed through its supported path.
- `BUSY` (12): resource is still in use, or hook verification needs recovery.

Check the result of every call. `BUSY` on a hook transition is not a generic
permission to retry blindly; see the hook recovery contract below.

## Foundation: identity and ownership

These calls are GUI-thread-only unless marked **any thread**.

- `status_text(status)` (**any thread**): borrowed diagnostic text; unknown
  values return an unknown-status string.
- `is_main_thread()` (**any thread**): nonzero only on the registered GUI thread.
- `owner_open(name, &owner)`: create an owner token. Keep the token, not the name,
  as the resource identity. Names must be 1..95 printable ASCII characters
  without spaces; duplicate active owner names conflict.
- `owner_close(owner)`: cancel its queued jobs/frame subscriptions if permitted;
  `BUSY` for owned hooks/services or an executing callback. No implicit unhook.
- `stats(&value)`: snapshot owner/hook/job/subscription/service counts, frame
  count and caught callback failures. Set `TPLLib_Stats.size` first.
- `log(message)` (**any thread**): synchronous UTF-8/ASCII text logging. Data is
  borrowed for the call only. Avoid secrets, save contents and per-frame spam.

## Foundation: modules and addresses

The following operations permit **any thread**. That does not grant safe
concurrent access to game-owned data.

- `module_info(moduleName, &info)`: inspect an already-loaded x64 PE module;
  null name selects the executable. Returns base, image size, timestamp, machine
  and disk SHA256. This does not pin lifetime by itself.
- `read_memory(source, destination, bytes)`: guarded process read, 1 byte to
  16 MiB; not a write API and not proof that an object is valid.
- `resolve_rva(moduleName, sha256, rva, expected, mask, count, &address)`:
  exact disk-hash gate plus checked in-image address/bytes. It does not require
  executable memory; hook creation checks that separately. Patterns are
  1..256 bytes, with at least one significant mask bit. Success pins the module.
- `find_unique(moduleName, sha256, bytes, mask, count, &address)`: search
  executable PE sections for exactly one masked match; absence/ambiguity fails.
  Success pins the module. A pattern does not establish ABI or object lifetime.
- Address outputs are cleared on failure. Do expensive hashing/scans during
  setup, not every frame. Never fabricate an expected hash from an unsupported
  executable simply to make a check pass.

## Foundation: hooks

- `hook_create(owner, target, expected32, detour, &original, &hook)`: create an
  exclusive, initially disabled hook with 32 reviewed original bytes. Target
  and detour must be executable; conflicting/overlapping entries fail. Returned
  original/trampoline and code modules remain retained for process lifetime.
- `hook_create_shared(owner, target, expected32, detour, &continuation, &hook)`:
  optional tail; check its macro/capability first. Creates a disabled member of
  a TPL chain. Pass reviewed original bytes even if TPL already patches it.
  Exclusive/shared modes cannot mix. Duplicate detours in a chain fail.
- `hook_enable(owner, hook, enabled)`: enabled must be 0 or 1. Only the owner
  can change a hook. Disabling is not a join. Shared order is creation order,
  newest first; re-enabling does not reorder the chain.
- `hook_state(owner, hook, &state)`: optional tail. Reports logical enabled,
  shared, physical target patch, byte verification, members and enabled members.
  It can return `CONFLICT` while still reporting useful state; initialize size.

An exclusive enable returning `BUSY` can leave the physical hook active but
unverified. A shared enable returning `BUSY` may leave the target patched while
the requested member remains disabled. Retry disable on the GUI thread and
inspect state. An unknown external patch must not be overwritten. Partial
multi-hook setup must explicitly disable all created/attempted members.

There is no destroy-hook API, no public multi-target atomic transaction and
no live unload. All shared/exclusive members consume the same 256 slots.

## Foundation: jobs and frames

- `post(owner, callback, context, &job)` (**any thread**): enqueue a one-shot
  GUI callback. Context must remain valid until completion/cancellation is
  established. The callback must catch its own exceptions.
- `cancel_job(owner, job)` (**any thread**): cancel a pending owned job.
  `NOT_FOUND` does not establish that the callback never started. No join.
- `subscribe_frame(owner, callback, context, &subscription)`: receive GUI-frame
  callbacks with sanitized dt. Context survives until unsubscribe and any
  executing callback finish. Subscription order is not a dependency contract.
- `unsubscribe_frame(owner, subscription)`: stop future callbacks. Respect
  ownership and in-flight lifetime; do not unload callback code.

Capacities: 128 concurrent owners, 256 hook slots, 1,024 queued jobs, 256 frame
subscriptions, 128 services. At most 128 queued jobs run per GUI frame. Tokens
are not recycled. Work queued by a callback waits for a subsequent frame.

## Foundation: services

- `publish_service(owner, name, version, table, bytes)`: register a borrowed,
  immutable-by-contract function table for process lifetime. Its storage and
  implementation must stay loaded. Duplicate name/version ownership conflicts
  are not a replacement mechanism. Names use the same 1..95 printable,
  non-space ASCII rule as owners; version and byte count must be nonzero.
- `query_service(name, version, minimumBytes, &table)`: request an exact version
  and sufficient table size. Handle absent providers; plugin ordering does not
  guarantee every optional service already exists when you start.

Document thread, pointer ownership, buffer size, error and lifetime rules for
your own service. Do not pass STL objects or exceptions across plugin compilers.

## UI service: `tpl.ui`, version 1

Query `sizeof(TPLLib_UI_API)` on the GUI thread. **All UI operations require
that thread.** UI sessions are not foundation owners; tokens are not raw pointers.

- `session_open(name, &session)`: start ownership of additions/reversible edits.
- `session_close(session)`: restore unchanged leased properties and remove
  additions. Other writers can cause conflicts; callback-time close is busy.
- `find(parent, nameSuffix, &widget)`: bounded unique suffix lookup; parent zero
  searches from GUI roots. Do not use translated captions as identity.
- `info(widget, &value)`: geometry, parent token, kind, effective flags and list
  selection/count. Set size first; stale handles fail after native destruction.
- `button_create(session, parent, style, &rect, caption, click, context, &widget)`:
  create a native-style button using a supported existing button as style.
  The session owns it; preserve the callback and context lifetime.
- `button_set(session, widget, &rect, caption, enabled)`: lease/reversibly edit
  a button. Native delegates remain intact. Fixed geometry needs updates when
  its parent resizes; the previous alignment is restored on session close.
- `key_event(session, widget, key)`: restricted dispatch during one real owned
  button click, once, to a visible/enabled MultiListBox in the same root with a
  native key callback. Version 1 accepts Delete (211) only. Not a global input API.

Kinds: `TPLLIB_UI_BUTTON`, `TPLLIB_UI_LIST`. Flags: `VISIBLE`, `ENABLED`,
`KEY_HANDLER`. Inspect selected/count together before treating selection as
valid. There are 32 sessions and 2,048 tracked live widgets; searches are bounded
at 16,384 widgets and 128 levels. Native GUI-instance replacement is unsupported.

The current save list's actual key delegates are on child widgets, not the
outer GamesList expected by the old example. Do not publish a save-deletion
feature based on this API without independently verifying that native route.

## Engine service: `tpl.engine`, version 1

Query `sizeof(TPLLib_Engine_API)`. `info(index, &info)` enumerates metadata;
`resolve(id, &function)` checks the pinned executable/entry bytes on the GUI
thread. Resolve every needed target before installing hooks. A patched target
may refuse later resolution; keep your checked continuation.

Bindings: 1 LoadSaveWindow construct, 2 close, 3 keyPressed, 4 ForgottenGUI
messageBox, 1000 GameWorld graphics update, 1001 reset, 1002 destructor.
The engine table exposes a count and the supported game SHA256. Metadata reports
ID, RVA, name, expected32 and evidence flags. Generation grants binary review,
never automatic runtime acceptance. The SDK's generated native header carries
the additional opaque types/function signatures, not object fields or sizes.

The first four have native C++/UI contracts; do not call them through guessed
signatures. Update/reset observation has one scoped live acceptance. Destructor
execution and arbitrary direct calls remain unconfirmed. There are no general
object allocations, character accessors or save-completion events in this API.
