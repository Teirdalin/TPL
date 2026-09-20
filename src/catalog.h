#pragma once
#include "common.h"
#include "tpl.h"
#include <set>

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
    Catalog() : rePresent(false),reBridge(false) {}
    void initialize(const std::wstring& gameRoot);
    void refreshLaunchSelection();
    void observeModules();
    void startPlugins();
    void tick(float dt);
    void toggle(size_t index);
    bool blocked(const std::wstring& dll) const;
    std::vector<std::wstring> configs(size_t index) const;
private:
    std::set<std::wstring> disabled, startupDisabled, active;
    std::string cfgSnapshot;
    std::vector<std::wstring> launchOrder;
    void scanFolder(const std::wstring& root, const std::string& origin);
    void saveState();
    void updateDesired();
    void startLegacyPlugins();
    void startOrderedPlugins(bool legacy);
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
