// SPDX-License-Identifier: LicenseRef-JDL-1
// Copyright (c) 2026 Teirdalin.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#include <float.h>
#include <string.h>
#include <sstream>
#include "tpllib_internal.h"
#include "tpllib_foreign_profiles.h"
#include <MinHook.h>

namespace tpllib {
namespace {
const unsigned OWNER_COUNT=128, HOOK_COUNT=256, JOB_COUNT=1024, FRAME_COUNT=256, SERVICE_COUNT=128;
struct Owner { TPLLib_Token id; char name[96]; bool conflictReported; };
struct Job { TPLLib_Token id, owner; TPLLib_Job fn; void* user; };
struct Frame { TPLLib_Token id, owner; TPLLib_Frame fn; void* user; };
struct Service { TPLLib_Token owner; char name[96]; uint32_t version, bytes; const void* table; };
struct Hook {
    TPLLib_Token id, owner;
    void* target;
    void* detour;
    bool enabled, snapshotValid;
    int group;
    unsigned char before[48], after[48];
};
const unsigned FOREIGN_DEPTH=4;
struct ForeignLinkGuard {
    void* block;
    void* detour;
    unsigned length;
    unsigned char bytes[44], detourBytes[32];
};
struct ForeignGuard {
    void* target;
    unsigned count;
    unsigned char entry[32];
    ForeignLinkGuard links[FOREIGN_DEPTH];
};
struct SharedTarget {
    Hook patch;
    void* trampoline;
    unsigned char expected[TPLLIB_HOOK_BYTES];
    ForeignGuard foreign;
};
Owner owners[OWNER_COUNT]={0};
Job jobs[JOB_COUNT]={0};
Frame callbacks[FRAME_COUNT]={0};
Service services[SERVICE_COUNT]={0};
Hook hooks[HOOK_COUNT]={0};
SharedTarget sharedTargets[HOOK_COUNT]={0};
unsigned char* routes=0;
size_t routePage=0;
CRITICAL_SECTION queueLock;
volatile LONG initialized=0;
DWORD mainThread=0;
TPLLib_Token nextToken=1, activeOwner=0;
uint64_t frameNumber=0, callbackFailures=0;
bool dispatching=false, hooksInitialized=false;
#ifdef TPLLIB_TESTING
bool failHookSnapshot=false;
const ForeignHookProfile* testForeignProfile=0;
unsigned testForeignProfileCount=0;
#endif
void (*logger)(const char*)=0;
struct Lock {
    Lock() { EnterCriticalSection(&queueLock); }
    ~Lock() { LeaveCriticalSection(&queueLock); }
};
bool ready() { return InterlockedCompareExchange(&initialized,0,0)==2; }
int isMainThread() { return ready() && GetCurrentThreadId()==mainThread; }
TPLLib_Status mainOnly() { return !ready()?TPLLIB_NOT_READY:(!isMainThread()?TPLLIB_WRONG_THREAD:TPLLIB_OK); }
bool hasOwner(TPLLib_Token id) {
    if(!id) return false;
    for(unsigned i=0;i<OWNER_COUNT;++i) if(owners[i].id==id) return true;
    return false;
}
bool nameValid(const char* s) {
    if(!s || !s[0]) return false;
    for(unsigned i=0;i<96;++i) {
        unsigned char c=static_cast<unsigned char>(s[i]);
        if(!c) return true;
        if(c<33 || c>126) return false;
    }
    return false;
}
TPLLib_Token token() { return nextToken==~TPLLib_Token(0)?0:nextToken++; }
const char* statusText(TPLLib_Status code) {
    static const char* names[]={"OK","Invalid argument","Not initialized","GUI thread required",
        "Not found","Build or ABI mismatch","Ambiguous signature","Unreadable memory",
        "Ownership or patch conflict","Capacity reached","Backend failure","Callback threw",
        "Resource still in use"};
    return code>=0 && code<sizeof(names)/sizeof(names[0])?names[code]:"Unknown status";
}
TPLLib_Status readMemory(const void* from,void* to,size_t bytes) {
    if(!from || !to || !bytes || bytes>16*1024*1024 || uintptr_t(from)>~uintptr_t(0)-bytes) return TPLLIB_INVALID;
    SIZE_T got=0;
    return ReadProcessMemory(GetCurrentProcess(),from,to,bytes,&got) && got==bytes?TPLLIB_OK:TPLLIB_UNREADABLE;
}
bool executable(const void* p) {
    MEMORY_BASIC_INFORMATION m;
    if(!VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
    return (m.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))!=0;
}
struct Module {
    HMODULE handle;
    IMAGE_NT_HEADERS64 nt;
    Module() : handle(0) { ZeroMemory(&nt,sizeof(nt)); }
    ~Module() { if(handle) FreeLibrary(handle); }
    TPLLib_Status open(const wchar_t* name) {
        if(!GetModuleHandleExW(0,name,&handle)) return TPLLIB_NOT_FOUND;
        IMAGE_DOS_HEADER dos;
        if(readMemory(handle,&dos,sizeof(dos)) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<0 || dos.e_lfanew>1024*1024) return TPLLIB_INVALID;
        if(readMemory(reinterpret_cast<char*>(handle)+dos.e_lfanew,&nt,sizeof(nt)) || nt.Signature!=IMAGE_NT_SIGNATURE ||
           nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
           nt.FileHeader.SizeOfOptionalHeader!=sizeof(IMAGE_OPTIONAL_HEADER64) || !nt.FileHeader.NumberOfSections ||
           nt.FileHeader.NumberOfSections>96 || nt.OptionalHeader.SizeOfImage<sizeof(nt) || nt.OptionalHeader.SizeOfImage>512*1024*1024) return TPLLIB_INVALID;
        return TPLLIB_OK;
    }
    bool pin() {
        HMODULE pinned=0;
        return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<const wchar_t*>(handle),&pinned)!=FALSE;
    }
};
TPLLib_Status fingerprint(HMODULE module,char result[65]) {
    wchar_t path[32768];
    DWORD n=GetModuleFileNameW(module,path,32768);
    if(!n || n>=32768) return TPLLIB_BACKEND_ERROR;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);
    if(file==INVALID_HANDLE_VALUE) return TPLLIB_BACKEND_ERROR;
    HCRYPTPROV provider=0; HCRYPTHASH hash=0;
    bool ok=CryptAcquireContextW(&provider,0,0,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)!=FALSE;
    if(ok) ok=CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)!=FALSE;
    BYTE chunk[65536]; DWORD got=0;
    while(ok) {
        if(!ReadFile(file,chunk,sizeof(chunk),&got,0)) { ok=false; break; }
        if(!got) break;
        ok=CryptHashData(hash,chunk,got,0)!=FALSE;
    }
    BYTE digest[32]; DWORD size=sizeof(digest);
    if(ok) ok=CryptGetHashParam(hash,HP_HASHVAL,digest,&size,0)!=FALSE;
    if(hash) CryptDestroyHash(hash);
    if(provider) CryptReleaseContext(provider,0);
    CloseHandle(file);
    if(!ok) return TPLLIB_BACKEND_ERROR;
    const char* hex="0123456789ABCDEF";
    for(unsigned i=0;i<32;++i) { result[2*i]=hex[digest[i]>>4]; result[2*i+1]=hex[digest[i]&15]; }
    result[64]=0; return TPLLIB_OK;
}
bool hashValid(const char* hash) {
    if(!hash) return false;
    for(unsigned i=0;i<64;++i) if(!((hash[i]>='0' && hash[i]<='9') || (hash[i]>='a' && hash[i]<='f') || (hash[i]>='A' && hash[i]<='F'))) return false;
    return hash[64]==0;
}
TPLLib_Status checkBuild(Module& module,const wchar_t* name,const char* sha) {
    if(!hashValid(sha)) return TPLLIB_INVALID;
    TPLLib_Status s=module.open(name); if(s) return s;
    char hash[65]; s=fingerprint(module.handle,hash); if(s) return s;
    return _stricmp(hash,sha)?TPLLIB_VERSION_MISMATCH:TPLLIB_OK;
}
bool patternValid(const uint8_t* bytes,const uint8_t* mask,uint32_t count) {
    if(!bytes || !mask || !count || count>256) return false;
    for(unsigned i=0;i<count;++i) if(mask[i]) return true;
    return false;
}
bool matches(const unsigned char* data,const uint8_t* bytes,const uint8_t* mask,uint32_t count) {
    for(unsigned i=0;i<count;++i) if((data[i]&mask[i])!=(bytes[i]&mask[i])) return false;
    return true;
}
TPLLib_Status moduleInfo(const wchar_t* name,TPLLib_Module* out) {
    if(!out || out->size<sizeof(*out)) return TPLLIB_INVALID;
    Module m; TPLLib_Status s=m.open(name); if(s) return s;
    TPLLib_Module value={0}; value.size=sizeof(value); value.base=reinterpret_cast<uintptr_t>(m.handle);
    value.image_size=m.nt.OptionalHeader.SizeOfImage; value.timestamp=m.nt.FileHeader.TimeDateStamp;
    value.machine=m.nt.FileHeader.Machine;
    s=fingerprint(m.handle,value.sha256); if(!s) *out=value; return s;
}
TPLLib_Status resolveRva(const wchar_t* name,const char* sha,uint32_t rva,const uint8_t* bytes,const uint8_t* mask,uint32_t count,void** out) {
    if(!out) return TPLLIB_INVALID; *out=0;
    if(!patternValid(bytes,mask,count)) return TPLLIB_INVALID;
    Module m; TPLLib_Status s=checkBuild(m,name,sha); if(s) return s;
    if(rva>=m.nt.OptionalHeader.SizeOfImage || count>m.nt.OptionalHeader.SizeOfImage-rva) return TPLLIB_INVALID;
    unsigned char data[256]; void* p=reinterpret_cast<char*>(m.handle)+rva;
    s=readMemory(p,data,count); if(s) return s;
    if(!matches(data,bytes,mask,count)) return TPLLIB_CONFLICT;
    if(!m.pin()) return TPLLIB_BACKEND_ERROR;
    *out=p; return TPLLIB_OK;
}
TPLLib_Status findUnique(const wchar_t* name,const char* sha,const uint8_t* bytes,const uint8_t* mask,uint32_t count,void** out) {
    if(!out) return TPLLIB_INVALID; *out=0;
    if(!patternValid(bytes,mask,count)) return TPLLIB_INVALID;
    Module m; TPLLib_Status s=checkBuild(m,name,sha); if(s) return s;
    IMAGE_DOS_HEADER dos;
    if(readMemory(m.handle,&dos,sizeof(dos))) return TPLLIB_UNREADABLE;
    size_t sectionOffset=dos.e_lfanew+sizeof(m.nt);
    void* found=0;
    // Scan executable sections in overlapping windows, evaluating each start only once.
    unsigned char chunk[65536+256];
    for(unsigned i=0;i<m.nt.FileHeader.NumberOfSections;++i) {
        IMAGE_SECTION_HEADER section;
        if(sectionOffset>m.nt.OptionalHeader.SizeOfImage-sizeof(section)) return TPLLIB_INVALID;
        if(readMemory(reinterpret_cast<char*>(m.handle)+sectionOffset,&section,sizeof(section))) return TPLLIB_UNREADABLE;
        sectionOffset+=sizeof(section);
        if(!(section.Characteristics&IMAGE_SCN_MEM_EXECUTE)) continue;
        size_t length=section.Misc.VirtualSize;
        if(section.VirtualAddress>=m.nt.OptionalHeader.SizeOfImage || length>m.nt.OptionalHeader.SizeOfImage-section.VirtualAddress) return TPLLIB_INVALID;
        if(length<count) continue;
        for(size_t offset=0;offset<=length-count;) {
            size_t starts=length-count-offset+1; if(starts>65536) starts=65536;
            char* base=reinterpret_cast<char*>(m.handle)+section.VirtualAddress+offset;
            s=readMemory(base,chunk,starts+count-1); if(s) return s;
            for(size_t j=0;j<starts;++j) if(matches(chunk+j,bytes,mask,count)) {
                if(found) return TPLLIB_AMBIGUOUS;
                found=base+j;
            }
            offset+=starts;
        }
    }
    if(!found) return TPLLIB_NOT_FOUND;
    if(!m.pin()) return TPLLIB_BACKEND_ERROR;
    *out=found; return TPLLIB_OK;
}
TPLLib_Status ownerOpen(const char* name,TPLLib_Token* out) {
    if(!out) return TPLLIB_INVALID; *out=0;
    TPLLib_Status s=mainOnly(); if(s) return s;
    if(!nameValid(name)) return TPLLIB_INVALID;
    Lock lock; unsigned slot=OWNER_COUNT;
    for(unsigned i=0;i<OWNER_COUNT;++i) {
        if(owners[i].id && !strcmp(name,owners[i].name)) return TPLLIB_CONFLICT;
        if(!owners[i].id && slot==OWNER_COUNT) slot=i;
    }
    if(slot==OWNER_COUNT) return TPLLIB_LIMIT;
    TPLLib_Token id=token(); if(!id) return TPLLIB_LIMIT;
    owners[slot].id=id; owners[slot].conflictReported=false; strcpy_s(owners[slot].name,name); *out=id; return TPLLIB_OK;
}
TPLLib_Status ownerClose(TPLLib_Token owner) {
    TPLLib_Status s=mainOnly(); if(s) return s;
    Lock lock;
    if(!hasOwner(owner)) return TPLLIB_NOT_FOUND;
    if(activeOwner==owner) return TPLLIB_BUSY;
    for(unsigned i=0;i<HOOK_COUNT;++i) if(hooks[i].id && hooks[i].owner==owner) return TPLLIB_BUSY;
    for(unsigned i=0;i<SERVICE_COUNT;++i) if(services[i].owner==owner) return TPLLIB_BUSY;
    for(unsigned i=0;i<JOB_COUNT;++i) if(jobs[i].owner==owner) jobs[i].id=0;
    for(unsigned i=0;i<FRAME_COUNT;++i) if(callbacks[i].owner==owner) callbacks[i].id=0;
    for(unsigned i=0;i<OWNER_COUNT;++i) if(owners[i].id==owner) owners[i].id=0;
    return TPLLIB_OK;
}
TPLLib_Status post(TPLLib_Token owner,TPLLib_Job fn,void* user,TPLLib_Token* out) {
    if(!out) return TPLLIB_INVALID; *out=0;
    if(!ready()) return TPLLIB_NOT_READY;
    if(!fn) return TPLLIB_INVALID;
    Lock lock; if(!hasOwner(owner)) return TPLLIB_NOT_FOUND;
    for(unsigned i=0;i<JOB_COUNT;++i) if(!jobs[i].id) {
        TPLLib_Token id=token(); if(!id) return TPLLIB_LIMIT;
        jobs[i].id=id; jobs[i].owner=owner; jobs[i].fn=fn; jobs[i].user=user; *out=id; return TPLLIB_OK;
    }
    return TPLLIB_LIMIT;
}
TPLLib_Status cancelJob(TPLLib_Token owner,TPLLib_Token id) {
    if(!ready()) return TPLLIB_NOT_READY;
    Lock lock;
    for(unsigned i=0;i<JOB_COUNT;++i) if(id && jobs[i].id==id) {
        if(jobs[i].owner!=owner) return TPLLIB_CONFLICT;
        jobs[i].id=0; return TPLLIB_OK;
    }
    return TPLLIB_NOT_FOUND;
}
TPLLib_Status subscribe(TPLLib_Token owner,TPLLib_Frame fn,void* user,TPLLib_Token* out) {
    if(!out) return TPLLIB_INVALID; *out=0;
    TPLLib_Status s=mainOnly(); if(s) return s;
    if(!hasOwner(owner)) return TPLLIB_NOT_FOUND;
    if(!fn) return TPLLIB_INVALID;
    Lock lock;
    for(unsigned i=0;i<FRAME_COUNT;++i) if(!callbacks[i].id) {
        TPLLib_Token id=token(); if(!id) return TPLLIB_LIMIT;
        callbacks[i].id=id; callbacks[i].owner=owner; callbacks[i].fn=fn; callbacks[i].user=user; *out=id; return TPLLIB_OK;
    }
    return TPLLIB_LIMIT;
}
TPLLib_Status unsubscribe(TPLLib_Token owner,TPLLib_Token id) {
    TPLLib_Status s=mainOnly(); if(s) return s;
    for(unsigned i=0;i<FRAME_COUNT;++i) if(id && callbacks[i].id==id) {
        if(callbacks[i].owner!=owner) return TPLLIB_CONFLICT;
        callbacks[i].id=0; return TPLLIB_OK;
    }
    return TPLLIB_NOT_FOUND;
}
TPLLib_Status publish(TPLLib_Token owner,const char* name,uint32_t version,const void* table,uint32_t bytes) {
    TPLLib_Status s=mainOnly(); if(s) return s;
    if(!hasOwner(owner)) return TPLLIB_NOT_FOUND;
    if(!nameValid(name) || !version || !table || !bytes) return TPLLIB_INVALID;
    unsigned slot=SERVICE_COUNT;
    for(unsigned i=0;i<SERVICE_COUNT;++i) {
        if(services[i].owner && !strcmp(services[i].name,name) && services[i].version==version) return TPLLIB_CONFLICT;
        if(!services[i].owner && slot==SERVICE_COUNT) slot=i;
    }
    if(slot==SERVICE_COUNT) return TPLLIB_LIMIT;
    services[slot].owner=owner; strcpy_s(services[slot].name,name); services[slot].version=version;
    services[slot].table=table; services[slot].bytes=bytes; return TPLLIB_OK;
}
TPLLib_Status query(const char* name,uint32_t version,uint32_t bytes,const void** out) {
    if(!out) return TPLLIB_INVALID; *out=0;
    TPLLib_Status s=mainOnly(); if(s) return s;
    if(!nameValid(name) || !version || !bytes) return TPLLIB_INVALID;
    bool named=false;
    for(unsigned i=0;i<SERVICE_COUNT;++i) if(services[i].owner && !strcmp(services[i].name,name)) {
        named=true;
        if(services[i].version==version && services[i].bytes>=bytes) { *out=services[i].table; return TPLLIB_OK; }
    }
    return named?TPLLIB_VERSION_MISMATCH:TPLLIB_NOT_FOUND;
}
bool pinAddress(void* address) {
    HMODULE m=0;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(address),&m)!=FALSE;
}
// Immutable, register-preserving x64 tail jumps use aligned writable pointer
// slots on a separate non-executable page. No route is freed during the process.
bool initializeRoutes() {
    if(routes) return true;
    SYSTEM_INFO info; GetSystemInfo(&info);
    size_t page=info.dwPageSize;
    if(page<HOOK_COUNT*2*8 || page>0x100000) return false;
    unsigned char* memory=static_cast<unsigned char*>(VirtualAlloc(0,page*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!memory) return false;
    for(unsigned i=0;i<HOOK_COUNT*2;++i) {
        unsigned char* code=memory+i*8;
        int32_t offset=static_cast<int32_t>(page-6);
        code[0]=0xff; code[1]=0x25; memcpy(code+2,&offset,sizeof(offset));
        code[6]=0xcc; code[7]=0xcc;
    }
    DWORD previous=0;
    if(!VirtualProtect(memory,page,PAGE_EXECUTE_READ,&previous) ||
       !FlushInstructionCache(GetCurrentProcess(),memory,page)) {
        VirtualFree(memory,0,MEM_RELEASE); return false;
    }
    routePage=page; routes=memory; return true;
}
void* route(unsigned index) { return routes+index*8; }
void routeTo(unsigned index,void* destination) {
    InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(routes+routePage+index*8),destination);
}
TPLLib_Status selectForeignDestination(void* target,const uint8_t* expected,void* destination,const TPLLib_ForeignHook** out) {
    *out=0;
    TPLLib_Status unmatched=TPLLIB_CONFLICT;
    const ForeignHookProfile* profiles=foreignHookProfiles;
    unsigned count=sizeof(foreignHookProfiles)/sizeof(foreignHookProfiles[0]);
#ifdef TPLLIB_TESTING
    if(testForeignProfile) { profiles=testForeignProfile; count=testForeignProfileCount; }
#endif
    for(unsigned i=0;i<count;++i) {
        const ForeignHookProfile& p=profiles[i];
        HMODULE module=GetModuleHandleW(p.targetModule);
        if(!module || uintptr_t(target)!=uintptr_t(module)+p.targetRva) continue;
        // A target address is not an owner identity. Absent/unrelated optional
        // plugins must never become implicit dependencies of another plugin.
        HMODULE foreign=GetModuleHandleW(p.foreign.module_name);
        if(!foreign || uintptr_t(destination)!=uintptr_t(foreign)+p.foreign.detour_rva) continue;
        // Different releases can share a module name and detour RVA.
        Module foreignBuild;
        TPLLib_Status s=checkBuild(foreignBuild,p.foreign.module_name,p.foreign.module_sha256);
        if(s==TPLLIB_VERSION_MISMATCH) { unmatched=s; continue; }
        if(s) return s;
        Module verified;
        s=checkBuild(verified,p.targetModule,p.targetSha256);
        if(s==TPLLIB_VERSION_MISMATCH) { unmatched=s; continue; }
        if(s) return s;
        if(memcmp(expected,p.expected,TPLLIB_HOOK_BYTES)) return TPLLIB_CONFLICT;
        *out=&p.foreign; return TPLLIB_OK;
    }
    return unmatched;
}
bool absoluteJump(const unsigned char* bytes,void** destination) {
    uint32_t offset=1; memcpy(&offset,bytes+2,4);
    if(bytes[0]!=0xff || bytes[1]!=0x25 || offset) return false;
    memcpy(destination,bytes+6,sizeof(*destination)); return true;
}
TPLLib_Status selectForeignProfile(void* target,const uint8_t* expected,const TPLLib_ForeignHook** out) {
    *out=0;
    unsigned char entry[32],relay[14];
    if(readMemory(target,entry,sizeof(entry)) || entry[0]!=0xe9 ||
        memcmp(entry+5,expected+5,27)) return TPLLIB_CONFLICT;
    int32_t displacement=0; memcpy(&displacement,entry+1,4);
    void* relayAddress=reinterpret_cast<void*>(uintptr_t(target)+5+displacement);
    void* destination=0;
    if(readMemory(relayAddress,relay,sizeof(relay)) || !absoluteJump(relay,&destination)) return TPLLIB_CONFLICT;
    return selectForeignDestination(target,expected,destination,out);
}
TPLLib_Status foreignIdentity(const TPLLib_ForeignHook* profile,void** physical) {
    if(!profile || profile->size<sizeof(*profile) || profile->reserved || profile->reserved2 ||
       !profile->module_name || !profile->module_name[0] || !profile->module_sha256 ||
       strlen(profile->module_sha256)!=64 || !physical) return TPLLIB_INVALID;
    *physical=0;
    Module module;
    TPLLib_Status s=module.open(profile->module_name); if(s) return s;
    char hash[65]; s=fingerprint(module.handle,hash); if(s) return s;
    if(_stricmp(hash,profile->module_sha256)) return TPLLIB_VERSION_MISMATCH;
    if(module.nt.OptionalHeader.SizeOfImage<TPLLIB_HOOK_BYTES ||
       profile->detour_rva>module.nt.OptionalHeader.SizeOfImage-TPLLIB_HOOK_BYTES) return TPLLIB_INVALID;
    *physical=reinterpret_cast<char*>(module.handle)+profile->detour_rva;
    return executable(*physical)?TPLLIB_OK:TPLLIB_CONFLICT;
}
bool privateExecutableSpan(void* start,unsigned bytes) {
    MEMORY_BASIC_INFORMATION m;
    return VirtualQuery(start,&m,sizeof(m)) && m.State==MEM_COMMIT && m.Type==MEM_PRIVATE &&
        executable(start) && uintptr_t(start)>=uintptr_t(m.BaseAddress) &&
        uintptr_t(start)-uintptr_t(m.BaseAddress)<=m.RegionSize &&
        bytes<=m.RegionSize-(uintptr_t(start)-uintptr_t(m.BaseAddress));
}
TPLLib_Status verifiedForeignTarget(void* target,const uint8_t* expected,
    const TPLLib_ForeignHook* profile,void** physical,ForeignGuard& guard) {
    *physical=0;
    guard.count=0;
    unsigned char entry[TPLLIB_HOOK_BYTES];
    TPLLib_Status s=readMemory(target,entry,sizeof(entry)); if(s) return s;
    if(entry[0]!=0xe9 || memcmp(entry+5,expected+5,TPLLIB_HOOK_BYTES-5)) return TPLLIB_CONFLICT;
    int32_t displacement=0; memcpy(&displacement,entry+1,sizeof(displacement));
    unsigned char* relay=reinterpret_cast<unsigned char*>(reinterpret_cast<uintptr_t>(target)+5+static_cast<intptr_t>(displacement));
    unsigned char* visited[FOREIGN_DEPTH]={0};
    for(unsigned depth=0;depth<FOREIGN_DEPTH;++depth) {
        for(unsigned i=0;i<depth;++i) if(visited[i]==relay) return TPLLIB_CONFLICT;
        visited[depth]=relay;
        if(!privateExecutableSpan(relay,14)) return TPLLIB_CONFLICT;
        unsigned char relayBytes[14]; s=readMemory(relay,relayBytes,14); if(s) return s;
        void* destination=0;
        if(!absoluteJump(relayBytes,&destination)) return TPLLIB_CONFLICT;
        // Only the first link may use a caller-supplied profile. Each nested
        // destination must independently match a reviewed native-target pair.
        if(depth) { s=selectForeignDestination(target,expected,destination,&profile); if(s) return s; }
        void* foreign=0; s=foreignIdentity(profile,&foreign); if(s) return s;
        if(destination!=foreign) return TPLLIB_CONFLICT;
        for(unsigned i=0;i<depth;++i) if(guard.links[i].detour==foreign) return TPLLIB_CONFLICT;
        ForeignLinkGuard& link=guard.links[depth]; link.detour=foreign;
        s=readMemory(foreign,link.detourBytes,32); if(s) return s;
        if(memcmp(link.detourBytes,profile->detour_expected32,32)) return TPLLIB_CONFLICT;
        // Terminal layouts: upstream MinHook's trampoline before the relay,
        // or the captured backend's original instructions immediately after it.
        for(unsigned layout=0;layout<2;++layout) for(unsigned copied=5;copied<=16;++copied) {
            unsigned length=copied+28;
            if(uintptr_t(relay)<copied+14) continue;
            unsigned char* block=layout?relay:relay-(copied+14);
            if(!privateExecutableSpan(block,length)) continue;
            unsigned char bytes[44];
            if(readMemory(block,bytes,length)) continue;
            unsigned char* trampoline=bytes+(layout?14:0);
            if(memcmp(bytes+(layout?0:copied+14),relayBytes,14)) continue;
            void* returnsTo=0;
            if(!absoluteJump(trampoline+copied,&returnsTo) ||
               returnsTo!=reinterpret_cast<char*>(target)+copied || memcmp(trampoline,expected,copied)) continue;
            link.block=block; link.length=length; memcpy(link.bytes,bytes,length);
            guard.target=target; guard.count=depth+1; memcpy(guard.entry,entry,32);
            *physical=guard.links[0].detour; return TPLLIB_OK;
        }
        // Captured nested layout: relay to this detour, followed by an absolute
        // continuation to the preceding relay. Never follow arbitrary thunks.
        unsigned char bytes[28]; void* previous=0;
        if(!privateExecutableSpan(relay,sizeof(bytes)) || readMemory(relay,bytes,sizeof(bytes)) ||
           memcmp(bytes,relayBytes,14) || !absoluteJump(bytes+14,&previous)) return TPLLIB_CONFLICT;
        link.block=relay; link.length=sizeof(bytes); memcpy(link.bytes,bytes,sizeof(bytes));
        relay=reinterpret_cast<unsigned char*>(previous);
    }
    return TPLLIB_CONFLICT;
}
TPLLib_Status verifyForeignGuard(const ForeignGuard& guard) {
    if(!guard.target) return TPLLIB_OK;
    unsigned char entry[32],bytes[44];
    if(!guard.count || guard.count>FOREIGN_DEPTH || readMemory(guard.target,entry,32) ||
       memcmp(entry,guard.entry,32)) return TPLLIB_CONFLICT;
    for(unsigned i=0;i<guard.count;++i) {
        const ForeignLinkGuard& link=guard.links[i];
        if(link.length>sizeof(bytes) || !privateExecutableSpan(link.block,link.length) ||
           readMemory(link.block,bytes,link.length) || memcmp(bytes,link.bytes,link.length)) return TPLLIB_CONFLICT;
        // TPL owns the first detour's patch; verifyHook checks that snapshot.
        if(i && (readMemory(link.detour,entry,32) || memcmp(entry,link.detourBytes,32))) return TPLLIB_CONFLICT;
    }
    return TPLLIB_OK;
}
TPLLib_Status verifyHook(Hook& hook);
TPLLib_Status createHook(TPLLib_Token owner,void* target,const uint8_t* expected,void* detour,void** original,
    TPLLib_Token* out,bool shared,const TPLLib_ForeignHook* foreignProfile) {
    if(!original || !out) return TPLLIB_INVALID; *original=0; *out=0;
    TPLLib_Status s=mainOnly(); if(s) return s;
    if(!hasOwner(owner)) return TPLLIB_NOT_FOUND;
    if(!expected || target==detour || uintptr_t(target)<16 || !executable(target) || !executable(detour)) return TPLLIB_INVALID;
    uintptr_t start=uintptr_t(target)-16;
    unsigned slot=HOOK_COUNT;
    int group=-1;
    for(unsigned i=0;i<HOOK_COUNT;++i) {
        if(hooks[i].id && start<uintptr_t(hooks[i].target)+32 && start+48>uintptr_t(hooks[i].target)-16) {
            if(!shared || hooks[i].target!=target || hooks[i].group<0 || hooks[i].detour==detour) return TPLLIB_CONFLICT;
            group=hooks[i].group;
        }
        if(!hooks[i].id && slot==HOOK_COUNT) slot=i;
    }
    if(slot==HOOK_COUNT) return TPLLIB_LIMIT;
    if(group>=0) {
        SharedTarget& chain=sharedTargets[group];
        if(memcmp(chain.expected,expected,TPLLIB_HOOK_BYTES)) return TPLLIB_CONFLICT;
        s=verifyForeignGuard(chain.foreign); if(s) return s;
        if(foreignProfile) {
            void* physical=0; s=foreignIdentity(foreignProfile,&physical); if(s) return s;
            if(!chain.foreign.target || physical!=chain.patch.target ||
               memcmp(foreignProfile->detour_expected32,chain.patch.before+16,TPLLIB_HOOK_BYTES)) return TPLLIB_CONFLICT;
        }
        s=verifyHook(chain.patch); if(s) return s;
        if(!pinAddress(detour)) return TPLLIB_INVALID;
        TPLLib_Token id;
        { Lock lock; id=token(); }
        if(!id) return TPLLIB_LIMIT;
        void* next=chain.trampoline;
        for(unsigned i=0;i<slot;++i) if(hooks[i].id && hooks[i].group==group && hooks[i].enabled) next=hooks[i].detour;
        routeTo(HOOK_COUNT+slot,next);
        Hook& hook=hooks[slot]; hook.id=id; hook.owner=owner; hook.target=target;
        hook.detour=detour; hook.group=group;
        *original=route(HOOK_COUNT+slot); *out=id; return TPLLIB_OK;
    }
    void* physicalTarget=target;
    const uint8_t* physicalExpected=expected;
    ForeignGuard foreignGuard={0};
    if(shared && !foreignProfile) {
        unsigned char live[TPLLIB_HOOK_BYTES];
        s=readMemory(target,live,sizeof(live)); if(s) return s;
        if(memcmp(live,expected,sizeof(live))) {
            s=selectForeignProfile(target,expected,&foreignProfile); if(s) return s;
        }
    }
    if(foreignProfile) {
        if(!shared) return TPLLIB_INVALID;
        s=verifiedForeignTarget(target,expected,foreignProfile,&physicalTarget,foreignGuard); if(s) return s;
        physicalExpected=foreignProfile->detour_expected32;
    }
    if(uintptr_t(physicalTarget)<16 || physicalTarget==detour) return TPLLIB_INVALID;
    uintptr_t physicalStart=uintptr_t(physicalTarget)-16;
    for(unsigned i=0;i<HOOK_COUNT;++i) if(hooks[i].id) {
        void* other=hooks[i].group<0?hooks[i].target:sharedTargets[hooks[i].group].patch.target;
        if(physicalStart<uintptr_t(other)+32 && physicalStart+48>uintptr_t(other)-16) return TPLLIB_CONFLICT;
    }
    unsigned char before[48]; s=readMemory(reinterpret_cast<void*>(physicalStart),before,sizeof(before)); if(s) return s;
    if(memcmp(before+16,physicalExpected,TPLLIB_HOOK_BYTES)) return TPLLIB_CONFLICT;
    if(before[16]==0xe9 || before[16]==0xeb || (before[16]==0xff && (before[17]==0x25 || before[17]==0x15))) return TPLLIB_CONFLICT;
    if(!pinAddress(target) || !pinAddress(physicalTarget) || !pinAddress(detour)) return TPLLIB_INVALID;
    for(unsigned i=1;i<foreignGuard.count;++i) if(!pinAddress(foreignGuard.links[i].detour)) return TPLLIB_INVALID;
    if(shared) {
        for(unsigned i=0;i<HOOK_COUNT;++i) if(!sharedTargets[i].patch.id) { group=static_cast<int>(i); break; }
        if(group<0) return TPLLIB_LIMIT;
        if(!initializeRoutes()) return TPLLIB_BACKEND_ERROR;
    }
    if(!hooksInitialized) {
        if(MH_Initialize()!=MH_OK) return TPLLIB_BACKEND_ERROR;
        hooksInitialized=true;
    }
    TPLLib_Token id;
    { Lock lock; id=token(); }
    if(!id) return TPLLIB_LIMIT;
    void* trampoline=0;
    void* destination=shared?route(group):detour;
    if(MH_CreateHook(physicalTarget,destination,&trampoline)!=MH_OK) return TPLLIB_BACKEND_ERROR;
    Hook& hook=hooks[slot]; hook.id=id; hook.owner=owner; hook.target=target; hook.detour=detour;
    hook.enabled=false; hook.snapshotValid=false; hook.group=group;
    memcpy(hook.before,before,sizeof(before));
    if(shared) {
        SharedTarget& chain=sharedTargets[group]; chain.patch=hook;
        chain.patch.target=physicalTarget; chain.patch.detour=destination; chain.trampoline=trampoline;
        chain.foreign=foreignGuard;
        memcpy(chain.expected,expected,sizeof(chain.expected));
        routeTo(group,trampoline); routeTo(HOOK_COUNT+slot,trampoline);
        *original=route(HOOK_COUNT+slot);
    } else *original=trampoline;
    *out=id; return TPLLIB_OK;
}
std::string diagnosticAddress(void* address) {
    std::ostringstream text;
    HMODULE module=0;
    if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<const char*>(address),&module)) {
        char path[32768]={0};
        if(GetModuleFileNameA(module,path,sizeof(path))) text<<path<<"+0x"<<std::hex<<(uintptr_t(address)-uintptr_t(module));
        else text<<address;
    } else text<<address<<" (private or unknown memory)";
    return text.str();
}
TPLLib_Status reportHookConflict(TPLLib_Token owner,void* target,TPLLib_Status result) {
    if(result!=TPLLIB_CONFLICT || !logger || !isMainThread()) return result;
    try {
        for(unsigned i=0;i<OWNER_COUNT;++i) if(owners[i].id==owner) {
            if(owners[i].conflictReported) return result;
            owners[i].conflictReported=true;
            std::ostringstream message;
            message<<"[Hook conflict] owner="<<owners[i].name<<" status=8 target="<<diagnosticAddress(target);
            for(unsigned j=0;j<HOOK_COUNT;++j) if(hooks[j].id && hooks[j].target==target) {
                for(unsigned k=0;k<OWNER_COUNT;++k) if(owners[k].id==hooks[j].owner)
                    message<<" registered-owner="<<owners[k].name;
            }
            // Observe at most two direct/indirect jumps. Never execute or modify them.
            void* cursor=target;
            for(unsigned depth=0;depth<2;++depth) {
                unsigned char bytes[6]; if(readMemory(cursor,bytes,sizeof(bytes))) break;
                int32_t displacement=0; void* next=0;
                if(bytes[0]==0xe9) {
                    memcpy(&displacement,bytes+1,4); next=reinterpret_cast<void*>(uintptr_t(cursor)+5+displacement);
                } else if(bytes[0]==0xff && bytes[1]==0x25) {
                    memcpy(&displacement,bytes+2,4);
                    if(readMemory(reinterpret_cast<void*>(uintptr_t(cursor)+6+displacement),&next,sizeof(next))) break;
                } else break;
                message<<" observed-jump="<<diagnosticAddress(next);
                if(next==cursor) break;
                cursor=next;
            }
            message<<". Patch/ownership validation failed; observed destinations do not prove which plugin installed the patch. No checks bypassed. Share full TPL.log; restart after changing plugins.";
            logger(message.str().c_str()); break;
        }
    } catch(...) { /* Diagnostics must not change hook results. */ }
    return result;
}
TPLLib_Status hookCreate(TPLLib_Token owner,void* target,const uint8_t* expected,void* detour,void** original,TPLLib_Token* out) {
    return reportHookConflict(owner,target,createHook(owner,target,expected,detour,original,out,false,0));
}
TPLLib_Status hookCreateShared(TPLLib_Token owner,void* target,const uint8_t* expected,void* detour,void** original,TPLLib_Token* out) {
    return reportHookConflict(owner,target,createHook(owner,target,expected,detour,original,out,true,0));
}
TPLLib_Status hookCreateSharedForeign(TPLLib_Token owner,void* target,const uint8_t* expected,
    const TPLLib_ForeignHook* profile,void* detour,void** original,TPLLib_Token* out) {
    return reportHookConflict(owner,target,createHook(owner,target,expected,detour,original,out,true,profile));
}
bool recoverSnapshot(const Hook& hook,const unsigned char current[48]) {
    // MinHook x64 uses E9 to its FF25 relay, optionally preceded by a hotpatch.
    unsigned offset=16, length=5;
    if(current[16]==0xeb && current[17]==0xf9) { offset=11; length=7; }
    if(current[offset]!=0xe9) return false;
    for(unsigned i=0;i<48;++i) if((i<offset || i>=offset+length) && current[i]!=hook.before[i]) return false;
    int32_t displacement=0; memcpy(&displacement,current+offset+1,sizeof(displacement));
    uintptr_t relay=uintptr_t(hook.target)-16+offset+5+intptr_t(displacement);
    unsigned char jump[14];
    if(readMemory(reinterpret_cast<void*>(relay),jump,sizeof(jump)) || jump[0]!=0xff || jump[1]!=0x25) return false;
    uint32_t indirectOffset=1; memcpy(&indirectOffset,jump+2,4);
    void* destination=0; memcpy(&destination,jump+6,sizeof(destination));
    return indirectOffset==0 && destination==hook.detour;
}
TPLLib_Status verifyHook(Hook& hook) {
        unsigned char current[48];
        TPLLib_Status s=readMemory(reinterpret_cast<char*>(hook.target)-16,current,sizeof(current)); if(s) return s;
        if(hook.enabled && !hook.snapshotValid) {
            if(!recoverSnapshot(hook,current)) return TPLLIB_CONFLICT;
            memcpy(hook.after,current,sizeof(current)); hook.snapshotValid=true;
        }
        if(memcmp(current,hook.enabled?hook.after:hook.before,sizeof(current))) return TPLLIB_CONFLICT;
        if(hook.enabled && !recoverSnapshot(hook,current)) return TPLLIB_CONFLICT;
        return TPLLIB_OK;
}
TPLLib_Status setPhysical(Hook& hook,bool enabled) {
        TPLLib_Status s=verifyHook(hook); if(s) return s;
        if(hook.enabled==(enabled!=0)) return TPLLIB_OK;
        // Queued application selects the correct instruction-pointer relocation
        // for disabling hotpatches in pinned MinHook 1.3.4.
        MH_STATUS result=enabled?MH_QueueEnableHook(hook.target):MH_QueueDisableHook(hook.target);
        if(result==MH_OK) result=MH_ApplyQueued();
        if(result!=MH_OK) {
            if(hook.enabled) MH_QueueEnableHook(hook.target); else MH_QueueDisableHook(hook.target);
            return TPLLIB_BACKEND_ERROR;
        }
        hook.enabled=enabled!=0;
        if(enabled) {
            hook.snapshotValid=false;
#ifdef TPLLIB_TESTING
            if(failHookSnapshot) { failHookSnapshot=false; return TPLLIB_BUSY; }
#endif
            if(readMemory(reinterpret_cast<char*>(hook.target)-16,hook.after,sizeof(hook.after))) return TPLLIB_BUSY;
            hook.snapshotValid=true;
        }
        return TPLLIB_OK;
}
void rebuildChain(int group) {
    void* next=sharedTargets[group].trampoline;
    // Every continuation points strictly to an older member, even while a
    // concurrent call observes a transition. This prevents cycles and repeats.
    for(unsigned i=0;i<HOOK_COUNT;++i) if(hooks[i].id && hooks[i].group==group) {
        routeTo(HOOK_COUNT+i,next);
        if(hooks[i].enabled) next=hooks[i].detour;
    }
    routeTo(group,next);
}
TPLLib_Status hookEnable(TPLLib_Token owner,TPLLib_Token id,int enabled) {
    TPLLib_Status s=mainOnly(); if(s) return s;
    if(enabled!=0 && enabled!=1) return TPLLIB_INVALID;
    for(unsigned i=0;i<HOOK_COUNT;++i) if(id && hooks[i].id==id) {
        Hook& hook=hooks[i]; if(hook.owner!=owner) return TPLLIB_CONFLICT;
        if(hook.group<0) return setPhysical(hook,enabled!=0);
        Hook& physical=sharedTargets[hook.group].patch;
        s=verifyForeignGuard(sharedTargets[hook.group].foreign); if(s) return s;
        bool needed=enabled!=0;
        for(unsigned j=0;j<HOOK_COUNT;++j)
            if(j!=i && hooks[j].id && hooks[j].group==hook.group && hooks[j].enabled) needed=true;
        s=setPhysical(physical,needed); if(s) return s;
        hook.enabled=enabled!=0;
        rebuildChain(hook.group);
        return TPLLIB_OK;
    }
    return TPLLIB_NOT_FOUND;
}
TPLLib_Status hookState(TPLLib_Token owner,TPLLib_Token id,TPLLib_HookState* out) {
    TPLLib_Status s=mainOnly(); if(s) return s;
    if(!out || out->size<sizeof(*out)) return TPLLIB_INVALID;
    TPLLib_HookState state={0}; state.size=sizeof(state); *out=state;
    for(unsigned i=0;i<HOOK_COUNT;++i) if(id && hooks[i].id==id) {
        Hook& hook=hooks[i]; if(hook.owner!=owner) return TPLLIB_CONFLICT;
        Hook& physical=hook.group<0?hook:sharedTargets[hook.group].patch;
        state.enabled=hook.enabled?1:0; state.shared=hook.group>=0?1:0;
        state.target_patched=physical.enabled?1:0;
        s=hook.group<0?TPLLIB_OK:verifyForeignGuard(sharedTargets[hook.group].foreign);
        if(!s) s=verifyHook(physical);
        state.target_verified=s==TPLLIB_OK?1:0;
        if(hook.group<0) { state.chain_members=1; state.chain_enabled=state.enabled; }
        else for(unsigned j=0;j<HOOK_COUNT;++j) if(hooks[j].id && hooks[j].group==hook.group) {
            ++state.chain_members; if(hooks[j].enabled) ++state.chain_enabled;
        }
        *out=state; return s;
    }
    return TPLLIB_NOT_FOUND;
}
TPLLib_Status stats(TPLLib_Stats* out) {
    TPLLib_Status s=mainOnly(); if(s) return s;
    if(!out || out->size<sizeof(*out)) return TPLLIB_INVALID;
    TPLLib_Stats value={0}; value.size=sizeof(value); value.frames=frameNumber; value.callback_failures=callbackFailures;
    Lock lock;
    for(unsigned i=0;i<OWNER_COUNT;++i) if(owners[i].id) ++value.owners;
    for(unsigned i=0;i<HOOK_COUNT;++i) if(hooks[i].id) ++value.hooks;
    for(unsigned i=0;i<JOB_COUNT;++i) if(jobs[i].id) ++value.queued_jobs;
    for(unsigned i=0;i<FRAME_COUNT;++i) if(callbacks[i].id) ++value.frame_callbacks;
    for(unsigned i=0;i<SERVICE_COUNT;++i) if(services[i].owner) ++value.services;
    *out=value; return TPLLIB_OK;
}
TPLLib_Status logMessage(const char* message) {
    if(!ready()) return TPLLIB_NOT_READY;
    if(!message) return TPLLIB_INVALID;
    try { if(logger) logger(message); else OutputDebugStringA(message); }
    catch(...) { return TPLLIB_CALLBACK_ERROR; }
    return TPLLIB_OK;
}
const TPLLib_API api={sizeof(TPLLib_API),TPLLIB_ABI_VERSION,
    TPLLIB_CAP_MODULES|TPLLIB_CAP_SIGNATURES|TPLLIB_CAP_HOOKS|TPLLIB_CAP_DISPATCH|TPLLIB_CAP_SERVICES|TPLLIB_CAP_LOGGING|TPLLIB_CAP_SHARED_HOOKS|TPLLIB_CAP_FOREIGN_HOOK_CHAINS,
    TPLLIB_VERSION,&statusText,&isMainThread,&ownerOpen,&ownerClose,&moduleInfo,&readMemory,
    &resolveRva,&findUnique,&hookCreate,&hookEnable,&post,&cancelJob,&subscribe,&unsubscribe,&publish,&query,&stats,&logMessage,
    &hookCreateShared,&hookState,&hookCreateSharedForeign};
