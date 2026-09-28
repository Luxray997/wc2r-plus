// SPDX-License-Identifier: MIT
#include "features/ui/multiplayer_lobby_screen.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "core/hook_utils.h"
#include "core/mod.h"
#include "core/log.h"
#include "core/named_window_hook.h"
#include "core/peer_messages.h"
#include "core/settings/mod_settings.h"
#include "features/lobby_policy.h"
#include "features/map_download.h"
#include "features/map_preview.h"
#include "features/peer_handshake.h"
#include "features/team_colours.h"
#include "features/ui/crown_image.h"
#include "features/ui/lobby_chat.h"
#include "features/ui/screen_kit.h"
#include "target/addresses.h"
#include "target/struct_offsets.h"

namespace {

using namespace game;
using namespace ui;

const Logger kLog{"mplobby"};

constexpr const char* kKeyEnabled = "enabled";

constexpr bool kDefaultEnabled = true;

ModSettings g_settings{"mplobby"};

constexpr const char* kWindowName = "multiplayer_lobby_screen";

constexpr float kSlotsFraction = 0.58f;
constexpr float kSlotSpacerFraction = 0.08f;
constexpr float kLeftColumnFraction = 0.58f;
constexpr float kColumnGutterFraction = 0.03f;

constexpr int kInfoRowCount = 7;

constexpr long long kEntryDeadMs = 1500;

constexpr float kSlotNameShare = 0.56f;
constexpr float kSlotRaceShare = 0.30f;
constexpr float kSlotTeamShare = 0.14f;
constexpr float kSlotEdgePixels = 10.0f;

constexpr float kChatToInputGap = 10.0f;

constexpr int kSlotCount = 8;

constexpr unsigned int kLobbyResourceMask = 0x23000;
constexpr unsigned int kLobbyTilesetMask = 0x1c000;
constexpr unsigned int kLobbyOnePeonBit = 0x200;

static_assert(team_colours::kCount == kSlotCount, "one colour per lobby slot");

constexpr unsigned int kCrownColour = Rgba(240, 206, 74, 255);
constexpr float kCrownBandFraction = 0.42f;
constexpr float kCrownProngWidth = 0.24f;
constexpr float kCrownMiddleTop = 0.22f;

constexpr float kMarkGap = 6.0f;

constexpr float kSwatchFraction = 0.38f;
constexpr float kCrownSizeMultiple = 2.0f;

constexpr float kCrownOverhang = 12.0f;

constexpr unsigned int kModBadgeColour = Rgba(96, 200, 120, 255);
constexpr unsigned int kModBadgeRing = Rgba(18, 36, 22, 255);
constexpr float kModBadgeAlone = 0.55f;
constexpr float kModBadgeWithCrown = 0.42f;
constexpr float kModBadgeRingWidth = 2.0f;

constexpr float kDlBarThickness = 0.07f;
constexpr float kDlBarMinThickness = 4.0f;
constexpr float kDlBarSideInset = 0.14f;
constexpr float kDlBarBottomInset = 0.10f;
constexpr unsigned int kDlBarTrack = Rgba(0, 0, 0, 160);

constexpr DWORD kDlWaitDotMs = 400;

constexpr unsigned int kDlBarFill = Rgba(240, 206, 74, 255);
constexpr unsigned int kDlBarDone = Rgba(96, 200, 120, 255);
constexpr unsigned int kDlBarNoMap = Rgba(200, 64, 52, 255);

constexpr float kDlBarSweepShare = 0.25f;

constexpr unsigned int kDlPanelBack = Rgba(0, 0, 0, 170);
constexpr float kDlPanelRounding = 12.0f;
constexpr float kDlPanelPad = 0.06f;
constexpr float kDlPanelBarH = 24.0f;

constexpr float kDlPopupWidth = 1648.0f;
constexpr float kDlPopupHeight = 760.0f;
constexpr float kDlPopupPad = 200.0f;
constexpr float kDlPopupTextTop = 125.0f;
constexpr float kDlPopupBottom = 115.0f;
constexpr float kDlPopupLineH = 120.0f;
constexpr unsigned int kDlBarSweepMs = 1400;

bool g_ready = false;

void** g_ppNkContext = nullptr;
LayoutSpaceBeginFn g_spaceBegin = nullptr;
LayoutSpaceEndFn g_spaceEnd = nullptr;
EndCurrentWindowFn g_endWindow = nullptr;
DrawNkPopupFn g_drawPopup = nullptr;
DrawMenuButtonFn g_menuButtonRaw = nullptr;
const unsigned int* g_skinPopupFrame = nullptr;
const unsigned int* g_skinPopupFrameAlt = nullptr;
const unsigned char* g_skinAltFlag = nullptr;
unsigned char* g_popup = nullptr;
const unsigned int* g_popupSkin = nullptr;
LayoutRowRatiosFn g_rowRatios = nullptr;
LayoutRowDynamicFn g_rowDynamic = nullptr;
CountWrappedLinesFn g_countLines = nullptr;
DrawWrappedLabelFn g_wrappedLabel = nullptr;
InputKeyPressedFn g_keyPressed = nullptr;
SpacingFn g_spacing = nullptr;
DisableScopeFn g_disableBegin = nullptr;
DisableScopeFn g_disableEnd = nullptr;
MillisecondClockFn g_clock = nullptr;
CreateGameTextureFn g_createTexture = nullptr;
NkDrawImageFn g_nkDrawImage = nullptr;
GameTexture* g_crownTexture = nullptr;
bool g_crownTextureFailed = false;

LobbySlotRowFn g_slotRow = nullptr;
LobbyLeaveButtonFn g_leaveButton = nullptr;
LobbyStartGameButtonFn g_startButton = nullptr;
LobbySendChatFn g_sendChat = nullptr;
LobbyMaxPlayersFn g_maxPlayers = nullptr;

ScreenTheme g_theme;
const unsigned int* g_chatSkin = nullptr;
const unsigned int* g_chatTextSkin = nullptr;

char* g_gameName = nullptr;
char* g_hostName = nullptr;
unsigned char* g_gameSpeed = nullptr;
char* g_mapName = nullptr;
unsigned char* g_playerCount = nullptr;
unsigned int* g_matchFlags = nullptr;
unsigned char* g_fixedStart = nullptr;
void** g_netSession = nullptr;

unsigned char* g_slots = nullptr;
unsigned char** g_slotNames = nullptr;
unsigned char* g_countdownActive = nullptr;
long long* g_enterMs = nullptr;

void* g_resourceKeys = nullptr;
void* g_tilesetKeys = nullptr;
void* g_teamOptions = nullptr;

unsigned char** g_chatBegin = nullptr;
unsigned char** g_chatEnd = nullptr;
void* g_chatInput = nullptr;
unsigned char* g_chatScrollRequest = nullptr;

char* g_selectedMapPath = nullptr;
unsigned short* g_selectedMapId = nullptr;

LookupLocalizedFn g_realLocalized = nullptr;
bool g_rewriteTeamLabels = false;

const char* const kTeamDigits[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};

const char* __cdecl HookedLookupLocalized(const char* key) {
    if (g_rewriteTeamLabels && key && g_teamOptions) {
        const KeyList teams = Keys(g_teamOptions);
        int index = 0;
        for (const char* const* k = teams.begin; k && k != teams.end; ++k, ++index) {
            if (!*k || strcmp(*k, key) != 0) continue;
            if (index < static_cast<int>(sizeof(kTeamDigits) / sizeof(kTeamDigits[0]))) {
                return kTeamDigits[index];
            }
            break;
        }
    }
    return g_realLocalized(key);
}

bool SlotIsPresent(int i) {
    if (!g_slots) return true;
    return g_slots[i * wc2r::kLobbySlotStride + wc2r::kLobbySlotState] != wc2r::kLobbySlotAbsent;
}

const char* SlotName(int i) {
    if (!g_slotNames || !*g_slotNames) return "";
    return StrData(*g_slotNames + i * wc2r::kSlotNameStride + wc2r::kSlotNameString);
}

const char* SeatedName(int lobbySlot) {
    constexpr unsigned char kSeated = 0;
    if (!g_slots || lobbySlot < 0 || lobbySlot >= kSlotCount) return nullptr;
    if (g_slots[lobbySlot * wc2r::kLobbySlotStride + wc2r::kLobbySlotState] != kSeated) return nullptr;
    return SlotName(lobbySlot);
}

int HostSlot() {
    const int byNetSlot = peer_messages::LobbySlotOfNetSlot(0);
    if (byNetSlot >= 0 && byNetSlot < kSlotCount && SlotIsPresent(byNetSlot)) return byNetSlot;
    if (!g_hostName || !g_hostName[0]) return -1;
    for (int i = 0; i < kSlotCount; ++i) {
        if (!SlotIsPresent(i)) continue;
        if (_stricmp(SlotName(i), g_hostName) == 0) return i;
    }
    return -1;
}

bool IsHost() {

    if (!g_netSession || !*g_netSession) return false;
    return *(static_cast<const unsigned char*>(*g_netSession) + wc2r::kSessionIsHost) != 0;
}

const char* KeyByIndex(void* vec, int index) {
    const KeyList keys = Keys(vec);
    if (!keys.begin || !keys.end) return "";
    const int count = static_cast<int>(keys.end - keys.begin);
    if (index < 0 || index >= count) return "";
    return keys.begin[index] ? keys.begin[index] : "";
}

const char* ResourcesKey(unsigned int flags) {
    switch (flags & kLobbyResourceMask) {
        case 0x1000: return KeyByIndex(g_resourceKeys, 2);
        case 0x2000: return KeyByIndex(g_resourceKeys, 3);
        case 0x3000: return KeyByIndex(g_resourceKeys, 4);
        case 0x20000: return KeyByIndex(g_resourceKeys, 1);
        default: return KeyByIndex(g_resourceKeys, 0);
    }
}

const char* TilesetKey(unsigned int flags) {
    switch (flags & kLobbyTilesetMask) {
        case 0x4000: return KeyByIndex(g_tilesetKeys, 1);
        case 0x8000: return KeyByIndex(g_tilesetKeys, 2);
        case 0xc000: return KeyByIndex(g_tilesetKeys, 3);
        case 0x1c000: return KeyByIndex(g_tilesetKeys, 4);
        default: return KeyByIndex(g_tilesetKeys, 0);
    }
}

int TilesetOverride(unsigned int flags) { return TilesetIndexForKey(TilesetKey(flags)); }

const char* WithColon(const char* text) {
    static char buf[128];
    if (!text || !text[0]) return "";
    size_t n = strlen(text);
    while (n > 0 && (text[n - 1] == ' ' || text[n - 1] == '	')) --n;
    if (n > 0 && text[n - 1] == ':') return text;
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%.*s:", static_cast<int>(n), text);
    return buf;
}

struct MarkSizes {
    float crown;
    float swatch;
    float gutter;
};

MarkSizes MarkSizesFor(float rowH) {
    MarkSizes m;
    m.swatch = Fl(rowH * kSwatchFraction);
    m.crown = Fl(m.swatch * kCrownSizeMultiple);

    m.gutter = Fl((m.crown - kCrownOverhang) + kMarkGap * 2.0f + m.swatch);
    return m;
}

struct SlotMarks {
    Rect gutter[kSlotCount];
    Rect nameBox[kSlotCount];
    int count = 0;
};
SlotMarks g_slotMarks;

std::string PlayersHeading() {
    std::string heading = Localized("mp_lobby_players");
    if (g_playerCount && g_maxPlayers) {
        char count[32];
        _snprintf_s(count, sizeof(count), _TRUNCATE, " (%u/%u)",
                    static_cast<unsigned>(*g_playerCount), static_cast<unsigned>(g_maxPlayers()));
        heading += count;
    }
    return heading;
}

void DrawSlotsPanel(void* ctx, const Rect& panel) {

    const float headerH = kCaptionH;
    const float pitch = Fl((panel.h - headerH) / static_cast<float>(kSlotCount));
    const float spacer = Fl(pitch * kSlotSpacerFraction);
    const float rowH = pitch - spacer;

    const MarkSizes marks = MarkSizesFor(rowH);
    const Rect rows{Fl(panel.x + marks.gutter), panel.y, Fl(panel.w - marks.gutter), panel.h};

    const float edge = rows.w > 0.0f ? kSlotEdgePixels / rows.w : 0.0f;
    const float inner = 1.0f - edge * 2.0f;
    const float ratios[5] = {inner * kSlotNameShare, edge, inner * kSlotRaceShare, edge,
                              inner * kSlotTeamShare};

    g_slotMarks = SlotMarks{};

    if (!BeginList(ctx, rows, "mod_lobby_slots", kGroupFlags)) return;

    g_rowRatios(ctx, 0, headerH, 5, ratios);
    const std::string playersHeading = PlayersHeading();
    Label(ctx, playersHeading.c_str(), kAlignLeft);
    g_spacing(ctx);
    Label(ctx, Localized("mp_lobby_race"), kAlignLeft);
    g_spacing(ctx);
    Label(ctx, Localized("mp_lobby_team"), kAlignLeft);

    Rect nameColumn{};
    const bool haveNameColumn = LastRowFirstCell(ctx, &nameColumn);

    g_rewriteTeamLabels = true;
    for (int i = 0; i < kSlotCount; ++i) {

        const float top = NextRowTop(ctx);
        g_slotMarks.gutter[i] = Rect{panel.x, top, marks.gutter, rowH};
        g_slotRow(i, ratios, rowH, spacer);

        if (haveNameColumn) g_slotMarks.nameBox[i] = Rect{nameColumn.x, top, nameColumn.w, rowH};
    }
    g_rewriteTeamLabels = false;
    g_slotMarks.count = kSlotCount;

    EndList(ctx);
}

void DrawCrownRects(void* canvas, const Rect& r);

bool EnsureCrownTexture() {
    if (g_crownTexture) return true;
    if (g_crownTextureFailed || !g_createTexture || !g_nkDrawImage) return false;

    GameTexture* tex = nullptr;
    g_createTexture(&tex, kCrownImageWidth, kCrownImageHeight, kCrownImageRgba,
                     kGameTextureFormatRgba8, kGameTextureFilterLinear);
    if (!tex || tex->glName == 0 || tex->w != kCrownImageWidth || tex->h != kCrownImageHeight) {
        g_crownTextureFailed = true;
        kLog.Warn("crown texture failed (%p name=%u %dx%d) -- drawing rects",
                        tex, tex ? tex->glName : 0u, tex ? tex->w : 0, tex ? tex->h : 0);
        return false;
    }
    g_crownTexture = tex;
    kLog.Info("crown texture created %dx%d GL name=%u", tex->w, tex->h,
                    tex->glName);
    return true;
}

void DrawCrown(void* canvas, const Rect& r) {
    if (EnsureCrownTexture()) {
        NkImage img;
        img.handle = g_crownTexture;

        img.w = static_cast<uint16_t>(kCrownImageWidth);
        img.h = static_cast<uint16_t>(kCrownImageHeight);
        img.region[0] = 0;
        img.region[1] = 0;
        img.region[2] = static_cast<uint16_t>(kCrownImageWidth);
        img.region[3] = static_cast<uint16_t>(kCrownImageHeight);

        g_nkDrawImage(canvas, r.x, r.y, r.w, r.h, &img, Rgba(255, 255, 255, 255));
        return;
    }
    DrawCrownRects(canvas, r);
}

void DrawCrownRects(void* canvas, const Rect& r) {
    const float bandH = Fl(r.h * kCrownBandFraction);
    const float bandY = Fl(r.y + r.h - bandH);
    const float prongW = Fl(r.w * kCrownProngWidth);
    const float middleTop = Fl(r.y + r.h * kCrownMiddleTop);

    FillRect(canvas, r.x, bandY, r.w, bandH, 0.0f, kCrownColour);

    FillRect(canvas, r.x, r.y, prongW, bandY - r.y, 0.0f, kCrownColour);
    FillRect(canvas, Fl(r.x + (r.w - prongW) * 0.5f), middleTop, prongW, bandY - middleTop, 0.0f,
              kCrownColour);
    FillRect(canvas, Fl(r.x + r.w - prongW), r.y, prongW, bandY - r.y, 0.0f, kCrownColour);
}

void DrawModBadge(void* canvas, const Rect& crown, bool withCrown) {
    const float size = Fl(crown.w * (withCrown ? kModBadgeWithCrown : kModBadgeAlone));
    if (size <= kModBadgeRingWidth * 2.0f) return;
    const float x = withCrown ? Fl(crown.x + crown.w - size) : Fl(crown.x + (crown.w - size) * 0.5f);
    const float y = withCrown ? Fl(crown.y + crown.h - size) : Fl(crown.y + (crown.h - size) * 0.5f);
    FillRect(canvas, x, y, size, size, size * 0.5f, kModBadgeRing);
    const float inner = size - kModBadgeRingWidth * 2.0f;
    FillRect(canvas, x + kModBadgeRingWidth, y + kModBadgeRingWidth, inner, inner, inner * 0.5f,
              kModBadgeColour);
}

void DrawDownloadBar(void* canvas, const Rect& box, const map_download::SlotDownloadStatus& st) {
    using map_download::SlotDownload;
    if (st.kind == SlotDownload::None || box.w <= 0.0f || box.h <= 0.0f) return;

    const float thick = Fl((std::max)(kDlBarMinThickness, box.h * kDlBarThickness));
    const float side = Fl(box.h * kDlBarSideInset);
    const float x = Fl(box.x + side);
    const float w = Fl(box.w - side * 2.0f);
    const float y = Fl(box.y + box.h - box.h * kDlBarBottomInset - thick);
    if (w <= thick * 2.0f) return;
    const float round = thick * 0.5f;

    FillRect(canvas, x, y, w, thick, round, kDlBarTrack);
    switch (st.kind) {
        case SlotDownload::Deciding: {
            const float seg = Fl(w * kDlBarSweepShare);
            const long long ms = g_clock ? g_clock() : static_cast<long long>(GetTickCount());
            const unsigned phase = static_cast<unsigned>(ms % kDlBarSweepMs);
            const float t = static_cast<float>(phase) / static_cast<float>(kDlBarSweepMs);
            FillRect(canvas, Fl(x + (w - seg) * t), y, seg, thick, round, kDlBarFill);
            break;
        }
        case SlotDownload::Downloading: {
            const float fw = Fl(w * static_cast<float>(st.percent) / 100.0f);
            if (fw > 0.0f) FillRect(canvas, x, y, (std::max)(fw, thick), thick, round, kDlBarFill);
            break;
        }
        case SlotDownload::Done:
            FillRect(canvas, x, y, w, thick, round, kDlBarDone);
            break;
        case SlotDownload::NoMap:
            FillRect(canvas, x, y, w, thick, round, kDlBarNoMap);
            break;
        default:
            break;
    }
}

void DrawSlotMarks(void* ctx) {
    void* canvas = WindowCanvas(ctx);
    if (!canvas || g_slotMarks.count == 0) return;

    const bool fixedStart = g_fixedStart && *g_fixedStart == 1;
    const int host = HostSlot();

    for (int i = 0; i < g_slotMarks.count; ++i) {
        const Rect& g = g_slotMarks.gutter[i];
        if (g.w <= 0.0f || !SlotIsPresent(i)) continue;

        const MarkSizes m = MarkSizesFor(g.h);
        if (m.crown <= 0.0f || m.swatch <= 0.0f) continue;

        const Rect crownRect{Fl(g.x - kCrownOverhang), Fl(g.y + (g.h - m.crown) * 0.5f), m.crown,
                             m.crown};
        if (i == host) DrawCrown(canvas, crownRect);
        if (handshake::LobbySlotRunsMod(i)) DrawModBadge(canvas, crownRect, i == host);
        DrawDownloadBar(canvas, g_slotMarks.nameBox[i], map_download::LobbySlotDownloadStatus(i));
        if (fixedStart) {
            FillRect(canvas, Fl(g.x + g.w - kMarkGap - m.swatch),
                      Fl(g.y + (g.h - m.swatch) * 0.5f), m.swatch, m.swatch, 0.0f,
                      team_colours::kColours[i]);
        }
    }
}

Rect MapPreviewBox(const Rect& column, float infoH) {
    const float available = column.h - kNameH - kGap - infoH - kGap;
    const float side = Fl((std::max)(0.0f, (std::min)(column.w, available)));
    return Rect{Fl(column.x + (column.w - side) * 0.5f), Fl(column.y + kNameH + kGap), side, side};
}

void InfoRow(void* ctx, const Rect& r, const char* caption, const char* value) {
    LabelAt(ctx, r, caption, kAlignLeft);
    LabelAt(ctx, r, value, kAlignRight);
}

constexpr float kLockButtonShare = 0.34f;

void LockRow(void* ctx, const Rect& r, const char* caption, bool locked, bool host,
             bool* flip) {
    const char* value = locked ? "Locked" : "Unlocked";
    LabelAt(ctx, r, caption, kAlignLeft);
    if (!host) {
        LabelAt(ctx, r, value, kAlignRight);
        return;
    }
    const float w = Fl(r.w * kLockButtonShare);
    if (ButtonAt(ctx, Rect{Fl(r.x + r.w - w), r.y, w, r.h}, value)) *flip = true;
}

void DrawInfoColumn(void* ctx, const Rect& r) {
    const unsigned int flags = g_matchFlags ? *g_matchFlags : 0;
    const float rowH = Fl(r.h / static_cast<float>(kInfoRowCount));
    float y = r.y;
    auto row = [&](const char* captionKey, const char* value) {
        InfoRow(ctx, Rect{r.x, y, r.w, rowH}, WithColon(Localized(captionKey)), value);
        y += rowH;
    };

    char speed[16] = "";
    if (g_gameSpeed) {
        _snprintf_s(speed, sizeof(speed), _TRUNCATE, "%u", static_cast<unsigned>(*g_gameSpeed));
    }

    std::string password;
    const bool known = lobby_policy::Password(&password);
    InfoRow(ctx, Rect{r.x, y, r.w, rowH}, "Password:",
            !known ? "Unknown" : password.empty() ? "None" : password.c_str());
    y += rowH;
    row("option_speed_game_speed", speed);
    row("customscenario_resources", Localized(ResourcesKey(flags)));
    row("customscenario_units_peasant",
        Localized((flags & kLobbyOnePeonBit) ? "option_common_on" : "option_common_off"));

    row("customscenario_starting_location",
        Localized((g_fixedStart && *g_fixedStart == 1) ? "customscenario_fixed_location"
                                                        : "customscenario_random_location"));

    const bool host = IsHost();
    lobby_policy::Locks locks = lobby_policy::Current();
    bool flipTeams = false, flipSlots = false;
    LockRow(ctx, Rect{r.x, y, r.w, rowH}, "Teams:", locks.teams, host, &flipTeams);
    y += rowH;
    LockRow(ctx, Rect{r.x, y, r.w, rowH}, "Slots:", locks.slots, host, &flipSlots);
    y += rowH;
    if (flipTeams || flipSlots) {
        if (flipTeams) locks.teams = !locks.teams;
        if (flipSlots) locks.slots = !locks.slots;
        lobby_policy::SetLocks(locks);
    }
}

Rect g_startRect{};

constexpr float kTipMaxWidth = 1100.0f;
constexpr float kTipPad = 26.0f;
constexpr float kTipBorder = 4.0f;
constexpr float kTipRounding = 6.0f;
constexpr float kTipCursorOffset = 44.0f;
constexpr unsigned int kTipFillColour = Rgba(12, 10, 8, 244);
constexpr unsigned int kTipBorderColour = Rgba(120, 96, 48, 255);

constexpr unsigned int kTipTextColour = Rgba(255, 255, 255, 255);

void DrawStartLockTooltip(void* ctx, const Rect& window) {
    if (g_startRect.w <= 0.0f) return;
    const float mx = ReadCtxFloat(ctx, nk::kCtxMouseX);
    const float my = ReadCtxFloat(ctx, nk::kCtxMouseY);
    if (mx < g_startRect.x || mx >= g_startRect.x + g_startRect.w || my < g_startRect.y ||
        my >= g_startRect.y + g_startRect.h) {
        return;
    }
    int slots[kSlotCount];
    const int n = map_download::PendingDownloadSlots(slots, kSlotCount);
    if (n <= 0) return;

    char names[224];
    names[0] = '\0';
    int used = 0;
    for (int i = 0; i < n; ++i) {
        const char* who = (slots[i] >= 0 && slots[i] < kSlotCount) ? SlotName(slots[i]) : "";
        if (!who || !who[0]) who = "a player";

        const int wrote = _snprintf_s(names + used, sizeof(names) - used, _TRUNCATE, "%s%s",
                                      used ? ", " : "", who);
        if (wrote < 0) break;
        used += wrote;
        if (used >= static_cast<int>(sizeof(names)) - 1) break;
    }
    char line[288];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "Pending map download from: %s", names);

