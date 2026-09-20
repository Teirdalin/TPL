// SPDX-License-Identifier: LicenseRef-JDL-1
// Copyright (c) 2026 Teirdalin.
#include "compat.h"
#include "tpllib_internal.h"
#include "TPL.CompatPayload.h"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <map>
#include <set>

namespace tpl {
namespace {
struct Alias { const char* legacy; const char* native; };
const Alias aliases[]={
#define TPL_COMPAT_ALIAS(legacy,native,entry) {legacy,native},
#include "compat_aliases.inc"
#undef TPL_COMPAT_ALIAS
};
std::map<HMODULE,std::wstring> origins;
bool reInstalled(const std::wstring& game) {
    return GetModuleHandleW(L"RE_Kenshi.dll") || exists(join(game,L"RE_Kenshi.dll")) || exists(join(game,L"RE_Kenshi\\RE_Kenshi.dll"));
}
const Alias* alias(const std::string& name) {
    for(unsigned i=0;i<sizeof(aliases)/sizeof(aliases[0]);++i) if(name==aliases[i].legacy) return &aliases[i];
    return 0;
}
std::string textField(const rapidjson::Value& v,const char* field) {
    if(!v.IsObject() || !v.HasMember(field) || !v[field].IsString()) return "";
    return std::string(v[field].GetString(),v[field].GetStringLength());
}
bool boolField(const rapidjson::Value& v,const char* field,bool expected) {
    return v.HasMember(field) && v[field].IsBool() && v[field].GetBool()==expected;
}
std::string profileProblem(const std::wstring& path,const std::wstring& root,const CompatibilityPlan& plan) {
    std::wstring file=join(root,L"TPL.Compat.json");
    if(!contained(root,path) || !contained(root,file)) return "Compatibility profile path is not contained";
    if(!exists(file)) return "Mapped imports, but no hash-pinned TPL.Compat.json profile";
    std::string bytes=readFile(file,256*1024);
    if(bytes.find('\0')!=std::string::npos) return "Invalid compatibility profile text";
    rapidjson::Document d; d.Parse<rapidjson::kParseValidateEncodingFlag>(bytes.c_str());
    if(d.HasParseError() || !d.IsObject() || !d.HasMember("schema") || !d["schema"].IsInt() || d["schema"].GetInt()!=1 ||
       !d.HasMember("plugins") || !d["plugins"].IsArray() || d["plugins"].Size()>256) return "Invalid TPL.Compat.json schema";
    const rapidjson::Value* selected=0;
    for(rapidjson::SizeType i=0;i<d["plugins"].Size();++i) {
        const rapidjson::Value& p=d["plugins"][i];
        std::string name=textField(p,"dll");
        if(name.empty() || name.find('\0')!=std::string::npos) return "Invalid compatibility DLL path";
        std::wstring candidate=join(root,widen(name));
        if(!contained(root,candidate)) return "Compatibility DLL path escaped its mod folder";
        if(lower(fullPath(candidate))!=lower(fullPath(path))) continue;
        if(selected) return "Ambiguous compatibility profile";
        selected=&p;
    }
    if(!selected) return "No compatibility profile for this DLL";
    if(textField(*selected,"abi")!="vc100-x64") return "Unsupported compatibility ABI profile";
    if(textField(*selected,"pluginSha256")!=plan.pluginHash || textField(*selected,"gameSha256")!=plan.gameHash)
        return "Compatibility profile hash mismatch";
    if(!boolField(*selected,"allowCacheRelocation",true) || !boolField(*selected,"dynamicKenshiLibLookups",false))
        return "Profile must approve cache relocation and exclude dynamic KenshiLib lookups";
    return "";
}
std::string unknownDependency(const LegacyImage& image) {
    for(size_t i=0;i<image.imports.size();++i) {
        std::wstring name=lower(widen(image.imports[i].module));
        if(name==L"kenshilib.dll") continue;
        if(name==L"tpl.kl.dll") return "Plugin already imports the private compatibility adapter";
        if(name.find(L"api-ms-win-")==0 || name.find(L"ext-ms-win-")==0) continue;
        if(!GetModuleHandleW(name.c_str())) return "Compatibility requires preloaded dependency: "+image.imports[i].module;
    }
    return "";
}
std::string identity(const std::wstring& path,const CompatibilityPlan& plan) {
    // Short directory keys fit legacy Win32 paths; full hashes remain in reports.
    return sha256Bytes(plan.pluginHash+":"+narrow(lower(fullPath(path)))).substr(0,32);
}
void cacheFile(const std::wstring& base,const std::wstring& path,const std::string& bytes) {
    if(!contained(base,path)) throw std::runtime_error("Compatibility cache path escaped TPL");
    if(path.size()>MAX_PATH-48) throw std::runtime_error("Compatibility cache path is too long for this Win32 build");
    if(exists(path)) {
        if(readFile(path,128*1024*1024)!=bytes) throw std::runtime_error("Compatibility cache differs; retained for inspection");
    } else writeFile(path,bytes,false);
}
}
CompatibilityPlan compatibilityPlan(const std::wstring& dll,const std::wstring& root,const std::wstring& game) {
    CompatibilityPlan p;
    try {
        std::string bytes=readFile(dll,128*1024*1024);
        p.pluginHash=sha256Bytes(bytes);
        LegacyImage image=legacyImage(bytes);
        bool delayed=false;
        std::set<std::string> mapped,missing;
        for(size_t i=0;i<image.imports.size();++i) if(lower(widen(image.imports[i].module))==L"kenshilib.dll") {
            p.needsBridge=true; delayed=delayed || image.imports[i].delayed;
            for(size_t n=0;n<image.imports[i].symbols.size();++n) {
                const LegacySymbol& s=image.imports[i].symbols[n];
                if(s.byOrdinal) { std::ostringstream v; v<<"ordinal #"<<s.ordinal; missing.insert(v.str()); }
                else if(alias(s.name)) mapped.insert(s.name); else missing.insert(s.name);
            }
        }
        p.mapped.assign(mapped.begin(),mapped.end()); p.missing.assign(missing.begin(),missing.end());
        if(!p.needsBridge) { p.reason=legacyProblem(dll,game); p.eligible=p.reason.empty(); return p; }
        if(reInstalled(game)) p.reason="RE_Kenshi is installed or loaded; compatibility bridge inactive";
        else if(GetModuleHandleW(L"KenshiLib.dll")) p.reason="Real KenshiLib is already loaded; compatibility bridge inactive";
        else if(!p.missing.empty()) {
            std::ostringstream v; v<<"KenshiLib: "<<p.missing.size()<<" unmapped imports; first: "<<p.missing[0]; p.reason=v.str();
        }
        else if(p.mapped.empty()) p.reason="KenshiLib import has no supported symbols";
        else if(image.signedImage || image.boundImports) p.reason="Signed or bound-import DLLs cannot use cached compatibility";
        else if(delayed) p.reason="Delayed KenshiLib imports require a separate adapter path";
        else if(!image.hasLegacyStart) p.reason="No local legacy startPlugin export";
        else if(lower(filename(dll))==L"tpl.kl.dll") p.reason="Private adapter filename is reserved";
        else p.reason=legacyProblem(dll,game,true);
        if(p.reason.empty()) p.reason=unknownDependency(image);
        if(p.reason.empty()) {
            p.gameHash=sha256(modulePath(GetModuleHandleW(0)));
            p.reason=profileProblem(dll,root,p);
        }
        p.eligible=p.reason.empty();
    } catch(const std::exception& e) { p.reason=e.what(); }
    return p;
}
std::wstring compatibilityReport(const std::wstring& dll,const std::wstring& game,const CompatibilityPlan& p) {
    rapidjson::StringBuffer buffer; rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
    w.StartObject(); w.Key("schema"); w.Int(1); w.Key("plugin"); w.String(narrow(dll).c_str());
    w.Key("pluginSha256"); w.String(p.pluginHash.c_str()); w.Key("gameSha256"); w.String(p.gameHash.c_str());
    w.Key("needsBridge"); w.Bool(p.needsBridge); w.Key("eligible"); w.Bool(p.eligible);
    w.Key("reason"); w.String(p.reason.c_str());
    w.Key("mapped"); w.StartArray();
    for(size_t i=0;i<p.mapped.size();++i) {
        w.StartObject(); w.Key("from"); w.String(p.mapped[i].c_str());
        w.Key("to"); w.String(alias(p.mapped[i])->native); w.EndObject();
    }
    w.EndArray(); w.Key("missing"); w.StartArray();
    for(size_t i=0;i<p.missing.size();++i) w.String(p.missing[i].c_str());
    w.EndArray(); w.EndObject();
    std::wstring home=join(game,L"TPL");
    std::wstring path=join(home,L"compat-reports\\"+widen(identity(dll,p))+L".json");
    if(!contained(game,path)) throw std::runtime_error("Compatibility report path escaped game directory");
    writeFile(path,buffer.GetString(),false); return path;
}
bool compatibilityOwns(HMODULE module) {
    return origins.find(module)!=origins.end();
}
std::wstring compatibilityOrigin(HMODULE module) {
    std::map<HMODULE,std::wstring>::const_iterator i=origins.find(module);
    return i==origins.end()?L"":i->second;
}
HMODULE loadCompatibility(const std::wstring& dll,const std::wstring& root,const std::wstring& game,
    const CompatibilityPlan& planned,std::wstring& loadedPath) {
    if(!planned.needsBridge || !planned.eligible) throw std::runtime_error("Compatibility plan is not eligible");
    for(std::map<HMODULE,std::wstring>::const_iterator i=origins.begin();i!=origins.end();++i)
        if(i->second==lower(fullPath(dll))) throw std::runtime_error("Original DLL already bridged; initialization skipped");
    if(!tpllib::getAPI(1)->is_main_thread()) throw std::runtime_error("Compatibility startup requires the GUI thread");
    CompatibilityPlan fresh=compatibilityPlan(dll,root,game);
    if(!fresh.eligible || !fresh.needsBridge || fresh.pluginHash!=planned.pluginHash || fresh.gameHash!=planned.gameHash)
        throw std::runtime_error("Compatibility preflight changed: "+fresh.reason);
    std::string original=readFile(dll,128*1024*1024);
    if(sha256Bytes(original)!=fresh.pluginHash) throw std::runtime_error("Plugin changed during compatibility preparation");
    std::string payload(reinterpret_cast<const char*>(compatibilityPayload),sizeof(compatibilityPayload));
    std::wstring home=join(game,L"TPL");
    std::wstring cache=join(home,L"compat-cache\\"+widen(sha256Bytes(payload).substr(0,32)));
    std::wstring adapter=join(cache,L"TPL.KL.dll");
    if(!contained(game,adapter)) throw std::runtime_error("Compatibility cache path escaped game directory");
    HMODULE bridge=GetModuleHandleW(L"TPL.KL.dll");
    if(bridge && lower(fullPath(modulePath(bridge)))!=lower(fullPath(adapter))) throw std::runtime_error("Another private adapter is already loaded");
    cacheFile(game,adapter,payload);
    loadedPath=join(join(cache,widen(identity(dll,fresh))),filename(dll));
    std::string converted=redirectKenshiImports(original,"TPL.KL.dll");
    cacheFile(game,loadedPath,converted);
    if(GetModuleHandleW(loadedPath.c_str())) throw std::runtime_error("Converted DLL already loaded; initialization skipped");
    if(reInstalled(game) || GetModuleHandleW(L"KenshiLib.dll")) throw std::runtime_error("RE_Kenshi/KenshiLib appeared during compatibility preparation");
    if(!bridge) bridge=LoadLibraryExW(adapter.c_str(),0,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!bridge) throw std::runtime_error("Cannot load independent compatibility adapter");
    typedef int (*Initialize)(const TPLLib_API*);
    Initialize initialize=reinterpret_cast<Initialize>(GetProcAddress(bridge,"TPL_KL_Initialize"));
    if(!initialize || initialize(tpllib::getAPI(TPLLIB_ABI_VERSION))) throw std::runtime_error("Compatibility adapter ABI mismatch");
    for(size_t i=0;i<fresh.mapped.size();++i)
        if(!GetProcAddress(bridge,fresh.mapped[i].c_str())) throw std::runtime_error("Compatibility adapter export missing: "+fresh.mapped[i]);
    if(reInstalled(game) || GetModuleHandleW(L"KenshiLib.dll")) throw std::runtime_error("RE_Kenshi/KenshiLib appeared before plugin load");
    HMODULE module=LoadLibraryExW(loadedPath.c_str(),0,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!module) { std::ostringstream error; error<<"Converted DLL load failed (Windows "<<GetLastError()<<")"; throw std::runtime_error(error.str()); }
    origins[module]=lower(fullPath(dll));
    return module;
}
}