void reportException() {
    ++callbackFailures;
    try { if(logger) logger("TPLLib callback threw; callback stopped (native faults are not isolated)"); } catch(...) {}
}
}
const TPLLib_API* getAPI(uint32_t version) { return version==TPLLIB_ABI_VERSION?&api:0; }
#ifdef TPLLIB_TESTING
void testFailNextHookSnapshot() { failHookSnapshot=true; }
void testUseForeignProfile(const ForeignHookProfile* profile,unsigned count) { testForeignProfile=profile; testForeignProfileCount=count; }
#endif
bool initialize(void (*log)(const char*)) {
    LONG state=InterlockedCompareExchange(&initialized,1,0);
    if(state) return state==2 && GetCurrentThreadId()==mainThread;
    if(!InitializeCriticalSectionAndSpinCount(&queueLock,4000)) { InterlockedExchange(&initialized,0); return false; }
    mainThread=GetCurrentThreadId(); logger=log;
    InterlockedExchange(&initialized,2);
    try { if(logger) logger("TPLLib 0.1.0-dev initialized; game-object bindings unavailable"); } catch(...) {}
    return true;
}
void frame(float dt) {
    if(!isMainThread() || dispatching) return;
    dispatching=true; ++frameNumber;
    if(!_finite(dt) || dt<0) dt=0;
    if(dt>1) dt=1;
    TPLLib_Token cutoff;
    { Lock lock; cutoff=nextToken; }
    // FIFO by token; callback-enqueued work waits until the following frame.
    for(unsigned n=0;n<128;++n) {
        Job job={0};
        {
            Lock lock; unsigned slot=JOB_COUNT;
            for(unsigned i=0;i<JOB_COUNT;++i) if(jobs[i].id && jobs[i].id<cutoff && (slot==JOB_COUNT || jobs[i].id<jobs[slot].id)) slot=i;
            if(slot==JOB_COUNT) break;
            job=jobs[slot]; jobs[slot].id=0;
        }
        activeOwner=job.owner;
        try { job.fn(job.user); } catch(...) { reportException(); }
        activeOwner=0;
    }
    for(unsigned i=0;i<FRAME_COUNT;++i) {
        Frame cb=callbacks[i];
        if(!cb.id || cb.id>=cutoff) continue;
        activeOwner=cb.owner;
        try { cb.fn(cb.user,dt); }
        catch(...) { if(callbacks[i].id==cb.id) callbacks[i].id=0; reportException(); }
        activeOwner=0;
    }
    dispatching=false;
}
}
