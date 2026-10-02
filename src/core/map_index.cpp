// SPDX-License-Identifier: MIT
#include "core/map_index.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <unordered_map>
#include <utility>

namespace map_index {

namespace {

bool WideUsing(UINT codePage, DWORD flags, const std::string& narrow, std::wstring* wide) {
    const int n = MultiByteToWideChar(codePage, flags, narrow.c_str(),
                                      static_cast<int>(narrow.size()), nullptr, 0);
    if (n <= 0) return false;
    wide->assign(static_cast<size_t>(n), L'\0');
    return MultiByteToWideChar(codePage, flags, narrow.c_str(), static_cast<int>(narrow.size()),
                               &(*wide)[0], n) == n;
}

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0,
                                      nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
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

namespace {

bool ReadMapFile(const std::wstring& path, std::vector<uint8_t>* out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart >= 0 &&
              static_cast<uint64_t>(sz.QuadPart) <= kMaxMapFileBytes;
    if (ok) {
        out->resize(static_cast<size_t>(sz.QuadPart));
        DWORD got = 0;
        ok = out->empty() ||
             (ReadFile(h, out->data(), static_cast<DWORD>(out->size()), &got, nullptr) &&
              got == out->size());
    }
    CloseHandle(h);
    return ok;
}

}  // namespace

bool HashFile(const std::wstring& path, uint8_t sha[kSha256Len]) {
    std::vector<uint8_t> buf;
    if (!ReadMapFile(path, &buf) || buf.empty()) return false;
    Sha256(buf.data(), buf.size(), sha);
    return true;
}

namespace {

constexpr uint32_t Tag(const char (&s)[5]) {
    return static_cast<uint32_t>(static_cast<uint8_t>(s[0])) |
           (static_cast<uint32_t>(static_cast<uint8_t>(s[1])) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(s[2])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(s[3])) << 24);
}

enum SectionId { kType, kVer, kDesc, kOwnr, kEra, kDim, kSide, kSign, kSectionCount };

struct Section {
    uint32_t tag;
    bool required;
};

const Section kHeaderScan[kSectionCount] = {
    {Tag("TYPE"), true}, {Tag("VER "), true}, {Tag("DESC"), true}, {Tag("OWNR"), true},
    {Tag("ERA "), true}, {Tag("DIM "), true}, {Tag("SIDE"), false}, {Tag("SIGN"), false},
};

constexpr uint32_t kNoSection = 0xffffffffu;
constexpr uint8_t kOwnerHuman = 5;

struct Stream {
    const uint8_t* data;
    uint64_t size;
    uint64_t pos = 0;
    uint32_t left;

