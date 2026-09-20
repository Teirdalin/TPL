// SPDX-License-Identifier: LicenseRef-JDL-1
// Copyright (c) 2026 Teirdalin. See PLUGIN_API_PERMISSION.md for API reuse rights.
#ifndef TPLLIB_API_H
#define TPLLIB_API_H
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>
#ifdef __cplusplus
extern "C" {
#endif
#pragma pack(push, 8)

#define TPLLIB_ABI_VERSION 1
#define TPLLIB_VERSION "0.1.0-dev"
#define TPLLIB_HOOK_BYTES 32
#define TPLLIB_CAP_MODULES 1u
#define TPLLIB_CAP_SIGNATURES 2u
#define TPLLIB_CAP_HOOKS 4u
#define TPLLIB_CAP_DISPATCH 8u
#define TPLLIB_CAP_SERVICES 16u
#define TPLLIB_CAP_LOGGING 32u
#define TPLLIB_CAP_SHARED_HOOKS 64u
/* Reserved capabilities: NOT provided by the foundation implementation. */
#define TPLLIB_CAP_GAME_OBJECTS 0x10000u
#define TPLLIB_CAP_KENSHILIB_ABI 0x20000u

typedef uint64_t TPLLib_Token;
typedef int32_t TPLLib_Status;
enum {
    TPLLIB_OK=0, TPLLIB_INVALID=1, TPLLIB_NOT_READY=2,
    TPLLIB_WRONG_THREAD=3, TPLLIB_NOT_FOUND=4, TPLLIB_VERSION_MISMATCH=5,
    TPLLIB_AMBIGUOUS=6, TPLLIB_UNREADABLE=7, TPLLIB_CONFLICT=8,
    TPLLIB_LIMIT=9, TPLLIB_BACKEND_ERROR=10, TPLLIB_CALLBACK_ERROR=11,
    TPLLIB_BUSY=12
};
typedef struct TPLLib_Module {
    uint32_t size;
    uint32_t image_size;
    uint32_t timestamp;
    uint32_t machine;
    uintptr_t base;
    char sha256[65];
} TPLLib_Module;
typedef struct TPLLib_Stats {
    uint32_t size;
    uint32_t owners;
    uint32_t hooks;
    uint32_t queued_jobs;
    uint32_t frame_callbacks;
    uint32_t services;
    uint64_t frames;
    uint64_t callback_failures;
} TPLLib_Stats;

typedef struct TPLLib_HookState {
    uint32_t size;
    uint32_t enabled;
    uint32_t shared;
    uint32_t target_patched;
    uint32_t target_verified;
    uint32_t chain_members;
    uint32_t chain_enabled;
    uint32_t reserved;
} TPLLib_HookState;
typedef void (*TPLLib_Job)(void* user);
typedef void (*TPLLib_Frame)(void* user, float dt);

/* Opaque owners identify cooperative plugins, not a security boundary.
   All calls except post, cancel_job, read_memory, module_info, resolve_rva,
   find_unique, is_main_thread, log and status_text require the GUI thread.
   Caller-owned pointers must remain valid for the call/callback lifetime.
   See docs/TPLLIB.md for capacities, cancellation and hook lifetime rules. */
typedef struct TPLLib_API {
    uint32_t size;
    uint32_t abi_version;
    uint32_t capabilities;
    const char* version;
    const char* (*status_text)(TPLLib_Status status);
    int (*is_main_thread)(void);
    TPLLib_Status (*owner_open)(const char* name, TPLLib_Token* owner);
    TPLLib_Status (*owner_close)(TPLLib_Token owner);
    TPLLib_Status (*module_info)(const wchar_t* module, TPLLib_Module* info);
    TPLLib_Status (*read_memory)(const void* source, void* destination, size_t bytes);
    TPLLib_Status (*resolve_rva)(const wchar_t* module, const char* sha256,
        uint32_t rva, const uint8_t* bytes, const uint8_t* mask, uint32_t count,
        void** address);
    TPLLib_Status (*find_unique)(const wchar_t* module, const char* sha256,
        const uint8_t* bytes, const uint8_t* mask, uint32_t count, void** address);
    TPLLib_Status (*hook_create)(TPLLib_Token owner, void* target,
        const uint8_t* expected32, void* detour, void** original, TPLLib_Token* hook);
    TPLLib_Status (*hook_enable)(TPLLib_Token owner, TPLLib_Token hook, int enabled);
    TPLLib_Status (*post)(TPLLib_Token owner, TPLLib_Job job, void* user, TPLLib_Token* id);
    TPLLib_Status (*cancel_job)(TPLLib_Token owner, TPLLib_Token id);
    TPLLib_Status (*subscribe_frame)(TPLLib_Token owner, TPLLib_Frame callback,
        void* user, TPLLib_Token* subscription);
    TPLLib_Status (*unsubscribe_frame)(TPLLib_Token owner, TPLLib_Token subscription);
    TPLLib_Status (*publish_service)(TPLLib_Token owner, const char* name,
        uint32_t version, const void* table, uint32_t bytes);
    TPLLib_Status (*query_service)(const char* name, uint32_t version,
        uint32_t minimum_bytes, const void** table);
    TPLLib_Status (*stats)(TPLLib_Stats* stats);
    TPLLib_Status (*log)(const char* message);
    /* Optional tail. Shared hooks run newest-created first. The returned
       continuation calls older enabled hooks, then the native function.
       Disabling does not wait for calls already in flight. Never unload. */
    TPLLib_Status (*hook_create_shared)(TPLLib_Token owner, void* target,
        const uint8_t* expected32, void* detour, void** continuation, TPLLib_Token* hook);
    TPLLib_Status (*hook_state)(TPLLib_Token owner, TPLLib_Token hook, TPLLib_HookState* state);
} TPLLib_API;

#define TPLLIB_HAS_SHARED_HOOKS(api) ((api) && \
    (api)->size >= offsetof(TPLLib_API, hook_state) + sizeof((api)->hook_state) && \
    ((api)->capabilities & TPLLIB_CAP_SHARED_HOOKS) && \
    (api)->hook_create_shared && (api)->hook_state)
typedef const TPLLib_API* (*TPLLib_GetAPIFn)(uint32_t abi_version);

#pragma pack(pop)
#ifdef __cplusplus
}
#endif
#endif
