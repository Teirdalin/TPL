#pragma once
namespace tpl {
// Rebuild stale font users before opening a window, outside input dispatch.
struct UiFrameRequests {
    bool fontsChanged, modsRequested;
    UiFrameRequests() : fontsChanged(false), modsRequested(false) {}
    void fontResized() { fontsChanged=true; }
    void openMods() { modsRequested=true; }
    bool takeFontRefresh() {
        bool result=fontsChanged; fontsChanged=false; return result;
    }
    bool takeOpenMods() {
        if(fontsChanged) return false;
        bool result=modsRequested; modsRequested=false; return result;
    }
};
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
