// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/sha256.h"

namespace map_download {

enum class StoreResult {
    Ok,
    AlreadyPresent,
    BadName,
    QuotaExceeded,
    NoRoot,
    WriteFailed,
};

std::wstring DownloadRoot();
void SetDownloadRoot(const std::wstring& root);

StoreResult StoreMap(const std::string& name, const uint8_t* bytes, size_t size,
                     const uint8_t sha[kSha256Len], std::wstring* outDir, std::wstring* outFullPath);

void EnumerateStored(void (*fn)(const std::wstring& dir, const std::string& name, void* ctx), void* ctx);

}  // namespace map_download