    bool Read(void* out, uint32_t n) {
        if (pos > size || size - pos < n) return false;
        if (n) memcpy(out, data + pos, n);
        pos += n;
        return true;
    }
    void Seek(uint32_t n) { pos += n; }
};

uint32_t LoadU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint32_t FindFirst(Stream& s, uint32_t tag) {
    if (static_cast<int32_t>(s.left) <= 0) return kNoSection;
    uint8_t hdr[8];
    while (s.Read(hdr, 8)) {
        s.left -= 8;
        if (static_cast<int32_t>(s.left) < 0) return kNoSection;
        const uint32_t len = LoadU32(hdr + 4);
        if (s.left <= len) return kNoSection;
        if (LoadU32(hdr) == tag) return len;
        if (len != 0) {
            s.Seek(len);
            s.left -= 4;
        }
        if (static_cast<int32_t>(s.left) < 1) return kNoSection;
    }
    return kNoSection;
}

uint32_t NextHeader(Stream& s, uint32_t* tag) {
    if (s.left <= 7) return kNoSection;
    uint8_t hdr[8];
    if (!s.Read(hdr, 8)) return kNoSection;
    s.left -= 8;
    *tag = LoadU32(hdr);
    return LoadU32(hdr + 4);
}

bool Handle(int id, uint32_t len, Stream& s, uint8_t owners[16], uint16_t* width, char desc[33]) {
    switch (id) {
        case kType: {
            if (len > s.left || len > 16) return false;
            s.left -= len;
            uint8_t buf[17] = {};
            if (!s.Read(buf, len)) return false;
            return memcmp(buf, "WAR2 MAP", 8) == 0 && buf[8] == 0;
        }
        case kVer: {
            if (len != 2 || static_cast<int32_t>(s.left) <= 1) return false;
            s.left -= 2;
            uint8_t v[2];
            if (!s.Read(v, 2)) return false;
            const uint16_t ver = static_cast<uint16_t>(v[0] | (v[1] << 8));
            return ver == 0x11 || ver == 0x13;
        }
        case kDesc:
            if (len != 32 || s.left <= 31) return false;
            s.left -= 32;
            return s.Read(desc, 32);
        case kOwnr:
            if (len != 16 || s.left < 16) return false;
            s.left -= 16;
            return s.Read(owners, 16);
        case kEra: {
            if (len != 2 || s.left <= 1) return false;
            s.left -= 2;
            uint8_t v[2];
            return s.Read(v, 2);
        }
        case kDim: {
            if (len != 4 || s.left <= 1) return false;
            s.left -= 2;
            uint8_t w[2];
            if (!s.Read(w, 2)) return false;
            *width = static_cast<uint16_t>(w[0] | (w[1] << 8));
            if (s.left < 2) return false;
            s.left -= 2;
            uint8_t h[2];
            return s.Read(h, 2);
        }
        case kSide: {
            if (len != 16 || s.left < 16) return false;
            s.left -= 16;
            uint8_t races[16];
            return s.Read(races, 16);
        }
        case kSign: {
            if (len != 4 || s.left <= 3) return false;
            s.left -= 4;
            uint8_t sign[4];
            return s.Read(sign, 4);
        }
    }
    return false;
}

}  // namespace

bool ParsePudHeader(const uint8_t* data, size_t size, PudHeader* out) {
    if (!data || size == 0 || size > kMaxMapFileBytes) return false;
    Stream s{data, size};
    s.left = static_cast<uint32_t>(size);
    uint8_t owners[16] = {};
    uint16_t width = 0;
    char desc[33] = {};

    int next = 0;
    while (next < kSectionCount) {
        uint32_t tag = kHeaderScan[kType].tag;
        const uint32_t len = next == 0 ? FindFirst(s, tag) : NextHeader(s, &tag);
        if (len == kNoSection || s.left < len) {

            if (next != kSectionCount - 1 || kHeaderScan[kSectionCount - 1].required) return false;
            break;
        }
        int id = next;
        bool skippedOnlyOptional = true;
        while (id < kSectionCount && kHeaderScan[id].tag != tag) {
            if (kHeaderScan[id].required) skippedOnlyOptional = false;
            ++id;
        }
        if (id == kSectionCount) {
            if (len != 0) {
                s.left -= len;
                s.Seek(len);
            }
            continue;
        }
        if (id != next && !skippedOnlyOptional) return false;
        if (!Handle(id, len, s, owners, &width, desc)) return false;
        next = id + 1;
    }

    out->players = 0;
    for (int i = 0; i < 8; ++i) {
        if (owners[i] == kOwnerHuman) ++out->players;
    }
    out->dim = width;
    out->desc.assign(desc, strnlen(desc, 32));
    return true;
}

namespace {

SRWLOCK g_lock = SRWLOCK_INIT;
struct Exclusive {
    Exclusive() { AcquireSRWLockExclusive(&g_lock); }
    ~Exclusive() { ReleaseSRWLockExclusive(&g_lock); }
    Exclusive(const Exclusive&) = delete;
    Exclusive& operator=(const Exclusive&) = delete;
};
struct Shared {
    Shared() { AcquireSRWLockShared(&g_lock); }
    ~Shared() { ReleaseSRWLockShared(&g_lock); }
    Shared(const Shared&) = delete;
    Shared& operator=(const Shared&) = delete;
};

constexpr int kMaxDepth = 16;
constexpr size_t kMaxFiles = 20000;

constexpr char kCacheHeader[] = "wc2r-mapindex v1";

struct Folder {
    std::vector<std::string> children;
    std::vector<size_t> maps;
};

struct Index {
    std::wstring root;
    std::vector<MapInfo> maps;
    std::map<std::string, Folder> folders;
};

struct CacheRec {
    bool isMap = false;
    uint64_t size = 0, mtime = 0;
    unsigned players = 0, dim = 0;
    std::string desc;
    uint8_t sha[kSha256Len] = {};
};

struct Added {
    std::wstring wide;
    MapInfo map;
};

Index g_index;
std::vector<Added> g_added;
bool g_running = false;
bool g_pending = false;
std::wstring g_pendingRoot, g_pendingCache;
BuildReport g_report;
bool g_haveReport = false;
std::atomic<BuildListener> g_listener{nullptr};

std::atomic<int> g_state{static_cast<int>(State::Idle)};
std::atomic<unsigned> g_generation{0};
std::atomic<bool> g_listing{false};
std::atomic<unsigned> g_found{0};
std::atomic<unsigned> g_done{0};

bool LessName(const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) < 0; }

std::wstring Stem(const std::wstring& file) {
    if (file == L"." || file == L"..") return file;
    const size_t dot = file.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0) return file;
    return file.substr(0, dot);
}

