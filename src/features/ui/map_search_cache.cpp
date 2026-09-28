// SPDX-License-Identifier: MIT
#include "features/ui/map_search_cache.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>

#include "core/log.h"
#include "features/map_download/game_map.h"
#include "features/map_download/map_index.h"

namespace ui {
namespace map_search_cache {
namespace {

constexpr char kHeader[] = "wc2r-mapsearch v1";

constexpr size_t kMaxCacheBytes = 16 * 1024 * 1024;

std::wstring CacheFilePath() {
    const std::wstring sibling = map_download::game_map::CacheFile();
    if (sibling.empty()) return std::wstring();
    const size_t slash = sibling.find_last_of(L'\\');
    if (slash == std::wstring::npos) return std::wstring();
    return sibling.substr(0, slash + 1) + L"map_search_index.txt";
}

std::string Escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::string Unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) {
            out.push_back(s[i]);
            continue;
        }
        switch (s[++i]) {
            case 't': out.push_back('\t'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case '\\': out.push_back('\\'); break;
            default: out.push_back(s[i]); break;
        }
    }
    return out;
}

uint64_t FileTimeValue(const FILETIME& ft) {
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

bool StatWide(const std::wstring& wide, uint64_t* size, uint64_t* mtime) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (wide.empty() || !GetFileAttributesExW(wide.c_str(), GetFileExInfoStandard, &a)) return false;
    *size = (static_cast<uint64_t>(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
    *mtime = FileTimeValue(a.ftLastWriteTime);
    return true;
}

bool StatPath(const std::string& gamePath, uint64_t* size, uint64_t* mtime) {
    std::wstring wide;
    return map_download::FromGamePath(gamePath, &wide) && StatWide(wide, size, mtime);
}

std::string HexTail(const std::string& path) {
    static const char* kHex = "0123456789abcdef";
    const size_t slash = path.find_last_of('\\');
    const std::string tail = slash == std::string::npos ? path : path.substr(slash + 1);
    std::string out;
    for (const char c : tail) {
        out.push_back(kHex[(static_cast<unsigned char>(c) >> 4) & 0xf]);
        out.push_back(kHex[static_cast<unsigned char>(c) & 0xf]);
    }
    return out;
}

bool ReadWholeFile(const std::wstring& path, std::string* out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart > 0 &&
              static_cast<uint64_t>(sz.QuadPart) <= kMaxCacheBytes;
    if (ok) {
        out->resize(static_cast<size_t>(sz.QuadPart));
        DWORD got = 0;
        ok = ReadFile(h, &(*out)[0], static_cast<DWORD>(out->size()), &got, nullptr) &&
             got == out->size();
    }
    CloseHandle(h);
    return ok;
}

void WriteWholeFile(const std::wstring& path, const std::string& text) {
    const std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD wrote = 0;
    const bool ok = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &wrote, nullptr) &&
                    wrote == text.size();
    CloseHandle(h);
    if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
    }
}

std::vector<std::string> SplitFields(const std::string& line, size_t want) {

    std::vector<std::string> out;
    size_t pos = 0;
    while (out.size() + 1 < want) {
        const size_t tab = line.find('\t', pos);
        if (tab == std::string::npos) return std::vector<std::string>();
        out.push_back(line.substr(pos, tab - pos));
        pos = tab + 1;
    }
    out.push_back(line.substr(pos));
    return out;
}

uint64_t ToU64(const std::string& s) { return _strtoui64(s.c_str(), nullptr, 10); }

}  // namespace

