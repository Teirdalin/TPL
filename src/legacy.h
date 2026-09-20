#pragma once
#include "common.h"
namespace tpl {
struct LegacySymbol {
    std::string name;
    unsigned ordinal;
    bool byOrdinal;
};
struct LegacyImport {
    std::string module;
    std::vector<LegacySymbol> symbols;
    bool delayed;
    size_t nameOffset;
};
struct LegacyImage {
    std::vector<LegacyImport> imports;
    bool signedImage,boundImports,hasLegacyStart;
    size_t checksumOffset;
};
LegacyImage legacyImage(const std::string& bytes);
std::string redirectKenshiImports(const std::string& bytes,const std::string& replacement);
std::vector<std::string> legacyImports(const std::string& image);
std::string legacyProblem(const std::wstring& dll,const std::wstring& game,bool allowDirectKenshiLib=false);
}
