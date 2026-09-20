#include "common.h"
static std::wstring home, selectedVersion;
static bool pending=false;
static bool safeVersion(const std::string& s) {
    return !s.empty() && s.size()<64 && s.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-_")==std::string::npos && s!="." && s!="..";
}
static void ready() {
    try {
        if(pending) {
            if(tpl::exists(tpl::join(home,L"current.txt"))) tpl::writeFile(tpl::join(home,L"previous.txt"),tpl::readFile(tpl::join(home,L"current.txt"),128));
            tpl::writeFile(tpl::join(home,L"current.txt"),tpl::narrow(selectedVersion));
            // Leave a concurrently staged newer release queued.
            std::wstring p=tpl::join(home,L"pending.txt");
            if(tpl::exists(p)&&tpl::trim(tpl::readFile(p,128))==tpl::narrow(selectedVersion)) DeleteFileW(p.c_str());
            DeleteFileW(tpl::join(home,L"attempt.txt").c_str());
        }
        tpl::log("TPL main-menu startup acknowledged");
    } catch(const std::exception& e) { tpl::log(e.what()); }
}
extern "C" __declspec(dllexport) void dllStartPlugin() {
    static LONG started=0; if(InterlockedCompareExchange(&started,1,0)!=0) return;
    try {
        // RE_Kenshi may execute a compatibility binary in a child folder while keeping the game cwd.
        wchar_t cwd[32768]; GetCurrentDirectoryW(32768,cwd); std::wstring game=cwd;
        if(!tpl::exists(tpl::join(game,L"Plugins_x64.cfg"))) game=tpl::parent(tpl::modulePath(GetModuleHandleW(0)));
        home=tpl::join(game,L"TPL"); tpl::setLogRoot(game);
        std::string version=tpl::trim(tpl::readFile(tpl::join(home,L"current.txt"),128));
        std::wstring queued=tpl::join(home,L"pending.txt"), attempt=tpl::join(home,L"attempt.txt");
        if(tpl::exists(queued)) {
            std::string candidate=tpl::trim(tpl::readFile(queued,128));
            bool failed=tpl::exists(attempt)&&tpl::trim(tpl::readFile(attempt,128))==candidate;
            if(!failed && safeVersion(candidate)) { version=candidate; pending=true; tpl::writeFile(attempt,version,false); }
            else tpl::log("Pending runtime did not reach its startup checkpoint; using installed version");
        }
        if(!safeVersion(version)) throw std::runtime_error("Invalid runtime version");
        selectedVersion=tpl::widen(version); std::wstring directory=tpl::join(tpl::join(home,L"versions"),selectedVersion);
        if(!tpl::contained(home,directory)) throw std::runtime_error("Runtime path escaped TPL");
        HMODULE runtime=LoadLibraryExW(tpl::join(directory,L"TPL.Runtime.dll").c_str(),0,LOAD_WITH_ALTERED_SEARCH_PATH);
        if(!runtime) throw std::runtime_error("Cannot load TPL runtime");
        typedef int (*Start)(const wchar_t*,const wchar_t*,void(*)());
        Start start=(Start)GetProcAddress(runtime,"TPL_RuntimeStart");
        if(!start || start(game.c_str(),directory.c_str(),ready)!=0) throw std::runtime_error("TPL runtime startup failed; see TPL.log");
    } catch(const std::exception& e) { tpl::log(e.what()); }
}
extern "C" __declspec(dllexport) void dllStopPlugin() {}
