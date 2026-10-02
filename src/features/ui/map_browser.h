// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/map_index.h"
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

private:
    enum class View { BuiltIn, Progress, Search, Browse };

    struct Choice {
        bool valid = false;
        bool builtIn = false;
        std::string name, path, desc;
        std::string folder;
        bool folderKnown = false;
        bool folderLookedUp = false;
        unsigned players = 0, dim = 0, index = 0;
    };

    struct LiveEntry {
        uint8_t* entry = nullptr;
        const char *name = "", *path = "", *desc = "";
        unsigned players = 0, dim = 0, index = 0;
    };

    View CurrentView() const;
    std::vector<LiveEntry> LiveEntries() const;
    bool BuiltInMode() const;
    bool Searching() const { return search_[0] != '\0'; }
    bool HasTypeDropdown() const { return b_.modeCurrent != nullptr; }

    void SetGameFolder(const std::string& gamePath);
    bool GameFolderPath(const std::string& folder, std::string* gamePath) const;
    void ReloadMaps();
    void OnModeChanged();

    void DescendInto(const std::string& child);
    void GoUp();
    bool AtRoot() const { return folder_.empty(); }

    void RefreshBrowse();
    void RefreshSearch();

    void ChoiceFromLive(const LiveEntry& le);
    void ChoiceFromMap(const map_index::MapInfo& m);
    uint8_t* ResolveChoiceEntry();

    std::string DirectoryText() const;
    void DrawRowIcon(void* ctx, float rowTopLayout, bool folder);
    bool ListRow(void* ctx, const char* label, bool folder);
    void DrawList(void* ctx, const Rect& r);
    void DrawIndexProgress(void* ctx, const Rect& r);

    MapListBinding b_;
    Choice choice_;
    char search_[96] = {0};
    bool open_ = false;

    std::string folder_;
    bool folderSeeded_ = false;

    unsigned browseGen_ = ~0u;
    std::string browseFolder_;
    std::vector<std::string> browseFolders_;
    std::vector<map_index::MapInfo> browseMaps_;

    unsigned searchGen_ = ~0u;
    std::string searchQuery_;
    std::vector<map_index::MapInfo> results_;

    Rect previewBox_{};
};

}  // namespace ui
