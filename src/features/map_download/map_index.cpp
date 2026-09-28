// SPDX-License-Identifier: MIT
#include "features/map_download/map_index.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <map>
#include <vector>

namespace map_download {
namespace {

constexpr uint64_t kMaxLocalMapBytes = 4ull * 1024 * 1024;

constexpr char kCacheHeader[] = "wc2r-maphash v1";

struct Entry {
    std::wstring path;
    std::string gamePath;
    uint64_t size = 0;
    uint64_t mtime = 0;
    uint8_t sha[kSha256Len] = {};
};

SRWLOCK g_lock = SRWLOCK_INIT;
struct Guard {
    Guard() { AcquireSRWLockExclusive(&g_lock); }
    ~Guard() { ReleaseSRWLockExclusive(&g_lock); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};
std::vector<Entry> g_entries;
std::vector<Entry> g_added;
std::atomic<bool> g_ready{false};
std::atomic<bool> g_started{false};
std::atomic<size_t> g_hashed{0};

bool IEndsWithPud(const wchar_t* name) {
    const size_t n = wcslen(name);
    return n > 4 && _wcsicmp(name + n - 4, L".pud") == 0;
}

void Walk(const std::wstring& dir, std::vector<Entry>* out) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == 0 ||
            (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0))) {
            continue;
        }
        const std::wstring child = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {

            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) Walk(child, out);
        } else if (IEndsWithPud(fd.cFileName)) {
            Entry e;
            e.path = child;
            e.size = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
            e.mtime = (static_cast<uint64_t>(fd.ftLastWriteTime.dwHighDateTime) << 32) |
                      fd.ftLastWriteTime.dwLowDateTime;
            if (e.size == 0 || e.size > kMaxLocalMapBytes) continue;
            if (!ToGamePath(e.path, &e.gamePath)) continue;
            out->push_back(e);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0,
                                      nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), static_cast<int>(s.size()),
                                      nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

bool ParseHex(const std::string& hex, uint8_t out[kSha256Len]) {
    if (hex.size() != kSha256Len * 2) return false;
    for (size_t i = 0; i < kSha256Len; ++i) {
        int v = 0;
        for (int k = 0; k < 2; ++k) {
            const char c = hex[i * 2 + k];
            int d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else return false;
            v = v * 16 + d;
        }
        out[i] = static_cast<uint8_t>(v);
    }
    return true;
}

std::string Hex(const uint8_t sha[kSha256Len]) {
    static const char* kHex = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < kSha256Len; ++i) {
        s.push_back(kHex[sha[i] >> 4]);
        s.push_back(kHex[sha[i] & 0xf]);
    }
    return s;
}

bool ReadText(const std::wstring& path, std::string* out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart > 0 && sz.QuadPart < 64 * 1024 * 1024;
    if (ok) {
        out->resize(static_cast<size_t>(sz.QuadPart));
        DWORD got = 0;
        ok = ReadFile(h, &(*out)[0], static_cast<DWORD>(out->size()), &got, nullptr) && got == out->size();
    }
    CloseHandle(h);
    return ok;
}

std::map<std::wstring, Entry> LoadCache(const std::wstring& file) {
    std::map<std::wstring, Entry> cache;
    std::string text;
    if (file.empty() || !ReadText(file, &text)) return cache;
    size_t pos = 0;
    bool first = true;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        const std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (first) {
            first = false;
            if (line != kCacheHeader) return cache;
            continue;
        }
        size_t t1 = line.find('\t');
        size_t t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1);
        size_t t3 = t2 == std::string::npos ? t2 : line.find('\t', t2 + 1);
        if (t3 == std::string::npos) continue;
        Entry e;
        if (!ParseHex(line.substr(0, t1), e.sha)) continue;
        e.size = _strtoui64(line.substr(t1 + 1, t2 - t1 - 1).c_str(), nullptr, 10);
        e.mtime = _strtoui64(line.substr(t2 + 1, t3 - t2 - 1).c_str(), nullptr, 10);
        e.path = FromUtf8(line.substr(t3 + 1));
        if (e.path.empty()) continue;
        cache[e.path] = e;
    }
    return cache;
}

void SaveCache(const std::wstring& file, const std::vector<Entry>& entries) {
    if (file.empty()) return;
    std::string text = kCacheHeader;
    text.push_back('\n');
    for (const Entry& e : entries) {
        text += Hex(e.sha);
        text += '\t' + std::to_string(e.size) + '\t' + std::to_string(e.mtime) + '\t' + Utf8(e.path) + '\n';
    }
    const std::wstring tmp = file + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD wrote = 0;
    const bool ok = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &wrote, nullptr) &&
                    wrote == text.size();
    CloseHandle(h);
    if (!ok || !MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING)) DeleteFileW(tmp.c_str());
}

}  // namespace

