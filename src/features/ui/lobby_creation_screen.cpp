// SPDX-License-Identifier: MIT
#include "features/ui/lobby_creation_screen.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

#include "MinHook.h"
#include "core/hook_utils.h"
#include "core/mod.h"
#include "core/log.h"
#include "core/settings/mod_settings.h"
#include "features/lobby_policy.h"
#include "features/map_preview.h"
#include "features/ui/map_browser.h"
#include "features/ui/map_panel.h"
#include "features/ui/screen_kit.h"
#include "target/addresses.h"
#include "target/struct_offsets.h"

namespace {

using namespace game;
using namespace ui;

const Logger kLog{"lobbycreation"};

constexpr const char* kKeyEnabled = "enabled";

constexpr bool kDefaultEnabled = true;

ModSettings g_settings{"lobbycreation"};

constexpr float kMapPanelFraction = 0.47f;
constexpr float kPanelGutterFraction = 0.03f;

bool g_ready = false;

LobbyCreationContentFn g_realContent = nullptr;

void** g_ppNkContext = nullptr;
LayoutSpaceBeginFn g_spaceBegin = nullptr;
LayoutSpaceEndFn g_spaceEnd = nullptr;

SliderU32Fn g_slider = nullptr;
LobbyCreateButtonFn g_createButton = nullptr;
PreviousGameStateFn g_previousState = nullptr;
TransitionGameStateFn g_transitionState = nullptr;

CopyMapListEntryFn g_copyEntry = nullptr;
RefreshMapListFromFolderFn g_refreshFromFolder = nullptr;
ValidateMapFileFn g_validateMap = nullptr;

ScreenTheme g_theme;

uint8_t* g_selected = nullptr;
uint8_t* g_pendingSetup = nullptr;
uint8_t* g_readyFlag = nullptr;
void* g_relPath = nullptr;

uint32_t* g_gameSpeed = nullptr;
char** g_nameBuffer = nullptr;
uint32_t* g_nameCapacity = nullptr;
char* g_password = nullptr;

struct DropdownBinding {
    const char* captionKey;
    const char* comboId;
    void* current;
    void* options;
    uint8_t* open;
};
DropdownBinding g_optionDropdowns[4];

MapBrowser g_browser;

bool PublishChoice(uint8_t* entry) {
    if (!entry || !g_copyEntry || !g_selected) return false;

    g_copyEntry(g_selected, entry);

    if (g_validateMap && g_pendingSetup) {
        const char* path = StrData(g_selected + wc2r::kEntryPath);
        if (g_validateMap(path)) {
            char* dst = reinterpret_cast<char*>(g_pendingSetup + wc2r::kPendingSetupName);
            strncpy_s(dst, wc2r::kPendingSetupNameCapacity, StrData(g_selected + wc2r::kEntryName), _TRUNCATE);
        }
    }
    return true;
}

void RefreshFromFolderThunk(void* mapList, void* folderList, const void* relativePath) {
    if (g_refreshFromFolder) g_refreshFromFolder(mapList, folderList, relativePath);
}

int SelectedTilesetOverride() {
    return TilesetIndexForKey(StrData(g_optionDropdowns[2].current));
}

void CancelToPreviousScreen() {
    if (g_readyFlag) *g_readyFlag = 0;
    if (g_previousState && g_transitionState) g_transitionState(g_previousState());
}

void DrawOptionsColumn(void* ctx, const Rect& column) {
    const float colW = (column.w - kGap) * 0.5f;
    const float cellH = column.h / 4.0f;
    const float rightX = column.x + colW + kGap;

    LabelAt(ctx, Rect{column.x, column.y, colW, kCaptionH}, Localized("mp_lobby_name"),
             kAlignCaption);
    if (g_nameBuffer && *g_nameBuffer && g_nameCapacity) {

        const int maxLen = static_cast<int>(*g_nameCapacity & 0x7fffffffu);
        TextEditAt(ctx, Rect{column.x, column.y + kCaptionH, colW, kRowH}, *g_nameBuffer, maxLen,
                    g_theme);
    }

    LabelAt(ctx, Rect{rightX, column.y, colW, kCaptionH}, "Password (Optional)", kAlignCaption);
    if (g_password) {
        TextEditAt(ctx, Rect{rightX, column.y + kCaptionH, colW, kRowH}, g_password,
                    kLobbyPasswordCapacity, g_theme);
    }

    for (int i = 0; i < 4; ++i) {
        const DropdownBinding& d = g_optionDropdowns[i];
        const float x = column.x + (i % 2) * (colW + kGap);
        const float y = column.y + (1 + i / 2) * cellH;
        LabelAt(ctx, Rect{x, y, colW, kCaptionH}, Localized(d.captionKey), kAlignCaption);
        SetRect(ctx, Rect{x, y + kCaptionH, colW, kRowH});
        Dropdown(d.comboId, d.current, d.options, d.open);
    }

    const float row4 = column.y + 3.0f * cellH;
    LabelAt(ctx, Rect{column.x, row4, colW, kCaptionH}, Localized("option_speed_game_speed"),
             kAlignCaption);
    if (g_slider && g_gameSpeed) {

        uint32_t lo = 0, hi = 8, step = 1;
        SetRect(ctx, Rect{column.x, row4 + kCaptionH, colW, kRowH});
        g_slider(g_gameSpeed, &lo, &hi, &step);
    }

    lobby_policy::Locks locks = lobby_policy::CreateChoice();
    const float lockW = (colW - kGap) * 0.5f;
    const float slotsX = rightX + lockW + kGap;
    LabelAt(ctx, Rect{rightX, row4, lockW, kCaptionH}, "Teams", kAlignCaption);
    LabelAt(ctx, Rect{slotsX, row4, lockW, kCaptionH}, "Slots", kAlignCaption);
    const bool flipTeams = ButtonAt(ctx, Rect{rightX, row4 + kCaptionH, lockW, kRowH},
                                    locks.teams ? "Locked" : "Unlocked");
    const bool flipSlots = ButtonAt(ctx, Rect{slotsX, row4 + kCaptionH, lockW, kRowH},
                                    locks.slots ? "Locked" : "Unlocked");
    if (flipTeams || flipSlots) {
        if (flipTeams) locks.teams = !locks.teams;
        if (flipSlots) locks.slots = !locks.slots;
        lobby_policy::SetCreateChoice(locks);
    }
}

void DrawFooterButtons(void* ctx, const Rect& window, float footerY, unsigned char* leave) {
    const float buttonW = FooterButtonWidth(window);
    const float x = Fl((window.w - buttonW * 2.0f) * 0.5f);

    if (ButtonAt(ctx, Rect{x, footerY, buttonW, kFooterH}, "Cancel")) {
        CancelToPreviousScreen();
        return;
    }

    struct CreateClosure {
        unsigned char* exitFlag;
    };
    CreateClosure closure{leave};
    SetRect(ctx, Rect{x + buttonW, footerY, buttonW, kFooterH});
    g_createButton(&closure);
}

void DrawScreen(void* ctx, const Rect& window) {
    const unsigned int prevSkin = PushSkin(*g_theme.screenSkin);
    g_spaceBegin(ctx, 1, 0.0f, 1000);

    unsigned char leave = 0;
    Rect lobbyPreview{};

    if (g_browser.IsOpen()) {

        g_browser.Draw(ctx, window);
    } else {
        DrawBanner();
        Label(ctx, Localized("mp_create_game"), kAlignCentre);

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
        const float optionsH = Fl(kCaptionH + kRowH + kGap) * 4.0f;

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
        const char* path = g_selected ? StrData(g_selected + wc2r::kEntryPath) : "";
        if (path[0]) {
            DrawMapPreviewAt(lobbyPreview.x, lobbyPreview.y, lobbyPreview.w, path, 0,
                              SelectedTilesetOverride());
        }
    }
}

char __cdecl HookedLobbyCreationContent() {

    g_settings.ReloadIfChanged();

    void* ctx = g_ppNkContext ? *g_ppNkContext : nullptr;
    Rect window;
    const bool haveWindow = ctx && WindowRect(ctx, &window);

    const bool takeOver = g_ready && g_settings.GetBool(kKeyEnabled) && haveWindow;

    if (!takeOver) {

        if (g_browser.IsOpen()) g_browser.Close();
        return g_realContent();
    }

    constexpr DWORD kReentryGapMs = 250;
    static DWORD lastFrameEnd = 0;
    if (lastFrameEnd != 0 && GetTickCount() - lastFrameEnd > kReentryGapMs && g_browser.IsOpen()) {
        g_browser.Close();
    }

    g_browser.ObserveMapsRoot();

    static bool announced = false;
    if (!announced) {
        announced = true;
        kLog.Info("drawing our screen (window %.0fx%.0f)", window.w, window.h);
    }

    DrawScreen(ctx, window);
    lastFrameEnd = GetTickCount();

    return 0;
}

void RegisterScreenSettings() {
    g_settings.BeginGroup("UI", "Enhanced UIs");

    g_settings.RegisterBool(kKeyEnabled, kDefaultEnabled, "Enhanced Create Multiplayer Game UI",
                             "The Create Multiplayer Game screen has a custom map selector, map "
                             "preview, and restructured UI.");
}

class LobbyCreationScreenMod : public IMod {
public:
    const char* Name() const override { return "lobbycreation"; }

