// Copyright (c) 2026 Teirdalin. Reuse permitted by PLUGIN_API_PERMISSION.md.
#include "examples.hpp"
namespace tpl_examples {
static const TPLLib_API* host=0;
static uint64_t count=0;
static TPLLib_Status next(uint64_t* value) {
    if(!value) return TPLLIB_INVALID;
    *value=0;
    if(!host) return TPLLIB_NOT_READY;
    if(!host->is_main_thread()) return TPLLIB_WRONG_THREAD;
    if(count==~uint64_t(0)) return TPLLIB_LIMIT;
    *value=++count;
    return TPLLIB_OK;
}
static const CounterAPI counter={sizeof(CounterAPI),1,next};
TPLLib_Status publishCounter(tpl::Plugin& plugin) {
    if(!plugin.has(TPLLIB_CAP_SERVICES)) return TPLLIB_NOT_READY;
    if(!plugin.api()->is_main_thread()) return TPLLIB_WRONG_THREAD;
    TPLLib_Status status=plugin.api()->publish_service(plugin.owner(),"example.counter",1,&counter,sizeof(counter));
    if(status==TPLLIB_OK) host=plugin.api();
    return status;
}
TPLLib_Status findCounter(tpl::Plugin& plugin,const CounterAPI** result) {
    TPLLib_Status status=plugin.service("example.counter",1,result);
    if(status!=TPLLIB_OK) return status;
    if(!*result || (*result)->size<sizeof(CounterAPI) || (*result)->version!=1 || !(*result)->next) {
        *result=0;
        return TPLLIB_VERSION_MISMATCH;
    }
    return TPLLIB_OK;
}
}