    void* canvas = WindowCanvas(ctx);
    if (!canvas || !g_countLines || !g_wrappedLabel) return;
    const float lineH = lobby_chat::FontLineHeight(ctx);
    const float tipW = kTipMaxWidth < window.w ? kTipMaxWidth : window.w;
    int lines = g_countLines(ctx, line, static_cast<int>(strlen(line)), tipW - kTipPad * 2.0f);
    if (lines < 1) lines = 1;
    const float tipH = static_cast<float>(lines) * lineH + kTipPad * 2.0f;

    float x = mx + kTipCursorOffset;
    float y = my - kTipCursorOffset - tipH;
    if (x + tipW > window.w) x = window.w - tipW;
    if (x < 0.0f) x = 0.0f;
    if (y < 0.0f) y = 0.0f;
    FillRect(canvas, x, y, tipW, tipH, kTipRounding, kTipBorderColour);
    FillRect(canvas, x + kTipBorder, y + kTipBorder, tipW - kTipBorder * 2.0f, tipH - kTipBorder * 2.0f,
             kTipRounding, kTipFillColour);
    SetRect(ctx, Rect{x + kTipPad, y + kTipPad, tipW - kTipPad * 2.0f, tipH - kTipPad * 2.0f});
    g_wrappedLabel(ctx, line, kTipTextColour);
}

