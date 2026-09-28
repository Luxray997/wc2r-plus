// SPDX-License-Identifier: MIT
#include "features/ui/screen_kit.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "core/log.h"
#include "features/ui/reload_image.h"
#include "target/addresses.h"
#include "target/struct_offsets.h"

namespace ui {
namespace {

using namespace game;

bool g_ready = false;

const Logger kLog{"ui"};

CreateGameTextureFn g_createTexture = nullptr;
NkDrawImageFn g_nkDrawImage = nullptr;
GameTexture* g_reloadTexture = nullptr;
bool g_reloadTextureFailed = false;

void** g_ppNkContext = nullptr;
OverrideNextRectFn g_rect = nullptr;
BeginNamedGroupFn g_beginGroup = nullptr;
EndGroupFn g_endGroup = nullptr;
LayoutRowDynamicFn g_row = nullptr;
DrawLabelFn g_label = nullptr;
DrawWrappedLabelFn g_wrappedLabel = nullptr;
DrawBannerFn g_banner = nullptr;
LookupLocalizedFn g_localized = nullptr;
FormatLocalizedFn g_format = nullptr;
ButtonLabelFn g_buttonLabel = nullptr;
DrawMenuButtonFn g_menuButton = nullptr;
TextEditFn g_textEdit = nullptr;
void* g_textEditFilter = nullptr;
DisableScopeFn g_disableBegin = nullptr;
DisableScopeFn g_disableEnd = nullptr;
NkFillRectFn g_fillRect = nullptr;
DrawLobbyDropdownFn g_dropdown = nullptr;
DrawLobbyDropdownFn g_folderDropdown = nullptr;
SetSkinFn g_setSkin = nullptr;
SetFontFn g_setFont = nullptr;
void* g_styleManager = nullptr;
StringAssignFn g_stringAssign = nullptr;
NkLayoutWidgetSpaceFn g_widgetSpace = nullptr;
LobbySlotDropdownFn g_slotDropdown = nullptr;

}  // namespace

bool Resolve() {

    if (g_ready) return true;

    g_ppNkContext = kNkContextMenus.Get();
    g_rect = kOverrideNextRect.Get();
    g_beginGroup = kBeginNamedGroup.Get();
    g_endGroup = kEndGroup.Get();
    g_row = kLayoutRowDynamic.Get();
    g_label = kDrawLabel.Get();
    g_wrappedLabel = kDrawWrappedLabel.Get();
    g_banner = kDrawBanner.Get();
    g_localized = kLookupLocalized.Get();
    g_format = kFormatLocalized.Get();
    g_buttonLabel = kButtonLabel.Get();
    g_menuButton = kDrawMenuButton.Get();
    g_textEdit = kTextEdit.Get();
    g_textEditFilter = kTextEditFilter.Get();
    g_disableBegin = kDisableBegin.Get();
    g_disableEnd = kDisableEnd.Get();
    g_fillRect = kNkFillRect.Get();
    g_dropdown = kDrawLobbyDropdown.Get();
    g_folderDropdown = kDrawFolderDropdown.Get();
    g_setSkin = kSetSkin.Get();
    g_setFont = kSetFont.Get();
    g_styleManager = kSkinManager.Get();
    g_stringAssign = kStringAssign.Get();
    g_widgetSpace = kNkLayoutWidgetSpace.Get();

    g_createTexture = kCreateGameTexture.Get();
    g_nkDrawImage = kNkDrawImage.Get();

    g_slotDropdown = kLobbySlotDropdown.Get();

    g_ready = g_ppNkContext && g_rect && g_beginGroup && g_endGroup && g_row && g_label &&
               g_wrappedLabel && g_banner && g_localized && g_format && g_buttonLabel &&
               g_menuButton && g_textEdit && g_disableBegin && g_disableEnd && g_fillRect &&
               g_dropdown && g_folderDropdown && g_setSkin && g_setFont && g_styleManager &&
               g_stringAssign && g_widgetSpace;
    return g_ready;
}

bool Ready() { return g_ready; }

Rect LobbyFrame(const Rect& window) {
    const float footerY = window.h - kFooterBottomGap;

    const float bottom = Fl(footerY + kFooterH * 0.5f + kFrameBorderAboveBottom);
    return Rect{kSideMargin, kContentTop, window.w - kSideMargin * 2.0f, bottom - kContentTop};
}

const char* StrData(const void* s) {
    if (!s) return "";
    const uint8_t* p = static_cast<const uint8_t*>(s);
    const uint32_t cap = *reinterpret_cast<const uint32_t*>(p + msvc::kStrCapacity);
    const char* data = (cap > msvc::kStrSsoMax) ? *reinterpret_cast<const char* const*>(p)
                                              : reinterpret_cast<const char*>(p);
    return data ? data : "";
}

std::string GameStr(const void* s) { return std::string(StrData(s)); }

void StrSet(void* s, const char* value) {
    if (g_stringAssign && s && value) g_stringAssign(s, value, strlen(value));
}

KeyList Keys(const void* vec) {
    KeyList out;
    if (!vec) return out;
    const char* const* const* p = static_cast<const char* const* const*>(vec);
    out.begin = p[0];
    out.end = p[1];
    return out;
}

std::vector<std::string> FolderNames(const void* folderVec, bool skipParentEntry) {
    std::vector<std::string> out;
    if (!folderVec) return out;
    uint8_t* const* p = static_cast<uint8_t* const*>(folderVec);
    uint8_t* begin = p[0];
    uint8_t* end = p[1];
    if (!begin || !end || end <= begin) return out;
    for (uint8_t* s = begin; s + msvc::kStrStride <= end; s += msvc::kStrStride) {
        const char* name = StrData(s);
        if (!name[0]) continue;
        if (skipParentEntry && strcmp(name, "..") == 0) continue;
        out.push_back(name);
    }
    return out;
}

unsigned int PushSkin(unsigned int skinId) {
    if (!g_setSkin || !g_styleManager) return 0;
    return g_setSkin(g_styleManager, skinId);
}
void PopSkin(unsigned int previous) {
    if (g_setSkin && g_styleManager) g_setSkin(g_styleManager, previous);
}
unsigned int PushFont(unsigned int fontId) {
    if (!g_setFont || !g_styleManager) return 0;
    return g_setFont(g_styleManager, fontId);
}
void PopFont(unsigned int previous) {
    if (g_setFont && g_styleManager) g_setFont(g_styleManager, previous);
}

float ReadCtxFloat(void* ctx, size_t offset) {
    return nk::CtxFloat(ctx, offset);
}
void SetRowSpacing(void* ctx, float spacing) {
    if (!ctx) return;
    *reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(ctx) + nk::kCtxSpacingY) = spacing;
}
unsigned int ReadCtxColour(void* ctx, size_t offset) {
    unsigned int v = 0;
    if (!ctx) return v;
    memcpy(&v, reinterpret_cast<const uint8_t*>(ctx) + offset, 4);
    return v;
}
void WriteCtxColour(void* ctx, size_t offset, unsigned int v) {
    if (!ctx) return;
    memcpy(reinterpret_cast<uint8_t*>(ctx) + offset, &v, 4);
}

