#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <sstream>

namespace tpl {
std::wstring widen(const std::string& value);
std::string narrow(const std::wstring& value);
std::wstring lower(std::wstring value);
std::wstring join(const std::wstring& a, const std::wstring& b);
std::wstring parent(const std::wstring& path);
std::wstring filename(const std::wstring& path);
std::wstring fullPath(const std::wstring& path);
bool exists(const std::wstring& path);
bool directory(const std::wstring& path);
bool contained(const std::wstring& root, const std::wstring& path);
void makeDirectories(const std::wstring& path);
std::string readFile(const std::wstring& path, size_t limit = 1024 * 1024);
void writeFile(const std::wstring& path, const std::string& bytes, bool backup = true);
std::vector<std::wstring> list(const std::wstring& dir, const wchar_t* pattern, bool dirs);
std::vector<std::string> lines(const std::string& value);
std::string trim(const std::string& value);
std::wstring modulePath(HMODULE module);
std::string sha256(const std::wstring& path);
std::string sha256Bytes(const std::string& bytes);
void setLogRoot(const std::wstring& root);
void beginLogSession();
void log(const char* message);
void log(const std::string& message);
bool patchImport(HMODULE module, const char* symbol, void* replacement, void** original);
}
