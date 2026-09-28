// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "features/map_download/sha256.h"

namespace map_download {

bool HashFile(const std::wstring& path, uint8_t sha[kSha256Len]);

bool ToGamePath(const std::wstring& wide, std::string* narrow);
bool FromGamePath(const std::string& narrow, std::wstring* wide);

namespace map_index {

void BuildNow(const std::wstring& mapsRoot, const std::wstring& cacheFile);
void StartBuild(const std::wstring& mapsRoot, const std::wstring& cacheFile);

bool Ready();
size_t Count();
size_t HashedLastBuild();

bool FindBySha(const uint8_t sha[kSha256Len], std::string* gamePath);

void Add(const std::wstring& path, const uint8_t sha[kSha256Len]);

void ResetForTest();

}  // namespace map_index
}  // namespace map_download
