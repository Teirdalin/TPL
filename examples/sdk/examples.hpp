// Copyright (c) 2026 Teirdalin. Reuse permitted by PLUGIN_API_PERMISSION.md.
#ifndef TPL_SDK_EXAMPLES_HPP
#define TPL_SDK_EXAMPLES_HPP
#include "tpl_plugin.hpp"

namespace tpl_examples {
// Keep this ABI header identical in providers and consumers. All calls are GUI-only.
#pragma pack(push,8)
struct CounterAPI {
    uint32_t size,version;
    TPLLib_Status (*next)(uint64_t* value);
};
#pragma pack(pop)
// Queue once during startup. The callback uses a process-lifetime API table.
TPLLib_Status queueHello(tpl::Plugin& plugin,TPLLib_Token* job);
// Demonstration service, not a game counter. Publishing retains the owner.
TPLLib_Status publishCounter(tpl::Plugin& plugin);
TPLLib_Status findCounter(tpl::Plugin& plugin,const CounterAPI** result);
}
#endif