void DrawFooterButtons(void* ctx, const Rect& window, float footerY) {
    const float buttonW = FooterButtonWidth(window);
    const bool host = IsHost();
    const float totalW = host ? buttonW * 2.0f : buttonW;
    const float x = Fl((window.w - totalW) * 0.5f);

    const bool dead = g_clock && g_enterMs && (g_clock() - *g_enterMs) < kEntryDeadMs;
    if (dead) g_disableBegin(ctx);

    SetRect(ctx, Rect{x, footerY, buttonW, kFooterH});
    g_leaveButton();

    g_startRect = Rect{};
    if (host) {
        g_startRect = Rect{x + buttonW, footerY, buttonW, kFooterH};
        SetRect(ctx, g_startRect);
        g_startButton();
    }

    if (dead) g_disableEnd(ctx);
}

Rect g_previewBox{};
bool g_downloadPanelShown = false;
map_download::DownloadPrompt g_popupPrompt;

void DrawDownloadPanel(void* ctx, const Rect& box, const map_download::DownloadPrompt& p) {
    using Kind = map_download::DownloadPrompt::Kind;
    g_downloadPanelShown = p.kind != Kind::None && box.w > 0.0f;
    if (!g_downloadPanelShown) return;

    if (p.kind == Kind::Asking) return;
    if (void* canvas = WindowCanvas(ctx)) {
        FillRect(canvas, box.x, box.y, box.w, box.h, kDlPanelRounding, kDlPanelBack);
    }
    const float pad = Fl(box.w * kDlPanelPad);
    const float x = Fl(box.x + pad);
    const float w = Fl(box.w - pad * 2.0f);
    const float midY = Fl(box.y + (box.h - kCaptionH) * 0.5f);
    char line[192];

    switch (p.kind) {
        case Kind::Waiting: {

            static const char* const kDots[] = {"", ".", "..", "..."};
            const unsigned step = (GetTickCount() / kDlWaitDotMs) % 4u;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "Looking for map%s", kDots[step]);

            LabelAt(ctx, Rect{x, midY, w, kCaptionH}, line, kAlignCentre);
            break;
        }
        case Kind::Downloading: {
            LabelAt(ctx, Rect{x, Fl(midY - kCaptionH * 0.5f), w, kCaptionH}, "Downloading", kAlignCentre);
            if (void* canvas = WindowCanvas(ctx)) {
                const float barY = Fl(midY + kCaptionH * 0.75f);
                const float barH = kDlPanelBarH;
                FillRect(canvas, x, barY, w, barH, barH * 0.5f, kDlBarTrack);
                const float fw = Fl(w * static_cast<float>(p.percent) / 100.0f);
                if (fw > 0.0f) FillRect(canvas, x, barY, (std::max)(fw, barH), barH, barH * 0.5f, kDlBarFill);
            }
            break;
        }
        default:
            break;
    }
}

