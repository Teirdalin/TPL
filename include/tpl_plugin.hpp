// SPDX-License-Identifier: LicenseRef-JDL-1
// Copyright (c) 2026 Teirdalin. See PLUGIN_API_PERMISSION.md for API reuse rights.
#ifndef TPL_PLUGIN_HPP
#define TPL_PLUGIN_HPP
#include "tpl.h"

namespace tpl {
// C++03 convenience layer over the public C ABI. No automatic shutdown calls:
// hooks/services can outlive the owner of a C++ object or the last GUI frame.
class Plugin {
    const TPLLib_API* api_;
    void (*hostLog_)(const char*);
    const wchar_t* game_;
    TPLLib_Token owner_;
    Plugin(const Plugin&);
    Plugin& operator=(const Plugin&);
public:
    Plugin() : api_(0),hostLog_(0),game_(0),owner_(0) {}
    TPLLib_Status connect(const TPL_Host* host,const char* name,
        uint32_t required=TPLLIB_CAP_LOGGING|TPLLIB_CAP_DISPATCH|TPLLIB_CAP_SERVICES) {
        if(api_) return TPLLIB_CONFLICT;
        if(!host || host->size<TPL_HOST_V1_SIZE || !name || !*name) return TPLLIB_INVALID;
        if(host->abi_version!=TPL_ABI_VERSION || !TPL_HOST_HAS_TPLLIB(host)) return TPLLIB_VERSION_MISMATCH;
        hostLog_=host->log;
        const TPLLib_API* candidate=host->get_tpllib(TPLLIB_ABI_VERSION);
        if(!candidate || candidate->size<offsetof(TPLLib_API,log)+sizeof(candidate->log) ||
           candidate->abi_version!=TPLLIB_ABI_VERSION || (candidate->capabilities&required)!=required)
            return TPLLIB_VERSION_MISMATCH;
        if(!candidate->is_main_thread || !candidate->owner_open || !candidate->owner_close ||
           !candidate->status_text || !candidate->log || !candidate->query_service) return TPLLIB_INVALID;
        if(!candidate->is_main_thread()) return TPLLIB_WRONG_THREAD;
        TPLLib_Token token=0;
        TPLLib_Status result=candidate->owner_open(name,&token);
        if(result!=TPLLIB_OK) return result;
        api_=candidate; owner_=token; game_=host->game_directory;
        return TPLLIB_OK;
    }
    const TPLLib_API* api() const { return api_; }
    TPLLib_Token owner() const { return owner_; }
    const wchar_t* game_directory() const { return game_; }
    bool has(uint32_t capabilities) const { return api_ && (api_->capabilities&capabilities)==capabilities; }
    TPLLib_Status log(const char* message) const {
        if(!message) return TPLLIB_INVALID;
        if(api_) return has(TPLLIB_CAP_LOGGING)?api_->log(message):TPLLIB_NOT_READY;
        if(hostLog_) { hostLog_(message); return TPLLIB_OK; }
        return TPLLIB_NOT_READY;
    }
    bool check(TPLLib_Status result,const char* operation) const {
        if(result==TPLLIB_OK) return true;
        if(operation) log(operation);
        if(api_) log(api_->status_text(result));
        return false;
    }
    template<class Table> TPLLib_Status service(const char* name,uint32_t version,const Table** out) const {
        if(!out) return TPLLIB_INVALID;
        *out=0;
        if(!has(TPLLIB_CAP_SERVICES)) return TPLLIB_NOT_READY;
        const void* value=0;
        TPLLib_Status result=api_->query_service(name,version,sizeof(Table),&value);
        if(result==TPLLIB_OK) *out=static_cast<const Table*>(value);
        return result;
    }
    TPLLib_Status close() {
        if(!api_) return TPLLIB_NOT_READY;
        TPLLib_Status result=api_->owner_close(owner_);
        if(result==TPLLIB_OK) { api_=0; owner_=0; game_=0; hostLog_=0; }
        return result;
    }
};
}
#endif
