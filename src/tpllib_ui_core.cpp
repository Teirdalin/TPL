// SPDX-License-Identifier: LicenseRef-JDL-1
// Copyright (c) 2026 Teirdalin.
#include "tpllib_ui_core.h"
#include <string.h>
#include <limits.h>
namespace tplui {
namespace {
const unsigned SESSION_LIMIT=32,WIDGET_LIMIT=2048;
struct Session { TPLLib_Token id; std::string name; Session():id(0){} };
struct Widget {
    TPLLib_Token id,session;
    void* pointer;
    bool created,leased;
    State before,applied;
    TPLLib_UI_Click click;
    void* user;
    Widget():id(0),session(0),pointer(0),created(false),leased(false),click(0),user(0){}
};
Session sessions[SESSION_LIMIT]; Widget widgets[WIDGET_LIMIT];
const TPLLib_API* host=0; Backend* backend=0;
TPLLib_Token sequence=1,provider=0,activeSession=0,activeWidget=0;
bool keySent=false;
TPLLib_Status guard() { return !host || !backend?TPLLIB_NOT_READY:(!host->is_main_thread()?TPLLIB_WRONG_THREAD:TPLLIB_OK); }
TPLLib_Token next() { return sequence==~TPLLib_Token(0)?0:sequence++; }
Session* session(TPLLib_Token id) {
    for(unsigned i=0;i<SESSION_LIMIT;++i) if(id && sessions[i].id==id) return &sessions[i];
    return 0;
}
Widget* widget(TPLLib_Token id) {
    for(unsigned i=0;i<WIDGET_LIMIT;++i) if(id && widgets[i].id==id) return &widgets[i];
    return 0;
}
Widget* byPointer(void* p) {
    for(unsigned i=0;i<WIDGET_LIMIT;++i) if(p && widgets[i].pointer==p) return &widgets[i];
    return 0;
}
Widget* track(void* p) {
    if(!p) return 0;
    Widget* existing=byPointer(p); if(existing) return existing;
    for(unsigned i=0;i<WIDGET_LIMIT;++i) if(!widgets[i].id) {
        TPLLib_Token id=next(); if(!id) return 0;
        widgets[i]=Widget(); widgets[i].id=id; widgets[i].pointer=p; return &widgets[i];
    }
    return 0;
}
bool text(const char* p,size_t maximum,bool empty=false) {
    if(!p || (!empty && !*p)) return false;
    for(size_t i=0;i<=maximum;++i) if(!p[i]) return true;
    return false;
}
bool rectValid(const TPLLib_UI_Rect* r) {
    return r && r->width>0 && r->height>0 && r->width<=32768 && r->height<=32768 &&
        r->left>=-32768 && r->left<=32768 && r->top>=-32768 && r->top<=32768;
}
bool equal(const State& a,const State& b) {
    return a.caption==b.caption && a.enabled==b.enabled && a.alignment==b.alignment &&
        memcmp(&a.rect,&b.rect,sizeof(a.rect))==0;
}
bool usable(const TPLLib_UI_Info& info) {
    return (info.flags&(TPLLIB_UI_VISIBLE|TPLLIB_UI_ENABLED))==(TPLLIB_UI_VISIBLE|TPLLIB_UI_ENABLED);
}
TPLLib_Status open(const char* name,TPLLib_Token* out) {
    if(!out) return TPLLIB_INVALID; *out=0;
    TPLLib_Status s=guard(); if(s) return s;
    if(!text(name,95)) return TPLLIB_INVALID;
    unsigned slot=SESSION_LIMIT;
    for(unsigned i=0;i<SESSION_LIMIT;++i) {
        if(sessions[i].id && sessions[i].name==name) return TPLLIB_CONFLICT;
        if(!sessions[i].id && slot==SESSION_LIMIT) slot=i;
    }
    if(slot==SESSION_LIMIT) return TPLLIB_LIMIT;
    try {
        sessions[slot].name=name; sessions[slot].id=next();
        if(!sessions[slot].id) return TPLLIB_LIMIT;
        *out=sessions[slot].id; return TPLLIB_OK;
    } catch(...) { return TPLLIB_BACKEND_ERROR; }
}
TPLLib_Status close(TPLLib_Token id) {
    TPLLib_Status s=guard(); if(s) return s;
    Session* owner=session(id); if(!owner) return TPLLIB_NOT_FOUND;
    if(activeSession) return TPLLIB_BUSY;
    TPLLib_Status result=TPLLIB_OK;
    for(unsigned i=0;i<WIDGET_LIMIT;++i) if(widgets[i].session==id) {
        Widget& w=widgets[i];
        try {
            if(w.created) backend->destroy(w.pointer);
            else if(w.leased) {
                if(equal(backend->buttonState(w.pointer),w.applied)) backend->setButton(w.pointer,w.before);
                else result=TPLLIB_CONFLICT;
            }
            w.session=0; w.leased=false; w.click=0; w.user=0;
        } catch(...) { return TPLLIB_BACKEND_ERROR; }
    }
    owner->id=0; owner->name.clear(); return result;
}
TPLLib_Status find(TPLLib_Token parent,const char* name,TPLLib_Token* out) {
    if(!out) return TPLLIB_INVALID; *out=0;
    TPLLib_Status s=guard(); if(s) return s;
    if(!text(name,255)) return TPLLIB_INVALID;
    Widget* root=widget(parent); if(parent && !root) return TPLLIB_NOT_FOUND;
    try {
        void* p=0; s=backend->find(root?root->pointer:0,name,&p); if(s) return s;
        Widget* w=track(p); if(!w) return p?TPLLIB_LIMIT:TPLLIB_NOT_FOUND;
        *out=w->id; return TPLLIB_OK;
    } catch(...) { return TPLLIB_BACKEND_ERROR; }
}
TPLLib_Status info(TPLLib_Token id,TPLLib_UI_Info* out) {
    if(!out || out->size<sizeof(*out)) return TPLLIB_INVALID;
    TPLLib_Status s=guard(); if(s) return s;
    Widget* w=widget(id); if(!w) return TPLLIB_NOT_FOUND;
    try {
        TPLLib_UI_Info value={0}; value.size=sizeof(value); void* parent=0;
        backend->describe(w->pointer,value,&parent);
        if(parent) { Widget* p=track(parent); if(!p) return TPLLIB_LIMIT; value.parent=p->id; }
        *out=value; return TPLLIB_OK;
    } catch(...) { return TPLLIB_BACKEND_ERROR; }
}
TPLLib_Status create(TPLLib_Token owner,TPLLib_Token parent,TPLLib_Token style,
    const TPLLib_UI_Rect* rect,const char* caption,TPLLib_UI_Click click,void* user,TPLLib_Token* out) {
    if(!out) return TPLLIB_INVALID; *out=0;
    TPLLib_Status s=guard(); if(s) return s;
    if(!session(owner)) return TPLLIB_NOT_FOUND;
    if(!rectValid(rect) || !text(caption,1024,true) || !click) return TPLLIB_INVALID;
    Widget* p=widget(parent); Widget* prototype=widget(style); if(!p || !prototype) return TPLLIB_NOT_FOUND;
    TPLLib_UI_Info description={0}; description.size=sizeof(description);
    s=info(style,&description); if(s) return s;
    if(description.kind!=TPLLIB_UI_BUTTON) return TPLLIB_INVALID;
    void* created=0;
    try {
        State state; state.rect=*rect; state.caption=caption; state.enabled=true;
        created=backend->createButton(p->pointer,prototype->pointer,state);
        Widget* w=track(created);
        if(!w) { if(created) backend->destroy(created); return created?TPLLIB_LIMIT:TPLLIB_BACKEND_ERROR; }
        w->session=owner; w->created=true; w->click=click; w->user=user;
        *out=w->id; return TPLLIB_OK;
    } catch(...) { if(created) { try { backend->destroy(created); } catch(...) {} } return TPLLIB_BACKEND_ERROR; }
}
TPLLib_Status set(TPLLib_Token owner,TPLLib_Token id,const TPLLib_UI_Rect* rect,const char* caption,int enabled) {
    TPLLib_Status s=guard(); if(s) return s;
    if(!session(owner)) return TPLLIB_NOT_FOUND;
    if(!rectValid(rect) || !text(caption,1024,true) || (enabled!=0 && enabled!=1)) return TPLLIB_INVALID;
    Widget* w=widget(id); if(!w) return TPLLIB_NOT_FOUND;
    if(w->session && w->session!=owner) return TPLLIB_CONFLICT;
    TPLLib_UI_Info description={0}; description.size=sizeof(description);
    s=info(id,&description); if(s) return s;
    if(description.kind!=TPLLIB_UI_BUTTON) return TPLLIB_INVALID;
    try {
        State current=backend->buttonState(w->pointer);
        if(w->leased && !equal(current,w->applied)) return TPLLIB_CONFLICT;
        if(!w->created && !w->leased) { w->before=current; w->applied=current; w->session=owner; w->leased=true; }
        State next=current; next.rect=*rect; next.caption=caption; next.enabled=enabled!=0; next.alignment=-1;
        try { backend->setButton(w->pointer,next); }
        catch(...) {
            Widget* live=widget(id);
            if(live) { try { backend->setButton(live->pointer,current); live->applied=current; } catch(...) {} }
            return TPLLIB_BACKEND_ERROR;
        }
        w=widget(id); if(!w) return TPLLIB_NOT_FOUND;
        w->applied=backend->buttonState(w->pointer); return TPLLIB_OK;
    } catch(...) { return TPLLIB_BACKEND_ERROR; }
}
TPLLib_Status key(TPLLib_Token owner,TPLLib_Token id,uint32_t code) {
    TPLLib_Status s=guard(); if(s) return s;
    if(!session(owner)) return TPLLIB_NOT_FOUND;
    if(code!=TPLLIB_UI_DELETE_KEY) return TPLLIB_INVALID;
    if(activeSession!=owner || !activeWidget || keySent) return TPLLIB_CONFLICT;
    Widget* source=widget(activeWidget); Widget* target=widget(id);
    if(!source || !target) return TPLLIB_NOT_FOUND;
    try {
        TPLLib_UI_Info state={0}; state.size=sizeof(state);
        s=info(id,&state); if(s) return s;
        if(state.kind!=TPLLIB_UI_LIST) return TPLLIB_INVALID;
        if(!usable(state) || !backend->clickAllowed(source->pointer) || !backend->sameRoot(source->pointer,target->pointer)) return TPLLIB_CONFLICT;
        if(!(state.flags&TPLLIB_UI_KEY_HANDLER)) return TPLLIB_NOT_FOUND;
        keySent=true; backend->key(target->pointer,code); return TPLLIB_OK;
    } catch(...) { return TPLLIB_CALLBACK_ERROR; }
}
const TPLLib_UI_API table={sizeof(table),TPLLIB_UI_VERSION,&open,&close,&find,&info,&create,&set,&key};
}
const TPLLib_UI_API* api() { return &table; }
TPLLib_Status initialize(const TPLLib_API* h,Backend* b) {
    if(!h || h->size<sizeof(TPLLib_API) || h->abi_version!=TPLLIB_ABI_VERSION || !b) return TPLLIB_INVALID;
    if(!h->is_main_thread()) return TPLLIB_WRONG_THREAD;
    if(host || backend) return h==host && b==backend?TPLLIB_OK:TPLLIB_CONFLICT;
    TPLLib_Status s=h->owner_open("tpl.ui.provider",&provider); if(s) return s;
    s=h->publish_service(provider,TPLLIB_UI_SERVICE,TPLLIB_UI_VERSION,&table,sizeof(table));
    if(s) { h->owner_close(provider); provider=0; return s; }
    host=h; backend=b; return TPLLIB_OK;
}
void destroyed(void* pointer) {
    Widget* w=byPointer(pointer); if(w) *w=Widget();
}
void clicked(void* pointer) {
    if(guard()!=TPLLIB_OK || activeSession) return;
    Widget* w=byPointer(pointer); if(!w || !w->created || !w->click || !session(w->session)) return;
    const TPLLib_Token id=w->id;
    try {
        TPLLib_UI_Info state={0}; state.size=sizeof(state);
        if(info(id,&state)!=TPLLIB_OK || !usable(state) || !backend->clickAllowed(pointer)) return;
        activeSession=w->session; activeWidget=id; keySent=false;
        w->click(w->user,id);
    } catch(...) {
        Widget* live=widget(id); if(live) live->click=0;
        host->log("TPLLib UI: button callback threw; callback disabled");
    }
    activeSession=0; activeWidget=0; keySent=false;
}
}
