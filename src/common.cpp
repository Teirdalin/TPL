#include "common.h"
#include <stdio.h>
#include <wincrypt.h>

namespace tpl {
static std::wstring logPath;
std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), 0, 0);
    if (!n) throw std::runtime_error("Invalid UTF-8 text");
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), &out[0], n);
    return out;
}
std::string narrow(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), (int)s.size(), 0, 0, 0, 0);
    if (!n) throw std::runtime_error("Invalid UTF-16 text");
    std::string out(n, 0);
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), (int)s.size(), &out[0], n, 0, 0);
    return out;
}
std::wstring lower(std::wstring s) { if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size()); return s; }
std::wstring join(const std::wstring& a, const std::wstring& b) { return a + L"\\" + b; }
std::wstring parent(const std::wstring& p) { return p.substr(0, p.find_last_of(L"\\/")); }
std::wstring filename(const std::wstring& p) { return p.substr(p.find_last_of(L"\\/") + 1); }
std::wstring fullPath(const std::wstring& p) {
    wchar_t buffer[32768]; DWORD n = GetFullPathNameW(p.c_str(), 32768, buffer, 0);
    if (!n || n >= 32768) throw std::runtime_error("Invalid path");
    return buffer;
}
bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
bool directory(const std::wstring& p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
bool contained(const std::wstring& root, const std::wstring& p) {
    std::wstring base = lower(fullPath(root));
    std::wstring target = lower(fullPath(p));
    if (target.compare(0, base.size()+1, base+L"\\") != 0) return false;
    // Reject links along the entire path below the trusted root.
    for (size_t n = base.size()+1; n <= target.size(); ++n) {
        if (n != target.size() && target[n] != L'\\') continue;
        DWORD a = GetFileAttributesW(target.substr(0,n).c_str());
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    }
    DWORD a = GetFileAttributesW(base.c_str());
    return a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_REPARSE_POINT);
}
void makeDirectories(const std::wstring& p) {
    if (directory(p)) return;
    std::wstring up = parent(p);
    if (up != p && !directory(up)) makeDirectories(up);
    if (!CreateDirectoryW(p.c_str(),0) && GetLastError()!=ERROR_ALREADY_EXISTS) throw std::runtime_error("Cannot create directory");
}
std::string readFile(const std::wstring& p, size_t limit) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_DELETE, 0, OPEN_EXISTING, 0, 0);
    if (h == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open file: " + narrow(p));
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h,&size) || size.QuadPart < 0 || (ULONGLONG)size.QuadPart > limit) { CloseHandle(h); throw std::runtime_error("File too large"); }
    std::string data((size_t)size.QuadPart,0); DWORD got = 0;
    bool ok = data.empty() || (ReadFile(h,&data[0],(DWORD)data.size(),&got,0) && got==data.size());
    CloseHandle(h);
    if (!ok) throw std::runtime_error("Cannot read file");
    return data;
}
void writeFile(const std::wstring& p, const std::string& data, bool backup) {
    makeDirectories(parent(p));
    static LONG sequence=0;
    wchar_t suffix[96]; swprintf_s(suffix,L".tpl-%lu-%lu-%lu.tmp",GetCurrentProcessId(),GetTickCount(),(DWORD)InterlockedIncrement(&sequence));
    std::wstring temp = p + suffix;
    HANDLE h = CreateFileW(temp.c_str(),GENERIC_WRITE,0,0,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,0);
    if (h==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot stage file");
    DWORD done=0;
    bool ok = (data.empty() || (WriteFile(h,data.data(),(DWORD)data.size(),&done,0) && done==data.size())) && FlushFileBuffers(h);
    CloseHandle(h);
    if (ok && exists(p) && backup) {
        std::wstring saved = p + suffix + L".bak";
        ok = CopyFileW(p.c_str(),saved.c_str(),TRUE)!=FALSE;
    }
    if (ok) ok = MoveFileExW(temp.c_str(),p.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if (!ok) { DeleteFileW(temp.c_str()); throw std::runtime_error("Cannot save file; original retained"); }
}
std::vector<std::wstring> list(const std::wstring& dir,const wchar_t* pattern,bool dirs) {
    std::vector<std::wstring> out; WIN32_FIND_DATAW f;
    HANDLE h = FindFirstFileW(join(dir,pattern).c_str(),&f);
    if (h==INVALID_HANDLE_VALUE) return out;
    do {
        if (wcscmp(f.cFileName,L".")==0 || wcscmp(f.cFileName,L"..")==0 || (f.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        if (((f.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0)==dirs) out.push_back(join(dir,f.cFileName));
    } while (FindNextFileW(h,&f));
    FindClose(h); std::sort(out.begin(),out.end()); return out;
}
std::vector<std::string> lines(const std::string& s) {
    std::vector<std::string> out; std::istringstream in(s); std::string line;
    while(std::getline(in,line)) { if(!line.empty() && line[line.size()-1]=='\r') line.resize(line.size()-1); out.push_back(line); }
    return out;
}
std::string trim(const std::string& s) { size_t a=s.find_first_not_of(" \t\r\n"), b=s.find_last_not_of(" \t\r\n"); return a==std::string::npos ? "" : s.substr(a,b-a+1); }
std::wstring modulePath(HMODULE h) { wchar_t p[32768]; DWORD n=GetModuleFileNameW(h,p,32768); if(!n||n>=32768) throw std::runtime_error("Cannot locate module"); return p; }
std::string sha256(const std::wstring& path) {
    HCRYPTPROV provider=0; HCRYPTHASH hash=0;
    if(!CryptAcquireContextW(&provider,0,0,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)) throw std::runtime_error("SHA256 unavailable");
    if(!CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)) { CryptReleaseContext(provider,0); throw std::runtime_error("SHA256 unavailable"); }
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);
    bool ok=file!=INVALID_HANDLE_VALUE; BYTE buffer[65536]; DWORD got;
    while(ok) {
        if(!ReadFile(file,buffer,sizeof(buffer),&got,0)) { ok=false; break; }
        if(!got) break;
        ok=CryptHashData(hash,buffer,got,0)!=FALSE;
    }
    if(file!=INVALID_HANDLE_VALUE) CloseHandle(file);
    BYTE digest[32]; DWORD size=32;
    if(ok) ok=CryptGetHashParam(hash,HP_HASHVAL,digest,&size,0)!=FALSE;
    CryptDestroyHash(hash); CryptReleaseContext(provider,0);
    if(!ok) throw std::runtime_error("Cannot hash file");
    static const char hex[]="0123456789ABCDEF"; std::string out;
    for(int i=0;i<32;++i) { out+=hex[digest[i]>>4]; out+=hex[digest[i]&15]; }
    return out;
}
void setLogRoot(const std::wstring& root) { logPath=join(root,L"TPL.log"); }
std::string sha256Bytes(const std::string& bytes) {
    HCRYPTPROV provider=0; HCRYPTHASH hash=0;
    if(!CryptAcquireContextW(&provider,0,0,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)) throw std::runtime_error("SHA256 unavailable");
    bool ok=CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)!=FALSE;
    if(ok) ok=bytes.size()<=MAXDWORD && CryptHashData(hash,reinterpret_cast<const BYTE*>(bytes.data()),static_cast<DWORD>(bytes.size()),0)!=FALSE;
    BYTE digest[32]; DWORD size=32;
    if(ok) ok=CryptGetHashParam(hash,HP_HASHVAL,digest,&size,0)!=FALSE;
    if(hash) CryptDestroyHash(hash);
    CryptReleaseContext(provider,0);
    if(!ok) throw std::runtime_error("Cannot hash bytes");
    static const char hex[]="0123456789ABCDEF";
    std::string result;
    for(unsigned i=0;i<32;++i) { result+=hex[digest[i]>>4]; result+=hex[digest[i]&15]; }
    return result;
}
void log(const char* s) { log(std::string(s)); }
void log(const std::string& s) {
    std::string line=s+"\r\n"; OutputDebugStringA(line.c_str());
    if(logPath.empty()) return;
    HANDLE h=CreateFileW(logPath.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
    if(h!=INVALID_HANDLE_VALUE) { DWORD n; WriteFile(h,line.data(),(DWORD)line.size(),&n,0); CloseHandle(h); }
}
bool patchImport(HMODULE m,const char* symbol,void* replacement,void** original) {
    if(!m) return false;
    BYTE* base=(BYTE*)m; IMAGE_DOS_HEADER* dos=(IMAGE_DOS_HEADER*)base;
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE) return false;
    IMAGE_NT_HEADERS* nt=(IMAGE_NT_HEADERS*)(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE) return false;
    DWORD rva=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if(!rva) return false;
    for(IMAGE_IMPORT_DESCRIPTOR* d=(IMAGE_IMPORT_DESCRIPTOR*)(base+rva); d->Name; ++d) {
        if(!d->OriginalFirstThunk) continue;
        IMAGE_THUNK_DATA* names=(IMAGE_THUNK_DATA*)(base+d->OriginalFirstThunk);
        IMAGE_THUNK_DATA* addresses=(IMAGE_THUNK_DATA*)(base+d->FirstThunk);
        for(; names->u1.AddressOfData; ++names,++addresses) {
            if(IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME* name=(IMAGE_IMPORT_BY_NAME*)(base+names->u1.AddressOfData);
            if(strcmp((char*)name->Name,symbol)) continue;
            void** slot=(void**)&addresses->u1.Function;
            if(*slot==replacement) return true;
            DWORD previous;
            if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&previous)) return false;
            *original=*slot; InterlockedExchangePointer(slot,replacement);
            DWORD ignored; VirtualProtect(slot,sizeof(void*),previous,&ignored);
            return true;
        }
    }
    return false;
}
}
