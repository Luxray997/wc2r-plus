// SPDX-License-Identifier: MIT
#include "features/ui/custom_scenario_screen.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

#include "MinHook.h"
#include "core/hook_utils.h"
#include "core/log.h"
#include "core/mod.h"
#include "core/settings/mod_settings.h"
#include "features/map_preview.h"
#include "features/ui/map_browser.h"
#include "features/ui/map_panel.h"
#include "features/ui/screen_kit.h"
#include "target/addresses.h"
#include "target/struct_offsets.h"

namespace {

using namespace game;
using namespace ui;

const Logger kLog{"customscenario"};

constexpr const char* kKeyEnabled = "enabled";

constexpr bool kDefaultEnabled = true;

ModSettings g_settings{"customscenario"};

constexpr float kMapPanelFraction = 0.47f;
constexpr float kPanelGutterFraction = 0.03f;

bool g_ready = false;

CustomScenarioBuildFn g_realBuild = nullptr;

void** g_ppNkContext = nullptr;
LayoutSpaceBeginFn g_spaceBegin = nullptr;
LayoutSpaceEndFn g_spaceEnd = nullptr;

LobbyStartButtonFn g_startButton = nullptr;
DestroyMapListEntryFn g_destroyEntry = nullptr;
PreviousGameStateFn g_previousState = nullptr;
TransitionGameStateFn g_transitionState = nullptr;

CopyMapListEntryFn g_copyEntry = nullptr;
RefreshBuiltInMapListFn g_refreshBuiltIn = nullptr;
RefreshMapListFromFolderFn g_refreshFromFolder = nullptr;
SelectBuiltInScenarioFn g_selectBuiltIn = nullptr;
ValidateMapFileFn g_validateMap = nullptr;

ScreenTheme g_theme;

uint8_t** g_mapListBegin = nullptr;
uint8_t** g_mapListEnd = nullptr;
uint32_t* g_mode = nullptr;
uint32_t* g_modeAtSelect = nullptr;
uint8_t* g_selected = nullptr;
uint8_t* g_pendingSetup = nullptr;
char* g_selectedMapPath = nullptr;
unsigned short* g_selectedMapId = nullptr;
uint8_t* g_leavingFlag = nullptr;

void* g_tilesetCurrent = nullptr;

struct DropdownBinding {
    const char* captionKey;
    const char* comboId;
    void* current;
    void* options;
    uint8_t* open;
};
DropdownBinding g_optionDropdowns[5];

MapBrowser g_browser;

bool PublishChoice(uint8_t* entry) {
    if (!entry || !g_copyEntry || !g_selected || !g_mode || !g_modeAtSelect) return false;

    g_copyEntry(g_selected, entry);
    *g_modeAtSelect = *g_mode;

    if (*g_mode == 0) {
        if (g_selectBuiltIn) g_selectBuiltIn();
        return true;
    }

    const char* path = StrData(g_selected + wc2r::kEntryPath);
    if (!g_validateMap || !g_validateMap(path)) return false;

    if (g_pendingSetup) memset(g_pendingSetup, 0, 0xec);
    if (g_selectedMapId) *g_selectedMapId = 0;

    if (g_selectedMapPath) strncpy_s(g_selectedMapPath, 0x104, path, _TRUNCATE);
    if (g_pendingSetup) {
        g_pendingSetup[0] = 1;
        strncpy_s(reinterpret_cast<char*>(g_pendingSetup + 0x10), 0xdc,
                   StrData(g_selected + wc2r::kEntryName), _TRUNCATE);
    }
    return true;
}

void RefreshFromFolderThunk(void* mapList, void* folderList, const void* relativePath) {
    if (g_refreshFromFolder) g_refreshFromFolder(mapList, folderList, relativePath);
}
void RefreshBuiltInThunk(void* mapList) {
    if (g_refreshBuiltIn) g_refreshBuiltIn(mapList);
}

int SelectedTilesetOverride() {
    static const struct {
        const char* key;
        int index;
    } kTilesetKeys[] = {
        {"customscenario_tileset_forest", 0},
        {"customscenario_tileset_winter", 1},
        {"customscenario_tileset_wasteland", 2},
        {"customscenario_tileset_orcswamp", 3},
    };
    const char* current = StrData(g_tilesetCurrent);
    for (const auto& k : kTilesetKeys) {
        if (strcmp(current, k.key) == 0) return k.index;
    }
    return -1;
}

void CancelToPreviousScreen() {
    if (g_leavingFlag) *g_leavingFlag = 0;
    if (g_mapListBegin && g_mapListEnd && g_destroyEntry) {
        uint8_t* begin = *g_mapListBegin;
        uint8_t* end = *g_mapListEnd;
        if (begin && end) {
            for (uint8_t* e = begin; e + wc2r::kEntryStride <= end; e += wc2r::kEntryStride) g_destroyEntry(e);
            *g_mapListEnd = begin;
        }
    }
    if (g_previousState && g_transitionState) g_transitionState(g_previousState());
}

void DrawOptionsColumn(void* ctx, const Rect& column) {
    const float colW = (column.w - kGap) * 0.5f;

    const float cellH = column.h / 3.0f;

    for (int i = 0; i < 5; ++i) {
        const DropdownBinding& d = g_optionDropdowns[i];
        const float x = column.x + (i % 2) * (colW + kGap);
        const float y = column.y + (i / 2) * cellH;

        LabelAt(ctx, Rect{x, y, colW, kCaptionH}, Localized(d.captionKey), kAlignCaption);
        SetRect(ctx, Rect{x, y + kCaptionH, colW, kRowH});
        Dropdown(d.comboId, d.current, d.options, d.open);
    }
}

void DrawFooterButtons(void* ctx, const Rect& window, float footerY, unsigned char* leave) {

    const float buttonW = FooterButtonWidth(window);
    const float x = Fl((window.w - buttonW * 2.0f) * 0.5f);

    if (ButtonAt(ctx, Rect{x, footerY, buttonW, kFooterH}, "Cancel")) {
        CancelToPreviousScreen();
        return;
    }

    struct StartClosure {
        void* unused;
        unsigned char* exitFlag;
    } closure{nullptr, leave};
    SetRect(ctx, Rect{x + buttonW, footerY, buttonW, kFooterH});
    g_startButton(&closure);
}

char DrawScreen(void* ctx, const Rect& window) {
    const unsigned int prevSkin = PushSkin(*g_theme.screenSkin);
    g_spaceBegin(ctx, 1, 0.0f, 1000);

    unsigned char leave = 0;
    Rect lobbyPreview{};

    if (g_browser.IsOpen()) {

        g_browser.Draw(ctx, window);
    } else {
        DrawBanner();
        Label(ctx, Localized("customscenario_title"), kAlignCentre);

        const Rect frame = LobbyFrame(window);

        PanelFrame(ctx, frame, "lobby_panel", g_theme.leftFrameSkin);

        const float contentX = frame.x + kFramePad;
        const float contentRight = frame.x + frame.w - kFramePad;
        const float contentW = contentRight - contentX;
        const float contentY = frame.y + kFramePad;
        const float footerY = window.h - kFooterBottomGap;
        const float contentH = (footerY - kGap) - contentY;

        const float mapW = Fl(contentW * kMapPanelFraction);
        const float gutter = Fl(contentW * kPanelGutterFraction);
        const float rightX = contentX + mapW + gutter;
        const float rightW = contentRight - rightX;
        const float optionsH = Fl(kCaptionH + kRowH + kGap) * 3.0f;

        const Rect mapColumn{contentX, contentY, mapW, contentH};
        map_panel::DrawColumn(ctx, mapColumn, g_selected, g_theme, &g_browser);
        DrawOptionsColumn(ctx, Rect{rightX, contentY, rightW, optionsH});
        map_panel::DrawDetails(ctx, Rect{rightX, contentY + optionsH + kGap, rightW,
                                         (contentY + contentH) - (contentY + optionsH + kGap)},
                               g_selected);

        DrawFooterButtons(ctx, window, footerY, &leave);
        lobbyPreview = map_panel::PreviewBox(mapColumn);
    }

    g_spaceEnd(ctx);
    PopSkin(prevSkin);

    if (g_browser.IsOpen()) {
        Rect box;
        const char* path = "";
        unsigned short id = 0;

        if (g_browser.PreviewBox(&box, &path, &id)) {
            DrawMapPreviewAt(box.x, box.y, box.w, path, id, -1);
        }
    } else if (lobbyPreview.w > 0.0f) {
        DrawMapPreviewAt(lobbyPreview.x, lobbyPreview.y, lobbyPreview.w,
                          g_selectedMapPath ? g_selectedMapPath : "",
                          g_selectedMapId ? *g_selectedMapId : 0, SelectedTilesetOverride());
    }
    return leave ? 1 : 0;
}

char __cdecl HookedCustomScenarioBuild() {

    g_settings.ReloadIfChanged();

    void* ctx = g_ppNkContext ? *g_ppNkContext : nullptr;
    Rect window;
    const bool haveWindow = ctx && WindowRect(ctx, &window);

    const bool takeOver = g_ready && g_settings.GetBool(kKeyEnabled) && haveWindow;

    if (!takeOver) {

        if (g_browser.IsOpen()) g_browser.Close();
        return g_realBuild();
    }

    constexpr DWORD kReentryGapMs = 250;
    static DWORD lastFrameEnd = 0;
    if (lastFrameEnd != 0 && GetTickCount() - lastFrameEnd > kReentryGapMs && g_browser.IsOpen()) {
        g_browser.Close();
    }

    static bool announced = false;
    if (!announced) {
        announced = true;
        kLog.Info("drawing our screen (window %.0fx%.0f)", window.w, window.h);
    }

    const char result = DrawScreen(ctx, window);
    lastFrameEnd = GetTickCount();
    return result;
}

void RegisterScreenSettings() {
    g_settings.BeginGroup("UI", "Enhanced UIs");

    g_settings.RegisterBool(kKeyEnabled, kDefaultEnabled, "Enhanced Custom Scenario UI",
                             "The Custom Scenario screen has a custom map selector, map preview, "
                             "and restructured UI.");

}

class CustomScenarioScreenMod : public IMod {
public:
    const char* Name() const override { return "customscenario"; }

