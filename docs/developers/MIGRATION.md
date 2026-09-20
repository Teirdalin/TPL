# Porting an RE_Kenshi plugin

TPL-native plugin development and running an unchanged RE_Kenshi DLL are two
different tasks. Renaming a DLL or changing a manifest does not replace its
imports, runtime initialization, object layouts or hook manager.

## Start with the source

1. Create a TPL starter and get its two log messages before bringing code over.
2. Inventory the old plugin's KenshiLib calls, native classes, hooks, UI use,
   third-party dependencies and assumptions about startup order.
3. Move independent logic/settings into your own module without changing its
   behavior. Check that you have permission to reuse every source portion.
4. Replace the C++ `void startPlugin()` entry with exported C `TPL_Start` and
   connect to the host. Startup occurs at the main menu, not before it.
5. Convert one supported feature at a time, checking statuses and lifetimes.
6. Leave unsupported features explicitly disabled until a real equivalent
   exists. Validate the same gameplay scenarios as the original plugin.

## Available adaptations

- **Logging:** use `tpl::Plugin::log` or the host logger; prefix the mod name.
- **Address relocation:** `resolve_rva` requires a pinned module hash and
  reviewed bytes. It is not a drop-in unchecked `GetRealAddress` equivalent.
- **Pattern lookup:** `find_unique` requires one exact masked match in
  executable sections and a matching file hash.
- **Hook installation:** use owned exclusive/shared hooks, exact 32-byte
  fingerprints and continuations. The chain/lifetime/error semantics differ
  from another hook library, so mechanical name replacement is insufficient.
- **Per-frame work:** use `TPL_Tick` or a foundation frame subscription. Both
  are GUI-frame callbacks, not arbitrary simulation or load events.
- **Worker-to-GUI dispatch:** use `post` with persistent owned context.
- **Plugin interop:** publish/query an explicit versioned C function table.
- **Native UI:** the current UI service supports bounded widget lookup,
  reversible button additions/edits, and restricted list-key routing only.

## Not yet equivalent

TPL 0.1.0 does not expose KenshiLib's complete native object/class interface.
General character, squad, inventory, navigation and serialization accessors
are absent. The reviewed engine service has seven narrow bindings, not all
game functions. A passing world-observer test does not fill these gaps.

Do not carry Kenshi's STL/native object types into the modern C-API starter.
Keep exact VC100 x64 `/MD` requirements wherever the reviewed native ABI needs
them. The helper does not translate C++ classes or invent their layouts.

## Unchanged legacy DLLs

TPL can experimentally initialize eligible ordinary DLLs from `RE_Kenshi.json`
when RE_Kenshi is absent. Plugins needing missing KenshiLib imports remain
blocked. The opt-in compatibility pipeline currently maps only narrowly
supported exports; it is not a universal automatic converter. Preserve the
working dependency until the exact plugin is ported and tested.

When RE_Kenshi is present, it keeps legacy startup ownership. For a TPL-native
port, use `TPL.json`; a DLL declared in both types belongs to TPL. Do not install
both old and new plugin copies and assume their hooks will cooperate. Shared
TPL hooks do not share another loader's hook chain.
