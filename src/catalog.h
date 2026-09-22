#pragma once
#include "common.h"
#include "tpl.h"
#include <map>
#include <set>
#include <utility>

namespace tpl {
struct Entry {
    std::wstring id, root, path, modFile, owner;
    std::string name, provider, status;
    bool plugin, startEnabled, desiredEnabled, loaded, running, missing, attempted;
    HMODULE module;
    TPL_TickFn tick;
    Entry() : plugin(false),startEnabled(false),desiredEnabled(false),loaded(false),running(false),missing(false),attempted(false),module(0),tick(0) {}
};
class Catalog {
public:
    std::wstring game, home;
    std::vector<Entry> entries;
    bool rePresent, reBridge;
    Catalog();
    ~Catalog();
    void initialize(const std::wstring& gameRoot);
    bool scanStep(size_t budget=1);
    bool discoveryComplete() const { return scanComplete; }
    void refreshLaunchSelection();
    void observeModules();
    std::vector<std::string> startupWarnings() const;
    bool startPluginStep();
    bool pluginStartupComplete() const { return startupComplete; }
    size_t reusedFolderCount() const { return cacheHits; }
    void startPlugins();
    void tick(float dt);
    void toggle(size_t index);
    bool blocked(const std::wstring& dll) const;
    std::vector<std::wstring> configs(size_t index) const;
private:
    struct ScanRoot { std::wstring path; std::string origin; };
    struct CachedFolder { std::string signature; std::vector<Entry> entries; };
    std::set<std::wstring> disabled, startupDisabled, active;
    std::string cfgSnapshot;
    std::vector<std::wstring> launchOrder;
    std::vector<ScanRoot> scanRoots;
    std::map<std::wstring,CachedFolder> cachedFolders, nextCache;
    size_t cacheHits, cacheMisses;
    size_t scanRoot;
    HANDLE scanHandle;
    WIN32_FIND_DATAW scanData;
    bool scanComplete;
    std::vector<std::pair<size_t,bool> > startupQueue;
    size_t startupIndex;
    bool startupPrepared, startupComplete, legacySuppressed;
    void scanFolder(const std::wstring& root, const std::string& origin);
    std::string folderSignature(const std::wstring& root) const;
    void loadCache();
    void saveCache() const;
    void finishDiscovery();
    void closeScanner();
    void saveState();
    void updateDesired();
    void preparePluginStartup();
    void queueOrderedPlugins(bool legacy);
    void startEntry(Entry& entry, bool legacy);
};
struct ConfigDocument {
    std::wstring path;
    std::string original, text;
    bool bom, crlf;
    void open(const std::wstring& file);
    void save(const std::string& utf8);
};
extern Catalog catalog;
}