struct NkPopupShape {
    unsigned char open = 0;
    std::string text;
    std::vector<std::function<void()>> buttons;
    unsigned int skin = 0;
    float width = kDlPopupWidth;
    float height = kDlPopupHeight;
    float pad = kDlPopupPad;
    float textTop = kDlPopupTextTop;
    float bottom = kDlPopupBottom;
    float lineH = kDlPopupLineH;
    unsigned char tail[0x20] = {};
};

static_assert(sizeof(std::string) == 0x18, "NKPopup text is an MSVC release std::string");
static_assert(sizeof(std::vector<std::function<void()>>) == 0x0c, "NKPopup buttons are a release vector");
static_assert(sizeof(std::function<void()>) == 0x28, "the NKPopup draw steps buttons by 0x28");

NkPopupShape g_askPopup;
map_download::DownloadPrompt g_popupFor;

bool PopupShapeOk() {
    static int ok = -1;
    if (ok < 0) {
        const auto base = reinterpret_cast<const char*>(&g_askPopup);
        auto at = [base](const void* field) { return reinterpret_cast<const char*>(field) - base; };
        ok = at(&g_askPopup.text) == 0x04 && at(&g_askPopup.buttons) == 0x1c && at(&g_askPopup.skin) == 0x28 &&
             at(&g_askPopup.lineH) == 0x40;
        if (!ok) kLog.Error("download popup layout does not match NKPopup -- not drawn");
    }
    return ok == 1;
}

