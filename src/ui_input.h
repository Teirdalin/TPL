#pragma once
#include <string>
namespace tpl {
inline bool newerRelease(const std::string& current,const std::string& latest) {
    unsigned parts[2][3]={{0}};
    const std::string* versions[2]={&current,&latest};
    for(unsigned v=0;v<2;++v) {
        size_t p=0;
        for(unsigned n=0;n<3;++n) {
            size_t start=p;
            while(p<versions[v]->size() && (*versions[v])[p]>='0' && (*versions[v])[p]<='9') {
                unsigned digit=(*versions[v])[p++]-'0';
                if(parts[v][n]>(2147483647u-digit)/10) return false;
                parts[v][n]=parts[v][n]*10+digit;
            }
            if(start==p) return false;
            if(n<2 && (p>=versions[v]->size() || (*versions[v])[p++]!='.')) return false;
        }
        if(p!=versions[v]->size()) return false;
    }
    for(unsigned n=0;n<3;++n) if(parts[0][n]!=parts[1][n]) return parts[1][n]>parts[0][n];
    return false;
}
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
    enum Target { None, Mods, Config, Update };
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
