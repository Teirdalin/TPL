#include "catalog.h"
#include "legacy.h"
#include "compat.h"
#include "tpllib_internal.h"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <tlhelp32.h>

namespace tpl {
Catalog catalog;
Catalog::Catalog() : rePresent(false),reBridge(false),scanRoot(0),scanHandle(INVALID_HANDLE_VALUE),
    cacheHits(0),cacheMisses(0),scanComplete(true),startupIndex(0),startupPrepared(false),startupComplete(false),legacySuppressed(false) {
    ZeroMemory(&scanData,sizeof(scanData));
}
Catalog::~Catalog() { closeScanner(); }
static std::wstring key(const std::wstring& path) { return lower(fullPath(path)); }
static std::string jsonText(const rapidjson::Value& v,const char* field,const std::string& fallback="") {
    return v.HasMember(field)&&v[field].IsString() ? v[field].GetString() : fallback;
}
static void parse(const std::string& bytes, rapidjson::Document& d) {
    d.Parse<rapidjson::kParseValidateEncodingFlag>(bytes.c_str());
    if(d.HasParseError()||!d.IsObject()) throw std::runtime_error("Invalid JSON object");
}
static void configFiles(const std::wstring& base,const std::wstring& dir,int depth,std::vector<std::wstring>& out) {
    std::vector<std::wstring> files=list(dir,L"*.cfg",false);
    for(size_t i=0;i<files.size() && out.size()<128;++i) if(contained(base,files[i])) out.push_back(files[i]);
    if(depth==0 || out.size()>=128) return;
    std::vector<std::wstring> dirs=list(dir,L"*",true);
    for(size_t i=0;i<dirs.size() && out.size()<128;++i) configFiles(base,dirs[i],depth-1,out);
}
void Catalog::initialize(const std::wstring& root) {
    closeScanner();
    game=fullPath(root); home=join(game,L"TPL"); entries.clear(); active.clear(); disabled.clear(); launchOrder.clear();
    scanRoots.clear(); cachedFolders.clear(); nextCache.clear(); cacheHits=cacheMisses=0; scanRoot=0; scanComplete=false;
    startupQueue.clear(); startupIndex=0; startupPrepared=false; startupComplete=false; legacySuppressed=false;
    std::wstring cfg=join(game,L"data\\mods.cfg");
    cfgSnapshot=exists(cfg)?readFile(cfg):"";
    std::vector<std::string> enabled=lines(cfgSnapshot);
    for(size_t i=0;i<enabled.size();++i) {
        std::string value=trim(enabled[i]);
        if(i==0 && value.compare(0,3,"\xEF\xBB\xBF")==0) value=value.substr(3);
        if(!value.empty() && value[0]!='#' && active.insert(lower(widen(value))).second) launchOrder.push_back(lower(widen(value)));
    }
    std::wstring state=join(home,L"state.json");
    if(exists(state)) {
        rapidjson::Document d; parse(readFile(state),d);
        if(d.HasMember("disabled")&&d["disabled"].IsArray()) {
            for(rapidjson::SizeType i=0;i<d["disabled"].Size();++i) {
                if(!d["disabled"][i].IsString()) throw std::runtime_error("Invalid disabled entry");
                disabled.insert(key(widen(d["disabled"][i].GetString())));
            }
        }
    }
    startupDisabled=disabled;
    loadCache();
    ScanRoot local={join(game,L"mods"),"Local"}; scanRoots.push_back(local);
    ScanRoot workshop={join(parent(parent(game)),L"workshop\\content\\233860"),"Workshop"}; scanRoots.push_back(workshop);
    ScanRoot plugins={join(home,L"plugins"),"Local"}; scanRoots.push_back(plugins);
    rePresent=GetModuleHandleW(L"RE_Kenshi.dll")!=0;
}
void Catalog::closeScanner() {
    if(scanHandle!=INVALID_HANDLE_VALUE) FindClose(scanHandle);
    scanHandle=INVALID_HANDLE_VALUE;
}
void Catalog::finishDiscovery() {
    if(scanComplete) return;
    closeScanner();
    std::set<std::wstring> found;
    for(size_t i=0;i<entries.size();++i) if(!entries[i].plugin) found.insert(lower(entries[i].modFile));
    for(std::set<std::wstring>::const_iterator i=active.begin();i!=active.end();++i) {
        if(found.count(*i)) continue;
        Entry e; e.id=L"missing:"+*i; e.modFile=*i; e.name=narrow(*i); e.provider="FCS";
        e.startEnabled=e.desiredEnabled=true; e.missing=true; e.status="Enabled; folder not found"; entries.push_back(e);
    }
    updateDesired();
    for(size_t i=0;i<entries.size();++i) entries[i].startEnabled=entries[i].desiredEnabled;
    scanComplete=true;
    try { saveCache(); }
    catch(const std::exception& e) { log(std::string("Catalog cache: ")+e.what()); }
    std::ostringstream message; message<<"Catalog cache: "<<cacheHits<<" folder(s) reused, "<<cacheMisses<<" rescanned"; log(message.str());
}
static void appendMetadata(std::ostringstream& value,const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&data)) { value<<"-;"; return; }
    value<<std::hex<<data.dwFileAttributes<<":"<<data.ftLastWriteTime.dwHighDateTime<<":"
        <<data.ftLastWriteTime.dwLowDateTime<<":"<<data.nFileSizeHigh<<":"<<data.nFileSizeLow<<";";
}
std::string Catalog::folderSignature(const std::wstring& root) const {
    std::ostringstream value;
    appendMetadata(value,root); appendMetadata(value,join(root,L"TPL.json")); appendMetadata(value,join(root,L"RE_Kenshi.json"));
    return value.str();
}
void Catalog::loadCache() {
    std::wstring path=join(home,L"catalog-cache.json"); if(!exists(path)) return;
    try {
        rapidjson::Document d; parse(readFile(path,4*1024*1024),d);
        if(!d.HasMember("schema") || !d["schema"].IsInt() || d["schema"].GetInt()!=1 ||
           !d.HasMember("folders") || !d["folders"].IsArray()) return;
        for(rapidjson::SizeType i=0;i<d["folders"].Size();++i) {
            const rapidjson::Value& folder=d["folders"][i];
            if(!folder.IsObject() || !folder.HasMember("path") || !folder["path"].IsString() ||
               !folder.HasMember("signature") || !folder["signature"].IsString() ||
               !folder.HasMember("entries") || !folder["entries"].IsArray()) continue;
            std::wstring root=fullPath(widen(folder["path"].GetString())); CachedFolder cached; cached.signature=folder["signature"].GetString();
            std::wstring owner;
            for(rapidjson::SizeType n=0;n<folder["entries"].Size();++n) {
                const rapidjson::Value& row=folder["entries"][n];
                if(!row.IsObject() || !row.HasMember("plugin") || !row["plugin"].IsBool() ||
                   !row.HasMember("path") || !row["path"].IsString() || !row.HasMember("name") || !row["name"].IsString() ||
                   !row.HasMember("provider") || !row["provider"].IsString()) { cached.entries.clear(); break; }
                Entry e; e.plugin=row["plugin"].GetBool(); e.root=root; e.path=fullPath(widen(row["path"].GetString()));
                e.name=row["name"].GetString(); e.provider=row["provider"].GetString();
                bool provider=e.provider=="TPL" || e.provider=="RE_Kenshi" || e.provider=="RE_Kenshi preload" ||
                    e.provider=="FCS / Local" || e.provider=="FCS / Workshop";
                std::wstring extension=lower(e.path);
                bool kind=e.plugin ? extension.size()>4 && extension.substr(extension.size()-4)==L".dll"
                    : extension.size()>4 && extension.substr(extension.size()-4)==L".mod";
                if(!provider || !kind || !contained(root,e.path)) { cached.entries.clear(); break; }
                e.id=key(e.path);
                if(!e.plugin) { e.modFile=filename(e.path); owner=e.id; }
                cached.entries.push_back(e);
            }
            for(size_t n=0;n<cached.entries.size();++n) if(cached.entries[n].plugin) cached.entries[n].owner=owner;
            cachedFolders[key(root)]=cached;
        }
    } catch(const std::exception& e) { cachedFolders.clear(); log(std::string("Catalog cache ignored: ")+e.what()); }
}
void Catalog::saveCache() const {
    rapidjson::StringBuffer buffer; rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
    w.StartObject(); w.Key("schema"); w.Int(1); w.Key("folders"); w.StartArray();
    for(std::map<std::wstring,CachedFolder>::const_iterator i=nextCache.begin();i!=nextCache.end();++i) {
        w.StartObject(); std::string root=narrow(i->first); w.Key("path"); w.String(root.c_str());
        w.Key("signature"); w.String(i->second.signature.c_str()); w.Key("entries"); w.StartArray();
        for(size_t n=0;n<i->second.entries.size();++n) {
            const Entry& e=i->second.entries[n]; std::string path=narrow(e.path);
            w.StartObject(); w.Key("plugin"); w.Bool(e.plugin); w.Key("path"); w.String(path.c_str());
            w.Key("name"); w.String(e.name.c_str()); w.Key("provider"); w.String(e.provider.c_str()); w.EndObject();
        }
        w.EndArray(); w.EndObject();
    }
    w.EndArray(); w.EndObject(); writeFile(join(home,L"catalog-cache.json"),buffer.GetString(),false);
}
bool Catalog::scanStep(size_t budget) {
    if(scanComplete || budget==0) return false;
    bool changed=false;
    size_t examined=0;
    while(examined<budget && !scanComplete) {
        if(scanHandle==INVALID_HANDLE_VALUE) {
            while(scanRoot<scanRoots.size() && scanHandle==INVALID_HANDLE_VALUE) {
                std::wstring pattern=join(scanRoots[scanRoot].path,L"*");
                scanHandle=FindFirstFileW(pattern.c_str(),&scanData);
                if(scanHandle==INVALID_HANDLE_VALUE) ++scanRoot;
            }
            if(scanRoot>=scanRoots.size()) { finishDiscovery(); break; }
        }
        size_t currentRoot=scanRoot;
        WIN32_FIND_DATAW item=scanData;
        if(!FindNextFileW(scanHandle,&scanData)) {
            closeScanner(); ++scanRoot;
        }
        ++examined;
        if(!(item.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || wcscmp(item.cFileName,L".")==0 || wcscmp(item.cFileName,L"..")==0) continue;
        std::wstring folder=join(scanRoots[currentRoot].path,item.cFileName);
        const std::string origin=scanRoots[currentRoot].origin;
        size_t before=entries.size();
        std::wstring folderKey=key(folder); std::string signature=folderSignature(folder);
        std::map<std::wstring,CachedFolder>::const_iterator cached=cachedFolders.find(folderKey);
        if(cached!=cachedFolders.end() && cached->second.signature==signature) {
            entries.insert(entries.end(),cached->second.entries.begin(),cached->second.entries.end());
            nextCache[folderKey]=cached->second; ++cacheHits;
        } else {
            try { scanFolder(folder,origin); }
            catch(const std::exception& e) { log("Catalog: "+narrow(folder)+": "+e.what()); }
            CachedFolder fresh; fresh.signature=signature;
            fresh.entries.insert(fresh.entries.end(),entries.begin()+before,entries.end());
            nextCache[folderKey]=fresh; ++cacheMisses;
        }
        if(entries.size()!=before) {
            for(size_t i=before;i<entries.size();++i) {
                Entry& e=entries[i]; e.module=0; e.tick=0; e.loaded=e.running=e.attempted=false;
                e.missing=false; e.startEnabled=e.plugin?false:active.count(lower(e.modFile))!=0;
                e.status=e.plugin?"Not loaded":(e.startEnabled?"Enabled at launch":"Disabled at launch");
            }
            updateDesired();
            for(size_t i=before;i<entries.size();++i) entries[i].startEnabled=entries[i].desiredEnabled;
            changed=true;
        }
    }
    if(!scanComplete && scanRoot>=scanRoots.size() && scanHandle==INVALID_HANDLE_VALUE) finishDiscovery();
    return changed;
}
void Catalog::scanFolder(const std::wstring& root,const std::string& origin) {
    std::vector<std::wstring> mods=list(root,L"*.mod",false);
    std::wstring owner;
    if(mods.size()==1) {
        Entry e; e.root=root; e.path=mods[0]; e.id=key(e.path); e.modFile=filename(e.path);
        e.name=narrow(e.modFile.substr(0,e.modFile.size()-4)); e.provider="FCS / "+origin;
        e.startEnabled=active.count(lower(e.modFile))!=0;
        e.status=e.startEnabled?"Enabled at launch":"Disabled at launch";
        entries.push_back(e); owner=e.id;
    } else if(mods.size()>1) {
        log("Ambiguous mod folder omitted: "+narrow(root)); return;
    }
    const wchar_t* manifests[]={L"TPL.json",L"RE_Kenshi.json"};
    for(int m=0;m<2;++m) {
        std::wstring file=join(root,manifests[m]); if(!exists(file)) continue;
        rapidjson::Document d; parse(readFile(file),d);
        const char* groups[]={m==0?"plugins":"Plugins","PreloadPlugins"};
        for(int g=0;g<(m==0?1:2);++g) {
            if(!d.HasMember(groups[g])||!d[groups[g]].IsArray()) continue;
            const rapidjson::Value& array=d[groups[g]];
            for(rapidjson::SizeType n=0;n<array.Size();++n) {
                std::string dll;
                if(m==0 && array[n].IsObject()) dll=jsonText(array[n],"dll");
                else if(m==1 && array[n].IsString()) dll=array[n].GetString();
                if(dll.empty()) continue;
                std::wstring path=fullPath(join(root,widen(dll)));
                if(!contained(root,path) || lower(path).substr(path.size()>4?path.size()-4:0)!=L".dll") { log("Rejected plugin path"); continue; }
                std::wstring id=key(path); bool duplicate=false;
                for(size_t p=0;p<entries.size();++p) if(entries[p].id==id) {
                    if(m==1 && g==1 && entries[p].provider=="RE_Kenshi") entries[p].provider="RE_Kenshi preload";
                    duplicate=true; break;
                }
                if(duplicate) continue;
                Entry e; e.plugin=true; e.root=root; e.path=path; e.id=id; e.owner=owner;
                e.name=m==0?jsonText(array[n],"name",dll):dll;
                e.provider=m==0?"TPL":(g==0?"RE_Kenshi":"RE_Kenshi preload");
                e.missing=!exists(path); e.status=e.missing?"DLL missing":"Not loaded";
                entries.push_back(e);
            }
        }
    }
}
void Catalog::refreshLaunchSelection() {
    // The vanilla launcher may change mods.cfg after Ogre loads TPL.
    std::wstring path=join(game,L"data\\mods.cfg");
    cfgSnapshot=exists(path)?readFile(path):""; active.clear(); launchOrder.clear();
    std::vector<std::string> selection=lines(cfgSnapshot);
    for(size_t i=0;i<selection.size();++i) {
        std::string name=trim(selection[i]);
        if(i==0 && name.compare(0,3,"\xEF\xBB\xBF")==0) name=name.substr(3);
        if(!name.empty() && name[0]!='#' && active.insert(lower(widen(name))).second) launchOrder.push_back(lower(widen(name)));
    }
    for(size_t i=0;i<entries.size();++i) if(!entries[i].plugin) {
        Entry& e=entries[i]; e.startEnabled=active.count(lower(e.modFile))!=0;
        if(e.startEnabled) disabled.erase(e.id);
        e.status=e.missing?"Enabled; folder not found":(e.startEnabled?"Enabled at launch":"Disabled at launch");
    }
    updateDesired();
    // Native preload decisions already made stay fixed for this process.
    for(size_t i=0;i<entries.size();++i) if(!entries[i].attempted && (entries[i].provider=="TPL" || (!rePresent && entries[i].provider=="RE_Kenshi"))) entries[i].startEnabled=entries[i].desiredEnabled;
}
void Catalog::updateDesired() {
    for(size_t i=0;i<entries.size();++i) {
        Entry& e=entries[i];
        if(!e.plugin) e.desiredEnabled=active.count(lower(e.modFile))!=0 && !disabled.count(e.id);
        else {
            e.desiredEnabled=!disabled.count(e.id) && !disabled.count(e.owner);
            if(!e.owner.empty() && e.provider!="RE_Kenshi preload") {
                for(size_t j=0;j<entries.size();++j) if(entries[j].id==e.owner && !active.count(lower(entries[j].modFile))) e.desiredEnabled=false;
            }
        }
    }
}
bool Catalog::blocked(const std::wstring& path) const {
    std::wstring id=key(path);
    for(size_t i=0;i<entries.size();++i) {
        const Entry& e=entries[i];
        if(e.plugin && e.id==id) return !e.startEnabled || e.provider=="TPL";
    }
    if(startupDisabled.count(id)!=0) return true;
    // Parent-mod disables must work even while the catalog is still being
    // discovered incrementally and RE_Kenshi is already requesting DLLs.
    for(std::set<std::wstring>::const_iterator i=startupDisabled.begin();i!=startupDisabled.end();++i) {
        std::wstring disabledId=lower(*i);
        if(disabledId.size()>4 && disabledId.substr(disabledId.size()-4)==L".mod" && parent(disabledId)==parent(id)) return true;
    }
    return false;
}
void Catalog::saveState() {
    rapidjson::StringBuffer buffer; rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
    w.StartObject(); w.Key("disabled"); w.StartArray();
    for(std::set<std::wstring>::const_iterator i=disabled.begin();i!=disabled.end();++i) { std::string s=narrow(*i); w.String(s.c_str()); }
    w.EndArray(); w.EndObject(); writeFile(join(home,L"state.json"),buffer.GetString());
}
void Catalog::toggle(size_t index) {
    if(index>=entries.size()) throw std::runtime_error("No mod selected");
    Entry& e=entries[index];
    if(e.provider=="External") throw std::runtime_error("This plugin is managed outside TPL and RE_Kenshi manifests");
    if(e.missing) throw std::runtime_error("Locate the missing mod before changing it");
    bool hasRe=e.provider.find("RE_Kenshi")==0;
    for(size_t i=0;i<entries.size();++i) if(entries[i].owner==e.id && entries[i].provider.find("RE_Kenshi")==0) hasRe=true;
    if(hasRe && rePresent && !reBridge) throw std::runtime_error("RE_Kenshi blocking is unavailable for this build");
    if(e.plugin && !e.desiredEnabled && !e.owner.empty() && e.provider!="RE_Kenshi preload") {
        for(size_t i=0;i<entries.size();++i) if(entries[i].id==e.owner && !entries[i].desiredEnabled)
            throw std::runtime_error("Enable the parent mod first");
    }
    std::set<std::wstring> oldDisabled=disabled, oldActive=active;
    bool enable=!e.desiredEnabled;
    if(enable) disabled.erase(e.id); else disabled.insert(e.id);
    if(!e.plugin) { if(enable) active.insert(lower(e.modFile)); else active.erase(lower(e.modFile)); }
    try {
        if(!e.plugin) {
            std::wstring p=join(game,L"data\\mods.cfg"); std::string current=exists(p)?readFile(p):"";
            if(current!=cfgSnapshot) throw std::runtime_error("Mod order changed externally; restart before editing");
            std::vector<std::string> oldLines=lines(current); std::string next;
            bool present=false;
            for(size_t i=0;i<oldLines.size();++i) {
                std::string value=trim(oldLines[i]);
                if(i==0 && value.compare(0,3,"\xEF\xBB\xBF")==0) value=value.substr(3);
                if(lower(widen(value))==lower(e.modFile)) { present=true; if(!enable) continue; }
                next+=oldLines[i]+"\r\n";
            }
            if(enable && !present) next+=narrow(e.modFile)+"\r\n";
            // Persist the restart policy first, then the FCS order; restore it if FCS saving fails.
            saveState();
            try { writeFile(p,next); } catch(...) { disabled=oldDisabled; saveState(); throw; }
            cfgSnapshot=next;
        } else saveState();
    } catch(...) { disabled=oldDisabled; active=oldActive; throw; }
    updateDesired();
}
void Catalog::observeModules() {
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());
    if(snap==INVALID_HANDLE_VALUE) return;
    MODULEENTRY32W m; ZeroMemory(&m,sizeof(m)); m.dwSize=sizeof(m);
    if(Module32FirstW(snap,&m)) do {
        std::wstring origin=compatibilityOrigin(m.hModule);
        std::wstring id=key(origin.empty()?m.szExePath:origin); bool found=false;
        for(size_t i=0;i<entries.size();++i) if(entries[i].plugin && entries[i].id==id) {
            entries[i].loaded=true; entries[i].module=m.hModule;
            if(!entries[i].running && !entries[i].attempted) entries[i].status="DLL loaded";
            found=true; break;
        }
        if(!found && (GetProcAddress(m.hModule,"TPL_Start") || GetProcAddress(m.hModule,"?startPlugin@@YAXXZ"))) {
            Entry e; e.plugin=true; e.path=origin.empty()?m.szExePath:origin; e.root=parent(e.path); e.id=id; e.name=narrow(filename(e.path));
            e.provider="External"; e.loaded=true; e.startEnabled=e.desiredEnabled=true; e.status="DLL loaded; unmanaged";
            entries.push_back(e);
        }
    } while(Module32NextW(snap,&m));
    CloseHandle(snap);
}
void Catalog::startPlugins() {
    while(!discoveryComplete()) scanStep(256);
    if(pluginStartupComplete()) startPluginStep();
    while(!pluginStartupComplete()) startPluginStep();
}
void Catalog::queueOrderedPlugins(bool legacy) {
    const char* provider=legacy?"RE_Kenshi":"TPL";
    std::vector<std::wstring> owners;
    for(size_t n=0;n<launchOrder.size();++n) {
        std::wstring owner; size_t count=0;
        for(size_t i=0;i<entries.size();++i) if(!entries[i].plugin && lower(entries[i].modFile)==launchOrder[n]) { owner=entries[i].id; ++count; }
        if(count==1) owners.push_back(owner);
        else if(count>1) for(size_t i=0;i<entries.size();++i) {
            Entry& e=entries[i];
            for(size_t j=0;j<entries.size();++j) if(entries[j].id==e.owner && lower(entries[j].modFile)==launchOrder[n] && e.provider==provider) {
                e.attempted=true; e.status="Ambiguous mod folders; skipped";
            }
        }
    }
    owners.push_back(L"");
    for(size_t n=0;n<owners.size();++n) for(size_t i=0;i<entries.size();++i) {
        Entry& e=entries[i];
        if(e.provider==provider && e.owner==owners[n] && e.startEnabled && !e.missing && !e.attempted)
            startupQueue.push_back(std::make_pair(i,legacy));
    }
}
std::vector<std::string> Catalog::startupWarnings() const {
    std::vector<std::string> warnings;
    std::map<std::wstring,size_t> first;
    for(size_t i=0;i<entries.size();++i) {
        const Entry& e=entries[i];
        if(!e.plugin || e.missing || (!e.startEnabled && !e.loaded)) continue;
        std::wstring dll=lower(filename(e.path));
        std::map<std::wstring,size_t>::const_iterator found=first.find(dll);
        if(found==first.end()) first[dll]=i;
        else {
            const Entry& other=entries[found->second];
            if(other.id!=e.id)
                warnings.push_back("Possible duplicate plugin (matching DLL filename, not proof of conflict): "+
                    narrow(other.path)+" ["+other.provider+"] and "+narrow(e.path)+" ["+e.provider+"]");
        }
        if(e.provider=="TPL" && e.loaded && !e.attempted)
            warnings.push_back("TPL plugin already loaded before TPL startup; its initializer will not be called again: "+narrow(e.path));
    }
    return warnings;
}
void Catalog::preparePluginStartup() {
    if(startupPrepared) return;
    startupPrepared=true;
    const std::vector<std::string> warnings=startupWarnings();
    for(size_t i=0;i<warnings.size();++i) log("[Startup check] "+warnings[i]);
    log("[Startup check] RE_Kenshi.dll "+std::string(GetModuleHandleW(L"RE_Kenshi.dll")?"loaded":"not loaded")+
        "; loader presence alone is not a conflict. Hook compatibility is checked when requested.");
    if(!tpllib::initialize(&log)) { log("TPLLib initialization failed; plugin startup skipped"); startupComplete=true; return; }
    queueOrderedPlugins(false);
    rePresent=rePresent || GetModuleHandleW(L"RE_Kenshi.dll")!=0;
    if(!rePresent) {
        queueOrderedPlugins(true);
        for(size_t i=0;i<entries.size();++i) {
            Entry& e=entries[i];
            if(e.provider=="RE_Kenshi preload" && e.startEnabled && !e.attempted) {
                e.attempted=true; e.status="Requires early preload support; skipped"; log(e.name+": "+e.status);
            }
        }
    } else legacySuppressed=true;
    startupComplete=startupQueue.empty();
}
bool Catalog::startPluginStep() {
    if(!scanComplete) return false;
    preparePluginStartup();
    if(startupComplete && legacySuppressed) {
        if(GetModuleHandleW(L"RE_Kenshi.dll")) { rePresent=true; return false; }
        if(!rePresent) {
            legacySuppressed=false; startupQueue.clear(); startupIndex=0;
            queueOrderedPlugins(true);
            for(size_t i=0;i<entries.size();++i) {
                Entry& e=entries[i];
                if(e.provider=="RE_Kenshi preload" && e.startEnabled && !e.attempted) {
                    e.attempted=true; e.status="Requires early preload support; skipped"; log(e.name+": "+e.status);
                }
            }
            startupComplete=startupQueue.empty();
        }
    }
    if(startupComplete) return false;
    std::pair<size_t,bool> next=startupQueue[startupIndex++];
    if(next.first<entries.size()) {
        if(next.second && GetModuleHandleW(L"RE_Kenshi.dll")) {
            rePresent=true;
            while(startupIndex<startupQueue.size() && startupQueue[startupIndex].second) ++startupIndex;
        } else startEntry(entries[next.first],next.second);
    }
    if(startupIndex>=startupQueue.size()) startupComplete=true;
    return true;
}
void Catalog::startEntry(Entry& e,bool legacy) {
    static TPL_Host host={sizeof(TPL_Host),TPL_ABI_VERSION,0,&log,&tpllib::getAPI}; host.game_directory=game.c_str();
    e.attempted=true;
    log("[Plugin start] "+e.name+" ["+e.provider+"] "+narrow(e.path));
    try {
        if(GetModuleHandleW(e.path.c_str())) { e.status="Already loaded; initialization skipped"; return; }
        HMODULE h=0; bool bridged=false;
        if(legacy) {
            CompatibilityPlan plan=compatibilityPlan(e.path,e.root,game);
            if(plan.needsBridge) {
                std::wstring report=compatibilityReport(e.path,game,plan);
                log(e.name+": compatibility report "+narrow(report));
            }
            if(!plan.eligible) { e.status=plan.reason; log(e.name+": "+e.status); return; }
            if(plan.needsBridge) {
                std::wstring cachedPath;
                h=loadCompatibility(e.path,e.root,game,plan,cachedPath); bridged=true;
            }
        }
        if(!h) h=LoadLibraryExW(e.path.c_str(),0,LOAD_WITH_ALTERED_SEARCH_PATH);
        if(!h) { DWORD error=GetLastError(); std::ostringstream message; message<<"DLL load failed (Windows "<<error<<")"; e.status=message.str(); log(e.name+": "+e.status); return; }
        e.module=h; e.loaded=true;
        if(legacy) {
            typedef void (*LegacyStart)();
            LegacyStart start=(LegacyStart)GetProcAddress(h,"?startPlugin@@YAXXZ");
            if(!start) { e.status="Missing legacy startPlugin export"; log(e.name+": "+e.status); return; }
            start(); e.running=true; e.status=bridged?"Legacy start returned (TPLLib bridge)":"Legacy start returned (TPL)"; log(e.name+": "+e.status); return;
        }
        TPL_StartFn start=(TPL_StartFn)GetProcAddress(h,"TPL_Start");
        if(!start) { e.status="Missing TPL_Start"; return; }
        int result=start(&host);
        if(result!=0) {
            std::ostringstream message; message<<"Initialization failed (plugin return "<<result<<"); see TPL.log";
            e.status=message.str(); log(e.name+": "+e.status+". Return codes are plugin-defined, not TPLLib status codes."); return;
        }
        e.tick=(TPL_TickFn)GetProcAddress(h,"TPL_Tick"); e.running=true; e.status="Running";
    } catch(const std::exception& error) { e.status=std::string("Initialization exception: ")+error.what(); log(e.name+": "+e.status); }
    catch(...) { e.status="Initialization threw an exception"; log(e.name+": initialization exception"); }
}
void Catalog::tick(float dt) {
    tpllib::frame(dt);
    for(size_t i=0;i<entries.size();++i) if(entries[i].running && entries[i].tick) {
        try { entries[i].tick(dt); } catch(...) { entries[i].tick=0; entries[i].status="Tick failed; callbacks stopped"; }
    }
}
std::vector<std::wstring> Catalog::configs(size_t index) const {
    std::vector<std::wstring> out;
    if(index<entries.size() && !entries[index].root.empty() && entries[index].provider!="External") configFiles(entries[index].root,entries[index].root,4,out);
    return out;
}
void ConfigDocument::open(const std::wstring& file) {
    path=file; original=readFile(path,256*1024); text=original;
    bom=text.compare(0,3,"\xEF\xBB\xBF")==0; if(bom) text=text.substr(3);
    if(text.find('\0')!=std::string::npos) throw std::runtime_error("Binary or UTF-16 config: edit externally");
    widen(text); crlf=text.find("\r\n")!=std::string::npos;
    std::string normalized;
    for(size_t i=0;i<text.size();++i) if(text[i]!='\r' || i+1==text.size() || text[i+1]!='\n') normalized+=text[i];
    text=normalized;
}
void ConfigDocument::save(const std::string& value) {
    if(value.size()>256*1024 || value.find('\0')!=std::string::npos) throw std::runtime_error("Config exceeds the text limit");
    widen(value);
    if(readFile(path,256*1024)!=original) throw std::runtime_error("File changed externally; reopen it before saving");
    if(value==text) return;
    std::string bytes=bom?"\xEF\xBB\xBF":"";
    for(size_t i=0;i<value.size();++i) { if(crlf && value[i]=='\n' && (i==0||value[i-1]!='\r')) bytes+='\r'; bytes+=value[i]; }
    writeFile(path,bytes); original=bytes; text=value;
}
}