Rect PopupButtonRect(const NkPopupShape& pp, int index, int count) {
    const float inner = pp.width - pp.pad * 2.0f;
    const float w = inner / static_cast<float>(count);
    return Rect{pp.pad + w * static_cast<float>(index), pp.height - pp.bottom - pp.lineH, w, pp.lineH};
}

bool PopupButton(const char* label, bool greyed) {
    void* ctx = g_ppNkContext ? *g_ppNkContext : nullptr;
    if (!ctx || !g_menuButtonRaw) return false;
    if (greyed) g_disableBegin(ctx);
    const bool clicked = g_menuButtonRaw(ctx, label) != 0;
    if (greyed) g_disableEnd(ctx);
    return clicked && !greyed;
}

void AskLeaveButton() {
    void* ctx = g_ppNkContext ? *g_ppNkContext : nullptr;
    if (!ctx) return;
    const Rect b = PopupButtonRect(g_askPopup, 0, 2);
    char line[64];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "%u seconds remaining", (g_popupFor.msLeft + 999u) / 1000u);
    const float inner = g_askPopup.width - g_askPopup.pad * 2.0f;
    LabelAt(ctx, Rect{g_askPopup.pad, b.y - g_askPopup.lineH, inner, g_askPopup.lineH}, line, kAlignCentre);
    SetRect(ctx, b);
    if (PopupButton("Leave", false)) map_download::AnswerDownloadPrompt(g_popupFor.generation, false);
}