void SetScrollOffsetY(void* panel, float value) {
    if (!panel) return;
    unsigned int* p = *reinterpret_cast<unsigned int**>(reinterpret_cast<uint8_t*>(panel) +
                                                         nk::kPanelOffsetY);
    if (p) *p = value > 0.0f ? static_cast<unsigned int>(value) : 0u;
}

float NextRowTop(void* ctx) {
    void* panel = CurrentPanel(ctx);
    if (!panel) return 0.0f;
    return PanelFloat(panel, nk::kPanelAtY) + PanelFloat(panel, nk::kPanelRowHeight);
}

bool LastRowFirstCell(void* ctx, Rect* out) {
    void* panel = CurrentPanel(ctx);
    if (!panel || !out || !g_widgetSpace) return false;
    uint8_t* p = reinterpret_cast<uint8_t*>(panel);
    int index;
    float itemOffset;
    memcpy(&index, p + nk::kPanelRowIndex, 4);
    memcpy(&itemOffset, p + nk::kPanelRowItemOffset, 4);
    if (index <= 0) return false;

    const int zeroIndex = 0;
    const float zeroOffset = 0.0f;
    memcpy(p + nk::kPanelRowIndex, &zeroIndex, 4);
    memcpy(p + nk::kPanelRowItemOffset, &zeroOffset, 4);
    float b[4] = {};
    g_widgetSpace(b, ctx, nullptr, 0);
    memcpy(p + nk::kPanelRowIndex, &index, 4);
    memcpy(p + nk::kPanelRowItemOffset, &itemOffset, 4);

    *out = Rect{b[0], b[1], b[2], b[3]};
    return b[2] > 0.0f && b[3] > 0.0f;
}