    bool Install() override {
        RegisterScreenSettings();
        g_settings.ReloadIfChanged();

        const uintptr_t base = Base();

        const bool kitReady = ui::Resolve();

        g_ppNkContext = kNkContextMenus.Get();
        g_spaceBegin = kLayoutSpaceBegin.Get();
        g_spaceEnd = kLayoutSpaceEnd.Get();

        g_slider = kSliderU32.Get();
        g_createButton = kLobbyCreateButton.Get();
        g_previousState = kPreviousGameState.Get();
        g_transitionState = kTransitionGameState.Get();

        g_copyEntry = kCopyMapListEntry.Get();
        g_refreshFromFolder = kRefreshMapListFromFolder.Get();
        g_validateMap = kValidateMapFile.Get();

        g_theme.screenSkin = kSkinLobbyCreation.Get();
        g_theme.leftFrameSkin = kSkinLobbyCreationLeft.Get();
        g_theme.rightFrameSkin = kSkinLobbyCreationRight.Get();
        g_theme.textEditSkin = kSkinTextEdit.Get();
        g_theme.headingFont = kFontLobbyCreationHeading.Get();

        g_selected = kLobbyCreationSelected.Get();
        g_pendingSetup = kPendingGameSetup.Get();
        g_readyFlag = kLobbyCreationReady.Get();
        g_relPath = kLobbyFolderRelPath.Get();

        g_gameSpeed = kLobbyGameSpeed.Get();
        g_nameBuffer = kLobbyNameBuffer.Get();
        g_nameCapacity = kLobbyNameCapacity.Get();
        g_password = kLobbyPassword.Get();

        g_optionDropdowns[0] = {"customscenario_resources", "resources",
                                 kLobbyResourcesCurrent.Get(),
                                 kLobbyResourcesOptions.Get(),
                                 kLobbyComboOpenResources.Get()};
        g_optionDropdowns[1] = {"customscenario_units", "units", kLobbyUnitsCurrent.Get(),
                                 kLobbyUnitsOptions.Get(), kLobbyComboOpenUnits.Get()};
        g_optionDropdowns[2] = {"customscenario_tileset", "tileset",
                                 kLobbyTilesetCurrent.Get(), kLobbyTilesetOptions.Get(),
                                 kLobbyComboOpenTileset.Get()};
        g_optionDropdowns[3] = {"customscenario_starting_location", "startingLocation",
                                 kLobbyStartLocCurrent.Get(),
                                 kLobbyStartLocOptions.Get(),
                                 kLobbyComboOpenStartLoc.Get()};

        MapListBinding b;
        b.listBegin = kLobbyMapListBegin.Get();
        b.listEnd = kLobbyMapListEnd.Get();
        b.folderList = kLobbyFolderList.Get();
        b.relPath = g_relPath;
        b.folderCurrent = kLobbyFolderCurrent.Get();
        b.selectedEntry = g_selected;
        b.mapSizeCurrent = kLobbyMapSizeCurrent.Get();
        b.mapSizeOptions = kLobbyMapSizeOptions.Get();
        b.comboOpenMapSize = kLobbyComboOpenMapSize.Get();
        b.refreshFromFolder = &RefreshFromFolderThunk;
        b.publish = &PublishChoice;
        b.theme = g_theme;
        b.logTag = "lobbycreation";
        g_browser.Bind(b);

        g_ready = kitReady && g_ppNkContext && g_spaceBegin && g_spaceEnd && g_slider &&
                   g_createButton && g_previousState && g_transitionState && g_copyEntry &&
                   g_refreshFromFolder && g_validateMap && g_theme.screenSkin &&
                   g_theme.leftFrameSkin && g_theme.rightFrameSkin && g_theme.headingFont &&
                   g_selected && g_readyFlag && g_relPath && g_gameSpeed && g_password &&
                   b.listBegin && b.listEnd && b.folderList;

        const bool hooked = InstallHook(kLobbyCreationContent.Target(),
                                         reinterpret_cast<void*>(&HookedLobbyCreationContent),
                                         reinterpret_cast<void**>(&g_realContent),
                                         "LobbyCreationContent (Create Multiplayer Game)");
        kLog.Info("%s, replacement screen %s",
                  g_ready ? "ready" : "NOT ready (an address failed to resolve)",
                  g_settings.GetBool(kKeyEnabled) ? "ON" : "off");
        return hooked;
    }
};

}  // namespace

MOD_REGISTER(LobbyCreationScreenMod)
