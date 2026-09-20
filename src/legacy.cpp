#include "legacy.h"
#include <set>
#include <cstring>
#include <cstddef>

namespace tpl {
namespace {
const size_t maxSymbols=65536;
const size_t maxSymbolName=65536;
const size_t maxNameBytes=16*1024*1024;
bool isKenshiLib(const std::string& name) {
    const char expected[]="kenshilib.dll";
    if(name.size()!=sizeof(expected)-1) return false;
    for(size_t i=0;i<name.size();++i) {
        char c=name[i]; if(c>='A' && c<='Z') c=char(c-'A'+'a');
        if(c!=expected[i]) return false;
    }
    return true;
}
class Image {
    const std::string& bytes;
    IMAGE_OPTIONAL_HEADER64 optional;
    std::vector<IMAGE_SECTION_HEADER> sections;
    size_t checksum;
    mutable size_t nameBytes;
public:
    template<class T> T read(size_t offset) const {
        if(offset>bytes.size() || sizeof(T)>bytes.size()-offset) throw std::runtime_error("Truncated PE image");
        T result; memcpy(&result,bytes.data()+offset,sizeof(T)); return result;
    }
    Image(const std::string& data) : bytes(data),checksum(0),nameBytes(0) {
        IMAGE_DOS_HEADER dos=read<IMAGE_DOS_HEADER>(0);
        if(dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<0 || read<DWORD>(dos.e_lfanew)!=IMAGE_NT_SIGNATURE)
            throw std::runtime_error("Invalid PE image");
        size_t offset=size_t(dos.e_lfanew)+sizeof(DWORD);
        IMAGE_FILE_HEADER file=read<IMAGE_FILE_HEADER>(offset); offset+=sizeof(file);
        if(file.Machine!=IMAGE_FILE_MACHINE_AMD64 || !(file.Characteristics&IMAGE_FILE_DLL))
            throw std::runtime_error("Requires a native x64 DLL");
        if(file.SizeOfOptionalHeader<sizeof(optional) || file.NumberOfSections==0 || file.NumberOfSections>96)
            throw std::runtime_error("Unsupported PE headers");
        optional=read<IMAGE_OPTIONAL_HEADER64>(offset);
        if(optional.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC || optional.NumberOfRvaAndSizes>IMAGE_NUMBEROF_DIRECTORY_ENTRIES)
            throw std::runtime_error("Unsupported PE directories");
        checksum=offset+offsetof(IMAGE_OPTIONAL_HEADER64,CheckSum);
        offset+=file.SizeOfOptionalHeader;
        if(optional.SizeOfHeaders>bytes.size() || offset>optional.SizeOfHeaders ||
           file.NumberOfSections>(optional.SizeOfHeaders-offset)/sizeof(IMAGE_SECTION_HEADER))
            throw std::runtime_error("Truncated PE headers");
        for(unsigned i=0;i<file.NumberOfSections;++i) sections.push_back(read<IMAGE_SECTION_HEADER>(offset+i*sizeof(IMAGE_SECTION_HEADER)));
        for(size_t i=0;i<sections.size();++i) {
            const IMAGE_SECTION_HEADER& s=sections[i];
            if(s.SizeOfRawData && (s.PointerToRawData<optional.SizeOfHeaders ||
               s.PointerToRawData>bytes.size() || s.SizeOfRawData>bytes.size()-s.PointerToRawData))
                throw std::runtime_error("Invalid PE section data");
            if(ULONGLONG(s.VirtualAddress)+std::max(s.Misc.VirtualSize,s.SizeOfRawData)>ULONGLONG(MAXDWORD)+1)
                throw std::runtime_error("Invalid PE section RVA");
        }
        if(directory(IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR).VirtualAddress) throw std::runtime_error("Managed plugins are unsupported");
    }
    IMAGE_DATA_DIRECTORY directory(unsigned index) const {
        IMAGE_DATA_DIRECTORY empty={0,0};
        return index<optional.NumberOfRvaAndSizes?optional.DataDirectory[index]:empty;
    }
    size_t offset(DWORD rva,size_t count) const {
        if(ULONGLONG(count)>ULONGLONG(MAXDWORD)-rva+1) throw std::runtime_error("Overflowing PE RVA");
        if(rva<optional.SizeOfHeaders && count<=optional.SizeOfHeaders-rva && rva<=bytes.size() && count<=bytes.size()-rva) return rva;
        for(size_t i=0;i<sections.size();++i) {
            const IMAGE_SECTION_HEADER& s=sections[i];
            if(rva<s.VirtualAddress) continue;
            size_t delta=size_t(rva)-s.VirtualAddress;
            if(delta>s.SizeOfRawData || count>s.SizeOfRawData-delta) continue;
            size_t pos=size_t(s.PointerToRawData)+delta;
            if(pos<=bytes.size() && count<=bytes.size()-pos) return pos;
        }
        throw std::runtime_error("Invalid PE RVA");
    }
    std::string name(DWORD rva,bool module=true,size_t limit=0) const {
        if(!rva) throw std::runtime_error("Missing PE name RVA");
        if(!limit) limit=module?512:maxSymbolName;
        std::string value;
        for(size_t i=0;i<limit;++i) {
            if(rva>MAXDWORD-i) break;
            if(nameBytes==maxNameBytes) throw std::runtime_error("PE name inspection limit reached");
            ++nameBytes;
            char c=read<char>(offset(rva+DWORD(i),1));
            if(!c) {
                if(value.empty() || (module && value.find_first_of("/\\:")!=std::string::npos)) break;
                offset(rva,value.size()+1);
                return value;
            }
            if(static_cast<unsigned char>(c)>127) break;
            value+=c;
        }
        throw std::runtime_error(module?"Invalid import name":"Invalid PE symbol name");
    }
    void symbols(DWORD lookup,DWORD iat,std::vector<LegacySymbol>& result,size_t& remaining) const {
        if(!lookup || !iat) throw std::runtime_error("Missing import thunk table");
        for(size_t i=0;i<=maxSymbols;++i) {
            size_t count=(i+1)*sizeof(ULONGLONG);
            size_t base=offset(lookup,count);
            ULONGLONG thunk=read<ULONGLONG>(base+i*sizeof(ULONGLONG));
            if(!thunk) {
                size_t address=offset(iat,count);
                if(read<ULONGLONG>(address+i*sizeof(ULONGLONG))) throw std::runtime_error("Unterminated import address table");
                return;
            }
            if(!remaining) throw std::runtime_error("Import symbol inspection limit reached");
            --remaining;
            LegacySymbol symbol; symbol.ordinal=0; symbol.byOrdinal=(thunk&IMAGE_ORDINAL_FLAG64)!=0;
            if(symbol.byOrdinal) {
                if(thunk&~(ULONGLONG(IMAGE_ORDINAL_FLAG64)|ULONGLONG(0xffff)))
                    throw std::runtime_error("Invalid import ordinal");
                symbol.ordinal=unsigned(thunk&0xffff);
            } else {
                if(thunk>0x7fffffff) throw std::runtime_error("Invalid import name thunk");
                read<WORD>(offset(DWORD(thunk),sizeof(WORD)));
                symbol.name=name(DWORD(thunk)+sizeof(WORD),false);
                offset(DWORD(thunk),sizeof(WORD)+symbol.name.size()+1);
            }
            result.push_back(symbol);
        }
        throw std::runtime_error("Unterminated import thunk table");
    }
    void imports(unsigned index,bool delay,std::vector<LegacyImport>& result,bool withSymbols,
                 bool& bound,size_t& remaining) const {
        IMAGE_DATA_DIRECTORY dir=directory(index);
        if(!dir.VirtualAddress && !dir.Size) return;
        const size_t stride=delay?32:sizeof(IMAGE_IMPORT_DESCRIPTOR);
        if(!dir.VirtualAddress || dir.Size<stride || dir.Size/stride>4096) throw std::runtime_error("Invalid import directory");
        size_t base=offset(dir.VirtualAddress,dir.Size);
        for(size_t i=0;i+stride<=dir.Size;i+=stride) {
            bool empty=true;
            for(size_t j=0;j<stride;j+=sizeof(DWORD)) if(read<DWORD>(base+i+j)) empty=false;
            if(empty) return;
            DWORD nameRva,lookup=0,iat=0,boundIat=0,unloadIat=0;
            bool boundDescriptor=false;
            if(delay) {
                if(read<DWORD>(base+i)!=1) throw std::runtime_error("Unsupported delay imports");
                nameRva=read<DWORD>(base+i+4);
                iat=read<DWORD>(base+i+12); lookup=read<DWORD>(base+i+16);
                boundIat=read<DWORD>(base+i+20); unloadIat=read<DWORD>(base+i+24);
                // VC10 reserves an empty bound IAT even when no binding has occurred.
                boundDescriptor=read<DWORD>(base+i+28)!=0;
                DWORD handle=read<DWORD>(base+i+8);
                if(withSymbols && handle) offset(handle,sizeof(ULONGLONG));
            } else {
                IMAGE_IMPORT_DESCRIPTOR descriptor=read<IMAGE_IMPORT_DESCRIPTOR>(base+i);
                nameRva=descriptor.Name; iat=descriptor.FirstThunk; lookup=descriptor.OriginalFirstThunk;
                boundDescriptor=descriptor.TimeDateStamp!=0;
                if(!lookup && !boundDescriptor) lookup=iat;
            }
            LegacyImport item; item.module=name(nameRva); item.delayed=delay;
            item.nameOffset=offset(nameRva,item.module.size()+1);
            bound=bound || boundDescriptor;
            if(withSymbols) {
                if(!lookup && boundDescriptor) throw std::runtime_error("Bound import has no name table: "+item.module);
                symbols(lookup,iat,item.symbols,remaining);
                size_t count=(item.symbols.size()+1)*sizeof(ULONGLONG);
                if(boundIat) offset(boundIat,count);
                if(unloadIat) offset(unloadIat,count);
            }
            result.push_back(item);
        }
        throw std::runtime_error("Unterminated import directory");
    }
    bool legacyStart() const {
        IMAGE_DATA_DIRECTORY dir=directory(IMAGE_DIRECTORY_ENTRY_EXPORT);
        if(!dir.VirtualAddress && !dir.Size) return false;
        if(!dir.VirtualAddress || dir.Size<sizeof(IMAGE_EXPORT_DIRECTORY)) throw std::runtime_error("Invalid export directory");
        IMAGE_EXPORT_DIRECTORY exports=read<IMAGE_EXPORT_DIRECTORY>(offset(dir.VirtualAddress,dir.Size));
        if(exports.NumberOfFunctions>maxSymbols || exports.NumberOfNames>maxSymbols)
            throw std::runtime_error("Export inspection limit reached");
        if(exports.NumberOfFunctions && !exports.AddressOfFunctions) throw std::runtime_error("Missing export address table");
        size_t functions=exports.NumberOfFunctions?offset(exports.AddressOfFunctions,size_t(exports.NumberOfFunctions)*sizeof(DWORD)):0;
        if(!exports.NumberOfNames) return false;
        if(!exports.AddressOfNames || !exports.AddressOfNameOrdinals) throw std::runtime_error("Missing export name table");
        size_t names=offset(exports.AddressOfNames,size_t(exports.NumberOfNames)*sizeof(DWORD));
        size_t ordinals=offset(exports.AddressOfNameOrdinals,size_t(exports.NumberOfNames)*sizeof(WORD));
        bool found=false,seen=false;
        for(size_t i=0;i<exports.NumberOfNames;++i) {
            WORD ordinal=read<WORD>(ordinals+i*sizeof(WORD));
            if(ordinal>=exports.NumberOfFunctions) throw std::runtime_error("Invalid export ordinal");
            std::string symbol=name(read<DWORD>(names+i*sizeof(DWORD)),false);
            if(symbol!="?startPlugin@@YAXXZ") continue;
            if(seen) throw std::runtime_error("Duplicate legacy startPlugin export");
            seen=true;
            DWORD target=read<DWORD>(functions+size_t(ordinal)*sizeof(DWORD));
            if(!target) continue;
            if(target>=dir.VirtualAddress && target-dir.VirtualAddress<dir.Size) {
                name(target,false,std::min(maxSymbolName,size_t(dir.Size-(target-dir.VirtualAddress))));
                continue;
            }
            offset(target,1); found=true;
        }
        return found;
    }
    LegacyImage details() const {
        LegacyImage result;
        result.signedImage=false; result.boundImports=false; result.hasLegacyStart=false;
        result.checksumOffset=checksum;
        IMAGE_DATA_DIRECTORY security=directory(IMAGE_DIRECTORY_ENTRY_SECURITY);
        result.signedImage=security.VirtualAddress!=0 || security.Size!=0;
        // The certificate directory uses a file offset, not an RVA.
        if(result.signedImage && (!security.VirtualAddress || !security.Size || security.VirtualAddress>bytes.size() ||
           security.Size>bytes.size()-security.VirtualAddress)) throw std::runtime_error("Invalid PE certificate directory");
        IMAGE_DATA_DIRECTORY bound=directory(IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT);
        result.boundImports=bound.VirtualAddress!=0 || bound.Size!=0;
        if(result.boundImports) {
            if(!bound.VirtualAddress || bound.Size<sizeof(IMAGE_BOUND_IMPORT_DESCRIPTOR))
                throw std::runtime_error("Invalid bound import directory");
            offset(bound.VirtualAddress,bound.Size);
        }
        size_t remaining=maxSymbols;
        imports(IMAGE_DIRECTORY_ENTRY_IMPORT,false,result.imports,true,result.boundImports,remaining);
        imports(IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT,true,result.imports,true,result.boundImports,remaining);
        result.hasLegacyStart=legacyStart();
        return result;
    }
};
void inspect(const std::wstring& path,const std::wstring& pluginDir,const std::wstring& game,
             std::set<std::wstring>& seen,size_t& remaining,bool allowDirectKenshiLib) {
    if(!seen.insert(lower(fullPath(path))).second) return;
    if(seen.size()>64) throw std::runtime_error("Dependency inspection limit reached");
    std::string data=readFile(path,std::min(remaining,size_t(128*1024*1024))); remaining-=data.size();
    std::vector<std::string> imports=legacyImports(data);
    for(size_t i=0;i<imports.size();++i) {
        std::wstring name=lower(widen(imports[i]));
        if(name==L"re_kenshi.dll") throw std::runtime_error("Requires RE_Kenshi runtime");
        if(name==L"kenshilib.dll" && !allowDirectKenshiLib) throw std::runtime_error("Requires KenshiLib initialization; keep RE_Kenshi");
    }
    std::vector<wchar_t> windows(32768),system(32768);
    UINT windowsLength=GetWindowsDirectoryW(&windows[0],DWORD(windows.size()));
    UINT systemLength=GetSystemDirectoryW(&system[0],DWORD(system.size()));
    if(!windowsLength || windowsLength>=windows.size() || !systemLength || systemLength>=system.size())
        throw std::runtime_error("Cannot locate system dependencies");
    for(size_t i=0;i<imports.size();++i) {
        std::wstring name=widen(imports[i]), lowered=lower(name);
        if(allowDirectKenshiLib && lowered==L"kenshilib.dll") continue;
        if(lowered.find(L"api-ms-win-")==0 || lowered.find(L"ext-ms-win-")==0) continue;
        HMODULE loaded=GetModuleHandleW(name.c_str());
        std::wstring dependency=loaded?modulePath(loaded):join(pluginDir,name);
        if(!exists(dependency)) dependency=join(game,name);
        if(!exists(dependency)) dependency=join(&system[0],name);
        if(!exists(dependency)) throw std::runtime_error("Dependency unavailable: "+imports[i]);
        if(contained(&windows[0],dependency)) continue;
        inspect(dependency,pluginDir,game,seen,remaining,false);
    }
}
}
LegacyImage legacyImage(const std::string& bytes) { return Image(bytes).details(); }
std::string redirectKenshiImports(const std::string& bytes,const std::string& replacement) {
    const LegacyImage image=legacyImage(bytes);
    if(image.signedImage) throw std::runtime_error("Cannot redirect KenshiLib imports in a signed image");
    if(image.boundImports) throw std::runtime_error("Cannot redirect KenshiLib imports in a bound image");
    if(replacement.empty() || replacement.find_first_of("/\\:")!=std::string::npos || replacement=="." || replacement=="..")
        throw std::runtime_error("Invalid KenshiLib replacement name");
    for(size_t i=0;i<replacement.size();++i) if(static_cast<unsigned char>(replacement[i])<32 || static_cast<unsigned char>(replacement[i])>126)
        throw std::runtime_error("Invalid KenshiLib replacement name");
    bool found=false;
    for(size_t i=0;i<image.imports.size();++i) {
        const LegacyImport& item=image.imports[i];
        if(!isKenshiLib(item.module)) continue;
        if(item.delayed) throw std::runtime_error("Cannot redirect delayed KenshiLib imports");
        if(replacement.size()>item.module.size()) throw std::runtime_error("KenshiLib replacement name is too long");
        found=true;
    }
    if(!found) return bytes;
    std::string result=bytes;
    for(size_t i=0;i<image.imports.size();++i) {
        const LegacyImport& item=image.imports[i];
        if(!isKenshiLib(item.module)) continue;
        memset(&result[item.nameOffset],0,item.module.size()+1);
        memcpy(&result[item.nameOffset],replacement.data(),replacement.size());
    }
    memset(&result[image.checksumOffset],0,sizeof(DWORD));
    return result;
}
std::vector<std::string> legacyImports(const std::string& bytes) {
    Image image(bytes); std::vector<LegacyImport> imports;
    bool bound=false; size_t remaining=maxSymbols;
    image.imports(IMAGE_DIRECTORY_ENTRY_IMPORT,false,imports,false,bound,remaining);
    image.imports(IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT,true,imports,false,bound,remaining);
    std::vector<std::string> result;
    for(size_t i=0;i<imports.size();++i) result.push_back(imports[i].module);
    return result;
}
std::string legacyProblem(const std::wstring& dll,const std::wstring& game,bool allowDirectKenshiLib) {
    try {
        std::set<std::wstring> seen; size_t remaining=256*1024*1024;
        inspect(dll,parent(dll),game,seen,remaining,allowDirectKenshiLib); return "";
    } catch(const std::exception& e) { return e.what(); }
}
}
