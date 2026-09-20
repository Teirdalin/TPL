// Copyright (c) 2026 Teirdalin. Reuse permitted by PLUGIN_API_PERMISSION.md.
#include "examples.hpp"
namespace tpl_examples {
static void hello(void* context) {
    const TPLLib_API* api=static_cast<const TPLLib_API*>(context);
    try { api->log("SDK example: queued GUI callback"); }
    catch(...) { /* Never send a C++ exception into another module. */ }
}
TPLLib_Status queueHello(tpl::Plugin& plugin,TPLLib_Token* job) {
    if(!job) return TPLLIB_INVALID;
    *job=0;
    if(!plugin.has(TPLLIB_CAP_DISPATCH|TPLLIB_CAP_LOGGING)) return TPLLIB_NOT_READY;
    // This borrowed table outlives the job; no stack/local context escapes.
    return plugin.api()->post(plugin.owner(),hello,const_cast<TPLLib_API*>(plugin.api()),job);
}
}