std::string Escape(const std::string& s) {
    std::string out;
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
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) {
            out.push_back(s[i]);
            continue;
        }
        switch (s[++i]) {
            case 't': out.push_back('\t'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            default: out.push_back(s[i]); break;
        }
    }
    return out;
}

std::string Hex(const uint8_t* p, size_t n) {
    static const char* kHex = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        s.push_back(kHex[p[i] >> 4]);
        s.push_back(kHex[p[i] & 0xf]);
    }
    return s;
}

bool ParseHex(const std::string& hex, uint8_t* out, size_t n) {
    if (hex.size() != n * 2) return false;
    for (size_t i = 0; i < n; ++i) {
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

std::vector<std::string> Split(const std::string& line, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t at = line.find(sep, start);
        out.push_back(line.substr(start, at == std::string::npos ? std::string::npos : at - start));
        if (at == std::string::npos) return out;
        start = at + 1;
    }
}

std::unordered_map<std::string, CacheRec> LoadCache(const std::wstring& file) {
    std::unordered_map<std::string, CacheRec> cache;
    std::vector<uint8_t> raw;
    if (file.empty()) return cache;
    {
        HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return cache;
        LARGE_INTEGER sz;
        if (GetFileSizeEx(h, &sz) && sz.QuadPart > 0 && sz.QuadPart < 64 * 1024 * 1024) {
            raw.resize(static_cast<size_t>(sz.QuadPart));
            DWORD got = 0;
            if (!ReadFile(h, raw.data(), static_cast<DWORD>(raw.size()), &got, nullptr) ||
                got != raw.size()) {
                raw.clear();
            }
        }
        CloseHandle(h);
    }
    const std::string text(raw.begin(), raw.end());
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
        const std::vector<std::string> f = Split(line, '\t');
        CacheRec r;
        std::string path;
        if (f.size() == 8 && f[0] == "M") {
            r.isMap = true;
            r.size = _strtoui64(f[1].c_str(), nullptr, 10);
            r.mtime = _strtoui64(f[2].c_str(), nullptr, 10);
            if (!ParseHex(f[3], r.sha, kSha256Len)) continue;
            r.players = static_cast<unsigned>(strtoul(f[4].c_str(), nullptr, 10));
            r.dim = static_cast<unsigned>(strtoul(f[5].c_str(), nullptr, 10));
            r.desc = Unescape(f[6]);
            path = f[7];
        } else if (f.size() == 4 && f[0] == "X") {
            r.size = _strtoui64(f[1].c_str(), nullptr, 10);
            r.mtime = _strtoui64(f[2].c_str(), nullptr, 10);
            path = f[3];
        } else {
            continue;
        }
        if (!path.empty()) cache[path] = r;
    }
    return cache;
}

