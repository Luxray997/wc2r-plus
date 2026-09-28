// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "features/ui/map_search_cache.h"
#include "features/ui/screen_kit.h"

namespace ui {

struct MapListBinding {

    uint8_t** listBegin = nullptr;
    uint8_t** listEnd = nullptr;

    void* folderList = nullptr;

    void* relPath = nullptr;

    void* folderCurrent = nullptr;

    uint8_t* selectedEntry = nullptr;

    void* mapSizeCurrent = nullptr;
    void* mapSizeOptions = nullptr;
    uint8_t* comboOpenMapSize = nullptr;

    void* modeCurrent = nullptr;
    void* modeOptions = nullptr;
    uint8_t* comboOpenMode = nullptr;
    uint32_t* mode = nullptr;
    uint32_t* modeAtSelect = nullptr;

    void (*refreshFromFolder)(void* mapList, void* folderList, const void* relativePath) = nullptr;

    void (*refreshBuiltIn)(void* mapList) = nullptr;

    bool (*publish)(uint8_t* entry) = nullptr;

    ScreenTheme theme;
    const char* logTag = "browser";
};

class MapBrowser {
public:
    void Bind(const MapListBinding& binding) { b_ = binding; }

    bool IsOpen() const { return open_; }

    void Open();
    void Close() { open_ = false; }

    void Draw(void* ctx, const Rect& window);

    bool PreviewBox(Rect* box, const char** path, unsigned short* mapId) const;

    void ObserveMapsRoot();

private:
    struct Choice {
        bool valid = false;
        bool builtIn = false;
        std::string name, path, desc, relDir;
        unsigned players = 0, dim = 0, index = 0;
    };

    struct LiveEntry {
        uint8_t* entry = nullptr;
        const char *name = "", *path = "", *desc = "";
        unsigned players = 0, dim = 0, index = 0;
    };

    std::vector<LiveEntry> LiveEntries() const;
    bool BuiltInMode() const;
    bool Searching() const { return search_[0] != '\0'; }
    bool HasTypeDropdown() const { return b_.modeCurrent != nullptr; }

    void RefreshFromCurrentFolder();
    void ReloadMaps();
    void SetFolder(const std::string& rel);
    void DescendInto(const std::string& folder);
    void GoUp();
    bool AtRoot() const;
    void OnModeChanged();
    void InvalidateIndex();

    void IndexDir(const std::string& rel, int depth);
    void BuildSearchIndex();

    void ChoiceFromLive(const LiveEntry& le);
    void ChoiceFromIndex(const IndexedMap& m);
    uint8_t* ResolveChoiceEntry();

    std::string DirectoryText();
    void DrawRowIcon(void* ctx, float rowTopLayout, bool folder);
    bool ListRow(void* ctx, const char* label, bool folder);
    void DrawList(void* ctx, const Rect& r);

    MapListBinding b_;
    Choice choice_;
    std::vector<IndexedMap> index_;

    std::vector<std::string> walkedDirs_;
    std::string mapsRoot_;
    Rect previewBox_{};
    char search_[96] = {0};
    bool open_ = false;
    bool indexBuilt_ = false;

    bool skipCacheOnce_ = false;
    bool mapsRootKnown_ = false;
};

}  // namespace ui
