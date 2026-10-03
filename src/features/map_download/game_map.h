// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string>

#include "core/sha256.h"

namespace map_download {
namespace game_map {

std::string LobbyMapName();

std::string SelectedMapPath();

std::wstring MapsRoot();
std::wstring CacheFile();

bool ReloadMapIndex();

bool ReadFileBounded(const std::string& gamePath, size_t limit, std::string* out);

bool SelectByPath(const std::string& gamePath);

bool FileHasSha(const std::string& gamePath, const uint8_t sha[kSha256Len]);

void RegisterDirectory(const std::wstring& dir);

enum class ServeRead { Ok, Unreadable, OutsideMaps };
ServeRead ReadServableFile(const std::string& path, std::string* out, std::wstring* finalPath);

}  // namespace game_map
}  // namespace map_download
