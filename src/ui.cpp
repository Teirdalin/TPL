#include "catalog.h"
#include "ui.h"
#include "ui_input.h"
#include "tpllib_internal.h"
#include "tpllib_engine.h"
#include "tpllib_ui_mygui.h"
#include <MyGUI_Gui.h>
#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_MultiListBox.h>
#include <MyGUI_ComboBox.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_ResourceTrueTypeFont.h>

namespace tpl {
static MyGUI::Gui* liveGui=0;
static void (*originalInit)(MyGUI::Gui*,const std::string&)=0;
static void (*readyCallback)()=0;
static bool pluginsStarted=false, uiFailed=false;
static MyGUI::Widget* panel=0;
static MyGUI::Widget* updateNotice=0;
static bool noticeShown=false, closeNoticeRequested=false, downloadRequested=false;
static std::string availableVersion;
static MyGUI::MultiListBox* table=0;
static MyGUI::EditBox* search=0;
static MyGUI::TextBox* status=0;
static MyGUI::TextBox* versionStatus=0;
static MyGUI::Button* enableButton=0;
static MyGUI::Button* configButton=0;
static MyGUI::Widget* editorPanel=0;
static MyGUI::ComboBox* fileChoice=0;
static MyGUI::EditBox* editor=0;
static MyGUI::TextBox* editorStatus=0;
static std::vector<std::wstring> configPaths;
static ConfigDocument document;
static size_t selected=MyGUI::ITEM_NONE, openFile=MyGUI::ITEM_NONE;
static bool rebuilding=false, choosing=false;
static DWORD lastStatusPoll=0;
static DWORD lastCatalogRefresh=0;
static bool updatePending=false;
static bool catalogObserved=false, catalogDirty=false;
static bool (*originalKeyPress)(MyGUI::InputManager*,MyGUI::KeyCode,MyGUI::Char)=0;
static bool (*originalKeyRelease)(MyGUI::InputManager*,MyGUI::KeyCode)=0;
static EscapeInput escapeInput;
static UiFrameRequests frameRequests;
static void (*originalFontSize)(MyGUI::ResourceTrueTypeFont*,float)=0;
static void fontSize(MyGUI::ResourceTrueTypeFont* font,float size) {
    frameRequests.fontResized();
    originalFontSize(font,size);
}
static std::string display(const std::string& s) {
    std::string out; for(size_t i=0;i<s.size();++i) { out+=s[i]; if(s[i]=='#') out+='#'; } return out;
}
static MyGUI::Widget* findNamed(MyGUI::EnumeratorWidgetPtr widgets,const std::string& suffix) {
    while(widgets.next()) {
        MyGUI::Widget* w=widgets.current(); const std::string& name=w->getName();
        if(name==suffix || (name.size()>suffix.size() && name.compare(name.size()-suffix.size(),suffix.size(),suffix)==0 && name[name.size()-suffix.size()-1]=='_')) return w;
        MyGUI::Widget* child=findNamed(w->getEnumerator(),suffix); if(child) return child;
    }
    return 0;
}
static MyGUI::Widget* liveWindow(const char* name) {
    return liveGui?findNamed(liveGui->getEnumerator(),name):0;
}
static bool keyPress(MyGUI::InputManager* input,MyGUI::KeyCode key,MyGUI::Char text) {
    if(key==MyGUI::KeyCode::Escape) {
        MyGUI::Widget* notice=liveWindow("TPL_UpdateNotice");
        MyGUI::Widget* config=liveWindow("TPL_Config");
        MyGUI::Widget* mods=liveWindow("TPL_ModPanel");
        EscapeInput::Target active=notice && notice->getVisible()?EscapeInput::Update:
            config && config->getVisible()?EscapeInput::Config:
            (mods && mods->getVisible()?EscapeInput::Mods:EscapeInput::None);
        if(escapeInput.press(active)) return true;
    }
    return originalKeyPress(input,key,text);
}
static bool keyRelease(MyGUI::InputManager* input,MyGUI::KeyCode key) {
    if(key==MyGUI::KeyCode::Escape && escapeInput.release()) return true;
    return originalKeyRelease(input,key);
}
static void destroyPanels() {
    // Independent modal roots outlive the native menu unless explicitly removed.
    MyGUI::Widget* config=liveWindow("TPL_Config");
    MyGUI::Widget* mods=liveWindow("TPL_ModPanel");
    MyGUI::Widget* notice=liveWindow("TPL_UpdateNotice");
    if(notice) {
        MyGUI::InputManager::getInstance().removeWidgetModal(notice);
        liveGui->destroyWidget(notice);
    }
    updateNotice=0; closeNoticeRequested=false; downloadRequested=false;
    if(config) {
        MyGUI::InputManager::getInstance().removeWidgetModal(config);
        liveGui->destroyWidget(config);
    }
    if(mods) {
        MyGUI::InputManager::getInstance().removeWidgetModal(mods);
        liveGui->destroyWidget(mods);
    }
    panel=0; editorPanel=0; table=0; search=0; status=0; versionStatus=0; editor=0;
    fileChoice=0; editorStatus=0; enableButton=0; configButton=0;
    selected=MyGUI::ITEM_NONE; openFile=MyGUI::ITEM_NONE;
    configPaths.clear(); escapeInput.takeClose();
}
static MyGUI::TextBox* label(MyGUI::Widget* root,int x,int y,int w,int h,const std::string& text) {
    MyGUI::TextBox* t=root->createWidget<MyGUI::TextBox>("Kenshi_TextboxStandardText",x,y,w,h,MyGUI::Align::Default);
    t->setCaption(display(text)); t->setNeedMouseFocus(false); return t;
}
static MyGUI::Button* button(MyGUI::Widget* root,int x,int y,int w,int h,const std::string& text,void (*fn)(MyGUI::Widget*),const std::string& name="") {
    MyGUI::Button* b=root->createWidget<MyGUI::Button>("Kenshi_Button1",x,y,w,h,MyGUI::Align::Default,name);
    b->setCaption(text); b->eventMouseButtonClick+=MyGUI::newDelegate(fn); return b;
}
static void setStatus(const std::string& text) { if(status) status->setCaption(display(text)); }
static std::string versionFile(const wchar_t* name,const char* fallback) {
    std::wstring path=join(catalog.home,name);
    return exists(path)?trim(readFile(path,128)):fallback;
}
static void showVersions(bool checking) {
    if(!versionStatus) return;
    std::string current=versionFile(L"current.txt","Unknown");
    std::string latest=checking?"Checking...":versionFile(L"latest.txt","Unavailable");
    versionStatus->setCaption(display("Current: "+current+"    Latest: "+latest));
}
static void selection(MyGUI::MultiListBox*,size_t index) {
    if(rebuilding) return;
    selected=MyGUI::ITEM_NONE;
    if(index!=MyGUI::ITEM_NONE) { size_t* data=table->getItemDataAt<size_t>(index,false); if(data) selected=*data; }
    bool valid=selected<catalog.entries.size();
    configButton->setEnabled(valid && !catalog.configs(selected).empty());
    enableButton->setEnabled(valid && !catalog.entries[selected].missing && catalog.entries[selected].provider!="External");
    enableButton->setStateSelected(valid && catalog.entries[selected].desiredEnabled);
    if(valid) {
        const Entry& e=catalog.entries[selected];
        setStatus(e.name+" | "+e.status+(e.startEnabled!=e.desiredEnabled?" | Restart required":""));
    }
}
static void populate() {
    if(!table) return; rebuilding=true; table->removeAllItems();
    std::wstring query=lower(widen(search->getOnlyText().asUTF8()));
    size_t retained=MyGUI::ITEM_NONE;
    for(size_t i=0;i<catalog.entries.size();++i) {
        Entry& e=catalog.entries[i];
        if(!query.empty() && lower(widen(e.name+" "+e.provider)).find(query)==std::wstring::npos) continue;
        size_t row=table->getItemCount(); table->addItem(display(e.name),i);
        table->setSubItemNameAt(1,row,e.provider);
        table->setSubItemNameAt(2,row,e.status);
        table->setSubItemNameAt(3,row,e.desiredEnabled?"On":"Off");
        table->setSubItemNameAt(4,row,e.startEnabled!=e.desiredEnabled?"Restart":"");
        if(i==selected) retained=row;
    }
    table->setIndexSelected(retained); rebuilding=false; selection(table,retained);
}
static void queryChanged(MyGUI::EditBox*) { populate(); }
static void toggleClicked(MyGUI::Widget*) {
    try { catalog.toggle(selected); populate(); setStatus("Saved. Restart Kenshi to apply mod changes."); }
    catch(const std::exception& e) { setStatus(e.what()); }
}
static void refreshClicked(MyGUI::Widget*) { catalog.observeModules(); populate(); }
static bool dirty() { return editor && editor->getOnlyText().asUTF8()!=document.text; }
static void closeEditor(MyGUI::Widget*) {
    if(!editorPanel) return;
    MyGUI::InputManager::getInstance().resetKeyFocusWidget();
    MyGUI::InputManager::getInstance().removeWidgetModal(editorPanel);
    editorPanel->setVisible(false);
}
static void saveClicked(MyGUI::Widget*) {
    try {
        if(openFile>=configPaths.size() || selected>=catalog.entries.size() || !contained(catalog.entries[selected].root,document.path)) throw std::runtime_error("Config path is no longer inside its mod");
        document.save(editor->getOnlyText().asUTF8());
        editorStatus->setCaption("Saved. Restart Kenshi to apply.");
    } catch(const std::exception& e) { editorStatus->setCaption(display(e.what())); }
}
static void chooseFile(MyGUI::ComboBox*,size_t index) {
    if(choosing || index>=configPaths.size()) return;
    if(openFile!=MyGUI::ITEM_NONE && dirty()) {
        choosing=true; fileChoice->setIndexSelected(openFile); choosing=false;
        editorStatus->setCaption("Unsaved changes. Save or cancel first."); return;
    }
    try {
        ConfigDocument next; next.open(configPaths[index]); document=next;
        editor->setOnlyText(document.text); editor->setEnabled(true); openFile=index; editorStatus->setCaption("");
    } catch(const std::exception& e) {
        openFile=MyGUI::ITEM_NONE; editor->setOnlyText(""); editor->setEnabled(false); editorStatus->setCaption(display(e.what()));
    }
}
static void configClicked(MyGUI::Widget*) {
    if(selected>=catalog.entries.size()) return;
    configPaths=catalog.configs(selected); if(configPaths.empty()) return;
    if(!editorPanel) {
        int w=panel->getWidth(), h=panel->getHeight();
        editorPanel=liveGui->createWidget<MyGUI::Widget>("Kenshi_GenericWindowSkin",panel->getLeft(),panel->getTop(),w,h,MyGUI::Align::Center,"Window","TPL_Config");
        label(editorPanel,16,10,w-32,28,"Configuration");
        fileChoice=editorPanel->createWidget<MyGUI::ComboBox>("Kenshi_ComboBox",16,46,w-32,34,MyGUI::Align::HStretch);
        fileChoice->setComboModeDrop(true); fileChoice->eventComboChangePosition+=MyGUI::newDelegate(chooseFile);
        editor=editorPanel->createWidget<MyGUI::EditBox>("Kenshi_EditBox",16,94,w-32,h-192,MyGUI::Align::Stretch);
        editor->setEditMultiLine(true); editor->setEditWordWrap(false); editor->setMaxTextLength(256*1024);
        editorStatus=label(editorPanel,16,h-88,w-32,32,""); editorStatus->setAlign(MyGUI::Align::HStretch|MyGUI::Align::Bottom);
        MyGUI::Button* save=button(editorPanel,w-228,h-48,100,32,"Save",saveClicked); save->setAlign(MyGUI::Align::Right|MyGUI::Align::Bottom);
        MyGUI::Button* cancel=button(editorPanel,w-116,h-48,100,32,"Cancel",closeEditor); cancel->setAlign(MyGUI::Align::Right|MyGUI::Align::Bottom);
    }
    openFile=MyGUI::ITEM_NONE; editor->setOnlyText(""); choosing=true; fileChoice->removeAllItems();
    for(size_t i=0;i<configPaths.size();++i) fileChoice->addItem(display(narrow(configPaths[i].substr(catalog.entries[selected].root.size()+1))));
    fileChoice->setIndexSelected(0); choosing=false; chooseFile(fileChoice,0);
    editorPanel->setVisible(true);
    MyGUI::InputManager::getInstance().addWidgetModal(editorPanel);
    MyGUI::InputManager::getInstance().setKeyFocusWidget(editor);
}
static void closePanel(MyGUI::Widget*) {
    if(editorPanel && editorPanel->getVisible()) closeEditor(0);
    MyGUI::InputManager::getInstance().resetKeyFocusWidget();
    MyGUI::InputManager::getInstance().removeWidgetModal(panel); panel->setVisible(false);
}
static void checkUpdate(MyGUI::Widget*) {
    try {
        showVersions(true);
        if(!updatePending) { runUpdater(true); updatePending=true; }
        setStatus("Checking GitHub Releases. Updates apply on the next launch.");
    }
    catch(const std::exception& e) { setStatus(e.what()); }
}
static void automaticClicked(MyGUI::Widget* sender) {
    MyGUI::Button* b=sender->castType<MyGUI::Button>(); bool next=!b->getStateSelected();
    try { writeFile(join(catalog.home,L"automatic-updates.txt"),next?"on":"off"); b->setStateSelected(next); }
    catch(const std::exception& e) { setStatus(e.what()); }
}
static void openPanel(MyGUI::Widget*) { frameRequests.openMods(); }
static void dismissUpdate(MyGUI::Widget*) { closeNoticeRequested=true; }
static void downloadUpdate(MyGUI::Widget*) { downloadRequested=true; closeNoticeRequested=true; }
static void showUpdateNotice(MyGUI::Widget* root) {
    if(noticeShown || availableVersion.empty() || !root || !root->getVisible()) return;
    if(MyGUI::InputManager::getInstance().isModalAny()) return;
    if((panel && panel->getVisible()) || (editorPanel && editorPanel->getVisible())) return;
    int w=std::min(560,root->getWidth()-32),h=180;
    if(w<400 || root->getHeight()<h+32) return;
    bool staged=versionFile(L"pending.txt","")==availableVersion;
    updateNotice=liveGui->createWidget<MyGUI::Widget>("Kenshi_GenericWindowSkin",
        root->getAbsoluteLeft()+(root->getWidth()-w)/2,root->getAbsoluteTop()+(root->getHeight()-h)/2,
        w,h,MyGUI::Align::Center,"Window","TPL_UpdateNotice");
    label(updateNotice,16,12,w-32,28,"TPL update available");
    label(updateNotice,16,46,w-32,28,"Installed: "+versionFile(L"current.txt","Unknown")+"    Latest: "+availableVersion);
    label(updateNotice,16,78,w-32,28,staged?"Update downloaded. Restart Kenshi to apply.":"A newer version of TPL is available.");
    if(!staged) button(updateNotice,w-252,h-48,120,32,"Update now",downloadUpdate);
    button(updateNotice,w-120,h-48,104,32,staged?"OK":"Later",dismissUpdate);
    MyGUI::InputManager::getInstance().addWidgetModal(updateNotice);
    noticeShown=true;
}
static void openPanelNow(MyGUI::Widget* sender) {
    try {
        MyGUI::Widget* root=sender->getParent();
        if(!panel) {
            int w=std::min(1120,root->getWidth()-32), h=std::min(760,root->getHeight()-32);
            if(w<600||h<380) { log("Viewport too small for Mods menu"); return; }
            panel=liveGui->createWidget<MyGUI::Widget>("Kenshi_GenericWindowSkin",root->getAbsoluteLeft()+(root->getWidth()-w)/2,root->getAbsoluteTop()+(root->getHeight()-h)/2,w,h,MyGUI::Align::Center,"Window","TPL_ModPanel");
            label(panel,16,12,100,30,"Mods");
            versionStatus=label(panel,120,12,w-136,30,"");
            search=panel->createWidget<MyGUI::EditBox>("Kenshi_EditBox",16,52,w-32,32,MyGUI::Align::HStretch);
            search->setMaxTextLength(160); search->eventEditTextChange+=MyGUI::newDelegate(queryChanged);
            table=panel->createWidget<MyGUI::MultiListBox>("Kenshi_MultiListBox",16,96,w-32,h-260,MyGUI::Align::Stretch);
            table->addColumn("Name",(w-40)*30/100); table->addColumn("Source",(w-40)*20/100);
            table->addColumn("Current status",(w-40)*28/100); table->addColumn("Enabled",(w-40)*12/100); table->addColumn("Pending",(w-40)*10/100);
            table->eventListChangePosition+=MyGUI::newDelegate(selection);
            enableButton=button(panel,16,h-150,32,30,"",toggleClicked);
            enableButton->changeWidgetSkin("Kenshi_TickBoxSkin");
            label(panel,56,h-147,180,28,"Enabled");
            configButton=button(panel,240,h-150,100,30,"Config",configClicked);
            button(panel,352,h-150,100,30,"Refresh",refreshClicked);
            MyGUI::Button* automatic=button(panel,16,h-108,32,30,"",automaticClicked);
            automatic->changeWidgetSkin("Kenshi_TickBoxSkin");
            std::wstring autoFile=join(catalog.home,L"automatic-updates.txt");
            automatic->setStateSelected(exists(autoFile)&&trim(readFile(autoFile))=="on");
            label(panel,56,h-105,180,28,"Automatic updates");
            button(panel,240,h-108,170,30,"Check for updates",checkUpdate);
            status=label(panel,16,h-66,w-150,50,""); status->setTextAlign(MyGUI::Align::Left|MyGUI::Align::Top);
            button(panel,w-116,h-52,100,32,"Close",closePanel);
        }
        populate(); panel->setVisible(true);
        MyGUI::InputManager::getInstance().addWidgetModal(panel);
        log("Mods panel opened");
        showVersions(true);
        checkUpdate(0);
    } catch(const std::exception& e) { log(std::string("Mods UI: ")+e.what()); destroyPanels(); }
}
static void frame(float dt) {
    if(uiFailed || !liveGui) return;
    try {
        if(frameRequests.takeFontRefresh()) {
            // Options can regenerate font textures without recreating our widgets.
            // Their old render batches must be removed before the next GUI draw.
            MyGUI::Widget* modsButton=liveWindow("TPL_ModsButton");
            bool hadWidgets=modsButton || panel || editorPanel;
            destroyPanels();
            if(modsButton) liveGui->destroyWidget(modsButton);
            if(hadWidgets) log("TPL UI rebuilt after native font resize");
        }
        MyGUI::Widget* options=findNamed(liveGui->getEnumerator(),"OptionsButton");
        MyGUI::Widget* continueButton=findNamed(liveGui->getEnumerator(),"ContinueButton");
        if(options && continueButton && options->getParent()==continueButton->getParent()) {
            MyGUI::Widget* root=options->getParent();
            if(!findNamed(root->getEnumerator(),"TPL_ModsButton")) {
                destroyPanels();
                const char* names[]={"ContinueButton","NewGameButton","LoadGameButton","ImportGameButton","OptionsButton","CreditsButton","ExitButton"};
                MyGUI::Widget* peers[7]; bool complete=true;
                for(int i=0;i<7;++i) { peers[i]=findNamed(root->getEnumerator(),names[i]); if(!peers[i]) complete=false; }
                if(complete) {
                    int top=peers[0]->getTop(), bottom=peers[6]->getBottom(), height=options->getHeight();
                    int step=(bottom-top-height)/7;
                    if(step>=height) {
                        for(int i=0;i<7;++i) peers[i]->setPosition(peers[i]->getLeft(),top+(i>=5?i+1:i)*step);
                        MyGUI::Button* b=button(root,options->getLeft(),top+5*step,options->getWidth(),height,"MODS",openPanel,"TPL_ModsButton");
                        MyGUI::Button* style=options->castType<MyGUI::Button>(false);
                        if(style) { b->setFontName(style->getFontName()); b->setFontHeight(style->getFontHeight()); }
                        log("Main-menu Mods button created");
                    }
                }
            }
            if(!pluginsStarted) {
                pluginsStarted=true;
                try {
                    if(!tpllib::initialize(&log)) log("TPLLib foundation unavailable");
                    else {
                        TPLLib_Status engine=tplengine::initialize(tpllib::getAPI(TPLLIB_ABI_VERSION));
                        log(engine==TPLLIB_OK?"TPLLib engine: reviewed native bindings available (live validation pending)":
                            "TPLLib engine bindings unavailable for this game or patched entry points");
                        if(!tplui::attach(liveGui)) log("TPLLib UI service unavailable");
                    }
                }
                catch(...) { log("TPLLib UI service initialization failed; continuing without it"); }
                if(readyCallback) readyCallback();
                std::wstring autoFile=join(catalog.home,L"automatic-updates.txt");
                try {
                    bool automatic=exists(autoFile)&&trim(readFile(autoFile))=="on";
                    runUpdater(true,!automatic); updatePending=true;
                } catch(const std::exception& e) { log(std::string("Startup update check: ")+e.what()); }
            }
        } else if(panel || editorPanel || updateNotice) destroyPanels();
        if(pluginsStarted) {
            bool visible=panel && liveWindow("TPL_ModPanel")==panel && panel->getVisible();
            if(catalog.scanStep(visible?2:1)) catalogDirty=true;
            if(catalog.discoveryComplete() && !catalogObserved) {
                catalog.observeModules(); catalogObserved=true; catalogDirty=true;
            }
            if(catalogObserved && catalog.startPluginStep()) catalogDirty=true;
            if(visible && catalogDirty && GetTickCount()-lastCatalogRefresh>200) {
                lastCatalogRefresh=GetTickCount(); populate(); catalogDirty=false;
            }
        }
        if(frameRequests.takeOpenMods()) {
            MyGUI::Widget* modsButton=liveWindow("TPL_ModsButton");
            if(modsButton) openPanelNow(modsButton);
        }
        // Close after input dispatch, never while MyGUI is traversing a key callback.
        EscapeInput::Target close=escapeInput.takeClose();
        if(close==EscapeInput::Update) closeNoticeRequested=true;
        if(close==EscapeInput::Config && editorPanel && liveWindow("TPL_Config")==editorPanel && editorPanel->getVisible()) closeEditor(0);
        else if(close==EscapeInput::Mods && panel && liveWindow("TPL_ModPanel")==panel && panel->getVisible()) closePanel(0);
        if(closeNoticeRequested) {
            if(updateNotice) {
                MyGUI::InputManager::getInstance().removeWidgetModal(updateNotice);
                liveGui->destroyWidget(updateNotice); updateNotice=0;
            }
            closeNoticeRequested=false;
            if(downloadRequested) {
                downloadRequested=false;
                checkUpdate(0);
                frameRequests.openMods();
            }
        }
        if(updatePending && GetTickCount()-lastStatusPoll>1000) {
            lastStatusPoll=GetTickCount(); std::wstring path=join(catalog.home,L"update-status.txt");
            if(exists(path)) {
                std::string message=trim(readFile(path,4096)); setStatus(message);
                bool checking=updaterRunning();
                showVersions(checking);
                if(!checking) {
                    updatePending=false;
                    std::string latest=versionFile(L"latest.txt","");
                    if(message.find("Update failed:")!=0 && message!="Checking for updates..." &&
                        newerRelease(versionFile(L"current.txt",""),latest)) availableVersion=latest;
                }
            }
        }
        if(options && continueButton && options->getParent()==continueButton->getParent())
            showUpdateNotice(options->getParent());
        catalog.tick(dt);
    } catch(const std::exception& e) { uiFailed=true; log(std::string("TPL UI disabled: ")+e.what()); }
}
static void guiInit(MyGUI::Gui* gui,const std::string& core) {
    originalInit(gui,core);
    try { catalog.refreshLaunchSelection(); } catch(const std::exception& e) { log(e.what()); }
    liveGui=gui; gui->eventFrameStart+=MyGUI::newDelegate(frame);
    log("MyGUI initialized; frame callback attached");
}
bool installUi(void (*ready)()) {
    readyCallback=ready;
    HMODULE game=GetModuleHandleW(0);
    if(!patchImport(game,"?setSize@ResourceTrueTypeFont@MyGUI@@QEAAXM@Z",(void*)&fontSize,(void**)&originalFontSize)) return false;
    if(!patchImport(game,"?injectKeyPress@InputManager@MyGUI@@QEAA_NUKeyCode@2@I@Z",(void*)&keyPress,(void**)&originalKeyPress)) return false;
    if(!patchImport(game,"?injectKeyRelease@InputManager@MyGUI@@QEAA_NUKeyCode@2@@Z",(void*)&keyRelease,(void**)&originalKeyRelease)) return false;
    return patchImport(GetModuleHandleW(0),"?initialise@Gui@MyGUI@@QEAAXAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z",(void*)&guiInit,(void**)&originalInit);
}
}