    bool Install() override {
        RegisterScreenSettings();
        g_settings.ReloadIfChanged();

        const bool kitReady = ui::Resolve();

        g_ppNkContext = kNkContextMenus.Get();
        g_spaceBegin = kLayoutSpaceBegin.Get();
        g_spaceEnd = kLayoutSpaceEnd.Get();

        g_startButton = kLobbyStartButton.Get();
        g_destroyEntry = kDestroyMapListEntry.Get();
        g_previousState = kPreviousGameState.Get();
        g_transitionState = kTransitionGameState.Get();
        g_leavingFlag = kLobbyLeavingFlag.Get();

        g_copyEntry = kCopyMapListEntry.Get();
        g_refreshBuiltIn = kRefreshBuiltInMapList.Get();
        g_refreshFromFolder = kRefreshMapListFromFolder.Get();
        g_selectBuiltIn = kSelectBuiltInScenario.Get();
        g_validateMap = kValidateMapFile.Get();

        g_theme.screenSkin = kSkinCustomScenario.Get();
        g_theme.leftFrameSkin = kSkinLobbyLeftFrame.Get();
        g_theme.rightFrameSkin = kSkinLobbyRightFrame.Get();
        g_theme.textEditSkin = kSkinTextEdit.Get();
        g_theme.headingFont = kFontLobbyHeading.Get();

        g_mapListBegin = kMapListBegin.Get();
        g_mapListEnd = kMapListEnd.Get();
        g_mode = kMapListMode.Get();
        g_modeAtSelect = kMapListModeAtSelect.Get();
        g_selected = kSelectedEntry.Get();
        g_pendingSetup = kPendingGameSetup.Get();
        g_selectedMapPath = kSelectedMapPath.Get();
        g_selectedMapId = kSelectedMapId.Get();

        g_tilesetCurrent = kTilesetCurrent.Get();

        g_optionDropdowns[0] = {"customscenario_race", "race", kRaceCurrent.Get(),
                                 kRaceOptions.Get(), kComboOpenRace.Get()};
        g_optionDropdowns[1] = {"customscenario_opponents", "opponents",
                                 kOpponentsCurrent.Get(), kOpponentsOptions.Get(),
                                 kComboOpenOpponents.Get()};
        g_optionDropdowns[2] = {"customscenario_tileset", "tileset", kTilesetCurrent.Get(),
                                 kTilesetOptions.Get(), kComboOpenTileset.Get()};
        g_optionDropdowns[3] = {"customscenario_resources", "resources",
                                 kResourcesCurrent.Get(), kResourcesOptions.Get(),
                                 kComboOpenResources.Get()};
        g_optionDropdowns[4] = {"customscenario_units", "units", kUnitsCurrent.Get(),
                                 kUnitsOptions.Get(), kComboOpenUnits.Get()};

        MapListBinding b;
        b.listBegin = g_mapListBegin;
        b.listEnd = g_mapListEnd;
        b.folderList = kFolderList.Get();
        b.relPath = kMapFolderRelPath.Get();
        b.folderCurrent = kMapFolderCurrent.Get();
        b.selectedEntry = g_selected;
        b.mapSizeCurrent = kMapSizeCurrent.Get();
        b.mapSizeOptions = kMapSizeOptions.Get();
        b.comboOpenMapSize = kComboOpenMapSize.Get();
        b.modeCurrent = kModeCurrent.Get();
        b.modeOptions = kModeOptions.Get();
        b.comboOpenMode = kComboOpenMode.Get();
        b.mode = g_mode;
        b.modeAtSelect = g_modeAtSelect;
        b.refreshFromFolder = &RefreshFromFolderThunk;
        b.refreshBuiltIn = &RefreshBuiltInThunk;
        b.publish = &PublishChoice;
        b.theme = g_theme;
        b.logTag = "customscenario";
        g_browser.Bind(b);

        g_ready = kitReady && g_ppNkContext && g_spaceBegin && g_spaceEnd && g_startButton &&
                   g_destroyEntry && g_previousState && g_transitionState && g_leavingFlag &&
                   g_copyEntry && g_refreshBuiltIn && g_refreshFromFolder && g_selectBuiltIn &&
                   g_validateMap && g_theme.screenSkin && g_theme.leftFrameSkin &&
                   g_theme.rightFrameSkin && g_theme.headingFont && g_mapListBegin &&
                   g_mapListEnd && g_mode && g_modeAtSelect && g_selected;

        const bool hooked = InstallHook(kCustomScenarioBuild.Target(),
                                         reinterpret_cast<void*>(&HookedCustomScenarioBuild),
                                         reinterpret_cast<void**>(&g_realBuild),
                                         "CustomScenarioBuild (screen + map preview)");
        kLog.Info("%s, replacement screen %s",
                  g_ready ? "ready" : "NOT ready (an address failed to resolve)",
                  g_settings.GetBool(kKeyEnabled) ? "ON" : "off");
        return hooked;
    }
};

}  // namespace

MOD_REGISTER(CustomScenarioScreenMod)
