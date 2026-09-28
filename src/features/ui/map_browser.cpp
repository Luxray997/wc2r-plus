// SPDX-License-Identifier: MIT
#include "features/ui/map_browser.h"
#include "target/struct_offsets.h"

#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "features/map_download/game_map.h"
#include "features/map_download/map_index.h"
#include "features/map_preview.h"

namespace ui {
namespace {

constexpr float kBrowserWFraction = 0.80f;
constexpr float kBrowserHFraction = 0.88f;
constexpr float kBrowserLeftFraction = 0.56f;

constexpr int kMaxIndexDepth = 8;
constexpr size_t kMaxIndexedMaps = 4000;

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

std::string LastComponent(const std::string& path) {
    const size_t slash = path.find_last_of('\\');
    return slash == std::string::npos ? path : path.substr(slash + 1);
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

void MapBrowser::RefreshFromCurrentFolder() {
    if (b_.refreshFromFolder) b_.refreshFromFolder(b_.listBegin, b_.folderList, b_.relPath);
}

void MapBrowser::ObserveMapsRoot() {
    const std::string rel = GameStr(b_.relPath);
    if (rel.empty()) return;
    if (!mapsRootKnown_ || rel.size() < mapsRoot_.size()) {
        mapsRoot_ = rel;
        mapsRootKnown_ = true;
    }
}

bool MapBrowser::AtRoot() const {
    if (!mapsRootKnown_) return true;
    return GameStr(b_.relPath).size() <= mapsRoot_.size();
}

void MapBrowser::InvalidateIndex() {
    index_.clear();
    indexBuilt_ = false;
}

void MapBrowser::ReloadMaps() {
    const Logger log{b_.logTag};
    if (!mapsRootKnown_) {

        log.Warn("reload: the maps root has not been seen yet -- nothing was reloaded");
        return;
    }

    const DWORD started = GetTickCount();
    std::wstring root;
    if (!map_download::FromGamePath(mapsRoot_, &root)) {

        log.Warn("reload: '%s' does not convert to a wide path -- the registry was NOT rescanned",
                  mapsRoot_.c_str());
    } else {
        map_download::game_map::RegisterDirectory(root);
    }

    RefreshFromCurrentFolder();
    InvalidateIndex();
    skipCacheOnce_ = true;
    log.Info("reload: registry rescanned from '%s', folder refreshed, search index dropped (%u ms)",
              mapsRoot_.c_str(), static_cast<unsigned>(GetTickCount() - started));
}

void MapBrowser::SetFolder(const std::string& rel) {
    StrSet(b_.relPath, rel.c_str());

    if (b_.folderCurrent) StrSet(b_.folderCurrent, LastComponent(rel).c_str());
    RefreshFromCurrentFolder();
}

void MapBrowser::DescendInto(const std::string& folder) {
    std::string rel = GameStr(b_.relPath);
    rel += '\\';
    rel += folder;
    SetFolder(rel);
}

void MapBrowser::GoUp() {
    if (AtRoot()) return;
    const std::string rel = GameStr(b_.relPath);
    const size_t slash = rel.find_last_of('\\');
    if (slash == std::string::npos) return;
    const std::string up = rel.substr(0, slash);
    SetFolder(up.size() < mapsRoot_.size() ? mapsRoot_ : up);
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
    InvalidateIndex();

    if (*b_.mode == 0) {

        if (b_.folderCurrent) StrSet(b_.folderCurrent, "");
        if (b_.refreshBuiltIn) b_.refreshBuiltIn(b_.listBegin);
        return;
    }
    if (b_.folderCurrent) StrSet(b_.folderCurrent, LastComponent(GameStr(b_.relPath)).c_str());
    RefreshFromCurrentFolder();
}

void MapBrowser::IndexDir(const std::string& rel, int depth) {
    if (depth > kMaxIndexDepth || index_.size() >= kMaxIndexedMaps) return;

    walkedDirs_.push_back(rel);
    StrSet(b_.relPath, rel.c_str());
    RefreshFromCurrentFolder();

    for (const LiveEntry& le : LiveEntries()) {
        if (index_.size() >= kMaxIndexedMaps) break;
        IndexedMap m;
        m.name = le.name;
        m.path = le.path;
        m.desc = le.desc;
        m.relDir = rel;
        m.players = le.players;
        m.dim = le.dim;
        index_.push_back(m);
    }

    const std::vector<std::string> children = FolderNames(b_.folderList, true);
    for (const std::string& child : children) {
        IndexDir(rel + '\\' + child, depth + 1);
    }
}

void MapBrowser::BuildSearchIndex() {
    if (indexBuilt_ || !mapsRootKnown_) return;

    if (!skipCacheOnce_ && map_search_cache::Load(mapsRoot_, &index_, b_.logTag)) {
        indexBuilt_ = true;
        return;
    }
    skipCacheOnce_ = false;

    const std::string here = GameStr(b_.relPath);
    const std::string root = mapsRoot_;
    const DWORD started = GetTickCount();

    index_.clear();
    walkedDirs_.clear();
    IndexDir(root, 0);
    indexBuilt_ = true;

    SetFolder(here);

    Logger{b_.logTag}.Info("search index built -- %u maps under '%s' in %u ms%s",
                           (unsigned)index_.size(), root.c_str(),
                           (unsigned)(GetTickCount() - started),
                           index_.size() >= kMaxIndexedMaps ? " (CAPPED)" : "");

    map_search_cache::Save(root, index_, walkedDirs_, b_.logTag);
}

void MapBrowser::ChoiceFromLive(const LiveEntry& le) {
    choice_.valid = true;
    choice_.builtIn = BuiltInMode();
    choice_.name = le.name;
    choice_.path = le.path;
    choice_.desc = le.desc;
    choice_.relDir = GameStr(b_.relPath);
    choice_.players = le.players;
    choice_.dim = le.dim;
    choice_.index = le.index;
}

void MapBrowser::ChoiceFromIndex(const IndexedMap& m) {
    choice_.valid = true;
    choice_.builtIn = false;
    choice_.name = m.name;
    choice_.path = m.path;
    choice_.desc = m.desc;
    choice_.relDir = m.relDir;
    choice_.players = m.players;
    choice_.dim = m.dim;
    choice_.index = 0;
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
    choice_.relDir = GameStr(b_.relPath);
    choice_.players = s[game::wc2r::kEntryPlayers];
    choice_.dim = *reinterpret_cast<const uint16_t*>(s + game::wc2r::kEntryDim);
    choice_.index = *reinterpret_cast<const uint32_t*>(s + game::wc2r::kEntryIndex);
}

uint8_t* MapBrowser::ResolveChoiceEntry() {
    if (!choice_.valid) return nullptr;
    if (!choice_.builtIn && GameStr(b_.relPath) != choice_.relDir) {
        SetFolder(choice_.relDir);
    }
    for (const LiveEntry& le : LiveEntries()) {
        if (choice_.builtIn) {
            if (le.index == choice_.index && choice_.name == le.name) return le.entry;
        } else if (choice_.name == le.name && choice_.path == le.path) {
            return le.entry;
        }
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

std::string MapBrowser::DirectoryText() {
    if (BuiltInMode()) return "";
    const std::string rel = GameStr(b_.relPath);

    if (Searching()) return "Maps/";

    std::string shown = "Maps/";
    if (mapsRootKnown_ && rel.size() > mapsRoot_.size()) {
        std::string below = rel.substr(mapsRoot_.size());
        for (size_t i = 0; i < below.size(); ++i) {
            if (below[i] == '\\') below[i] = '/';
        }
        if (!below.empty() && below[0] == '/') below.erase(0, 1);
        shown += below;
        if (!shown.empty() && shown[shown.size() - 1] != '/') shown += '/';
    }
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
    if (!BeginList(ctx, r, "select_map_list")) return;

    const SizeFilter filter = CurrentSizeFilter(b_);

    if (Searching()) {

        if (BuiltInMode()) {

            for (const LiveEntry& le : LiveEntries()) {
                if (!PassesSizeFilter(filter, le.dim)) continue;
                if (!ContainsNoCase(le.name, search_)) continue;
                if (ListRow(ctx, le.name, false)) ChoiceFromLive(le);
            }
        } else {

            for (const IndexedMap& m : index_) {
                if (!PassesSizeFilter(filter, m.dim)) continue;
                if (!ContainsNoCase(m.name.c_str(), search_)) continue;
                if (ListRow(ctx, m.name.c_str(), false)) ChoiceFromIndex(m);
            }
        }
        EndList(ctx);
        return;
    }

    if (!BuiltInMode()) {
        const std::vector<std::string> folders = FolderNames(b_.folderList, true);
        for (size_t i = 0; i < folders.size(); ++i) {
            if (ListRow(ctx, folders[i].c_str(), true)) {
                DescendInto(folders[i]);

                EndList(ctx);
                return;
            }
        }
    }

    for (const LiveEntry& le : LiveEntries()) {
        if (!PassesSizeFilter(filter, le.dim)) continue;
        if (ListRow(ctx, le.name, false)) ChoiceFromLive(le);
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
    const bool reloadGreyed = BuiltInMode();
    if (ButtonAt(ctx, reloadBox, "", reloadGreyed)) ReloadMaps();
    DrawReloadIcon(ctx, reloadBox, reloadGreyed);

    const float dirX = x0 + reloadW + kGap;
    const float dirW = leftW - reloadW - upW - kGap * 2.0f;
    const bool dirGreyed = BuiltInMode() || Searching();
    LockedBoxAt(ctx, Rect{dirX, dirY, dirW, kRowH}, DirectoryText().c_str(), "map_browser_path");

    const Rect upBox{x0 + leftW - upW, dirY, upW, kRowH};
    const bool upGreyed = dirGreyed || AtRoot();
    if (ButtonAt(ctx, upBox, "", upGreyed)) GoUp();
    DrawUpArrow(ctx, upBox, upGreyed);

    const float listY = dirY + kRowH + kGap;

    if (Searching() && !BuiltInMode()) BuildSearchIndex();
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
