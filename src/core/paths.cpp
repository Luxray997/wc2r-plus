// SPDX-License-Identifier: MIT
#include "core/paths.h"

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <cstdio>
#include <share.h>

namespace paths {

namespace {

constexpr wchar_t kGameFolder[] = L"Warcraft2Remastered";
constexpr wchar_t kModFolder[] = L"wc2r-plus";

std::wstring Resolve() {
    PWSTR savedGames = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_SavedGames, KF_FLAG_CREATE, nullptr, &savedGames) != S_OK) {
        if (savedGames) CoTaskMemFree(savedGames);
        return std::wstring();
    }
    const std::wstring game = std::wstring(savedGames) + L"\\" + kGameFolder;
    CoTaskMemFree(savedGames);
    const std::wstring root = game + L"\\" + kModFolder;
    CreateDirectoryW(game.c_str(), nullptr);
    CreateDirectoryW(root.c_str(), nullptr);
    for (const wchar_t* sub : {L"config", L"logs", L"resources"}) {
        CreateDirectoryW((root + L"\\" + sub).c_str(), nullptr);
    }
    const DWORD attr = GetFileAttributesW(root.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) return std::wstring();
    return root;
}

std::wstring Under(const wchar_t* sub, const std::wstring& file) {
    const std::wstring& root = Root();
    if (root.empty()) return std::wstring();
    return root + L"\\" + sub + L"\\" + file;
}

}  // namespace

const std::wstring& Root() {
    static const std::wstring root = Resolve();
    return root;
}

std::wstring Config(const std::wstring& file) { return Under(L"config", file); }
std::wstring Logs(const std::wstring& file) { return Under(L"logs", file); }
std::wstring Resources(const std::wstring& file) { return Under(L"resources", file); }

std::wstring Widen(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                      nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &out[0], n);
    return out;
}

std::string Narrow(const std::wstring& wide) {
    if (wide.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                      nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), &out[0], n,
                        nullptr, nullptr);
    return out;
}

FILE* OpenShared(const std::string& utf8Path, const wchar_t* mode, int shareFlag) {
    if (utf8Path.empty()) return nullptr;
    return _wfsopen(Widen(utf8Path).c_str(), mode, shareFlag);
}

}  // namespace paths