bool WindowRect(void* ctx, Rect* out) {
    void* panel = CurrentPanel(ctx);
    if (!panel || !out) return false;
    const float* clip = reinterpret_cast<const float*>(reinterpret_cast<uint8_t*>(panel) +
                                                        nk::kPanelClipX);
    *out = Rect{clip[0], clip[1], clip[2], clip[3]};
    return out->w > 100.0f && out->h > 100.0f;
}

void SetRect(void* ctx, const Rect& r) {
    if (g_rect) g_rect(ctx, r.x, r.y, r.w, r.h);
}

void LabelAt(void* ctx, const Rect& r, const char* text, int align) {
    if (!g_label) return;
    SetRect(ctx, r);
    g_label(ctx, text ? text : "", align);
}

void Label(void* ctx, const char* text, int align) {
    if (!g_label) return;
    g_label(ctx, text ? text : "", align);
}

void HeadingAt(void* ctx, const Rect& r, const char* text, int align, const ScreenTheme& theme) {
    const unsigned int prevFont = theme.headingFont ? PushFont(*theme.headingFont) : 0;
    LabelAt(ctx, r, text, align);
    if (theme.headingFont) PopFont(prevFont);
}

bool ButtonAt(void* ctx, const Rect& r, const char* text, bool greyed) {
    if (!g_menuButton) return false;
    if (greyed && g_disableBegin) g_disableBegin(ctx);
    SetRect(ctx, r);
    const bool clicked = g_menuButton(ctx, text ? text : "") != 0;
    if (greyed && g_disableEnd) g_disableEnd(ctx);
    return clicked && !greyed;
}

void LockedBoxAt(void* ctx, const Rect& r, const char* text, const char* id) {
    if (!text) text = "";
    if (!g_slotDropdown || !id) {
        ButtonAt(ctx, r, text, true);
        return;
    }

    struct {
        union {
            char inlineText[16];
            const char* heap;
        };
        uint32_t size;
        uint32_t capacity;
    } view{};
    static_assert(sizeof(view) == 0x18, "MSVC std::string is 24 bytes");
    const size_t len = strlen(text);
    view.size = static_cast<uint32_t>(len);
    if (len <= msvc::kStrSsoMax) {
        memcpy(view.inlineText, text, len + 1);
        view.capacity = msvc::kStrSsoMax;
    } else {
        view.heap = text;
        view.capacity = static_cast<uint32_t>(len);
    }
    SetRect(ctx, r);
    char unusedFlag = 0;

    g_slotDropdown(1, 0, id, &view, nullptr, &unusedFlag, r.h, 0);
}

void WrappedLabelAt(void* ctx, const Rect& r, const char* text) {
    if (!g_wrappedLabel || !text || !text[0]) return;
    SetRect(ctx, r);
    g_wrappedLabel(ctx, text, ReadCtxColour(ctx, nk::kCtxTextColour));
}

void PanelFrame(void* ctx, const Rect& r, const char* name, const unsigned int* skin) {
    if (!g_beginGroup || !g_endGroup) return;
    const unsigned int prev = skin ? PushSkin(*skin) : 0;
    SetRect(ctx, r);
    const int opened = g_beginGroup(ctx, name, kGroupFlags);
    if (skin) PopSkin(prev);
    if (opened) g_endGroup(ctx);
}

bool TextEditAt(void* ctx, const Rect& r, char* buf, int maxLen, const ScreenTheme& theme) {
    if (!g_textEdit || !buf) return false;
    const float spacingBefore = ReadCtxFloat(ctx, nk::kCtxSpacingY);
    SetRect(ctx, r);
    const unsigned int prev = theme.textEditSkin ? PushSkin(*theme.textEditSkin) : 0;
    SetRowSpacing(ctx, spacingBefore);
    const unsigned int state = g_textEdit(ctx, kTextEditFlags, buf, maxLen, g_textEditFilter);
    if (theme.textEditSkin) PopSkin(prev);
    SetRowSpacing(ctx, spacingBefore);
    return (state & 1u) != 0;
}

