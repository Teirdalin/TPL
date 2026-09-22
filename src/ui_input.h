#pragma once
#include <string>
namespace tpl {
inline bool releaseVersion(const std::string& version,unsigned* parts) {
        size_t p=0;
        for(unsigned n=0;n<3;++n) {
            parts[n]=0;
            size_t start=p;
            while(p<version.size() && version[p]>='0' && version[p]<='9') {
                unsigned digit=version[p++]-'0';
                if(parts[n]>(2147483647u-digit)/10) return false;
                parts[n]=parts[n]*10+digit;
            }
            if(start==p) return false;
            if(n<2 && (p>=version.size() || version[p++]!='.')) return false;
        }
        return p==version.size();
}
inline bool newerRelease(const std::string& current,const std::string& latest) {
    unsigned parts[2][3]={{0}};
    if(!releaseVersion(current,parts[0]) || !releaseVersion(latest,parts[1])) return false;
    for(unsigned n=0;n<3;++n) if(parts[0][n]!=parts[1][n]) return parts[1][n]>parts[0][n];
    return false;
}
inline bool verifiedReleaseResult(const std::string& request,const std::string& receipt,std::string& latest) {
    latest.clear();
    if(request.empty() || receipt.compare(0,request.size()+1,request+"\n")) return false;
    std::string version=receipt.substr(request.size()+1);
    unsigned parts[3]; if(!releaseVersion(version,parts)) return false;
    latest=version; return true;
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