bool HashFile(const std::wstring& path, uint8_t sha[kSha256Len]) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    std::vector<uint8_t> buf;
    bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart > 0 &&
              static_cast<uint64_t>(sz.QuadPart) <= kMaxLocalMapBytes;
    if (ok) {
        buf.resize(static_cast<size_t>(sz.QuadPart));
        DWORD got = 0;
        ok = ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr) && got == buf.size();
    }
    CloseHandle(h);
    if (ok) Sha256(buf.data(), buf.size(), sha);
    return ok;
}

namespace {

bool WideUsing(UINT codePage, DWORD flags, const std::string& narrow, std::wstring* wide) {
    const int n = MultiByteToWideChar(codePage, flags, narrow.c_str(),
                                      static_cast<int>(narrow.size()), nullptr, 0);
    if (n <= 0) return false;
    wide->assign(static_cast<size_t>(n), L'\0');
    return MultiByteToWideChar(codePage, flags, narrow.c_str(), static_cast<int>(narrow.size()),
                               &(*wide)[0], n) == n;
}

}  // namespace

bool ToGamePath(const std::wstring& wide, std::string* narrow) {
    if (wide.empty() || wide.size() >= MAX_PATH) return false;
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.c_str(),
                                      static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);

    if (n <= 0 || n >= MAX_PATH) return false;
    narrow->assign(static_cast<size_t>(n), '\0');
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.c_str(),
                               static_cast<int>(wide.size()), &(*narrow)[0], n, nullptr,
                               nullptr) == n;
}

bool FromGamePath(const std::string& narrow, std::wstring* wide) {
    if (narrow.empty()) return false;
    if (WideUsing(CP_UTF8, MB_ERR_INVALID_CHARS, narrow, wide)) return true;
    return WideUsing(CP_ACP, 0, narrow, wide);
}

namespace map_index {

void BuildNow(const std::wstring& mapsRoot, const std::wstring& cacheFile) {
    std::vector<Entry> found;
    Walk(mapsRoot, &found);
    const std::map<std::wstring, Entry> cache = LoadCache(cacheFile);

    size_t hashed = 0;
    std::vector<Entry> kept;
    kept.reserve(found.size());
    for (Entry& e : found) {
        auto it = cache.find(e.path);
        if (it != cache.end() && it->second.size == e.size && it->second.mtime == e.mtime) {
            memcpy(e.sha, it->second.sha, kSha256Len);
        } else {
            if (!HashFile(e.path, e.sha)) continue;
            ++hashed;
        }
        kept.push_back(e);
    }
    SaveCache(cacheFile, kept);
    {
        Guard g;

        for (const Entry& e : g_added) {
            bool dup = false;
            for (const Entry& k : kept) {
                if (_wcsicmp(k.path.c_str(), e.path.c_str()) == 0) { dup = true; break; }
            }
            if (!dup) kept.push_back(e);
        }
        g_entries.swap(kept);
    }
    g_hashed = hashed;
    g_ready = true;
}

void StartBuild(const std::wstring& mapsRoot, const std::wstring& cacheFile) {
    bool expected = false;
    if (!g_started.compare_exchange_strong(expected, true)) return;
    struct Args {
        std::wstring root;
        std::wstring cache;
    };
    Args* args = new Args{mapsRoot, cacheFile};
    HANDLE h = CreateThread(
        nullptr, 0,
        [](LPVOID p) -> DWORD {
            Args* a = static_cast<Args*>(p);
            BuildNow(a->root, a->cache);
            delete a;
            return 0;
        },
        args, 0, nullptr);
    if (h) {
        CloseHandle(h);
    } else {
        delete args;
        g_started = false;
    }
}

bool Ready() { return g_ready; }

size_t Count() {
    Guard g;
    return g_entries.size();
}

size_t HashedLastBuild() { return g_hashed; }

bool FindBySha(const uint8_t sha[kSha256Len], std::string* gamePath) {
    Guard g;
    for (const Entry& e : g_entries) {
        if (memcmp(e.sha, sha, kSha256Len) == 0) {
            *gamePath = e.gamePath;
            return true;
        }
    }
    return false;
}

void Add(const std::wstring& path, const uint8_t sha[kSha256Len]) {
    Entry e;
    e.path = path;
    if (!ToGamePath(path, &e.gamePath)) return;
    memcpy(e.sha, sha, kSha256Len);
    Guard g;
    g_added.push_back(e);
    for (Entry& k : g_entries) {
        if (_wcsicmp(k.path.c_str(), path.c_str()) == 0) {
            memcpy(k.sha, sha, kSha256Len);
            return;
        }
    }
    g_entries.push_back(e);
}

void ResetForTest() {
    Guard g;
    g_entries.clear();
    g_added.clear();
    g_ready = false;
    g_started = false;
    g_hashed = 0;
}

}  // namespace map_index
}  // namespace map_download
