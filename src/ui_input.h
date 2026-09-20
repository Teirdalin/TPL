#pragma once
namespace tpl {
// Consume a complete Escape gesture, even after its window closes.
struct EscapeInput {
    enum Target { None, Mods, Config };
    bool held;
    Target pending;
    EscapeInput() : held(false), pending(None) {}
    bool press(Target active) {
        if(held) return true;
        if(active==None) return false;
        held=true; pending=active; return true;
    }
    bool release() {
        bool consumed=held; held=false; return consumed;
    }
    Target takeClose() {
        Target result=pending; pending=None; return result;
    }
};
}
