#pragma once
#include "tpllib_ui.h"
#include <string>
namespace tplui {
struct State {
    TPLLib_UI_Rect rect;
    std::string caption;
    bool enabled;
    int alignment;
    State():enabled(true),alignment(-1){}
};
class Backend {
public:
    virtual ~Backend() {}
    virtual TPLLib_Status find(void* parent,const char* suffix,void** out)=0;
    virtual void describe(void* widget,TPLLib_UI_Info& info,void** parent)=0;
    virtual State buttonState(void* widget)=0;
    virtual void setButton(void* widget,const State& state)=0;
    virtual void* createButton(void* parent,void* style,const State& state)=0;
    virtual void destroy(void* widget)=0;
    virtual bool clickAllowed(void* sender)=0;
    virtual bool sameRoot(void* a,void* b)=0;
    virtual void key(void* widget,uint32_t code)=0;
};
TPLLib_Status initialize(const TPLLib_API* host,Backend* backend);
void destroyed(void* widget);
void clicked(void* widget);
const TPLLib_UI_API* api();
}
