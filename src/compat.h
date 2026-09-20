#pragma once
#include "legacy.h"
namespace tpl {
struct CompatibilityPlan {
    bool needsBridge, eligible;
    std::string reason, pluginHash, gameHash;
    std::vector<std::string> mapped, missing;
    CompatibilityPlan() : needsBridge(false),eligible(false) {}
};
CompatibilityPlan compatibilityPlan(const std::wstring& dll,const std::wstring& root,const std::wstring& game);
std::wstring compatibilityReport(const std::wstring& dll,const std::wstring& game,const CompatibilityPlan& plan);
HMODULE loadCompatibility(const std::wstring& dll,const std::wstring& root,const std::wstring& game,
    const CompatibilityPlan& plan,std::wstring& loadedPath);
bool compatibilityOwns(HMODULE module);
std::wstring compatibilityOrigin(HMODULE module);
}