void SaveCache(const std::wstring& file, const std::vector<std::pair<std::string, CacheRec>>& recs) {
    if (file.empty()) return;
    std::string text = kCacheHeader;
    text.push_back('\n');
    for (const auto& pr : recs) {
        const CacheRec& r = pr.second;
        if (r.isMap) {
            text += "M\t" + std::to_string(r.size) + '\t' + std::to_string(r.mtime) + '\t' +
                    Hex(r.sha, kSha256Len) + '\t' + std::to_string(r.players) + '\t' +
                    std::to_string(r.dim) + '\t' + Escape(r.desc) + '\t' + pr.first + '\n';
        } else {
            text += "X\t" + std::to_string(r.size) + '\t' + std::to_string(r.mtime) + '\t' +
                    pr.first + '\n';
        }
    }
    const std::wstring tmp = file + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD wrote = 0;
    const bool ok = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &wrote, nullptr) &&
                    wrote == text.size();
    CloseHandle(h);
    if (!ok || !MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
    }
}

struct Found {
    std::wstring wide;
    std::string path;
    std::string folder;
    std::string name;
    uint64_t size = 0, mtime = 0;
};

uint64_t FileTime(const FILETIME& ft) {
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

void Walk(const std::wstring& dir, const std::string& folder, int depth, Index* idx,
          std::vector<Found>* files, bool* capped) {
    Folder& node = idx->folders[folder];
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch,
                                nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<WIN32_FIND_DATAW> dirs, here;
    do {
        if (fd.cFileName[0] == L'.' &&
            (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0))) {
            continue;
        }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) dirs.push_back(fd);
        } else {
            here.push_back(fd);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    auto byName = [](const WIN32_FIND_DATAW& a, const WIN32_FIND_DATAW& b) {
        return _wcsicmp(a.cFileName, b.cFileName) < 0;
    };
    std::sort(dirs.begin(), dirs.end(), byName);
    std::sort(here.begin(), here.end(), byName);

    for (const WIN32_FIND_DATAW& f : here) {
        if (files->size() >= kMaxFiles) {
            *capped = true;
            break;
        }
        Found e;
        e.wide = dir + L"\\" + f.cFileName;
        if (!ToGamePath(e.wide, &e.path)) continue;
        e.folder = folder;
        e.name = Utf8(Stem(f.cFileName));
        e.size = (static_cast<uint64_t>(f.nFileSizeHigh) << 32) | f.nFileSizeLow;
        e.mtime = FileTime(f.ftLastWriteTime);
        files->push_back(std::move(e));
        g_found.fetch_add(1, std::memory_order_relaxed);
    }

    if (depth >= kMaxDepth) return;
    for (const WIN32_FIND_DATAW& d : dirs) {
        const std::string name = Utf8(d.cFileName);
        if (name.empty()) continue;

        node.children.push_back(name);
        Walk(dir + L"\\" + d.cFileName, folder.empty() ? name : folder + '\\' + name, depth + 1, idx,
             files, capped);
    }
}

bool FolderUnder(const std::wstring& root, const std::wstring& path, std::string* folder) {
    if (root.empty() || path.size() <= root.size() + 1 ||
        _wcsnicmp(path.c_str(), root.c_str(), root.size()) != 0 || path[root.size()] != L'\\') {
        return false;
    }
    const std::wstring rest = path.substr(root.size() + 1);
    const size_t slash = rest.find_last_of(L'\\');
    *folder = slash == std::wstring::npos ? std::string() : Utf8(rest.substr(0, slash));
    return true;
}

