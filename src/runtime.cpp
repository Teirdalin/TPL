#include "catalog.h"
#include "ui.h"

namespace tpl {
static std::wstring runtimeDirectory;
static HMODULE (WINAPI *originalLoad)(LPCWSTR)=0;
static HMODULE WINAPI reLoad(LPCWSTR path) {
    if(path) {
        try { if(catalog.blocked(path)) { log("RE_Kenshi DLL blocked: "+narrow(path)); SetLastError(ERROR_ACCESS_DISABLED_BY_POLICY); return 0; } }
        catch(...) { log("RE_Kenshi path check failed"); SetLastError(ERROR_INVALID_NAME); return 0; }
    }
    return originalLoad(path);
}
void runUpdater(bool force) {
    std::wstring script=join(runtimeDirectory,L"TPL.Update.ps1");
    if(!exists(script)) throw std::runtime_error("Updater is missing");
    wchar_t windows[MAX_PATH]; GetWindowsDirectoryW(windows,MAX_PATH);
    std::wstring executable=join(windows,L"System32\\WindowsPowerShell\\v1.0\\powershell.exe");
    std::wstring command=L"\""+executable+L"\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \""+script+L"\" -TplHomePath \""+catalog.home+L"\""+(force?L" -Force":L"");
    STARTUPINFOW si; PROCESS_INFORMATION pi; ZeroMemory(&si,sizeof(si)); ZeroMemory(&pi,sizeof(pi));
    si.cb=sizeof(si); si.dwFlags=STARTF_USESHOWWINDOW; si.wShowWindow=SW_HIDE;
    if(!CreateProcessW(executable.c_str(),&command[0],0,0,FALSE,CREATE_NO_WINDOW,0,catalog.game.c_str(),&si,&pi)) throw std::runtime_error("Cannot start updater");
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
}
}
extern "C" __declspec(dllexport) int TPL_RuntimeStart(const wchar_t* game,const wchar_t* runtime,void (*ready)()) {
    try {
        tpl::setLogRoot(game); tpl::runtimeDirectory=runtime;
        std::string executableHash=tpl::sha256(tpl::modulePath(GetModuleHandleW(0)));
        if((executableHash!="A596AB4E407C67B58599C54FFB32DC1BF2B64510CDEBD3FA9359EF05A576AEB1" && executableHash!="504B362CDE850D56AFB1CEA6F5B7B0EE014D9DD7B47E188599D91C804502CD3E") ||
           tpl::sha256(tpl::modulePath(GetModuleHandleW(L"MyGUIEngine_x64.dll")))!="FEA6F1F935703812485BD6606FF4D3CEEA172ECE131ED4C6DCBF7A7E25B083A8") {
            tpl::log("Unsupported game/MyGUI binary; TPL initialization skipped"); return 1;
        }
        tpl::catalog.initialize(game);
        HMODULE re=GetModuleHandleW(L"RE_Kenshi.dll");
        if(re && tpl::sha256(tpl::modulePath(re))=="58900F1678E4EEF407FB19D12527FD8C1D3DBC5BC09903C088DC52A9134A04EE") {
            // This changes only RE_Kenshi's import slot, preserving other LoadLibrary users.
            tpl::catalog.reBridge=tpl::patchImport(re,"LoadLibraryW",(void*)&tpl::reLoad,(void**)&tpl::originalLoad);
            tpl::log(tpl::catalog.reBridge?"RE_Kenshi load filter installed (live compatibility unverified)":"RE_Kenshi load filter unavailable");
        }
        else if(re) tpl::log("Unsupported RE_Kenshi binary; native toggle enforcement unavailable");
        if(!tpl::installUi(ready)) { tpl::log("Missing supported MyGUI initialization import"); return 1; }
        tpl::log("TPL 0.1.0 initialized"); return 0;
    } catch(const std::exception& e) { tpl::log(std::string("TPL initialization failed: ")+e.what()); return 1; }
}
