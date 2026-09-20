// SPDX-License-Identifier: LicenseRef-JDL-1
// Copyright (c) 2026 Teirdalin.
#include "tpllib_ui_mygui.h"
#include "tpllib_ui_core.h"
#include "tpllib_internal.h"
#include <MyGUI_Gui.h>
#include <MyGUI_Button.h>
#include <MyGUI_MultiListBox.h>
#include <MyGUI_WidgetManager.h>
#include <MyGUI_InputManager.h>
#include <stdexcept>
#include <sstream>
namespace tplui {
namespace {
MyGUI::Widget* widget(void* p) { return static_cast<MyGUI::Widget*>(p); }
MyGUI::Widget* root(MyGUI::Widget* p) {
    unsigned depth=0;
    while(p && p->getParent()) { if(++depth>128) throw std::runtime_error("UI ancestry limit"); p=p->getParent(); }
    return p;
}
bool suffix(const std::string& name,const std::string& wanted) {
    return name==wanted || (name.size()>wanted.size() &&
        name[name.size()-wanted.size()-1]=='_' && name.compare(name.size()-wanted.size(),wanted.size(),wanted)==0);
}
void search(MyGUI::EnumeratorWidgetPtr iterator,const std::string& name,
    MyGUI::Widget*& found,unsigned& remaining,unsigned depth) {
    if(depth>128) throw std::runtime_error("UI traversal depth limit");
    while(iterator.next()) {
        if(!remaining--) throw std::runtime_error("UI traversal item limit");
        MyGUI::Widget* p=iterator.current();
        if(suffix(p->getName(),name)) {
            if(found) throw static_cast<TPLLib_Status>(TPLLIB_AMBIGUOUS);
            found=p;
        }
        search(p->getEnumerator(),name,found,remaining,depth+1);
    }
}
uint32_t flags(MyGUI::Widget* p) {
    uint32_t result=TPLLIB_UI_VISIBLE|TPLLIB_UI_ENABLED; unsigned depth=0;
    while(p) {
        if(++depth>128) throw std::runtime_error("UI ancestry limit");
        if(!p->getVisible()) result&=~TPLLIB_UI_VISIBLE;
        if(!p->getEnabled()) result&=~TPLLIB_UI_ENABLED;
        p=p->getParent();
    }
    return result;
}
std::string traceName(const std::string& value) {
    std::string result=value.substr(0,96);
    for(size_t i=0;i<result.size();++i) {
        const unsigned char c=static_cast<unsigned char>(result[i]);
        if(c<32 || c>126 || c=='"') result[i]='?';
    }
    return result;
}
void traceLine(const std::string& value) {
    tpllib::getAPI(TPLLIB_ABI_VERSION)->log(value.c_str());
}
bool traceWidget(MyGUI::Widget* w,const char* relation,unsigned depth,unsigned& remaining) {
    if(!w || !remaining) return false;
    --remaining;
    std::ostringstream line;
    line<<"TPL UI input trace: "<<relation<<" depth="<<depth
        <<" name=\""<<traceName(w->getName())<<"\" type="<<traceName(w->getTypeName())
        <<" visible="<<w->getVisible()<<" enabled="<<w->getEnabled()
        <<" keyFocus="<<w->getNeedKeyFocus()
        <<" keyPress="<<!w->eventKeyButtonPressed.empty()
        <<" keyRelease="<<!w->eventKeyButtonReleased.empty()
        <<" click="<<!w->eventMouseButtonClick.empty();
    MyGUI::MultiListBox* list=w->castType<MyGUI::MultiListBox>(false);
    if(list) line<<" selection="<<!list->eventListChangePosition.empty()
                 <<" accept="<<!list->eventListSelectAccept.empty();
    traceLine(line.str()); return true;
}
void traceTree(MyGUI::Widget* w,unsigned depth,unsigned& remaining) {
    if(depth>12 || !traceWidget(w,"tree",depth,remaining)) return;
    // getEnumerator already follows the client widget. Describe it, but do not
    // traverse it again or duplicate its children in the report.
    MyGUI::Widget* client=w->getClientWidget();
    if(client && client!=w) traceWidget(client,"client",depth,remaining);
    MyGUI::EnumeratorWidgetPtr children=w->getEnumerator();
    while(remaining && children.next()) traceTree(children.current(),depth+1,remaining);
}
void traceMissingLoadInput(MyGUI::Widget* w) {
    // One read-only snapshot per process, with no callbacks, focus changes,
    // captions, list contents, save paths or game-object memory access.
    static bool reported=false;
    if(reported || !suffix(w->getName(),"GamesList") || !(flags(w)&TPLLIB_UI_VISIBLE)) return;
    reported=true;
    try {
        unsigned remaining=96;
        traceLine("TPL UI input trace: BEGIN missing GamesList key delegate; read-only; limit=96 depth=12");
        MyGUI::Widget* focus=MyGUI::InputManager::getInstance().getKeyFocusWidget();
        if(!focus) traceLine("TPL UI input trace: keyboard focus is null");
        for(unsigned depth=0;focus && depth<16;++depth,focus=focus->getParent())
            traceWidget(focus,"key-focus-ancestry",depth,remaining);
        MyGUI::Widget* ancestor=w;
        for(unsigned depth=0;ancestor && depth<16;++depth,ancestor=ancestor->getParent())
            traceWidget(ancestor,"list-ancestry",depth,remaining);
        traceTree(w->getParent()?w->getParent():w,0,remaining);
        traceLine(remaining?"TPL UI input trace: END (skin-only children not enumerated)":
                            "TPL UI input trace: END (node limit reached)");
    } catch(...) { traceLine("TPL UI input trace: snapshot failed; no input was dispatched"); }
}
class MyGuiBackend : public Backend,public MyGUI::IUnlinkWidget {
public:
    MyGUI::Gui* gui;
    unsigned counter;
    MyGuiBackend():gui(0),counter(0){}
    void _unlinkWidget(MyGUI::Widget* p) { destroyed(p); }
    TPLLib_Status find(void* parent,const char* name,void** out) {
        MyGUI::Widget* found=0; unsigned remaining=16384;
        try { search(parent?widget(parent)->getEnumerator():gui->getEnumerator(),name,found,remaining,0); }
        catch(int s) { return s; }
        *out=found; return found?TPLLIB_OK:TPLLIB_NOT_FOUND;
    }
    void describe(void* p,TPLLib_UI_Info& out,void** parent) {
        MyGUI::Widget* w=widget(p); *parent=w->getParent();
        out.rect.left=w->getLeft(); out.rect.top=w->getTop(); out.rect.width=w->getWidth(); out.rect.height=w->getHeight();
        out.flags=flags(w); out.kind=0; out.selected=~uint64_t(0); out.count=0;
        if(!w->eventKeyButtonPressed.empty()) out.flags|=TPLLIB_UI_KEY_HANDLER;
        if(w->castType<MyGUI::Button>(false)) out.kind=TPLLIB_UI_BUTTON;
        MyGUI::MultiListBox* list=w->castType<MyGUI::MultiListBox>(false);
        if(list) {
            out.kind=TPLLIB_UI_LIST; out.selected=list->getIndexSelected(); out.count=list->getItemCount();
            if(!(out.flags&TPLLIB_UI_KEY_HANDLER)) traceMissingLoadInput(w);
        }
    }
    State buttonState(void* p) {
        MyGUI::Button* b=widget(p)->castType<MyGUI::Button>();
        State s; s.rect.left=b->getLeft(); s.rect.top=b->getTop(); s.rect.width=b->getWidth(); s.rect.height=b->getHeight();
        s.caption=b->getCaption().asUTF8(); s.enabled=b->getEnabled(); s.alignment=b->getAlign().getValue(); return s;
    }
    void setButton(void* p,const State& s) {
        MyGUI::Button* b=widget(p)->castType<MyGUI::Button>();
        b->setAlign(s.alignment<0?MyGUI::Align(MyGUI::Align::Default):MyGUI::Align(static_cast<MyGUI::Align::Enum>(s.alignment)));
        b->setCoord(s.rect.left,s.rect.top,s.rect.width,s.rect.height);
        b->setCaption(s.caption); b->setEnabled(s.enabled);
    }
    void* createButton(void* parent,void* style,const State& s) {
        MyGUI::Button* prototype=widget(style)->castType<MyGUI::Button>();
        std::ostringstream name; name<<"TPLLib_UI_"<<++counter;
        MyGUI::Button* b=widget(parent)->createWidget<MyGUI::Button>("Kenshi_Button1",
            s.rect.left,s.rect.top,s.rect.width,s.rect.height,MyGUI::Align::Default,name.str());
        try {
            b->setFontName(prototype->getFontName()); b->setFontHeight(prototype->getFontHeight());
            b->setTextAlign(MyGUI::Align::Center); b->setCaption(s.caption); b->setEnabled(s.enabled);
            b->eventMouseButtonClick+=MyGUI::newDelegate(this,&MyGuiBackend::onClick);
        } catch(...) { gui->destroyWidget(b); throw; }
        return b;
    }
    void destroy(void* p) { gui->destroyWidget(widget(p)); }
    void onClick(MyGUI::Widget* p) { clicked(p); }
    bool clickAllowed(void* p) {
        MyGUI::Widget* focus=MyGUI::InputManager::getInstance().getMouseFocusWidget();
        for(unsigned depth=0;focus && depth<128;++depth,focus=focus->getParent()) if(focus==widget(p)) return true;
        return false;
    }
    bool sameRoot(void* a,void* b) { return root(widget(a))==root(widget(b)); }
    void key(void* p,uint32_t code) {
        widget(p)->eventKeyButtonPressed(widget(p),MyGUI::KeyCode(static_cast<MyGUI::KeyCode::Enum>(code)),0);
    }
};
MyGuiBackend live;
}
bool attach(MyGUI::Gui* gui) {
    if(live.gui) return live.gui==gui;
    const TPLLib_API* host=tpllib::getAPI(TPLLIB_ABI_VERSION);
    if(!gui || !tpllib::initialize(0)) return false;
    // Register before publishing handles so every destruction invalidates them.
    MyGUI::WidgetManager::getInstance().registerUnlinker(&live);
    if(initialize(host,&live)!=TPLLIB_OK) {
        MyGUI::WidgetManager::getInstance().unregisterUnlinker(&live); return false;
    }
    live.gui=gui; return true;
}
}