void Insert(Index& idx, const Added& added) {
    MapInfo m = added.map;
    const bool listed = FolderUnder(idx.root, added.wide, &m.folder);
    if (!listed) m.folder.clear();
    for (MapInfo& existing : idx.maps) {
        if (_stricmp(existing.path.c_str(), m.path.c_str()) == 0) {
            existing = m;
            return;
        }
    }
    idx.maps.push_back(m);
    const size_t at = idx.maps.size() - 1;
    if (!listed) return;

    std::string parent;
    size_t start = 0;
    while (!m.folder.empty()) {
        const size_t slash = m.folder.find('\\', start);
        const std::string child = m.folder.substr(start, slash == std::string::npos ? std::string::npos
                                                                                    : slash - start);
        const std::string here = parent.empty() ? child : parent + '\\' + child;
        Folder& p = idx.folders[parent];
        if (std::find(p.children.begin(), p.children.end(), child) == p.children.end()) {
            p.children.insert(std::upper_bound(p.children.begin(), p.children.end(), child, LessName),
                              child);
        }
        idx.folders[here];
        parent = here;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    std::vector<size_t>& list = idx.folders[m.folder].maps;
    list.insert(std::upper_bound(list.begin(), list.end(), at,
                                 [&](size_t a, size_t b) {
                                     return LessName(idx.maps[a].name, idx.maps[b].name);
                                 }),
                at);
}

void Build(const std::wstring& root, const std::wstring& cacheFile, Index* out, BuildReport* report) {
    const DWORD started = GetTickCount();
    g_listing.store(true);
    g_found.store(0);
    g_done.store(0);

    out->root = root;
    std::vector<Found> files;
    bool capped = false;
    Walk(root, std::string(), 0, out, &files, &capped);
    g_listing.store(false);

    const std::unordered_map<std::string, CacheRec> cache = LoadCache(cacheFile);
    std::vector<std::pair<std::string, CacheRec>> recs;
    recs.reserve(files.size());
    unsigned read = 0;
    std::vector<uint8_t> buf;
    for (const Found& f : files) {
        CacheRec r;
        const auto hit = cache.find(f.path);
        if (hit != cache.end() && hit->second.size == f.size && hit->second.mtime == f.mtime) {
            r = hit->second;
        } else if (f.size <= kMaxMapFileBytes) {
            ++read;
            r.size = f.size;
            r.mtime = f.mtime;
            PudHeader hdr;
            if (ReadMapFile(f.wide, &buf) && ParsePudHeader(buf.data(), buf.size(), &hdr)) {
                r.isMap = true;
                r.players = hdr.players;
                r.dim = hdr.dim;
                r.desc = hdr.desc;
                Sha256(buf.data(), buf.size(), r.sha);
            }
        } else {
            r.size = f.size;
            r.mtime = f.mtime;
        }
        g_done.fetch_add(1, std::memory_order_relaxed);
        if (r.isMap) {
            MapInfo m;
            m.name = f.name;
            m.path = f.path;
            m.folder = f.folder;
            m.desc = r.desc;
            m.players = r.players;
            m.dim = r.dim;
            m.size = r.size;
            m.mtime = r.mtime;
            memcpy(m.sha, r.sha, kSha256Len);
            out->maps.push_back(std::move(m));
            out->folders[f.folder].maps.push_back(out->maps.size() - 1);
        }
        recs.emplace_back(f.path, std::move(r));
    }
    SaveCache(cacheFile, recs);

    report->maps = static_cast<unsigned>(out->maps.size());
    report->folders = static_cast<unsigned>(out->folders.size());
    report->filesRead = read;
    report->ms = GetTickCount() - started;
    report->capped = capped;
    report->root = root;
}

void Install(Index& fresh, const BuildReport& report) {
    for (const Added& a : g_added) Insert(fresh, a);
    g_index = std::move(fresh);
    g_report = report;
    g_haveReport = true;
    g_state.store(static_cast<int>(State::Ready));
    g_generation.fetch_add(1);
}

DWORD WINAPI BuildThread(LPVOID) {
    for (;;) {
        std::wstring root, cache;
        {
            Exclusive g;
            root = g_pendingRoot;
            cache = g_pendingCache;
            g_pending = false;
        }
        Index fresh;
        BuildReport report;
        Build(root, cache, &fresh, &report);
        {
            Exclusive g;
            if (g_pending) continue;
            Install(fresh, report);
            g_running = false;
        }
        if (const BuildListener listener = g_listener.load()) listener(report);
        return 0;
    }
}

}  // namespace

void Reload(const std::wstring& mapsRoot, const std::wstring& cacheFile) {
    {
        Exclusive g;
        g_pendingRoot = mapsRoot;
        g_pendingCache = cacheFile;
        g_state.store(static_cast<int>(State::Indexing));
        if (g_running) {
            g_pending = true;
            return;
        }
        g_running = true;
        g_pending = false;
    }
    HANDLE h = CreateThread(nullptr, 0, &BuildThread, nullptr, 0, nullptr);
    if (h) {
        CloseHandle(h);
        return;
    }

    BuildThread(nullptr);
}

