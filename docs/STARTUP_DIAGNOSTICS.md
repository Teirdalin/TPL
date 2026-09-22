# Startup diagnostics for 0.1.3

Before starting its plugin queue, TPL checks the discovered catalog for active
or already-loaded DLLs with matching filenames at different paths. These are
possible duplicates, not proof of an incompatible plugin. Disabled, unloaded
copies are ignored. The check does not rescan or hash every DLL.

TPL also reports plugins already loaded before its startup and records each
initialization attempt with its provider and full path. Another loader's
presence alone is not a conflict. A legacy manifest alone is not a conflict.
No files or mod selections are changed by these diagnostics.

Automatic foreign-hook profiles are selected only when the observed entry jump
and relay point to the profile's loaded module and exact detour RVA. A matching
game-function RVA alone is insufficient. Missing optional modules and unrelated
detours are skipped, rather than producing a misleading missing-dependency error.
The selected profile still requires full module hashes, original bytes, and
trampoline validation. Unknown patches remain blocked until independently reviewed.

When hook creation returns TPLLIB_CONFLICT, the runtime logs the first such
failure per owner registration, including the requesting owner, target module
and offset, and existing TPL owners. It observes at most two E9/FF25 jumps and
reports their destination modules when available. This is evidence about the
current patch, not proof of who installed it. Existing fingerprint checks and
reviewed coexistence profiles remain mandatory. Later plugin loads can still
introduce conflicts; startup checks cannot certify all future behavior.

Failed plugin startup now records the plugin's return code in TPL.log and the
Mods status. Plugin return codes are not interpreted as TPLLib status codes.

For support, request the full TPL.log from the failed launch, the game version
and platform, and the relevant plugin version. Do not request API keys or
configuration files containing secrets. Use the first hook rejection, not the
last cascading error or the preceding plugin's load order, to guide diagnosis.
Restart Kenshi after changing native plugins; existing hooks are not unloaded.

## Update notice

Startup runs the updater asynchronously and waits for that process to finish
before interpreting its result. A newer stable version gets a dismissible
main-menu notice once per launch. Failed checks do not display stale update
notices. Version comparison is numeric. Escape is consumed by the notice.

With automatic updates disabled, startup uses CheckOnly, which fetches release
metadata without downloading assets or staging a runtime. Update now requests
a download and opens Mods for progress; Later leaves the installation alone.
With automatic updates enabled, a successfully staged update asks the player
to restart. Other modal windows defer the notice.

These diagnostics shipped in 0.1.3. Version 0.1.4 added request-bound
version receipts: a previous launch's status or version file cannot report a
successful current check. Checking/unchecked labels are white, a confirmed
up-to-date result is green, and a confirmed newer release is red. The dark
panel's labels and commands also use white text for contrast.

## Combined Mod Rows

The Mods view groups native plugins by their recorded parent mod ID. It does
not merge unrelated installs with matching names, change load order, or hide
standalone/unmanaged plugins. Native load failures, missing files, exceptions,
and skipped initialization take priority over healthy plugin or FCS status.
The selected row identifies the failing DLL. Search still matches bundled DLL
names, and Config scans the owning mod folder as before.

The combined toggle applies to the mod and all its recorded plugins as one
persisted change. Existing partial selections show Mixed; clicking that state
enables the bundle. Restart indicators include changes to any child plugin.
External mod-order edits still reject the whole change and restore the previous
in-memory selection.
