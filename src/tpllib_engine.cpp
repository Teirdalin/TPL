// SPDX-License-Identifier: LicenseRef-JDL-1
// Copyright (c) 2026 Teirdalin.
#include "tpllib_engine.h"
#include <string.h>
namespace tplengine {
namespace {
struct Binding { uint32_t id; const char* name; uint32_t rva; const char* bytes; };
#define TPL_ENGINE_BINDING(id,name,rva,bytes) {id,name,rva,bytes},
const Binding bindings[]={
#include <tpllib_engine_profile.inc>
};
#undef TPL_ENGINE_BINDING
const char* const GAME_SHA=TPL_ENGINE_GAME_SHA;
const uint32_t count=sizeof(bindings)/sizeof(bindings[0]);
const TPLLib_API* host=0;
TPLLib_Token owner=0;
unsigned nibble(char c) { return c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:16; }
bool bytes(const Binding& b,uint8_t* out) {
    if(strlen(b.bytes)!=64) return false;
    for(unsigned i=0;i<32;++i) {
        unsigned hi=nibble(b.bytes[i*2]),lo=nibble(b.bytes[i*2+1]);
        if(hi>15 || lo>15) return false;
        out[i]=static_cast<uint8_t>(hi*16+lo);
    }
    return true;
}
TPLLib_Status info(uint32_t index,TPLLib_Engine_Info* out) {
    if(!out || out->size<sizeof(*out)) return TPLLIB_INVALID;
    if(index>=count) return TPLLIB_NOT_FOUND;
    const Binding& b=bindings[index]; TPLLib_Engine_Info value={0};
    value.size=sizeof(value); value.id=b.id; value.rva=b.rva;
    value.evidence=TPLLIB_ENGINE_BINARY_VERIFIED; value.name=b.name;
    if(!bytes(b,value.expected32)) return TPLLIB_INVALID;
    *out=value; return TPLLIB_OK;
}
TPLLib_Status checked(const TPLLib_API* h,const Binding& b,void** out) {
    uint8_t expected[32],mask[32]; memset(mask,255,sizeof(mask));
    if(!bytes(b,expected)) return TPLLIB_INVALID;
    TPLLib_Status result=h->resolve_rva(0,GAME_SHA,b.rva,expected,mask,32,out);
    if(result!=TPLLIB_OK) *out=0;
    if(result==TPLLIB_OK && !*out) return TPLLIB_BACKEND_ERROR;
    return result;
}
TPLLib_Status resolve(uint32_t id,void** out) {
    if(!out) return TPLLIB_INVALID; *out=0;
    if(!host) return TPLLIB_NOT_READY;
    if(!host->is_main_thread()) return TPLLIB_WRONG_THREAD;
    try {
        for(uint32_t i=0;i<count;++i) if(bindings[i].id==id) return checked(host,bindings[i],out);
        return TPLLIB_NOT_FOUND;
    } catch(...) { *out=0; return TPLLIB_BACKEND_ERROR; }
}
const TPLLib_Engine_API table={sizeof(table),TPLLIB_ENGINE_VERSION,count,0,GAME_SHA,&info,&resolve};
}
const TPLLib_Engine_API* api() { return &table; }
TPLLib_Status initialize(const TPLLib_API* h) {
    if(!h || h->size<sizeof(*h) || h->abi_version!=TPLLIB_ABI_VERSION ||
       !h->is_main_thread || !h->resolve_rva || !h->owner_open || !h->owner_close || !h->publish_service)
        return TPLLIB_INVALID;
    if(!h->is_main_thread()) return TPLLIB_WRONG_THREAD;
    if(host) return host==h?TPLLIB_OK:TPLLIB_CONFLICT;
    try {
        for(uint32_t i=0;i<count;++i) {
            void* address=0; TPLLib_Status s=checked(h,bindings[i],&address);
            if(s!=TPLLIB_OK) return s;
        }
        TPLLib_Status s=h->owner_open("tpl.engine.provider",&owner);
        if(s!=TPLLIB_OK) return s;
        s=h->publish_service(owner,TPLLIB_ENGINE_SERVICE,TPLLIB_ENGINE_VERSION,&table,sizeof(table));
        if(s!=TPLLIB_OK) { h->owner_close(owner); owner=0; return s; }
        host=h; return TPLLIB_OK;
    } catch(...) { if(owner) h->owner_close(owner); owner=0; return TPLLIB_BACKEND_ERROR; }
}
}
