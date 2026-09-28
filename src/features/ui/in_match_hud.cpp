// SPDX-License-Identifier: MIT
#include "features/ui/in_match_hud.h"

#include <cstdint>
#include <cstring>

#include "core/hook_utils.h"
#include "core/mod.h"
#include "target/addresses.h"
#include "target/struct_offsets.h"

namespace {

using namespace game;
using namespace game::wc2r;

constexpr int kChatEditControl = 6;

uint32_t Fnv1a(const char* s) {
    uint32_t h = 0x811c9dc5u;
    for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 0x01000193u;
    return h;
}

void Place(void* ctx, int x, int y, int w, int h) {
    kOverrideNextRect.Get()(ctx, static_cast<float>(x), static_cast<float>(y),
                            static_cast<float>(w), static_cast<float>(h));
}

bool LookupPiece(void* atlas, const char* name, NkImage* img) {
    return kHudAtlasLookup.Get()(atlas, img, Fnv1a(name));
}

void DrawFrameArt(void* ctx, void* atlas, int view, int w, int h) {
    const auto size = kHudFramePieceSize.Get();
    const auto draw = kHudDrawImage.Get();
    const auto tiled = kHudDrawTiled.Get();
    int tlW = 0, tlH = 0, blW = 0, trW = 0, trH = 0, brW = 0, brH = 0;
    int wh[2] = {0, 0};
    NkImage img{};

    if (LookupPiece(atlas, *kHudSelectionCount.Get() ? "TopLeftAlt" : "TopLeft", &img)) {
        size(wh, &img, view);
        tlW = wh[0];
        tlH = wh[1];
        Place(ctx, 0, 0, tlW, tlH);
        draw(ctx, img);
    }
    if (LookupPiece(atlas, "TileableLeft", &img)) tiled(img, 0, tlH, h - tlH, 0, view);
    if (LookupPiece(atlas, "BottomLeft", &img)) {
        size(wh, &img, view);
        blW = wh[0];
        Place(ctx, 0, h - wh[1], blW, wh[1]);
        draw(ctx, img);
    }
    if (LookupPiece(atlas, "TopRight", &img)) {
        size(wh, &img, view);
        trW = wh[0];
        trH = wh[1];
        Place(ctx, w - trW, 0, trW, trH);
        draw(ctx, img);
    }
    if (LookupPiece(atlas, "BottomRight", &img)) {
        size(wh, &img, view);
        brW = wh[0];
        brH = wh[1];
        Place(ctx, w - brW, h - brH, brW, brH);
        draw(ctx, img);
    }
    if (LookupPiece(atlas, "Right", &img)) {
        size(wh, &img, view);
        tiled(img, w - wh[0], trH, h - brH - trH, 0, view);
    }
    if (LookupPiece(atlas, "Top", &img)) tiled(img, tlW, 0, w - trW - tlW, 1, view);
    if (LookupPiece(atlas, "Bottom", &img)) {
        size(wh, &img, view);
        tiled(img, tlW, h - wh[1], w - brW - blW, 1, view);
    }
}

void DrawResourceBar(void* ctx, int w, int view) {

    int x = w - 0xc0 - 0x12c;
    const uint32_t flags = *kGameSettingsFlags.Get();
    if (flags & 0x2000000) {
        x = 0;
    } else if (!(flags & 0x4000000)) {
        x = x / 2;
    }
    const float rx = static_cast<float>(x) + 0.0f;
    const auto cell = kHudResourceCell.Get();
    cell(rx, 0.0f, 300.0f, 14.0f, 0, 4, 0, *kHudGold.Get(), view);
    cell(rx, 0.0f, 300.0f, 14.0f, 1, 4, 1, *kHudLumber.Get(), view);
    cell(rx, 0.0f, 300.0f, 14.0f, 2, 4, 2, *kHudOil.Get(), view);

    const unsigned p = *kLocalPlayer.Get();
    const int used = static_cast<int>(kFoodA.Get()[p]) - static_cast<int>(kFoodB.Get()[p]);
    uint16_t cap = kFoodCap.Get()[p];
    if (cap > 200) cap = 200;
    kHudFoodCell.Get()(rx, 0.0f, 300.0f, 14.0f, 3, 4, used, cap, view);
}

void DrawPortbar(int view) {
    const uint8_t count = *kHudSelectionCount.Get();
    const int style = ((*kGameSettingsFlags.Get() & 0x10000000) || count > 9) ? 4 : 3;
    void* other = *kHudSelectionOther.Get();

    bool group = count > 1;
    if (!group && other) {

        const uint8_t type = (*kHudSelectedUnit.Get())[kUnitTypeByte];
        group = (kUnitTypeFlags.Get()[type] & 0x400) && kHudUnitHasCargo.Get()(other);
    }
    if (group) {
        kHudGroupInfo.Get()(5, 5, 3, style, view);
        return;
    }
    if (count != 1) return;

    const uint8_t type = (*kHudSelectedUnit.Get())[kUnitTypeByte];
    if (const HudPortraitFn portrait = kHudPortraitByType.Get()[type]) {
        portrait(5.0f, 5.0f, 166.0f, 166.0f, view);
    }
    kHudUnitInfo.Get()(0, 5, 5, 3, style, view);
}

void DrawChatBlock(void* ctx, int bottom, void* hovered, int view, bool paused) {
    const auto write = kWriteChatPromptLine.Get();
    const uint8_t mode = *kChatTargetMode.Get();
    const char* key = nullptr;
    switch (mode) {
    case 1:
    case 6: key = "message_generic"; break;
    case 2: key = "message_to_all"; break;
    case 3: key = "message_to_ally"; break;
    case 4: key = "message_to_enemy"; break;
    case 5: key = "message_to_player"; break;
    case 7: key = "message_to_none"; break;
    default: break;
    }
    if (!key) {
        write(nullptr, nullptr, nullptr);
    } else {
        const uint8_t* edit = kDlgGetControl.Get()(*kChatDialog.Get(), kChatEditControl);
        const char* text = *reinterpret_cast<const char* const*>(edit + kDlgEditText);
        const char* banner = kLookupLocalized.Get()(key);
        if (mode == 5) {
            const char* name = reinterpret_cast<const char*>(
                kMpLobbySlots.Get() + *kChatTargetSlot.Get() * kLobbySlotStride + kLobbySlotName);
            write(banner, name, text);
        } else {
            write(banner, text, nullptr);
        }
    }

    kBuildMapMessagesPanel.Get()(ctx, 182.0f, 22.0f, 294.0f, static_cast<float>(bottom - 0xc),
                                 0x10);
    if (hovered && !paused) kHudCommandTooltip.Get()(hovered, bottom, view);
}

void __cdecl OnBuildInMatchHud(char whichView) {
    const int view = whichView ? 1 : 0;
    void* ctx = view ? *kHudCtxView1.Get() : *kHudCtxView0.Get();
    void* manager = view ? kHudSkinManagerView1.Get() : kHudSkinManagerView0.Get();
    *kHudCtx.Get() = ctx;
    *kHudSkinManager.Get() = manager;
    kHudBeginFrame.Get()();
    if (*kHudSkipFrame.Get()) return;

    const bool alt = *kSkinAltFlag.Get() != 0;
    const unsigned int skin = view ? (alt ? *kHudSkinView1Alt.Get() : *kHudSkinView1.Get())
                                   : (alt ? *kHudSkinView0Alt.Get() : *kHudSkinView0.Get());
    const unsigned int prevFont = kSetFont.Get()(manager, *kHudFont.Get());
    const unsigned int prevSkin = kSetSkin.Get()(manager, skin);

    const int w = static_cast<int>(kHudUiWidth.Get()());
    const int h = static_cast<int>(kHudUiHeight.Get()());
    if (kBeginNamedWindow.Get()(ctx, "game_hud", 0.0f, 0.0f, static_cast<float>(w),
                                static_cast<float>(h), 0x120)) {
        const auto spaceBegin = kLayoutSpaceBegin.Get();
        const auto spaceEnd = kLayoutSpaceEnd.Get();
        const auto beginGroup = kBeginNamedGroup.Get();
        const auto endGroup = kEndGroup.Get();
        spaceBegin(ctx, 1, 0.0f, 1000);

        if (void* atlas = view ? *kHudFrameAtlasView1.Get() : *kHudFrameAtlasView0.Get()) {
            DrawFrameArt(ctx, atlas, view, w, h);
        }

        const int w2 = static_cast<int>(kHudUiWidth.Get()());
        Place(ctx, 0xb0, 0, w2 - 0xc0, 0x10);
        if (beginGroup(ctx, "resourcebar", 0x20)) {
            spaceBegin(ctx, 1, 0.0f, 1000);
            DrawResourceBar(ctx, w2, view);
            spaceEnd(ctx);
            endGroup(ctx);
        }
        Place(ctx, 0xb0, h - 0x10, w2 - 0xc0, 0x10);
        if (beginGroup(ctx, "bottom", 0x20)) endGroup(ctx);
        Place(ctx, w2 - 0x10, 0, 0x10, h);
        if (beginGroup(ctx, "right", 0x20)) endGroup(ctx);

        Place(ctx, 0, 0, 0xbc, 0xa0);
        if (beginGroup(ctx, "minimap", 0x20)) {
            spaceBegin(ctx, 1, 0.0f, 1000);
            kHudMenuButtons.Get()(view);
            kOverrideNextRect.Get()(ctx, 24.0f, 26.0f, 128.0f, 128.0f);
            NkImage minimap{};
            minimap.handle = view ? kHudMinimapTexture1.Get()() : *kHudMinimapTexture0.Get();
            kHudDrawImage.Get()(ctx, minimap);
            spaceEnd(ctx);
            endGroup(ctx);
        }

        Place(ctx, 0, 0xa0, 0xbc, 0xb0);
        if (beginGroup(ctx, "portbar", 0x20)) {
            spaceBegin(ctx, 1, 0.0f, 1000);
            DrawPortbar(view);
            spaceEnd(ctx);
            endGroup(ctx);
        }

        void* hovered = nullptr;
        Place(ctx, 0, 0x150, 0xbc, 0x90);
        if (beginGroup(ctx, "statbar", 0x20)) {
            spaceBegin(ctx, 1, 0.0f, 1000);
            hovered = kHudCommandButtons.Get()(view);
            spaceEnd(ctx);
            endGroup(ctx);
        }

        const bool paused = *kMatchPauseFlags.Get() != 0;
        DrawChatBlock(ctx, h - 0x20, hovered, view, paused);

        if (!*kNetworkedMatch.Get() && !*kHudNoObjectives.Get() &&
            !(*kGameSettingsFlags.Get() & 0x20)) {
            const int lines = kHudObjectivesLines.Get()(ctx, 0xe6);
            Place(ctx, 0xb8, 0x18, 0xe6, lines << 4);
            if (beginGroup(ctx, "objectives", 0x20)) {
                kHudObjectivesDraw.Get()(ctx, 0xe6, 16.0f);
                endGroup(ctx);
            }
        }

        spaceEnd(ctx);
        kEndCurrentWindow.Get()(ctx);
    }

    kSetSkin.Get()(manager, prevSkin);
    kSetFont.Get()(manager, prevFont);
}

EditControlEventProcFn g_realEditProc = nullptr;

constexpr unsigned char kScanBackspace = 0x0e;
constexpr unsigned char kModCtrlAlt = 0x0c;

bool IsTypedCharacter(const uint8_t* evt) {
    uint16_t number;
    memcpy(&number, evt + kEventNumber, sizeof(number));
    return (number == 0 || number == 1) && !(evt[kEventModifiers] & kModCtrlAlt) &&
           evt[kEventScan] != kScanBackspace && evt[kEventChar] != 0;
}

bool IsChatEdit(const uint8_t* control) {
    void* dialog = *kChatDialog.Get();
    return dialog && control == kDlgGetControl.Get()(dialog, kChatEditControl);
}

unsigned __cdecl OnEditControlEvent(uint8_t* control, uint8_t* evt) {
    const unsigned accepted = g_realEditProc(control, evt);
    if (accepted && control && evt && *kMatchPauseFlags.Get() == 1 && IsTypedCharacter(evt) &&
        IsChatEdit(control)) {
        char* text = *reinterpret_cast<char**>(control + kDlgEditText);
        const size_t len = strlen(text);
        if (len < kDlgEditMaxLen) {
            text[len] = static_cast<char>(evt[kEventChar]);
            text[len + 1] = '\0';
            control[kDlgEditChanged] = 1;
            kDlgEditRefresh.Get()(control);
        }
    }
    return accepted;
}

class InMatchHudMod : public IMod {
public:
    const char* Name() const override { return "hud"; }

    bool Install() override {
        return InstallHook(kBuildInMatchHud.Target(), reinterpret_cast<void*>(&OnBuildInMatchHud),
                           nullptr, "BuildInMatchHudScreen") &&
               InstallHook(kEditControlEventProc.Target(),
                           reinterpret_cast<void*>(&OnEditControlEvent),
                           reinterpret_cast<void**>(&g_realEditProc), "EditControlEventProc");
    }
};

}  // namespace

MOD_REGISTER(InMatchHudMod)