bool Load(const std::string& mapsRoot, std::vector<IndexedMap>* maps, const char* logTag) {
    const Logger log{logTag};
    maps->clear();

    const std::wstring file = CacheFilePath();
    std::string text;
    if (file.empty() || !ReadWholeFile(file, &text)) {
        log.Info("search index cache: nothing stored yet -- building from the game");
        return false;
    }

    const DWORD started = GetTickCount();
    std::vector<IndexedMap> loaded;
    size_t dirsChecked = 0;
    bool haveRoot = false;
    size_t pos = 0;
    bool first = true;

    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        const std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;

        if (first) {
            first = false;
            if (line != kHeader) return false;
            continue;
        }
        if (line.empty()) continue;

        if (line[0] == 'R') {
            const std::vector<std::string> f = SplitFields(line, 2);
            if (f.empty() || Unescape(f[1]) != mapsRoot) return false;
            haveRoot = true;
            continue;
        }

        if (line[0] == 'D') {
            const std::vector<std::string> f = SplitFields(line, 3);
            if (f.empty()) continue;
            const std::string dir = Unescape(f[2]);
            uint64_t size = 0, mtime = 0;
            if (!StatPath(dir, &size, &mtime) || mtime != ToU64(f[1])) {
                log.Info("search index cache: '%s' has changed -- rebuilding", dir.c_str());
                return false;
            }
            ++dirsChecked;
            continue;
        }

        if (line[0] == 'M') {
            const std::vector<std::string> f = SplitFields(line, 9);
            if (f.empty()) continue;
            IndexedMap m;
            m.dim = static_cast<unsigned>(ToU64(f[3]));
            m.players = static_cast<unsigned>(ToU64(f[4]));
            m.relDir = Unescape(f[5]);
            m.name = Unescape(f[6]);
            m.path = Unescape(f[7]);
            m.desc = Unescape(f[8]);
            uint64_t size = 0, mtime = 0;
            if (!StatPath(m.path, &size, &mtime) || size != ToU64(f[1]) || mtime != ToU64(f[2])) {
                log.Info("search index cache: '%s' has changed -- rebuilding", m.path.c_str());
                return false;
            }
            loaded.push_back(m);
            continue;
        }
    }

    if (!haveRoot) return false;

    *maps = loaded;
    log.Info("search index from cache -- %u maps, %u directories checked in %u ms",
             static_cast<unsigned>(maps->size()), static_cast<unsigned>(dirsChecked),
             static_cast<unsigned>(GetTickCount() - started));
    return true;
}

void Save(const std::string& mapsRoot, const std::vector<IndexedMap>& maps,
          const std::vector<std::string>& dirs, const char* logTag) {
    const Logger log{logTag};
    const std::wstring file = CacheFilePath();
    if (file.empty()) {
        log.Warn("search index cache: no folder to write it in -- it will rebuild every launch");
        return;
    }

    std::string text = kHeader;
    text += "\nR\t" + Escape(mapsRoot) + "\n";

    for (const std::string& dir : dirs) {
        uint64_t size = 0, mtime = 0;
        if (!StatPath(dir, &size, &mtime)) continue;
        char num[32];
        _snprintf_s(num, sizeof(num), _TRUNCATE, "%llu", mtime);
        text += "D\t";
        text += num;
        text += '\t' + Escape(dir) + '\n';
    }

    size_t dropped = 0;
    for (const IndexedMap& m : maps) {
        uint64_t size = 0, mtime = 0;

        if (!StatPath(m.path, &size, &mtime)) {
            ++dropped;
            if (dropped <= 4) {
                log.Warn("search index cache: cannot find '%s' [%s] -- left out, so a search will "
                         "not find it on a cached launch",
                         m.path.c_str(), HexTail(m.path).c_str());
            }
            continue;
        }
        char nums[96];
        _snprintf_s(nums, sizeof(nums), _TRUNCATE, "%llu\t%llu\t%u\t%u", size, mtime, m.dim,
                    m.players);
        text += "M\t";
        text += nums;
        text += '\t' + Escape(m.relDir);
        text += '\t' + Escape(m.name);
        text += '\t' + Escape(m.path);
        text += '\t' + Escape(m.desc) + '\n';
    }

    WriteWholeFile(file, text);

    log.Info("search index cached -- %u of %u maps, %u directories%s",
             static_cast<unsigned>(maps.size() - dropped), static_cast<unsigned>(maps.size()),
             static_cast<unsigned>(dirs.size()),
             dropped > 4 ? " (more than 4 dropped; only the first 4 are named)" : "");
}

}  // namespace map_search_cache
}  // namespace ui