void BuildNow(const std::wstring& mapsRoot, const std::wstring& cacheFile) {
    g_state.store(static_cast<int>(State::Indexing));
    Index fresh;
    BuildReport report;
    Build(mapsRoot, cacheFile, &fresh, &report);
    {
        Exclusive g;
        Install(fresh, report);
    }
    if (const BuildListener listener = g_listener.load()) listener(report);
}

State GetState() { return static_cast<State>(g_state.load()); }

Progress GetProgress() {
    Progress p;
    p.state = GetState();
    p.listing = g_listing.load(std::memory_order_relaxed);
    p.filesFound = g_found.load(std::memory_order_relaxed);
    p.filesDone = g_done.load(std::memory_order_relaxed);
    return p;
}

unsigned Generation() { return g_generation.load(); }

bool LastBuildReport(BuildReport* out) {
    Shared g;
    if (!g_haveReport) return false;
    *out = g_report;
    return true;
}

void SetBuildListener(BuildListener listener) { g_listener.store(listener); }

bool Search(const std::string& query, std::vector<MapInfo>* out) {
    out->clear();
    Shared g;
    if (GetState() != State::Ready) return false;
    for (const MapInfo& m : g_index.maps) {
        const auto hit = std::search(m.name.begin(), m.name.end(), query.begin(), query.end(),
                                     [](char a, char b) {
                                         return (a >= 'A' && a <= 'Z' ? a + 32 : a) ==
                                                (b >= 'A' && b <= 'Z' ? b + 32 : b);
                                     });
        if (hit != m.name.end() || query.empty()) out->push_back(m);
    }
    return true;
}

bool FolderContents(const std::string& folder, std::vector<std::string>* folders,
                    std::vector<MapInfo>* maps) {
    folders->clear();
    maps->clear();
    Shared g;
    if (GetState() != State::Ready) return false;
    const auto it = g_index.folders.find(folder);
    if (it == g_index.folders.end()) return false;
    *folders = it->second.children;
    for (const size_t i : it->second.maps) maps->push_back(g_index.maps[i]);
    return true;
}

bool FindBySha(const uint8_t sha[kSha256Len], std::string* gamePath) {
    Shared g;
    if (GetState() != State::Ready) return false;
    for (const MapInfo& m : g_index.maps) {
        if (memcmp(m.sha, sha, kSha256Len) == 0) {
            *gamePath = m.path;
            return true;
        }
    }
    return false;
}

bool FindByPath(const std::string& gamePath, MapInfo* out) {
    Shared g;
    if (GetState() != State::Ready) return false;
    for (const MapInfo& m : g_index.maps) {
        if (_stricmp(m.path.c_str(), gamePath.c_str()) == 0) {
            *out = m;
            return true;
        }
    }
    return false;
}

size_t Count() {
    Shared g;
    return GetState() == State::Ready ? g_index.maps.size() : 0;
}

bool Add(const std::wstring& path) {
    std::vector<uint8_t> buf;
    PudHeader hdr;
    MapInfo m;
    if (!ReadMapFile(path, &buf) || !ParsePudHeader(buf.data(), buf.size(), &hdr) ||
        !ToGamePath(path, &m.path)) {
        return false;
    }
    const size_t slash = path.find_last_of(L'\\');
    m.name = Utf8(Stem(slash == std::wstring::npos ? path : path.substr(slash + 1)));
    m.desc = hdr.desc;
    m.players = hdr.players;
    m.dim = hdr.dim;
    m.size = buf.size();
    Sha256(buf.data(), buf.size(), m.sha);
    {
        WIN32_FILE_ATTRIBUTE_DATA a;
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) m.mtime = FileTime(a.ftLastWriteTime);
    }

    const Added added{path, m};
    Exclusive g;
    bool replaced = false;
    for (Added& a : g_added) {
        if (_stricmp(a.map.path.c_str(), m.path.c_str()) == 0) {
            a = added;
            replaced = true;
        }
    }
    if (!replaced) g_added.push_back(added);
    if (GetState() == State::Ready) {
        Insert(g_index, added);
        g_generation.fetch_add(1);
    }
    return true;
}

void ResetForTest() {
    Exclusive g;
    g_index = Index();
    g_added.clear();
    g_running = false;
    g_pending = false;
    g_haveReport = false;
    g_state.store(static_cast<int>(State::Idle));
}

}  // namespace map_index
