// SPDX-License-Identifier: MIT
#include "features/map_download/map_store.h"

#include <windows.h>

#include <cstring>

#include "features/map_download/limits.h"
#include "features/map_download/map_name.h"

namespace map_download {
namespace {

std::wstring g_root;

constexpr uint64_t kStoreQuotaBytes = 64ull * 1024 * 1024;
constexpr uint32_t kStoreQuotaFiles = 512;

constexpr int kMaxVersions = 99;

std::wstring Widen(const std::string& ascii) {

    std::wstring w;
    w.reserve(ascii.size());
    for (char c : ascii) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    return w;
}

bool EnsureRealDir(const std::wstring& dir) {
    if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    const DWORD attr = GetFileAttributesW(dir.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) return false;
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT) return false;
    return true;
}

bool EnsureOurs(const std::wstring& root, const std::wstring& dir) {
    const size_t cut = root.find_last_of(L'\\');
    if (cut == std::wstring::npos) return false;
    const DWORD parent = GetFileAttributesW(root.substr(0, cut).c_str());
    if (parent == INVALID_FILE_ATTRIBUTES || !(parent & FILE_ATTRIBUTE_DIRECTORY)) return false;
    return EnsureRealDir(root) && EnsureRealDir(dir);
}

bool ReadWhole(const std::wstring& path, std::string* out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart >= 0 && sz.QuadPart <= kMaxMapBytes;
    if (ok) {
        out->resize(static_cast<size_t>(sz.QuadPart));
        DWORD got = 0;
        ok = !out->empty() && ReadFile(h, &(*out)[0], static_cast<DWORD>(out->size()), &got, nullptr) &&
             got == out->size();
    }
    CloseHandle(h);
    return ok;
}

void WalkRoot(const std::wstring& root, uint64_t* bytes, uint32_t* files,
              void (*fn)(const std::wstring&, const std::string&, void*), void* ctx) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((root + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == 0 ||
            (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0))) {
            continue;
        }
        const std::wstring child = root + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                WalkRoot(child, bytes, files, fn, ctx);
            }
        } else {
            if (bytes) *bytes += (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
            if (files) ++*files;
            if (fn) {

                std::string narrow;
                for (const wchar_t* p = fd.cFileName; *p; ++p) narrow.push_back(static_cast<char>(*p));
                fn(root, narrow, ctx);
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

}  // namespace

void SetDownloadRoot(const std::wstring& root) { g_root = root; }

std::wstring DownloadRoot() { return g_root; }

StoreResult StoreMap(const std::string& name, const uint8_t* bytes, size_t size,
                     const uint8_t sha[kSha256Len], std::wstring* outDir, std::wstring* outFullPath) {
    if (ValidateMapName(name.c_str(), name.size()) != NameFault::Ok) return StoreResult::BadName;
    if (size == 0 || size > kMaxMapBytes) return StoreResult::WriteFailed;

    const std::wstring root = DownloadRoot();
    if (root.empty()) return StoreResult::NoRoot;

    std::wstring dir, final;
    for (int v = 1;; ++v) {
        if (v > kMaxVersions) return StoreResult::QuotaExceeded;
        dir = root + L"\\V" + std::to_wstring(v);
        final = dir + L"\\" + Widen(name);
        if (GetFileAttributesW(final.c_str()) == INVALID_FILE_ATTRIBUTES) break;
        std::string existing;
        if (ReadWhole(final, &existing) && existing.size() == size) {
            uint8_t digest[kSha256Len];
            Sha256(reinterpret_cast<const uint8_t*>(existing.data()), existing.size(), digest);
            if (memcmp(digest, sha, kSha256Len) == 0) {
                if (outDir) *outDir = dir;
                if (outFullPath) *outFullPath = final;
                return StoreResult::AlreadyPresent;
            }
        }
    }
    if (outDir) *outDir = dir;
    if (outFullPath) *outFullPath = final;

    uint64_t used = 0;
    uint32_t count = 0;
    WalkRoot(root, &used, &count, nullptr, nullptr);
    if (used + size > kStoreQuotaBytes || count + 1 > kStoreQuotaFiles) return StoreResult::QuotaExceeded;

    if (!EnsureOurs(root, dir)) return StoreResult::WriteFailed;

    const std::wstring part = final + L".part";
    HANDLE h = CreateFileW(part.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return StoreResult::WriteFailed;
    DWORD wrote = 0;
    const bool ok = WriteFile(h, bytes, static_cast<DWORD>(size), &wrote, nullptr) && wrote == size &&
                    FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok) {
        DeleteFileW(part.c_str());
        return StoreResult::WriteFailed;
    }

    if (!MoveFileExW(part.c_str(), final.c_str(), MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(part.c_str());
        return StoreResult::WriteFailed;
    }
    return StoreResult::Ok;
}

void EnumerateStored(void (*fn)(const std::wstring& dir, const std::string& name, void* ctx), void* ctx) {
    const std::wstring root = DownloadRoot();
    if (!root.empty()) WalkRoot(root, nullptr, nullptr, fn, ctx);
}

}  // namespace map_download