void AskDownloadButton() {

    if (PopupButton("Download", !g_popupFor.clickable)) {
        map_download::AnswerDownloadPrompt(g_popupFor.generation, true);

        game::kCloseNkPopup.Get()(&g_askPopup);
    }
}

void DrawGamePopup(NkPopupShape* pp, const char* text) {
    if (pp->text != text) pp->text = text;
    const bool alt = g_skinAltFlag && *g_skinAltFlag == 1;
    const unsigned int* frame = alt ? g_skinPopupFrameAlt : g_skinPopupFrame;
    pp->skin = frame ? *frame : 0;
    pp->open = 1;
    const unsigned int prev = g_popupSkin ? ui::PushSkin(*g_popupSkin) : 0;
    g_drawPopup(pp);
    if (g_popupSkin) ui::PopSkin(prev);
    pp->open = 0;
}

void DrawDownloadPopup(const map_download::DownloadPrompt& p) {
    using Kind = map_download::DownloadPrompt::Kind;
    if (!g_drawPopup || !PopupShapeOk()) return;
    g_popupFor = p;
    char text[160];
    if (p.kind == Kind::Asking) {
        if (g_askPopup.buttons.empty()) {
            g_askPopup.buttons.emplace_back([] { AskLeaveButton(); });
            g_askPopup.buttons.emplace_back([] { AskDownloadButton(); });
        }
        _snprintf_s(text, sizeof(text), _TRUNCATE, "Download %s (%u KB)?", p.mapName.c_str(),
                    (p.sizeBytes + 1023u) / 1024u);
        DrawGamePopup(&g_askPopup, text);
    }
}