bool FilterFieldAt(void* ctx, const Rect& r, char* buf, int maxLen, const char* placeholder,
                    const ScreenTheme& theme) {
    const bool focused = TextEditAt(ctx, r, buf, maxLen, theme);
    if (!focused && buf && buf[0] == '\0' && placeholder) {
        const unsigned int prevColour = ReadCtxColour(ctx, nk::kCtxTextColour);
        WriteCtxColour(ctx, nk::kCtxTextColour, kPlaceholderColour);

        LabelAt(ctx, Rect{r.x + kGap, r.y, r.w - kGap, r.h}, placeholder, kAlignLeft);
        WriteCtxColour(ctx, nk::kCtxTextColour, prevColour);
    }
    return focused;
}

void DrawUpArrow(void* ctx, const Rect& button, bool greyed) {
    if (!g_fillRect) return;
    void* canvas = WindowCanvas(ctx);
    if (!canvas) return;

    unsigned int colour = ReadCtxColour(ctx, nk::kCtxTextColour);
    if (greyed) colour = (colour & 0x00ffffffu) | (0x60u << 24);

    const int steps = 9;
    const float side = (std::min)(button.w, button.h) * 0.46f;
    const float cx = button.x + button.w * 0.5f;
    const float top = button.y + (button.h - side) * 0.5f;
    const float stepH = side / static_cast<float>(steps);
    for (int i = 0; i < steps; ++i) {

        const float t = static_cast<float>(i) / static_cast<float>(steps - 1);
        const float headW = side * t;
        const float shaftW = side * 0.34f;
        const float w = (i < steps * 2 / 3) ? headW : shaftW;
        if (w <= 0.0f) continue;
        g_fillRect(canvas, cx - w * 0.5f, top + stepH * i, w, stepH + 1.0f, 0.0f, colour);
    }
}

constexpr float kReloadIconFraction = 0.58f;
constexpr float kReloadIconOffsetY = -2.5f;

bool EnsureReloadTexture() {
    if (g_reloadTexture) return true;
    if (g_reloadTextureFailed || !g_createTexture || !g_nkDrawImage) return false;

    GameTexture* tex = nullptr;
    g_createTexture(&tex, kReloadImageWidth, kReloadImageHeight, kReloadImageRgba,
                    kGameTextureFormatRgba8, kGameTextureFilterLinear);
    if (!tex || tex->glName == 0 || tex->w != kReloadImageWidth || tex->h != kReloadImageHeight) {
        g_reloadTextureFailed = true;
        kLog.Warn("reload icon texture failed (%p name=%u %dx%d) -- drawing rects", tex,
                  tex ? tex->glName : 0u, tex ? tex->w : 0, tex ? tex->h : 0);
        return false;
    }
    g_reloadTexture = tex;
    kLog.Info("reload icon texture created %dx%d GL name=%u", tex->w, tex->h, tex->glName);
    return true;
}

void DrawReloadRects(void* canvas, const Rect& box, unsigned int colour) {
    if (!g_fillRect) return;
    const float cx = box.x + box.w * 0.5f;
    const float cy = box.y + box.h * 0.5f;
    const float radius = box.w * 0.5f;
    const float thick = (std::max)(2.0f, box.w * 0.17f);

    constexpr float kTau = 6.28318531f;
    constexpr float kGapTurns = 0.13f;
    constexpr int kSegments = 30;
    for (int i = 0; i <= kSegments; ++i) {
        const float turn = kGapTurns + (1.0f - kGapTurns) * static_cast<float>(i) /
                                            static_cast<float>(kSegments);
        g_fillRect(canvas, cx + radius * sinf(turn * kTau) - thick * 0.5f,
                   cy - radius * cosf(turn * kTau) - thick * 0.5f, thick, thick, 0.0f, colour);
    }

    const float headLen = thick * 2.4f;
    const float headHalf = thick * 1.5f;
    constexpr int kHeadSteps = 7;
    for (int i = 0; i < kHeadSteps; ++i) {
        const float half = headHalf * (1.0f - static_cast<float>(i) / kHeadSteps);
        const float colW = headLen / static_cast<float>(kHeadSteps);
        g_fillRect(canvas, cx - headLen * 0.45f + colW * i, cy - radius - half, colW + 1.0f,
                   half * 2.0f, 0.0f, colour);
    }
}

