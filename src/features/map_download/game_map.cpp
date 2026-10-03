// SPDX-License-Identifier: MIT
#include "features/map_download/game_map.h"

#include "core/log.h"
#include "core/map_index.h"
#include "core/paths.h"

#include <windows.h>
#include <shlobj.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

#include <cstring>

#include "features/map_download/limits.h"
#include "features/map_download/map_store.h"
#include "target/addresses.h"
#include "target/struct_offsets.h"

namespace map_download {
namespace game_map {
namespace {

std::wstring FinalPathOf(HANDLE h) {
    wchar_t buf[1024];
    const DWORD n = GetFinalPathNameByHandleW(h, buf, 1024, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (n == 0 || n >= 1024) return std::wstring();
    std::wstring p(buf, n);
    if (p.compare(0, 8, L"\\\\?\\UNC\\") == 0) return L"\\\\" + p.substr(8);
    if (p.compare(0, 4, L"\\\\?\\") == 0) return p.substr(4);
    return p;
}

std::wstring FinalDirPath(const std::wstring& dir) {
    HANDLE h = CreateFileW(dir.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::wstring();
    std::wstring p = FinalPathOf(h);
    CloseHandle(h);
    return p;
}

bool PathUnder(const std::wstring& path, const std::wstring& root) {
    if (root.empty() || path.size() <= root.size() + 1) return false;
    if (CompareStringOrdinal(path.c_str(), static_cast<int>(root.size()), root.c_str(),
                             static_cast<int>(root.size()), TRUE) != CSTR_EQUAL) {
        return false;
    }
    return path[root.size()] == L'\\' || root.back() == L'\\';
}

}  // namespace

std::string LobbyMapName() {
    const char* p = game::kMpLobbyMapNameText.Get();
    size_t n = 0;
    while (n < 64 && p[n]) ++n;
    return std::string(p, n);
}

std::string SelectedMapPath() {
    const char* p = game::kSelectedMapPath.Get();
    size_t n = 0;
    while (n < game::wc2r::kMaxGamePath && p[n]) ++n;
    return n < game::wc2r::kMaxGamePath ? std::string(p, n) : std::string();
}

std::wstring MapsRoot() {
    const char* base = game::kMapsBaseDir.Get();
    size_t n = 0;
    while (n < game::wc2r::kMaxGamePath && base[n]) ++n;
    if (n == 0 || n >= game::wc2r::kMaxGamePath) return std::wstring();
    std::wstring wide;
    if (!map_index::FromGamePath(std::string(base, n), &wide)) return std::wstring();

    if (wide.back() != L'\\' && wide.back() != L'/') wide.push_back(L'\\');
    return wide + L"Maps";
}

std::wstring CacheFile() { return paths::Resources(L"map_index.txt"); }

namespace {

void LogIndexBuilt(const map_index::BuildReport& r) {
    Logger{"mapindex"}.Info("ready -- %u maps in %u folders, %u files read (the rest cached), %u ms%s",
                            r.maps, r.folders, r.filesRead, r.ms, r.capped ? " (CAPPED)" : "");
}

}  // namespace

bool ReloadMapIndex() {
    const std::wstring root = MapsRoot();
    if (root.empty()) return false;
    map_index::SetBuildListener(&LogIndexBuilt);
    map_index::Reload(root, CacheFile());
    return true;
}

bool ReadFileBounded(const std::string& gamePath, size_t limit, std::string* out) {
    std::wstring wide;
    if (!map_index::FromGamePath(gamePath, &wide)) return false;
    HANDLE h = CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart > 0 &&
              static_cast<uint64_t>(sz.QuadPart) <= limit;
    if (ok) {
        out->resize(static_cast<size_t>(sz.QuadPart));
        DWORD got = 0;
        ok = ReadFile(h, &(*out)[0], static_cast<DWORD>(out->size()), &got, nullptr) &&
             got == out->size();
    }
    CloseHandle(h);
    return ok;
}

bool SelectByPath(const std::string& gamePath) {
    if (gamePath.empty() || gamePath.size() >= game::wc2r::kMaxGamePath) return false;
    std::string bytes;
    if (!ReadFileBounded(gamePath, 4u * 1024 * 1024, &bytes)) return false;
    if (!game::kScanPudMetadataFromBuffer.Get()(
            reinterpret_cast<const unsigned char*>(bytes.data()),
            static_cast<unsigned>(bytes.size()))) {
        return false;
    }
    unsigned char info[0x48] = {};
    if (!game::kSelectMapAndReadHeader.Get()(gamePath.c_str(), info, 0)) return false;
    *game::kMissingMapFlag.Get() = 0;
    return true;
}

bool FileHasSha(const std::string& gamePath, const uint8_t sha[kSha256Len]) {
    std::wstring wide;
    uint8_t digest[kSha256Len];
    return map_index::FromGamePath(gamePath, &wide) && map_index::HashFile(wide, digest) &&
           memcmp(digest, sha, kSha256Len) == 0;
}

void RegisterDirectory(const std::wstring& dir) {
    std::wstring buf = dir;
    struct StdWString {
        const wchar_t* ptr;
        wchar_t pad[6];
        size_t size;
        size_t cap;
    } obj;
    obj.ptr = buf.c_str();
    obj.size = buf.size();
    obj.cap = buf.size() < 8 ? 8 : buf.size();
    game::kScanDirectoryIntoMapRegistry.Get()(&obj);
}

ServeRead ReadServableFile(const std::string& path, std::string* out, std::wstring* finalPath) {
    std::wstring wide;
    if (!map_index::FromGamePath(path, &wide)) return ServeRead::Unreadable;
    HANDLE h = CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return ServeRead::Unreadable;
    *finalPath = FinalPathOf(h);
    const std::wstring maps = MapsRoot();
    bool inside = false;
    if (!finalPath->empty() && !maps.empty()) {
        const std::wstring base = maps.substr(0, maps.size() - 4);
        inside = PathUnder(*finalPath, FinalDirPath(maps)) ||
                 PathUnder(*finalPath, FinalDirPath(base + L"Data\\Maps"));
    }
    if (!inside) {
        CloseHandle(h);
        return ServeRead::OutsideMaps;
    }
    LARGE_INTEGER sz;
    bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart > 0 &&
              static_cast<uint64_t>(sz.QuadPart) <= kMaxMapBytes;
    if (ok) {
        out->resize(static_cast<size_t>(sz.QuadPart));
        DWORD got = 0;
        ok = ReadFile(h, &(*out)[0], static_cast<DWORD>(out->size()), &got, nullptr) &&
             got == out->size();
    }
    CloseHandle(h);
    return ok ? ServeRead::Ok : ServeRead::Unreadable;
}

}  // namespace game_map
}  // namespace map_download
