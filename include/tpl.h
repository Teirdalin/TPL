// SPDX-License-Identifier: LicenseRef-JDL-1
// Copyright (c) 2026 Teirdalin. See PLUGIN_API_PERMISSION.md for API reuse rights.
#ifndef TPL_API_H
#define TPL_API_H
#include <stdint.h>
#include "tpllib.h"
#ifdef __cplusplus
extern "C" {
#endif
#define TPL_ABI_VERSION 1
#pragma pack(push, 8)
typedef struct TPL_Host {
    uint32_t size;
    uint32_t abi_version;
    const wchar_t* game_directory;
    void (*log)(const char* message);
    /* Optional tail extension. Check size before accessing on older hosts. */
    TPLLib_GetAPIFn get_tpllib;
} TPL_Host;
#pragma pack(pop)
#define TPL_HOST_V1_SIZE offsetof(TPL_Host, get_tpllib)
#define TPL_HOST_HAS_TPLLIB(host) ((host) && (host)->size >= sizeof(TPL_Host) && (host)->get_tpllib)
/* TPL_Start runs on the GUI thread once the main menu exists. Return 0 on success.
   Optional TPL_Tick(float) runs on the same thread. DLLs remain loaded until exit.
   Do not pass C++ containers, exceptions, or ownership across this interface. */
typedef int (*TPL_StartFn)(const TPL_Host*);
typedef void (*TPL_TickFn)(float);
#ifdef __cplusplus
}
#endif
#endif