void DrawReloadIcon(void* ctx, const Rect& button, bool greyed) {
    void* canvas = WindowCanvas(ctx);
    if (!canvas) return;

    const float side = (std::min)(button.w, button.h) * kReloadIconFraction;
    const Rect box{Fl(button.x + (button.w - side) * 0.5f),
                   Fl(button.y + (button.h - side) * 0.5f + kReloadIconOffsetY), side, side};

    if (EnsureReloadTexture()) {
        NkImage img;
        img.handle = g_reloadTexture;

        img.w = static_cast<uint16_t>(kReloadImageWidth);
        img.h = static_cast<uint16_t>(kReloadImageHeight);
        img.region[0] = 0;
        img.region[1] = 0;
        img.region[2] = static_cast<uint16_t>(kReloadImageWidth);
        img.region[3] = static_cast<uint16_t>(kReloadImageHeight);

        g_nkDrawImage(canvas, box.x, box.y, box.w, box.h, &img,
                      greyed ? Rgba(255, 255, 255, 0x60) : Rgba(255, 255, 255, 255));
        return;
    }

    unsigned int colour = ReadCtxColour(ctx, nk::kCtxTextColour);
    if (greyed) colour = (colour & 0x00ffffffu) | (0x60u << 24);
    DrawReloadRects(canvas, box, colour);
}

bool BeginList(void* ctx, const Rect& r, const char* name, unsigned int flags) {
    if (!g_beginGroup) return false;
    SetRect(ctx, r);
    return g_beginGroup(ctx, name, flags) != 0;
}

void EndList(void* ctx) {
    if (g_endGroup) g_endGroup(ctx);
}

bool ListRowButton(void* ctx, const char* label) {
    if (g_row) g_row(ctx, kRowH, 1);
    return g_buttonLabel && g_buttonLabel(ctx, label ? label : "") != 0;
}

bool ContainsNoCase(const char* haystack, const char* needle) {
    if (!needle || !needle[0]) return true;
    if (!haystack) return false;
    const size_t n = strlen(needle);
    const size_t h = strlen(haystack);
    if (n > h) return false;
    for (size_t i = 0; i + n <= h; ++i) {
        if (_strnicmp(haystack + i, needle, n) == 0) return true;
    }
    return false;
}

void FillRect(void* canvas, float x, float y, float w, float h, float rounding, unsigned int col) {
    if (g_fillRect && canvas) g_fillRect(canvas, x, y, w, h, rounding, col);
}

const char* Localized(const char* key) { return g_localized ? g_localized(key) : key; }

const char* FormatLocalized(const char* key, const char* arg) {
    return g_format ? g_format(key, arg) : key;
}

void DrawBanner() {
    if (g_banner) g_banner();
}

int TilesetIndexForKey(const char* key) {
    static const struct {
        const char* key;
        int index;
    } kTilesetKeys[] = {
        {"customscenario_tileset_forest", 0},
        {"customscenario_tileset_winter", 1},
        {"customscenario_tileset_wasteland", 2},
        {"customscenario_tileset_orcswamp", 3},
    };
    if (!key) return -1;
    for (const auto& k : kTilesetKeys) {
        if (strcmp(key, k.key) == 0) return k.index;
    }
    return -1;
}

bool Dropdown(const char* comboId, void* current, const void* options, uint8_t* openFlag) {
    if (!g_dropdown) return false;
    return g_dropdown(comboId, current, options, openFlag, kRowH) != 0;
}

bool FolderDropdown(const char* comboId, void* current, const void* options, uint8_t* openFlag) {
    if (!g_folderDropdown) return false;
    return g_folderDropdown(comboId, current, options, openFlag, kRowH) != 0;
}

}  // namespace ui