void DrawScreen(void* ctx, const Rect& window) {

    lobby_policy::ApplyCreateChoiceOnce();
    g_previewBox = Rect{};
    g_spaceBegin(ctx, 1, 0.0f, 1000);

    const map_download::DownloadPrompt prompt = map_download::LobbyDownloadPrompt();
    const bool asking = prompt.kind == map_download::DownloadPrompt::Kind::Asking;

    const bool countingDown = g_countdownActive && *g_countdownActive;

    const bool blocked = countingDown || asking;
    if (blocked) g_disableBegin(ctx);

    DrawBanner();

    Label(ctx, g_gameName ? g_gameName : "", kAlignCentre);

    const Rect frame = LobbyFrame(window);
    PanelFrame(ctx, frame, "lobby_panel", g_theme.leftFrameSkin);

    const float contentX = frame.x + kFramePad;
    const float contentRight = frame.x + frame.w - kFramePad;
    const float contentW = contentRight - contentX;
    const float contentY = frame.y + kFramePad;
    const float footerY = window.h - kFooterBottomGap;
    const float contentH = (footerY - kGap) - contentY;

    const float leftW = Fl(contentW * kLeftColumnFraction);
    const float gutter = Fl(contentW * kColumnGutterFraction);
    const float rightX = contentX + leftW + gutter;
    const float rightW = contentRight - rightX;

    const float slotsH = Fl(contentH * kSlotsFraction);
    DrawSlotsPanel(ctx, Rect{contentX, contentY, leftW, slotsH});

    const float chatY = Fl(contentY + slotsH + kGap);
    const float chatH = (contentY + contentH) - chatY;
    lobby_chat::DrawHistory(ctx, Rect{contentX, chatY, leftW, Fl(chatH - kRowH - kChatToInputGap)});
    lobby_chat::DrawInput(ctx, Rect{contentX, Fl(contentY + contentH - kRowH), leftW, kRowH});

    const Rect rightColumn{rightX, contentY, rightW, contentH};
    const float infoH = kCaptionH * static_cast<float>(kInfoRowCount);

    HeadingAt(ctx, Rect{rightX, contentY, rightW, kNameH},
               g_mapName && g_mapName[0] ? g_mapName : "No map", kAlignCentre, g_theme);
    g_previewBox = MapPreviewBox(rightColumn, infoH);
    DrawDownloadPanel(ctx, g_previewBox, prompt);
    DrawInfoColumn(ctx, Rect{rightX, Fl(contentY + contentH - infoH), rightW, infoH});

    DrawFooterButtons(ctx, window, footerY);

    if (blocked) g_disableEnd(ctx);
    DrawStartLockTooltip(ctx, window);
    g_spaceEnd(ctx);
    g_popupPrompt = prompt;
}

named_window::Result OnMultiplayerLobbyWindow(void* ctx) {

    g_settings.ReloadIfChanged();

    lobby_chat::OnFrame(GetTickCount());

    Rect window;
    const bool haveWindow = ctx && WindowRect(ctx, &window);
    const bool takeOver = g_ready && g_settings.GetBool(kKeyEnabled) && haveWindow;

    if (!takeOver) return named_window::Result::Continue;

    static bool announced = false;
    if (!announced) {
        announced = true;
        kLog.Info("drawing our screen (window %.0fx%.0f)", window.w, window.h);
    }

    DrawScreen(ctx, window);

    if (g_previewBox.w > 0.0f && g_selectedMapPath && g_selectedMapId && !g_downloadPanelShown &&
        !map_download::LobbyMapMissing()) {
        DrawMapPreviewAt(g_previewBox.x, g_previewBox.y, g_previewBox.w, g_selectedMapPath,
                          *g_selectedMapId, TilesetOverride(g_matchFlags ? *g_matchFlags : 0));
    }

    DrawSlotMarks(ctx);

    DrawDownloadPopup(g_popupPrompt);

    if (g_drawPopup && g_popup) {
        const unsigned int prev = g_popupSkin ? ui::PushSkin(*g_popupSkin) : 0;
        g_drawPopup(g_popup);
        if (g_popupSkin) ui::PopSkin(prev);
    }

    g_endWindow(ctx);

    map_download::PerformPendingLobbyLeave();
    return named_window::Result::Suppress;
}

void RegisterScreenSettings() {
    g_settings.BeginGroup("UI", "Enhanced UIs");

    g_settings.RegisterBool(kKeyEnabled, kDefaultEnabled, "Enhanced Multiplayer Lobby UI",
                             "The multiplayer lobby has a map preview, compact game info, "
                             "restructured UI, and additional host controls. A crown is shown "
                             "beside the host and a green dot beside other players with the mod.");

}

class MultiplayerLobbyScreenMod : public IMod {
public:
    const char* Name() const override { return "mplobby"; }

