# Lifecycle, threads and ownership

## Startup and frames

TPL discovers `TPL.json` in local mod folders, this Steam library's Workshop
folders and `TPL/plugins/<folder>`. The manifest must have a lowercase
`plugins` array of `{ "name": "...", "dll": "relative-file.dll" }` objects.
The DLL must remain inside its folder. A mod folder with exactly one `.mod`
inherits that FCS owner's launch selection. Multiple `.mod` files are ambiguous.

TPL calls `extern "C" __declspec(dllexport) int TPL_Start(const TPL_Host*)`
once at its main-menu checkpoint. Return zero only when startup succeeded.
An optional `TPL_Tick(float)` runs on the GUI thread afterward. Do not use
RE_Kenshi's C++ `void startPlugin()` name for a TPL-native plugin.

FCS-owned TPL plugins follow current `mods.cfg` order, then manifest order;
standalone plugins follow afterward. This is not dependency resolution.
A DLL in both manifest types belongs to TPL, not two startup owners.

Do not initialize the game, start threads, install hooks or wait in `DllMain`.
Use `TPL_Start`. Do not retain the host struct pointer; `tpl::Plugin` copies
the function-table/path references it needs. Host API/service tables remain
borrowed and must never be modified or freed.

A GUI tick is not a simulation tick, a loaded-save event, or proof that a
character exists. `TPL_Tick` receives the native GUI delta; validate it before
using it numerically. Foundation frame subscriptions instead get a finite,
clamped 0..1-second delta. Neither promises simulation timing.

## Worker threads

Workers can compute plain plugin-owned data, log, inspect loaded modules,
resolve checked addresses, and submit/cancel jobs. These permissions do not
make raw engine state thread-safe. Apply game/UI effects only on the correct
native thread and in a verified lifetime/phase.

Use `post(owner, callback, context, &job)` to return to the GUI thread. Context
must outlive execution, not just the `post` call. Do not post stack variables
that disappear when the worker returns. Cancellation is not a join: `NOT_FOUND`
may mean the job is already running or completed. Keep context until completion
is independently established. Never wait on the GUI thread for a worker that
is waiting on that same GUI thread.

At most 128 queued jobs execute per GUI frame. Jobs posted by callbacks wait
until the next frame. This bounds count, not callback duration: a slow callback
still stalls the game. Catch plugin C++ exceptions inside every exported entry,
job, frame, click and service callback, especially across different compilers.
Host exception handling is not a cross-CRT or access-violation safety guarantee.

## Owners and services

Open one foundation owner for your plugin; save the returned token. Owners
identify cooperative resources, not a security boundary. `tpl::Plugin` does
this for you and deliberately has no automatic closing destructor.

Closing an owner cancels its pending jobs/subscriptions. It returns `BUSY`
while callbacks execute or hooks/services are owned. A published service and
its function pointers must live until process exit. There is no replacement or
unpublication operation. Use static tables with explicit `size` and `version`
fields and document the thread/ownership contract of your own callbacks.

UI sessions are separate tokens from foundation owners. Close those sessions
explicitly on the GUI thread when a feature stops, respecting callback-time
`BUSY`. A widget token expires on native destruction; reacquire it, not its old
address, when the window reappears.

## Hooks and world resets

Resolve reviewed targets before enabling hooks. Never turn arbitrary current
bytes into an expected fingerprint. Match the exact native signature and
normally call the continuation once with unchanged arguments. Calling the
target address from its own detour recurses. Preserve code/context for process
lifetime; disabling does not join in-flight calls or free trampolines.

Shared TPL hooks run newest-created first; each continuation calls older
enabled members and then the original. They do not coordinate patches made by
RE_Kenshi or other hook managers. Decline a conflict rather than force it.

The observed GameWorld pointer can remain identical across a reset while all
its contents change. Invalidate future object handles and asynchronous request
epochs at reset **entry**. A successful reset return is not load completion.
The destructor ends object lifetime but was not captured in the first live
observer trace. No general character handles or game fields are exposed yet.

## Compiler boundaries

The starter's modern compiler is for public C tables only. Pass scalars,
opaque tokens, explicit lengths and borrowed buffers as documented. Do not
pass `std::string`, containers, RTTI-dependent C++ objects, exceptions, file
handles owned by a different CRT, or allocation/deallocation ownership across
that boundary. Allocate/free your own memory in your own module.

Raw game-native C++ signatures are a separate interface, not converted by
`tpl::Plugin`. They need reviewed layout, calling convention, supported binary,
phase and the VC100 x64 `/MD` ABI. A successful compiler build or DLL load does
not prove engine compatibility.
