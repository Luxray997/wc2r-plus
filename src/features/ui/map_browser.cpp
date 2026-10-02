// SPDX-License-Identifier: MIT
#include "features/ui/map_browser.h"
#include "target/struct_offsets.h"

#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "features/map_download/game_map.h"
#include "features/map_preview.h"
#include "target/addresses.h"

namespace ui {
namespace {

constexpr float kBrowserWFraction = 0.80f;
constexpr float kBrowserHFraction = 0.88f;
constexpr float kBrowserLeftFraction = 0.56f;

constexpr unsigned int kBarTrack = Rgba(0, 0, 0, 160);
constexpr unsigned int kBarFill = Rgba(240, 206, 74, 255);
constexpr float kBarH = 24.0f;
constexpr float kBarWFraction = 0.7f;

constexpr float kIconSize = 46.0f;

constexpr float kIconInset = 26.0f + kIconSize * 0.2f;

constexpr unsigned int kFolderColour = Rgba(226, 184, 68, 255);
constexpr unsigned int kMapColour = Rgba(96, 186, 108, 255);
constexpr unsigned int kIconHole = Rgba(18, 14, 10, 220);

struct SizeFilter {
    bool all = true;
    const char* wanted = "";
};

SizeFilter CurrentSizeFilter(const MapListBinding& b) {
    SizeFilter f;
    const KeyList sizes = Keys(b.mapSizeOptions);
    const char* current = StrData(b.mapSizeCurrent);
    f.all = sizes.size() == 0 || (sizes.begin[0] && strcmp(sizes.begin[0], current) == 0);
    const char* underscore = strrchr(current, '_');
    f.wanted = underscore ? underscore + 1 : current;
    return f;
}

bool PassesSizeFilter(const SizeFilter& f, unsigned dim) {
    if (f.all) return true;
    char buf[16];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%u", dim);
    return strcmp(buf, f.wanted) == 0;
}

}  // namespace

std::vector<MapBrowser::LiveEntry> MapBrowser::LiveEntries() const {
    std::vector<LiveEntry> out;
    if (!b_.listBegin || !b_.listEnd) return out;
    uint8_t* begin = *b_.listBegin;
    uint8_t* end = *b_.listEnd;
    if (!begin || !end || end <= begin) return out;
    for (uint8_t* e = begin; e + game::wc2r::kEntryStride <= end; e += game::wc2r::kEntryStride) {
        LiveEntry le;
        le.entry = e;
        le.name = StrData(e + game::wc2r::kEntryName);
        le.path = StrData(e + game::wc2r::kEntryPath);
        le.desc = StrData(e + game::wc2r::kEntryDesc);
        le.players = e[game::wc2r::kEntryPlayers];
        le.dim = *reinterpret_cast<const uint16_t*>(e + game::wc2r::kEntryDim);
        le.index = *reinterpret_cast<const uint32_t*>(e + game::wc2r::kEntryIndex);
        out.push_back(le);
    }
    return out;
}

bool MapBrowser::BuiltInMode() const {
    return HasTypeDropdown() && b_.mode && *b_.mode == 0;
}

MapBrowser::View MapBrowser::CurrentView() const {
    if (BuiltInMode()) return View::BuiltIn;
    if (map_index::GetState() != map_index::State::Ready) return View::Progress;
    return Searching() ? View::Search : View::Browse;
}

void MapBrowser::SetGameFolder(const std::string& gamePath) {
    StrSet(b_.relPath, gamePath.c_str());

    if (b_.folderCurrent) {
        const size_t slash = gamePath.find_last_of('\\');
        StrSet(b_.folderCurrent,
               (slash == std::string::npos ? gamePath : gamePath.substr(slash + 1)).c_str());
    }
    if (b_.refreshFromFolder) b_.refreshFromFolder(b_.listBegin, b_.folderList, b_.relPath);
}

bool MapBrowser::GameFolderPath(const std::string& folder, std::string* gamePath) const {
    std::wstring wide = map_download::game_map::MapsRoot();
    if (wide.empty()) return false;
    if (!folder.empty()) {
        std::wstring rel;
        if (!map_index::FromGamePath(folder, &rel)) return false;
        wide += L'\\';
        wide += rel;
    }
    const unsigned cp = game::kStdFsCodePage.Get()();
    BOOL lossy = FALSE;
    const bool utf8 = cp == CP_UTF8;
    const DWORD flags = utf8 ? 0 : WC_NO_BEST_FIT_CHARS;
    BOOL* lossyOut = utf8 ? nullptr : &lossy;
    const int n = WideCharToMultiByte(cp, flags, wide.c_str(), static_cast<int>(wide.size()), nullptr,
                                      0, nullptr, lossyOut);
    if (n <= 0 || lossy) return false;
    gamePath->assign(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(cp, flags, wide.c_str(), static_cast<int>(wide.size()), &(*gamePath)[0], n,
                        nullptr, lossyOut);
    return !lossy;
}

void MapBrowser::ReloadMaps() {
    const Logger log{b_.logTag};
    const std::wstring root = map_download::game_map::MapsRoot();
    if (root.empty()) {
        log.Warn("reload: the game has not published its maps root yet -- nothing was reloaded");
        return;
    }
    const DWORD started = GetTickCount();
    map_download::game_map::RegisterDirectory(root);
    map_download::game_map::ReloadMapIndex();
    log.Info("reload: game registry rescanned (%u ms), map index rebuilding",
             static_cast<unsigned>(GetTickCount() - started));
}

void MapBrowser::OnModeChanged() {
    if (!b_.mode) return;
    const KeyList opts = Keys(b_.modeOptions);
    const char* current = StrData(b_.modeCurrent);
    uint32_t index = static_cast<uint32_t>(opts.size());
    for (size_t i = 0; i < opts.size(); ++i) {
        if (opts.begin[i] && strcmp(opts.begin[i], current) == 0) {
            index = static_cast<uint32_t>(i);
            break;
        }
    }
    *b_.mode = index;

    if (*b_.mode == 0) {

        if (b_.folderCurrent) StrSet(b_.folderCurrent, "");
        if (b_.refreshBuiltIn) b_.refreshBuiltIn(b_.listBegin);
    }

}

void MapBrowser::DescendInto(const std::string& child) {
    folder_ = folder_.empty() ? child : folder_ + '\\' + child;
}

void MapBrowser::GoUp() {
    const size_t slash = folder_.find_last_of('\\');
    folder_ = slash == std::string::npos ? std::string() : folder_.substr(0, slash);
}

void MapBrowser::RefreshBrowse() {
    const unsigned gen = map_index::Generation();
    if (gen == browseGen_ && folder_ == browseFolder_) return;
    if (!map_index::FolderContents(folder_, &browseFolders_, &browseMaps_) && !folder_.empty()) {

        folder_.clear();
        map_index::FolderContents(folder_, &browseFolders_, &browseMaps_);
    }
    browseGen_ = gen;
    browseFolder_ = folder_;
}

void MapBrowser::RefreshSearch() {
    const unsigned gen = map_index::Generation();
    if (gen == searchGen_ && searchQuery_ == search_) return;
    searchQuery_ = search_;
    map_index::Search(searchQuery_, &results_);
    searchGen_ = gen;
}

void MapBrowser::DrawIndexProgress(void* ctx, const Rect& r) {
    const map_index::Progress p = map_index::GetProgress();
    float fraction = 0.0f;
    char text[96];
    if (p.state == map_index::State::Idle) {
        _snprintf_s(text, sizeof(text), _TRUNCATE, "Waiting for the game...");
    } else if (p.listing) {
        _snprintf_s(text, sizeof(text), _TRUNCATE, "Finding maps: %u", p.filesFound);
    } else {
        if (p.filesFound > 0) {
            fraction = static_cast<float>(p.filesDone) / static_cast<float>(p.filesFound);
        }
        _snprintf_s(text, sizeof(text), _TRUNCATE, "Indexing maps: %u / %u", p.filesDone,
                    p.filesFound);
    }

    if (BeginList(ctx, r, "select_map_list")) EndList(ctx);

    const float barW = Fl(r.w * kBarWFraction);
    const float barX = r.x + Fl((r.w - barW) * 0.5f);
    const float barY = r.y + Fl(r.h * 0.5f);
    LabelAt(ctx, Rect{r.x, barY - kRowH - kGap, r.w, kRowH}, text, kAlignCentre);
    void* canvas = WindowCanvas(ctx);
    if (!canvas) return;
    FillRect(canvas, barX, barY, barW, kBarH, kBarH * 0.5f, kBarTrack);
    if (fraction > 0.0f) {
        FillRect(canvas, barX, barY, Fl(barW * (std::min)(fraction, 1.0f)), kBarH, kBarH * 0.5f,
                 kBarFill);
    }
}

void MapBrowser::ChoiceFromLive(const LiveEntry& le) {
    choice_ = Choice{};
    choice_.valid = true;
    choice_.builtIn = BuiltInMode();
    choice_.name = le.name;
    choice_.path = le.path;
    choice_.desc = le.desc;
    choice_.players = le.players;
    choice_.dim = le.dim;
    choice_.index = le.index;
}

void MapBrowser::ChoiceFromMap(const map_index::MapInfo& m) {
    choice_ = Choice{};
    choice_.valid = true;
    choice_.name = m.name;
    choice_.path = m.path;
    choice_.desc = m.desc;
    choice_.folder = m.folder;
    choice_.folderKnown = true;
    choice_.players = m.players;
    choice_.dim = m.dim;
}

void MapBrowser::Open() {
    open_ = true;
    search_[0] = '\0';
    choice_ = Choice{};
    if (!b_.selectedEntry) return;
    const uint8_t* s = b_.selectedEntry;
    choice_.valid = StrData(s + game::wc2r::kEntryName)[0] != '\0';
    choice_.builtIn = b_.modeAtSelect && *b_.modeAtSelect == 0;
    choice_.name = GameStr(s + game::wc2r::kEntryName);
    choice_.path = GameStr(s + game::wc2r::kEntryPath);
    choice_.desc = GameStr(s + game::wc2r::kEntryDesc);
    choice_.players = s[game::wc2r::kEntryPlayers];
    choice_.dim = *reinterpret_cast<const uint16_t*>(s + game::wc2r::kEntryDim);
    choice_.index = *reinterpret_cast<const uint32_t*>(s + game::wc2r::kEntryIndex);

}

uint8_t* MapBrowser::ResolveChoiceEntry() {
    if (!choice_.valid) return nullptr;
    if (!choice_.builtIn) {
        map_index::MapInfo m;
        if (!choice_.folderKnown && map_index::FindByPath(choice_.path, &m)) {
            choice_.folder = m.folder;
            choice_.folderKnown = true;
        }
        std::string gamePath;
        if (!choice_.folderKnown || !GameFolderPath(choice_.folder, &gamePath)) {
            Logger{b_.logTag}.Warn("confirm: no game folder path for '%s'", choice_.path.c_str());
            return nullptr;
        }
        if (GameStr(b_.relPath) != gamePath) SetGameFolder(gamePath);
    }

    uint8_t* byName = nullptr;
    int named = 0;
    for (const LiveEntry& le : LiveEntries()) {
        if (choice_.builtIn) {
            if (le.index == choice_.index && choice_.name == le.name) return le.entry;
        } else if (_stricmp(choice_.path.c_str(), le.path) == 0) {
            return le.entry;
        } else if (choice_.name == le.name) {
            byName = le.entry;
            ++named;
        }
    }
    if (named == 1) {
        Logger{b_.logTag}.Warn("confirm: '%s' matched by name only -- the game lists it at another "
                               "path", choice_.path.c_str());
        return byName;
    }
    return nullptr;
}

bool MapBrowser::PreviewBox(Rect* box, const char** path, unsigned short* mapId) const {
    *path = "";
    *mapId = 0;
    if (!open_ || previewBox_.w <= 0.0f || !choice_.valid) return false;
    *box = previewBox_;

    if (choice_.builtIn) {
        *mapId = static_cast<unsigned short>(kBuiltInIdBase + choice_.index);
    } else {
        *path = choice_.path.c_str();
    }
    return true;
}

std::string MapBrowser::DirectoryText() const {
    if (BuiltInMode()) return "";
    if (Searching()) return "Maps/";
    std::string shown = "Maps/";
    for (const char c : folder_) shown.push_back(c == '\\' ? '/' : c);
    if (!folder_.empty()) shown += '/';
    return shown;
}

void MapBrowser::DrawRowIcon(void* ctx, float rowTopLayout, bool folder) {
    void* panel = CurrentPanel(ctx);
    void* canvas = WindowCanvas(ctx);
    if (!panel || !canvas) return;

    const float x = PanelFloat(panel, game::nk::kPanelBoundsX) + kIconInset;
    const float y = rowTopLayout - ScrollOffsetY(panel) + (kRowH - kIconSize) * 0.5f;

    const unsigned int colour = folder ? kFolderColour : kMapColour;

    if (folder) {
        FillRect(canvas, x, y, kIconSize * 0.55f, kIconSize * 0.24f, 2.0f, colour);
        FillRect(canvas, x, y + kIconSize * 0.24f, kIconSize, kIconSize * 0.76f, 2.0f, colour);
    } else {
        FillRect(canvas, x, y, kIconSize, kIconSize, 2.0f, colour);
        FillRect(canvas, x + kIconSize * 0.28f, y + kIconSize * 0.28f, kIconSize * 0.44f,
                  kIconSize * 0.44f, 1.0f, kIconHole);
    }
}

bool MapBrowser::ListRow(void* ctx, const char* label, bool folder) {
    const float rowTop = NextRowTop(ctx);
    const bool clicked = ListRowButton(ctx, label);
    DrawRowIcon(ctx, rowTop, folder);
    return clicked;
}

void MapBrowser::DrawList(void* ctx, const Rect& r) {
    const View view = CurrentView();
    if (view == View::Progress) {
        DrawIndexProgress(ctx, r);
        return;
    }

    if (view == View::Browse) RefreshBrowse();
    if (view == View::Search) RefreshSearch();

    if (!BeginList(ctx, r, "select_map_list")) return;
    const SizeFilter filter = CurrentSizeFilter(b_);

    switch (view) {
        case View::BuiltIn:

            for (const LiveEntry& le : LiveEntries()) {
                if (!PassesSizeFilter(filter, le.dim)) continue;
                if (Searching() && !ContainsNoCase(le.name, search_)) continue;
                if (ListRow(ctx, le.name, false)) ChoiceFromLive(le);
            }
            break;

        case View::Search:

            for (const map_index::MapInfo& m : results_) {
                if (!PassesSizeFilter(filter, m.dim)) continue;
                if (ListRow(ctx, m.name.c_str(), false)) ChoiceFromMap(m);
            }
            break;

        case View::Browse: {

            std::string into;
            for (const std::string& f : browseFolders_) {
                if (ListRow(ctx, f.c_str(), true) && into.empty()) into = f;
            }
            for (const map_index::MapInfo& m : browseMaps_) {
                if (!PassesSizeFilter(filter, m.dim)) continue;
                if (ListRow(ctx, m.name.c_str(), false)) ChoiceFromMap(m);
            }
            if (!into.empty()) DescendInto(into);
            break;
        }

        case View::Progress:
            break;
    }
    EndList(ctx);
}

void MapBrowser::Draw(void* ctx, const Rect& window) {
    previewBox_ = Rect{};

    const float browserW = Fl(window.w * kBrowserWFraction);
    const float browserH = Fl(window.h * kBrowserHFraction);
    const Rect frame{Fl((window.w - browserW) * 0.5f), Fl((window.h - browserH) * 0.5f), browserW,
                     browserH};

    PanelFrame(ctx, frame, "select_map", b_.theme.rightFrameSkin);

    const float x0 = frame.x + kFramePad;
    const float innerRight = frame.x + browserW - kFramePad;
    const float innerW = innerRight - x0;

    HeadingAt(ctx, Rect{x0, frame.y + kFramePad, innerW, kNameH}, "Select Map", kAlignCentre,
               b_.theme);

    const float bodyY = frame.y + kFramePad + kNameH + kGap;
    const float bodyBottom = frame.y + browserH - kFramePad;
    const float leftW = Fl(innerW * kBrowserLeftFraction);
    const float rightX = x0 + leftW + kGap * 2.0f;

    const float rightW = innerRight - rightX;

    if (HasTypeDropdown()) {
        const float half = (leftW - kGap) * 0.5f;
        SetRect(ctx, Rect{x0, bodyY, half, kRowH});
        if (Dropdown("type", b_.modeCurrent, b_.modeOptions, b_.comboOpenMode)) OnModeChanged();
        SetRect(ctx, Rect{x0 + half + kGap, bodyY, half, kRowH});
        Dropdown("mapsize", b_.mapSizeCurrent, b_.mapSizeOptions, b_.comboOpenMapSize);
    } else {
        SetRect(ctx, Rect{x0, bodyY, leftW, kRowH});
        Dropdown("mapsize", b_.mapSizeCurrent, b_.mapSizeOptions, b_.comboOpenMapSize);
    }

    const float searchY = bodyY + kRowH + kGap;
    FilterFieldAt(ctx, Rect{x0, searchY, leftW, kRowH}, search_, sizeof(search_), "Search all maps",
                   b_.theme);

    const float dirY = searchY + kRowH + kGap;
    const float upW = kRowH;

    const float reloadW = kRowH;
    const Rect reloadBox{x0, dirY, reloadW, kRowH};
    const bool reloadGreyed =
        BuiltInMode() || map_index::GetState() == map_index::State::Indexing;
    if (ButtonAt(ctx, reloadBox, "", reloadGreyed)) ReloadMaps();
    DrawReloadIcon(ctx, reloadBox, reloadGreyed);

    const float dirX = x0 + reloadW + kGap;
    const float dirW = leftW - reloadW - upW - kGap * 2.0f;
    const bool dirGreyed = CurrentView() != View::Browse;
    LockedBoxAt(ctx, Rect{dirX, dirY, dirW, kRowH}, DirectoryText().c_str(), "map_browser_path");

    const Rect upBox{x0 + leftW - upW, dirY, upW, kRowH};
    const bool upGreyed = dirGreyed || AtRoot();
    if (ButtonAt(ctx, upBox, "", upGreyed)) GoUp();
    DrawUpArrow(ctx, upBox, upGreyed);

    const float listY = dirY + kRowH + kGap;

    if (map_index::GetState() == map_index::State::Idle && !BuiltInMode()) {
        map_download::game_map::ReloadMapIndex();
    }

    if (choice_.valid && !choice_.builtIn && !choice_.folderLookedUp &&
        map_index::GetState() == map_index::State::Ready) {
        choice_.folderLookedUp = true;
        map_index::MapInfo m;
        if (map_index::FindByPath(choice_.path, &m)) {
            choice_.folder = m.folder;
            choice_.folderKnown = true;
            if (!folderSeeded_) folder_ = m.folder;
        }
        folderSeeded_ = true;
    }
    DrawList(ctx, Rect{x0, listY, leftW, bodyBottom - listY});

    const float buttonsH = kRowH;
    const float buttonsY = bodyBottom - buttonsH;

    const float detailsBottom = buttonsY;

    PanelFrame(ctx, Rect{rightX, bodyY, rightW, detailsBottom - bodyY}, "select_map_details",
                b_.theme.leftFrameSkin);

    const float dx = rightX + kFramePad;
    const float dw = rightW - kFramePad * 2.0f;
    HeadingAt(ctx, Rect{dx, bodyY + kFramePad, dw, kNameH},
               choice_.valid ? choice_.name.c_str() : "", kAlignCentre, b_.theme);

    const float previewY = bodyY + kFramePad + kNameH + kGap;

    const float previewSide = (std::max)(
        0.0f, (std::min)(dw, detailsBottom - previewY - kFramePad - kCaptionH * 5.0f));

    if (choice_.valid) {
        float y = previewY + previewSide + kGap;
        char line[128];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "%u x %u", choice_.dim, choice_.dim);
        LabelAt(ctx, Rect{dx, y, dw, kCaptionH}, line, kAlignLeft);
        y += kCaptionH;

        char players[16];
        _snprintf_s(players, sizeof(players), _TRUNCATE, "%u", choice_.players);
        LabelAt(ctx, Rect{dx, y, dw, kCaptionH},
                 FormatLocalized("customscenario_maxplayers", players), kAlignLeft);
        y += kCaptionH;

        if (!choice_.desc.empty() && choice_.desc != choice_.name && y + kCaptionH < detailsBottom) {
            WrappedLabelAt(ctx, Rect{dx, y, dw, detailsBottom - y - kFramePad},
                            choice_.desc.c_str());
        }
    }

    const bool confirmable = choice_.valid && choice_.builtIn == BuiltInMode();
    const float cancelW = Fl(rightW * 0.5f);
    if (ButtonAt(ctx, Rect{rightX, buttonsY, cancelW, buttonsH}, "Cancel")) Close();
    if (ButtonAt(ctx, Rect{rightX + cancelW, buttonsY, rightW - cancelW, buttonsH}, "Confirm",
                  !confirmable)) {
        uint8_t* entry = ResolveChoiceEntry();
        if (entry && b_.publish) b_.publish(entry);
        Close();
    }

    if (previewSide > 0.0f) {
        previewBox_ = Rect{dx + (dw - previewSide) * 0.5f, previewY, previewSide, previewSide};
    }
}

}  // namespace ui
