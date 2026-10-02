// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/sha256.h"

namespace map_index {

bool ToGamePath(const std::wstring& wide, std::string* narrow);
bool FromGamePath(const std::string& narrow, std::wstring* wide);

bool HashFile(const std::wstring& path, uint8_t sha[kSha256Len]);

constexpr uint64_t kMaxMapFileBytes = 4ull * 1024 * 1024;

struct MapInfo {
    std::string name;
    std::string path;
    std::string folder;
    std::string desc;
    unsigned players = 0;
    unsigned dim = 0;
    uint64_t size = 0;
    uint64_t mtime = 0;
    uint8_t sha[kSha256Len] = {};
};

struct PudHeader {
    unsigned players = 0;
    unsigned dim = 0;
    std::string desc;
};
bool ParsePudHeader(const uint8_t* data, size_t size, PudHeader* out);

enum class State { Idle, Indexing, Ready };

struct Progress {
    State state = State::Idle;
    bool listing = false;
    unsigned filesFound = 0;
    unsigned filesDone = 0;
};

void Reload(const std::wstring& mapsRoot, const std::wstring& cacheFile);

void BuildNow(const std::wstring& mapsRoot, const std::wstring& cacheFile);

State GetState();
Progress GetProgress();

unsigned Generation();

struct BuildReport {
    unsigned maps = 0;
    unsigned folders = 0;
    unsigned filesRead = 0;
    unsigned ms = 0;
    bool capped = false;
    std::wstring root;
};
bool LastBuildReport(BuildReport* out);

using BuildListener = void (*)(const BuildReport&);
void SetBuildListener(BuildListener listener);

bool Search(const std::string& query, std::vector<MapInfo>* out);

bool FolderContents(const std::string& folder, std::vector<std::string>* folders,
                    std::vector<MapInfo>* maps);

bool FindBySha(const uint8_t sha[kSha256Len], std::string* gamePath);

bool FindByPath(const std::string& gamePath, MapInfo* out);

size_t Count();

bool Add(const std::wstring& path);

void ResetForTest();

}  // namespace map_index
