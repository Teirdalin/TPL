// SPDX-License-Identifier: LicenseRef-JDL-1
// Copyright (c) 2026 Teirdalin.
#include "tpllib.h"
#include <string>
static const TPLLib_API* api=0;
extern "C" __declspec(dllexport) int TPL_KL_Initialize(const TPLLib_API* host) {
    if(!host || host->abi_version!=TPLLIB_ABI_VERSION || host->size<sizeof(TPLLib_API) ||
       !(host->capabilities&TPLLIB_CAP_LOGGING) || !host->log || (api && api!=host)) return 1;
    api=host; return 0;
}
extern "C" void TPL_CompatDebug(const char* text) {
    try { if(api && text) api->log((std::string("[legacy/debug] ")+text).c_str()); } catch(...) {}
}
extern "C" void TPL_CompatError(const char* text) {
    try { if(api && text) api->log((std::string("[legacy/error] ")+text).c_str()); } catch(...) {}
}
