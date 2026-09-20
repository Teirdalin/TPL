// SPDX-License-Identifier: LicenseRef-JDL-1
// Copyright (c) 2026 Teirdalin. See PLUGIN_API_PERMISSION.md for API reuse rights.
#ifndef TPLLIB_ENGINE_H
#define TPLLIB_ENGINE_H
#include "tpllib.h"
#ifdef __cplusplus
extern "C" {
#endif
#pragma pack(push, 8)
#define TPLLIB_ENGINE_SERVICE "tpl.engine"
#define TPLLIB_ENGINE_VERSION 1u
#define TPLLIB_ENGINE_BINARY_VERIFIED 1u
#define TPLLIB_ENGINE_RUNTIME_VERIFIED 2u
enum {
    TPLLIB_ENGINE_LOAD_SAVE_CONSTRUCT=1,
    TPLLIB_ENGINE_LOAD_SAVE_CLOSE=2,
    TPLLIB_ENGINE_LOAD_SAVE_KEY=3,
    TPLLIB_ENGINE_MESSAGE_BOX=4
};
typedef struct TPLLib_Engine_Info {
    uint32_t size, id, rva, evidence;
    const char* name;
    uint8_t expected32[32];
} TPLLib_Engine_Info;
/* Exact-build, GUI-thread native addresses, not portable game-object handles.
   Resolution proves identity/bytes, NOT lifetime or a valid call context.
   See docs/developers/API_REFERENCE.md before using these functions. */
typedef struct TPLLib_Engine_API {
    uint32_t size, version, count, reserved;
    const char* game_sha256;
    TPLLib_Status (*info)(uint32_t index, TPLLib_Engine_Info* info);
    TPLLib_Status (*resolve)(uint32_t id, void** function);
} TPLLib_Engine_API;
#pragma pack(pop)
#ifdef __cplusplus
}
#endif
#endif