    bool Install() override {
        RegisterScreenSettings();
        g_settings.ReloadIfChanged();

        const uintptr_t base = Base();

        const bool kitReady = ui::Resolve();

        g_ppNkContext = kNkContextMenus.Get();
        g_spaceBegin = kLayoutSpaceBegin.Get();
        g_spaceEnd = kLayoutSpaceEnd.Get();
        g_endWindow = kEndCurrentWindow.Get();
        g_drawPopup = kDrawNkPopup.Get();
        g_menuButtonRaw = kDrawMenuButton.Get();
        g_skinPopupFrame = kSkinPopupFrame.Get();
        g_skinPopupFrameAlt = kSkinPopupFrameAlt.Get();
        g_skinAltFlag = kSkinAltFlag.Get();
        g_popup = kMpLobbyPopup.Get();
        g_popupSkin = kSkinMpLobbyPopup.Get();
        g_rowRatios = kLayoutRowRatios.Get();
        g_rowDynamic = kLayoutRowDynamic.Get();
        g_countLines = kCountWrappedLines.Get();
        g_wrappedLabel = kDrawWrappedLabel.Get();
        g_keyPressed = kInputKeyPressed.Get();
        g_spacing = kSpacing.Get();
        g_disableBegin = kDisableBegin.Get();
        g_disableEnd = kDisableEnd.Get();
        g_clock = kMillisecondClock.Get();

        g_createTexture = kCreateGameTexture.Get();
        g_nkDrawImage = kNkDrawImage.Get();

        g_slotRow = kMpLobbySlotRow.Get();
        g_leaveButton = kMpLobbyLeaveButton.Get();
        g_startButton = kMpLobbyStartGameButton.Get();
        g_sendChat = kMpLobbySendChat.Get();
        g_maxPlayers = kMpLobbyMaxPlayers.Get();

        g_theme.leftFrameSkin = kSkinLobbyCreationLeft.Get();
        g_theme.rightFrameSkin = kSkinLobbyCreationRight.Get();
        g_theme.textEditSkin = kSkinTextEdit.Get();
        g_theme.headingFont = kFontMultiplayerLobbyHeading.Get();
        g_chatSkin = kSkinMultiplayerLobbyChat.Get();
        g_chatTextSkin = kSkinMultiplayerLobbyText.Get();

        g_gameName = kMpLobbyGameName.Get();
        g_hostName = kMpLobbyHostName.Get();
        g_gameSpeed = kMpLobbyGameSpeed.Get();
        g_mapName = kMpLobbyMapNameText.Get();
        g_playerCount = kMpLobbyPlayerCount.Get();
        g_matchFlags = kMpLobbyMatchFlags.Get();
        g_slots = kMpLobbySlots.Get();
        g_slotNames = kMpLobbySlotNames.Get();
        g_countdownActive = kMpLobbyCountdownActive.Get();
        g_enterMs = kMpLobbyEnterMs.Get();
        g_fixedStart = kMpLobbyFixedStartLocation.Get();
        g_netSession = kNetSession.Get();

        g_resourceKeys = kMpLobbyResourceKeys.Get();
        g_tilesetKeys = kMpLobbyTilesetKeys.Get();
        g_teamOptions = kMpLobbyTeamOptions.Get();

        g_chatBegin = kMpLobbyChatBegin.Get();
        g_chatEnd = kMpLobbyChatEnd.Get();
        g_chatInput = kMpLobbyChatInput.Get();
        g_chatScrollRequest = kMpLobbyChatScrollRequest.Get();

        lobby_chat::Binding chat;
        chat.begin = g_chatBegin;
        chat.end = g_chatEnd;
        chat.input = g_chatInput;
        chat.scrollRequest = g_chatScrollRequest;
        chat.frameSkin = g_chatSkin;
        chat.textSkin = g_chatTextSkin;
        chat.countLines = g_countLines;
        chat.rowDynamic = g_rowDynamic;
        chat.wrappedLabel = g_wrappedLabel;
        chat.keyPressed = g_keyPressed;
        chat.sendChat = g_sendChat;
        chat.seatedName = &SeatedName;
        chat.theme = g_theme;
        lobby_chat::Bind(chat);

        g_selectedMapPath = kSelectedMapPath.Get();
        g_selectedMapId = kSelectedMapId.Get();

        g_ready = kitReady && g_ppNkContext && g_spaceBegin && g_spaceEnd && g_endWindow &&
                   g_rowRatios && g_rowDynamic && g_countLines && g_wrappedLabel &&
                   g_keyPressed && g_spacing && g_disableBegin && g_disableEnd && g_clock &&
                   g_countdownActive && g_enterMs && g_slots && g_slotNames && g_slotRow && g_leaveButton && g_startButton && g_sendChat &&
                   g_maxPlayers && g_theme.leftFrameSkin && g_theme.headingFont && g_gameName &&
                   g_hostName && g_mapName && g_matchFlags && g_netSession &&
                   g_chatBegin && g_chatEnd && g_chatInput;

        const bool registered = named_window::Register(kWindowName, &OnMultiplayerLobbyWindow);
        const bool localizedHooked =
            InstallHook(kLookupLocalized.Target(),
                         reinterpret_cast<void*>(&HookedLookupLocalized),
                         reinterpret_cast<void**>(&g_realLocalized),
                         "LookupLocalizedString (multiplayer lobby team numbers)");

        if (!localizedHooked) g_realLocalized = kLookupLocalized.Get();

        kLog.Info("%s, replacement screen %s%s",
                  g_ready ? "ready" : "NOT ready (an address failed to resolve)",
                  g_settings.GetBool(kKeyEnabled) ? "ON" : "off",
                  localizedHooked ? "" : " (team numbers unavailable)");
        return registered;
    }
};

}  // namespace

MOD_REGISTER(MultiplayerLobbyScreenMod)
