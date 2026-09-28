// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>

namespace ui {

struct IndexedMap {
    std::string name, path, desc, relDir;
    unsigned players = 0, dim = 0;
};

namespace map_search_cache {

bool Load(const std::string& mapsRoot, std::vector<IndexedMap>* maps, const char* logTag);

void Save(const std::string& mapsRoot, const std::vector<IndexedMap>& maps,
          const std::vector<std::string>& dirs, const char* logTag);

}  // namespace map_search_cache
}  // namespace ui
