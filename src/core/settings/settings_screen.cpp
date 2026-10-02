// SPDX-License-Identifier: MIT
#include "core/settings/settings_screen.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "MinHook.h"
#include "core/hook_utils.h"
#include "core/log.h"
#include "core/mod.h"
#include "core/settings/mod_settings.h"
#include "core/settings/settings_registry.h"
#include "core/settings/native_settings_catalog.h"
#include "target/addresses.h"
#include "target/struct_offsets.h"

namespace {

using namespace game;

const Logger kLog{"settings-screen"};

float ReadCtxFloat(void* ctx, size_t offset) {
    if (!ctx) return 0.0f;
    return *reinterpret_cast<const float*>(reinterpret_cast<const unsigned char*>(ctx) + offset);
}

void SetRowSpacing(void* ctx, float spacing) {
    if (!ctx) return;
    *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(ctx) + nk::kCtxSpacingY) = spacing;
}

void* CurrentPanel(void* ctx) {
    if (!ctx) return nullptr;
    void* win =
        *reinterpret_cast<void**>(reinterpret_cast<unsigned char*>(ctx) + nk::kCtxCurrentWindow);
    if (!win) return nullptr;
    return *reinterpret_cast<void**>(reinterpret_cast<unsigned char*>(win) + nk::kWindowPanel);
}

float ReadPanelFloat(void* ctx, size_t offset) {
    void* panel = CurrentPanel(ctx);
    if (!panel) return -1.0f;
    return *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(panel) + offset);
}
constexpr unsigned int kNkWindowNoInput = 0x1000;

unsigned int CurrentPanelFlags(void* ctx) {
    void* panel = CurrentPanel(ctx);
    if (!panel) return 0;
    return *reinterpret_cast<unsigned int*>(reinterpret_cast<unsigned char*>(panel) + nk::kPanelFlags);
}

constexpr unsigned int kTextEditFlags = 0x260;

constexpr int kAlignTitle = 0x12;
constexpr int kAlignRow = 0x11;

constexpr unsigned int kGroupNoScrollbar = 0x20;
constexpr unsigned int kGroupBorder = 0x01;

constexpr int kGameStateMainMenu = 0x04;

constexpr uint32_t kPauseScreenRoot = 1;
constexpr uint32_t kPauseScreenOptions = 7;

constexpr float kUiSpaceHeight = 2160.0f;

constexpr float kPanelTop = 320.0f;
constexpr float kPanelHeight = 1580.0f;
constexpr float kFooterHeight = 150.0f;
constexpr float kGap = 20.0f;

constexpr float kPanelWidthPercent = 80.0f;
constexpr float kCategoryPanelPercent = 30.0f;

constexpr float kPanelPad = 34.0f;
constexpr float kPanelPadTop = 28.0f;

constexpr float kRowHeight = 90.0f;
constexpr float kCategoryRowHeight = 120.0f;

constexpr float kRowSpacing = 16.0f;

constexpr float kTextRowSpacing = 40.0f;

constexpr float kSearchRowHeight = 120.0f;

constexpr float kTitleTop = 150.0f;
constexpr float kTitleHeight = 120.0f;

BuildOptionsMenuScreenFn g_realBuildOptionsMenuScreen = nullptr;
BeginNamedWindowFn g_beginWindow = nullptr;
EndCurrentWindowFn g_endWindow = nullptr;
GetUiSpaceWidthFn g_uiWidth = nullptr;
OverrideNextRectFn g_rect = nullptr;
BeginNamedGroupFn g_beginGroup = nullptr;
DoScrollbarVFn g_realDoScrollbarV = nullptr;
EndGroupFn g_endGroup = nullptr;
LayoutSpaceBeginFn g_spaceBegin = nullptr;
LayoutSpaceEndFn g_spaceEnd = nullptr;
LayoutRowDynamicFn g_row = nullptr;
DrawLabelFn g_label = nullptr;
DrawMenuButtonFn g_button = nullptr;
DrawBannerFn g_banner = nullptr;
SliderU32Fn g_slider = nullptr;
FlagCheckboxFn g_checkbox = nullptr;
RadioFn g_radio = nullptr;
TextEditFn g_textEdit = nullptr;
void* g_textEditFilter = nullptr;
ComboBeginFn g_comboBegin = nullptr;
ComboItemFn g_comboItem = nullptr;
ComboScopeFn g_comboClose = nullptr;
ComboScopeFn g_comboEnd = nullptr;
DisplayCountFn g_displayCount = nullptr;
const unsigned int* g_skinComboClosed = nullptr;
const unsigned int* g_skinComboClosedAlt = nullptr;
const unsigned int* g_skinComboOpen = nullptr;
const unsigned int* g_skinComboOpenAlt = nullptr;
NkFillRectFn g_fillRect = nullptr;
CountWrappedLinesFn g_countWrappedLines = nullptr;
DrawWrappedLabelFn g_drawWrappedLabel = nullptr;
DisableScopeFn g_disableBegin = nullptr;
DisableScopeFn g_disableEnd = nullptr;
SetSkinFn g_setSkin = nullptr;
void* g_skinManager = nullptr;
const unsigned int* g_skinWindow = nullptr;
const unsigned int* g_skinWindowAlt = nullptr;
const unsigned char* g_skinAltFlag = nullptr;
const unsigned int* g_skinContentPanel = nullptr;
const unsigned int* g_skinTextEdit = nullptr;
LookupLocalizedFn g_lookupLocalized = nullptr;
TransitionGameStateFn g_transitionState = nullptr;
uint32_t* g_pauseMenuScreen = nullptr;
void** g_ppNkContext = nullptr;

constexpr const char* kKeyHideScrollbarArrows = "hidescrollbararrows";

constexpr bool kDefaultHideScrollbarArrows = true;

constexpr bool kScreenEnabled = true;

constexpr bool kTooltipsEnabled = true;

ModSettings g_settings{"settings"};

std::string g_category;
DWORD g_lastFrame = 0;
std::vector<std::string> g_categoryCache;
constexpr DWORD kReentryGapMs = 250;

struct Snapshot {
    bool valid = false;
    std::vector<double> nums;
    std::vector<std::string> strs;
};
Snapshot g_snapshot;

bool g_dirty = false;

void TakeSnapshot() {
    SettingsRegistry& core = SettingsRegistry::Get();
    g_snapshot.nums.clear();
    g_snapshot.strs.clear();
    for (int i = 0; i < core.Count(); ++i) {
        g_snapshot.nums.push_back(core.GetNum(i));
        g_snapshot.strs.push_back(core.GetStr(i));
    }
    g_snapshot.valid = true;
}

void RestoreSnapshot() {
    if (!g_snapshot.valid) return;
    SettingsRegistry& core = SettingsRegistry::Get();
    for (int i = 0; i < core.Count() && i < (int)g_snapshot.nums.size(); ++i) {
        if (core.GetNum(i) != g_snapshot.nums[i]) core.SetNum(i, g_snapshot.nums[i]);
        if (core.GetStr(i) != g_snapshot.strs[i]) core.SetStr(i, g_snapshot.strs[i]);
    }
}

void SaveEverything() {
    for (ModSettings* s : ModSettingsList::Get().All()) s->Save();
    SettingsRegistry::Get().PersistNative();
    g_dirty = false;
}

unsigned int PushSkin(unsigned int skinId) {
    if (!g_setSkin || !g_skinManager) return 0;
    return g_setSkin(g_skinManager, skinId);
}
void PopSkin(unsigned int previous) {
    if (g_setSkin && g_skinManager) g_setSkin(g_skinManager, previous);
}

unsigned int WindowSkin() {
    if (!g_skinWindow || !g_skinWindowAlt || !g_skinAltFlag) return 0;
    return (*g_skinAltFlag == 1) ? *g_skinWindowAlt : *g_skinWindow;
}

int BeginPanelGroup(void* ctx, const char* name, unsigned int flags) {
    if (!g_beginGroup) return 0;
    const unsigned int prev = g_skinContentPanel ? PushSkin(*g_skinContentPanel) : 0;
    const int opened = g_beginGroup(ctx, name, flags);
    if (g_skinContentPanel) PopSkin(prev);
    return opened;
}

int BeginPaddedPanel(void* ctx, const char* frameName, const char* contentName, float x, float y,
                      float w, float h, unsigned int contentFlags,
                      bool suppressScrollbarWidth = false) {
    const float pad = kPanelPad;
    const float top = pad + kPanelPadTop;
    g_rect(ctx, x, y, w, h);
    if (!BeginPanelGroup(ctx, frameName, kGroupNoScrollbar)) return 0;

    g_spaceBegin(ctx, 1, 0.0f, 1000);
    g_rect(ctx, pad, top, w - pad * 2.0f, h - top - pad);
    float* scrollbarX =
        reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(ctx) + nk::kCtxScrollbarSize);
    const float savedScrollbarX = *scrollbarX;
    if (suppressScrollbarWidth) *scrollbarX = 0.0f;
    const int contentOpened = g_beginGroup(ctx, contentName, contentFlags);
    *scrollbarX = savedScrollbarX;
    if (!contentOpened) {
        g_spaceEnd(ctx);
        g_endGroup(ctx);
        return 0;
    }
    return 1;
}

void EndPaddedPanel(void* ctx) {
    g_endGroup(ctx);
    g_spaceEnd(ctx);
    g_endGroup(ctx);
}

void Row(void* ctx, float h, int cols) {
    if (g_row) g_row(ctx, h, cols);
}
void Label(void* ctx, const char* text, int align) {
    if (g_label) g_label(ctx, text, align);
}
bool Button(void* ctx, const char* text) {
    return g_button && g_button(ctx, text) != 0;
}
bool Slider(uint32_t* value, uint32_t lo, uint32_t hi) {
    if (!g_slider || hi <= lo) return false;
    uint32_t loV = lo, hiV = hi, step = 1;
    return g_slider(value, &loV, &hiV, &step);
}

bool BoolCheckbox(const char* text, bool* value) {
    if (!g_checkbox) return false;
    uint32_t scratch = *value ? 1u : 0u;
    if (!g_checkbox(text, &scratch, 1u)) return false;
    *value = (scratch & 1u) != 0;
    return true;
}

bool Radio(const char* text, bool selected) {
    if (!g_radio) return false;
    uint8_t scratch = selected ? 1 : 0;
    const bool clicked = g_radio(text, &scratch);
    return clicked && scratch != 0;
}

bool TextEdit(void* ctx, char* buf, int maxLen) {
    if (!g_textEdit) return false;

    const float spacingBefore = ReadCtxFloat(ctx, nk::kCtxSpacingY);
    const unsigned int prev = g_skinTextEdit ? PushSkin(*g_skinTextEdit) : 0;
    SetRowSpacing(ctx, spacingBefore);
    const unsigned int state = g_textEdit(ctx, kTextEditFlags, buf, maxLen, g_textEditFilter);
    if (g_skinTextEdit) PopSkin(prev);
    SetRowSpacing(ctx, spacingBefore);
    return (state & 1u) != 0;
}

bool ContainsNoCase(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;
    for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        if (_strnicmp(haystack.c_str() + i, needle.c_str(), needle.size()) == 0) return true;
    }
    return false;
}

constexpr int kEditBufferSize = 256;
struct EditBuffer {
    int index = -1;
    char text[kEditBufferSize] = {0};
};
std::vector<EditBuffer> g_editBuffers;

char* EditBufferFor(int index, const std::string& current) {
    for (EditBuffer& b : g_editBuffers) {
        if (b.index == index) return b.text;
    }
    g_editBuffers.push_back(EditBuffer{});
    EditBuffer& b = g_editBuffers.back();
    b.index = index;
    strncpy_s(b.text, current.c_str(), _TRUNCATE);
    return b.text;
}

constexpr const char* kSearchCategory = "Search";
char g_searchText[128] = {0};

const char* LabelText(const SettingDef& d) {
    if (!g_lookupLocalized || d.label.empty()) return d.label.c_str();
    const char* shown = g_lookupLocalized(d.label.c_str());
    return shown ? shown : d.label.c_str();
}

const char* OptionText(const SettingDef& d, int i) {
    if (i < 0 || i >= (int)d.options.size()) return "";
    const std::string& raw = d.options[(size_t)i];
    if (!g_lookupLocalized || raw.empty()) return raw.c_str();
    const char* shown = g_lookupLocalized(raw.c_str());
    return shown ? shown : raw.c_str();
}

constexpr float kComboItemHeight = 110.0f;
constexpr float kComboPopupPad = 40.0f;
constexpr float kComboPopupMaxHeight = 900.0f;

std::vector<unsigned char> g_comboOpen;

int g_combosOpenThisFrame = 0;

unsigned char* ComboOpenSlot(int settingIndex) {
    if (settingIndex < 0) return nullptr;
    if ((int)g_comboOpen.size() <= settingIndex) g_comboOpen.resize((size_t)settingIndex + 1, 0);
    return &g_comboOpen[(size_t)settingIndex];
}

unsigned int ComboSkin(bool open) {
    const bool alt = g_skinAltFlag && *g_skinAltFlag == 1;
    if (open) {
        if (!g_skinComboOpen || !g_skinComboOpenAlt) return 0;
        return alt ? *g_skinComboOpenAlt : *g_skinComboOpen;
    }
    if (!g_skinComboClosed || !g_skinComboClosedAlt) return 0;
    return alt ? *g_skinComboClosedAlt : *g_skinComboClosed;
}

bool ComboBegin(void* ctx, int settingIndex, const char* selected, float w, float h) {
    if (!g_comboBegin) return false;
    unsigned char* open = ComboOpenSlot(settingIndex);
    const float spacingBefore = ReadCtxFloat(ctx, nk::kCtxSpacingY);
    const unsigned int prev = PushSkin(ComboSkin(open && *open != 0));
    SetRowSpacing(ctx, spacingBefore);
    const int opened = g_comboBegin(ctx, selected, kAlignRow, w, h);
    PopSkin(prev);
    SetRowSpacing(ctx, spacingBefore);
    if (open) *open = opened ? 1 : 0;
    if (opened) ++g_combosOpenThisFrame;
    return opened != 0;
}

void RefreshDisplayMonitorOptions() {
    if (!g_displayCount) return;
    SettingsRegistry& core = SettingsRegistry::Get();
    const int idx = core.Find("game", "display_monitor");
    if (idx < 0) return;
    const int count = g_displayCount();

    if (count < 1 || count > 16) return;

    std::vector<std::string> names;
    for (int i = 0; i < count; ++i) {
        char buf[32];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "Display %d", i + 1);
        names.push_back(buf);
    }
    core.SetOptions(idx, std::move(names));
}

std::vector<std::string> Categories() {
    std::vector<std::string> out;
    SettingsRegistry& core = SettingsRegistry::Get();
    for (int i = 0; i < core.Count(); ++i) {
        if (core.DefAt(i).hidden) continue;
        const std::string& c = core.DefAt(i).category;
        bool seen = false;
        for (const std::string& existing : out) {
            if (_stricmp(existing.c_str(), c.c_str()) == 0) { seen = true; break; }
        }
        if (!seen) out.push_back(c);
    }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        return _stricmp(a.c_str(), b.c_str()) < 0;
    });
    return out;
}

struct SubGroup {
    std::string name;
    std::vector<int> items;
};

std::vector<SubGroup> GroupBySubcategory(const std::string& category) {
    std::vector<SubGroup> groups;
    SettingsRegistry& core = SettingsRegistry::Get();
    for (int i = 0; i < core.Count(); ++i) {
        const SettingDef& d = core.DefAt(i);
        if (d.hidden) continue;
        if (_stricmp(d.category.c_str(), category.c_str()) != 0) continue;

        SubGroup* found = nullptr;
        for (SubGroup& g : groups) {
            if (_stricmp(g.name.c_str(), d.subcategory.c_str()) == 0) {
                found = &g;
                break;
            }
        }
        if (!found) {
            groups.push_back(SubGroup{d.subcategory, {}});
            found = &groups.back();
        }
        found->items.push_back(i);
    }
    return groups;
}

bool CategoryListFits(void* ctx, size_t count, float h) {
    if (count == 0) return true;
    const float spacingY = ReadCtxFloat(ctx, nk::kCtxSpacingY);
    const float groupPadY = ReadCtxFloat(ctx, nk::kCtxGroupPaddingY);
    const float needed = (float)count * kCategoryRowHeight + (float)(count - 1) * spacingY;
    const float available = h - (kPanelPad + kPanelPadTop) - kPanelPad - groupPadY * 2.0f;
    return needed <= available;
}

void DrawCategoryPanel(void* ctx, const std::vector<std::string>& categories) {
    Row(ctx, kCategoryRowHeight, 1);
    for (const std::string& c : categories) {

        const bool current = _stricmp(c.c_str(), g_category.c_str()) == 0;
        if (current && g_disableBegin) g_disableBegin(ctx);
        if (Button(ctx, c.c_str()) && !current) g_category = c;
        if (current && g_disableEnd) g_disableEnd(ctx);
    }
}

constexpr float kTipMaxWidth = 1100.0f;
constexpr float kTipPad = 26.0f;
constexpr float kTipBorder = 4.0f;
constexpr float kTipRounding = 6.0f;

constexpr float kTipCursorOffset = 44.0f;

constexpr int kTipMaxLines = 8;

constexpr float kTipFallbackLineHeight = 44.0f;

constexpr unsigned int RgbaColour(unsigned int r, unsigned int g, unsigned int b, unsigned int a) {
    return (a << 24) | (b << 16) | (g << 8) | r;
}
constexpr unsigned int kTipFillColour = RgbaColour(12, 10, 8, 244);
constexpr unsigned int kTipBorderColour = RgbaColour(120, 96, 48, 255);
constexpr unsigned int kTipTextFallback = RgbaColour(255, 255, 255, 255);

constexpr unsigned int kInMatchBackdrop = RgbaColour(26, 20, 14, 246);
constexpr unsigned int kInMatchBackdropBorder = RgbaColour(120, 96, 48, 255);
constexpr float kBackdropPad = 28.0f;
constexpr float kBackdropBorder = 5.0f;
constexpr float kBackdropRounding = 0.0f;

struct HoverTip {
    bool active = false;

    const char* text = nullptr;
    float mouseX = 0.0f;
    float mouseY = 0.0f;
};
HoverTip g_tip;

float PanelFloat(void* panel, size_t offset) {
    return *reinterpret_cast<const float*>(reinterpret_cast<const unsigned char*>(panel) + offset);
}

float ScrollOffsetY(void* panel) {
    const unsigned int* p = *reinterpret_cast<const unsigned int* const*>(
        reinterpret_cast<const unsigned char*>(panel) + nk::kPanelOffsetY);
    return p ? (float)*p : 0.0f;
}

unsigned int ReadCtxColour(void* ctx, size_t offset) {
    unsigned int v = 0;
    memcpy(&v, reinterpret_cast<const unsigned char*>(ctx) + offset, 4);
    return v;
}

float FontLineHeight(void* ctx) {
    const unsigned char* font = *reinterpret_cast<const unsigned char* const*>(
        reinterpret_cast<const unsigned char*>(ctx) + nk::kCtxFont);
    if (!font) return kTipFallbackLineHeight;
    const float h = *reinterpret_cast<const float*>(font + nk::kUserFontHeight);

    return (h > 1.0f && h < 500.0f) ? h : kTipFallbackLineHeight;
}

void CaptureTooltip(void* ctx, const SettingDef& d, float layoutY0, float layoutY1) {
    if (!kTooltipsEnabled || d.description.empty()) return;
    void* panel = CurrentPanel(ctx);
    if (!panel) return;

    const float scroll = ScrollOffsetY(panel);
    const float y0 = layoutY0 - scroll;
    const float y1 = layoutY1 - scroll;
    const float mx = ReadCtxFloat(ctx, nk::kCtxMouseX);
    const float my = ReadCtxFloat(ctx, nk::kCtxMouseY);

    const float x = PanelFloat(panel, nk::kPanelBoundsX);
    const float w = PanelFloat(panel, nk::kPanelBoundsW);
    if (mx < x || mx >= x + w || my < y0 || my >= y1) return;

    const float cx = PanelFloat(panel, nk::kPanelClipX);
    const float cy = PanelFloat(panel, nk::kPanelClipY);
    const float cw = PanelFloat(panel, nk::kPanelClipW);
    const float ch = PanelFloat(panel, nk::kPanelClipH);
    if (mx < cx || mx >= cx + cw || my < cy || my >= cy + ch) return;

    g_tip.active = true;
    g_tip.text = d.description.c_str();
    g_tip.mouseX = mx;
    g_tip.mouseY = my;
}

void DrawInMatchBackdrop(void* ctx, float panelX, float panelW, float footerY) {
    if (!g_fillRect) return;
    void* win =
        *reinterpret_cast<void**>(reinterpret_cast<unsigned char*>(ctx) + nk::kCtxCurrentWindow);
    if (!win) return;
    void* canvas = reinterpret_cast<unsigned char*>(win) + nk::kWindowBuffer;

    const float x = panelX - kBackdropPad;
    const float y = kTitleTop - kBackdropPad;
    const float w = panelW + kBackdropPad * 2.0f;
    const float h = (footerY + kFooterHeight + kBackdropPad) - y;

    g_fillRect(canvas, x, y, w, h, kBackdropRounding, kInMatchBackdropBorder);
    g_fillRect(canvas, x + kBackdropBorder, y + kBackdropBorder, w - kBackdropBorder * 2.0f,
                h - kBackdropBorder * 2.0f, kBackdropRounding, kInMatchBackdrop);
}

void DrawTooltip(void* ctx, float uiW) {
    if (!g_tip.active || !g_tip.text || !*g_tip.text) return;

    if (g_combosOpenThisFrame > 0) return;
    if (!g_fillRect || !g_countWrappedLines || !g_drawWrappedLabel || !g_rect) return;

    void* win =
        *reinterpret_cast<void**>(reinterpret_cast<unsigned char*>(ctx) + nk::kCtxCurrentWindow);
    if (!win) return;
    void* canvas = reinterpret_cast<unsigned char*>(win) + nk::kWindowBuffer;

    const float lineH = FontLineHeight(ctx);
    const float wrapW = kTipMaxWidth - kTipPad * 2.0f;
    int lines = g_countWrappedLines(ctx, g_tip.text, (int)strlen(g_tip.text), wrapW);
    if (lines < 1) lines = 1;
    if (lines > kTipMaxLines) lines = kTipMaxLines;

    const float tipW = kTipMaxWidth;
    const float tipH = (float)lines * lineH + kTipPad * 2.0f;

    float x = g_tip.mouseX + kTipCursorOffset;
    float y = g_tip.mouseY + kTipCursorOffset;
    if (x + tipW > uiW) x = g_tip.mouseX - kTipCursorOffset - tipW;
    if (y + tipH > kUiSpaceHeight) y = g_tip.mouseY - kTipCursorOffset - tipH;
    if (x < 0.0f) x = 0.0f;
    if (y < 0.0f) y = 0.0f;

    g_fillRect(canvas, x, y, tipW, tipH, kTipRounding, kTipBorderColour);
    g_fillRect(canvas, x + kTipBorder, y + kTipBorder, tipW - kTipBorder * 2.0f,
                tipH - kTipBorder * 2.0f, kTipRounding, kTipFillColour);

    unsigned int textColour = ReadCtxColour(ctx, nk::kCtxTextColour);
    if ((textColour >> 24) == 0) textColour = kTipTextFallback;
    g_rect(ctx, x + kTipPad, y + kTipPad, tipW - kTipPad * 2.0f, tipH - kTipPad * 2.0f);
    g_drawWrappedLabel(ctx, g_tip.text, textColour);
}

bool RowIsInactive(const SettingDef& d, SettingsRegistry& core) {
    if (!d.activeWhen.empty()) {

        const int on = core.Find(d.owner.c_str(), d.activeWhen.c_str());
        return on >= 0 && core.GetNum(on) == 0.0;
    }
    if (d.owner != "game") return false;
    if (_stricmp(d.key.c_str(), "ui_scale_fixed") == 0) {

        const int mode = core.Find("game", "ui_scale_mode");
        return mode < 0 || (int)core.GetNum(mode) != 1;
    }
    if (_stricmp(d.key.c_str(), "healthbars_below") == 0) {

        const int mode = core.Find("game", "healthbar_mode");
        return mode >= 0 && (int)core.GetNum(mode) == 1;
    }
    return false;
}

void DrawSettingRow(void* ctx, SettingsRegistry& core, int i, float rowH, bool showCategory,
                     bool allowTextEdit = true) {
    const SettingDef& d = core.DefAt(i);
    char buf[320];

    const bool inactive = RowIsInactive(d, core);
    struct DisableScope {
        bool on;
        void* ctx;
        ~DisableScope() { if (on && g_disableEnd) g_disableEnd(ctx); }
    } scope{inactive, ctx};
    if (inactive && g_disableBegin) g_disableBegin(ctx);

    if (d.input == InputType::Options && !d.options.empty()) {
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%s%s%s", showCategory ? d.category.c_str() : "",
                   showCategory ? " / " : "", LabelText(d));
        Row(ctx, rowH, 2);
        Label(ctx, buf, kAlignRow);

        const int current = OptionIndexForValue(d, core.GetNum(i));
        void* panel = CurrentPanel(ctx);

        const float comboW = panel ? PanelFloat(panel, nk::kPanelBoundsW) * 0.5f : 600.0f;
        float comboH = (float)d.options.size() * kComboItemHeight + kComboPopupPad;
        if (comboH > kComboPopupMaxHeight) comboH = kComboPopupMaxHeight;

        if (ComboBegin(ctx, i, OptionText(d, current), comboW, comboH)) {

            const float paneSpacing = ReadCtxFloat(ctx, nk::kCtxSpacingY);
            SetRowSpacing(ctx, 0.0f);
            for (int o = 0; o < (int)d.options.size(); ++o) {

                Row(ctx, kComboItemHeight, 1);
                if (g_comboItem && g_comboItem(ctx, OptionText(d, o), kAlignRow)) {
                    core.SetNum(i, ValueForOptionIndex(d, o));
                    g_dirty = true;
                    if (g_comboClose) g_comboClose(ctx);
                }
            }
            if (g_comboEnd) g_comboEnd(ctx);
            SetRowSpacing(ctx, paneSpacing);
        }
        return;
    }

    switch (d.type) {
        case SettingType::Bool: {
            _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%s%s%s", showCategory ? d.category.c_str() : "",
                       showCategory ? " / " : "", LabelText(d));
            Row(ctx, rowH, 1);
            bool v = core.GetNum(i) != 0.0;
            if (BoolCheckbox(buf, &v)) {
                core.SetNum(i, v ? 1.0 : 0.0);
                g_dirty = true;
            }
            break;
        }
        case SettingType::Int:
        case SettingType::Float: {
            const bool bounded = d.numMin != d.numMax && d.numMin >= 0;
            if (d.type == SettingType::Float) {
                _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%s%s%s: %g", showCategory ? d.category.c_str() : "",
                           showCategory ? " / " : "", LabelText(d), core.GetNum(i));
            } else {
                _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%s%s%s: %d", showCategory ? d.category.c_str() : "",
                           showCategory ? " / " : "", LabelText(d), (int)core.GetNum(i));
            }
            Row(ctx, rowH, bounded ? 2 : 1);
            Label(ctx, buf, kAlignRow);
            if (bounded) {
                uint32_t v = (uint32_t)(int)core.GetNum(i);
                if (Slider(&v, (uint32_t)d.numMin, (uint32_t)d.numMax)) {
                    core.SetNum(i, (double)v);
                    g_dirty = true;
                }
            }
            break;
        }
        case SettingType::String: {

            if (!allowTextEdit) {
                _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%s%s%s: %s", showCategory ? d.category.c_str() : "",
                           showCategory ? " / " : "", LabelText(d), core.GetStr(i).c_str());
                Row(ctx, rowH, 1);
                Label(ctx, buf, kAlignRow);
                break;
            }
            _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%s%s%s", showCategory ? d.category.c_str() : "",
                       showCategory ? " / " : "", LabelText(d));

            Row(ctx, kSearchRowHeight, 2);
            Label(ctx, buf, kAlignRow);
            char* text = EditBufferFor(i, core.GetStr(i));
            const bool typing = TextEdit(ctx, text, kEditBufferSize);

            if (!typing && core.GetStr(i) != text) {
                core.SetStr(i, text);
                g_dirty = true;
            }
            break;
        }
    }
}

bool IsTextRow(const SettingDef& d) {
    return d.type == SettingType::String && d.input == InputType::Text;
}

void DrawSettingsPane(void* ctx) {
    SettingsRegistry& core = SettingsRegistry::Get();
    const float rowH = kRowHeight;

    for (const SubGroup& group : GroupBySubcategory(g_category)) {
        const std::vector<int>& items = group.items;
        const bool firstIsText = !items.empty() && IsTextRow(core.DefAt(items.front()));
        SetRowSpacing(ctx, firstIsText ? kTextRowSpacing : kRowSpacing);
        Row(ctx, rowH, 1);
        Label(ctx, group.name.c_str(), kAlignTitle);
        for (size_t k = 0; k < items.size(); ++k) {
            const bool thisIsText = IsTextRow(core.DefAt(items[k]));
            const bool nextIsText =
                k + 1 < items.size() && IsTextRow(core.DefAt(items[k + 1]));
            const float wanted = (thisIsText || nextIsText) ? kTextRowSpacing : kRowSpacing;
            SetRowSpacing(ctx, wanted);

            const float tipY0 = ReadPanelFloat(ctx, nk::kPanelAtY) + ReadPanelFloat(ctx, nk::kPanelRowHeight);
            DrawSettingRow(ctx, core, items[k], rowH,  false);
            CaptureTooltip(ctx, core.DefAt(items[k]),
                            tipY0,
                            ReadPanelFloat(ctx, nk::kPanelAtY) + ReadPanelFloat(ctx, nk::kPanelRowHeight));

            const float liveRowH = thisIsText ? ReadPanelFloat(ctx, nk::kPanelRowHeight) : 0.0f;
            const float liveSpacing = thisIsText ? ReadCtxFloat(ctx, nk::kCtxSpacingY) : 0.0f;
        }
    }
}

void DrawSearchPane(void* ctx) {
    SettingsRegistry& core = SettingsRegistry::Get();
    const float rowH = kRowHeight;

    const std::string query = g_searchText;
    int matches = 0;
    for (int i = 0; i < core.Count(); ++i) {
        const SettingDef& d = core.DefAt(i);
        if (d.hidden) continue;

        if (!ContainsNoCase(LabelText(d), query) && !ContainsNoCase(d.label, query) &&
            !ContainsNoCase(d.key, query) && !ContainsNoCase(d.category, query)) {
            continue;
        }
        SetRowSpacing(ctx, kRowSpacing);
        const float tipY0 = ReadPanelFloat(ctx, nk::kPanelAtY) + ReadPanelFloat(ctx, nk::kPanelRowHeight);
        DrawSettingRow(ctx, core, i, rowH,  true,  false);
        CaptureTooltip(ctx, d, tipY0,
                        ReadPanelFloat(ctx, nk::kPanelAtY) + ReadPanelFloat(ctx, nk::kPanelRowHeight));
        ++matches;
    }

    if (matches == 0) {
        Row(ctx, rowH, 1);
        Label(ctx, query.empty() ? "Type to search all settings." : "No settings match.",
               kAlignRow);
    }
}

bool DrawFooter(void* ctx, float rowH) {
    Row(ctx, rowH, 3);
    if (Button(ctx, "Cancel")) {

        RestoreSnapshot();
        SaveEverything();
        return true;
    }

    if (!g_dirty && g_disableBegin) g_disableBegin(ctx);
    if (Button(ctx, "Apply") && g_dirty) {
        SaveEverything();

        TakeSnapshot();
    }
    if (!g_dirty && g_disableEnd) g_disableEnd(ctx);

    if (Button(ctx, "Save and Exit")) {
        SaveEverything();
        return true;
    }
    return false;
}

void DrawScreen(int variant) {
    void* ctx = g_ppNkContext ? *g_ppNkContext : nullptr;
    if (!ctx || !g_beginWindow || !g_uiWidth) return;

    const DWORD frameStart = GetTickCount();

    const DWORD now = GetTickCount();
    const bool freshEntry = (now - g_lastFrame) > kReentryGapMs;
    g_lastFrame = now;

    if (freshEntry) {

        RefreshDisplayMonitorOptions();
    }
    if (freshEntry || g_categoryCache.empty()) {
        g_categoryCache = Categories();
        g_categoryCache.push_back(kSearchCategory);
    }
    const std::vector<std::string>& categories = g_categoryCache;

    g_tip = HoverTip{};
    g_combosOpenThisFrame = 0;
    if (freshEntry) {
        TakeSnapshot();
        g_dirty = false;

        g_editBuffers.clear();
        g_searchText[0] = '\0';
        if (!categories.empty()) g_category = categories.front();
    }
    if (g_category.empty() && !categories.empty()) g_category = categories.front();

    const float uiW = std::floor(g_uiWidth());
    const float panelW = std::floor(uiW * kPanelWidthPercent * 0.01f);
    const float panelX = std::floor((uiW - panelW) * 0.5f);
    const float leftW = std::floor(panelW * kCategoryPanelPercent * 0.01f);
    const float rightX = panelX + leftW + kGap;
    const float rightW = panelW - leftW - kGap;
    const float contentH = kPanelHeight - kFooterHeight - kGap;
    const float footerY = kPanelTop + contentH + kGap;

    const unsigned int prevSkin = PushSkin(WindowSkin());

    if (!g_beginWindow(ctx, "mod_settings", 0.0f, 0.0f, uiW, kUiSpaceHeight, kGroupNoScrollbar)) {
        g_endWindow(ctx);
        PopSkin(prevSkin);
        return;
    }

    g_spaceBegin(ctx, 1, 0.0f, 1000);

    if (variant == 1) DrawInMatchBackdrop(ctx, panelX, panelW, footerY);

    if (variant == 0 && g_banner) g_banner();
    g_rect(ctx, panelX, kTitleTop, panelW, kTitleHeight);
    Label(ctx, g_lookupLocalized ? g_lookupLocalized("option_title") : "Game Options", kAlignTitle);

    const bool categoriesFit = CategoryListFits(ctx, categories.size(), contentH);
    if (BeginPaddedPanel(ctx, "mod_settings_categories", "mod_settings_categories_inner", panelX,
                          kPanelTop, leftW, contentH, 0, categoriesFit)) {
        DrawCategoryPanel(ctx, categories);
        EndPaddedPanel(ctx);
    }

    const bool searching = _stricmp(g_category.c_str(), kSearchCategory) == 0;
    const float searchRowH = kSearchRowHeight;
    const float searchStripH = searchRowH + kPanelPadTop + kGap;
    float contentTop = kPanelTop;
    float contentSpace = contentH;

    if (searching) {
        g_rect(ctx, rightX + kPanelPad, contentTop + kPanelPadTop, rightW - kPanelPad * 2.0f,
                searchRowH);

        const unsigned int prevEditSkin = g_skinTextEdit ? PushSkin(*g_skinTextEdit) : 0;
        const unsigned int state = g_textEdit
                                        ? g_textEdit(ctx, kTextEditFlags, g_searchText,
                                                      (int)sizeof(g_searchText), g_textEditFilter)
                                        : 0u;
        if (g_skinTextEdit) PopSkin(prevEditSkin);

        contentTop += searchStripH + kGap;
        contentSpace -= searchStripH + kGap;
    }

    if (BeginPaddedPanel(ctx, "mod_settings_content", "mod_settings_content_inner", rightX,
                          contentTop, rightW, contentSpace, 0)) {

        float* spacingY =
            reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(ctx) + nk::kCtxSpacingY);
        const float savedSpacingY = *spacingY;
        *spacingY = kRowSpacing;
        if (searching) {
            DrawSearchPane(ctx);
        } else {
            DrawSettingsPane(ctx);
        }
        *spacingY = savedSpacingY;
        EndPaddedPanel(ctx);
    }

    bool close = false;
    g_rect(ctx, panelX, footerY, panelW, kFooterHeight);
    if (g_beginGroup(ctx, "mod_settings_footer", kGroupNoScrollbar)) {
        close = DrawFooter(ctx, kFooterHeight - kGap);
        g_endGroup(ctx);
    }

    DrawTooltip(ctx, uiW);

    g_spaceEnd(ctx);
    g_endWindow(ctx);
    PopSkin(prevSkin);

    if (close) {
        if (variant == 1) {

            if (g_pauseMenuScreen && *g_pauseMenuScreen == kPauseScreenOptions) {
                kLog.Info("leaving to the pause menu root");
                *g_pauseMenuScreen = kPauseScreenRoot;
            } else {
                kLog.Warn("NOT leaving -- pause selector is %u, expected %u",
                          g_pauseMenuScreen ? *g_pauseMenuScreen : 0u,
                          (unsigned)kPauseScreenOptions);
            }
        } else if (g_transitionState) {
            kLog.Info("leaving to the main menu");
            g_transitionState(kGameStateMainMenu);
        }
    }
}

float __cdecl HookedDoScrollbarV(void* state, void* out, float x, float y, float w, float h,
                                  int hasScrolling, float offset, float target, float step,
                                  float buttonPixelInc, void* style, void* in, void* font) {
    if (style) {
        int* showButtons =
            reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(style) + nk::kScrollbarShowButtons);
        static bool saved = false;
        static int savedValue = 0;
        if (!saved) {
            savedValue = *showButtons;
            saved = true;
        }
        *showButtons = g_settings.GetBool(kKeyHideScrollbarArrows) ? 0 : savedValue;
    }
    return g_realDoScrollbarV(state, out, x, y, w, h, hasScrolling, offset, target, step,
                               buttonPixelInc, style, in, font);
}

void __cdecl HookedBuildOptionsMenuScreen(int variant) {
    g_settings.ReloadIfChanged();

    if (!kScreenEnabled || (variant != 0 && variant != 1)) {
        g_realBuildOptionsMenuScreen(variant);
        return;
    }
    DrawScreen(variant);
}

void RegisterScreenSettings() {
    g_settings.BeginGroup("UI", "Fixes");

    g_settings.RegisterBool(kKeyHideScrollbarArrows, kDefaultHideScrollbarArrows, "Fix Overscroll",
                             "Fixes scrollable sections having empty space at the bottom. Requires "
                             "disabling the scrollbar arrows.");
}

class SettingsScreenMod : public IMod {
public:
    const char* Name() const override { return "settings"; }

    bool Install() override {
        RegisterScreenSettings();
        g_settings.ReloadIfChanged();

        RegisterNativeSettingsCatalog();

        g_beginWindow = kBeginNamedWindow.Get();
        g_endWindow = kEndCurrentWindow.Get();
        g_uiWidth = kGetUiSpaceWidth.Get();
        g_rect = kOverrideNextRect.Get();
        g_beginGroup = kBeginNamedGroup.Get();
        g_endGroup = kEndGroup.Get();
        g_spaceBegin = kLayoutSpaceBegin.Get();
        g_spaceEnd = kLayoutSpaceEnd.Get();
        g_row = kLayoutRowDynamic.Get();
        g_label = kDrawLabel.Get();
        g_button = kDrawMenuButton.Get();
        g_banner = kDrawBanner.Get();
        g_slider = kSliderU32.Get();
        g_checkbox = kFlagCheckbox.Get();
        g_radio = kRadio.Get();
        g_textEdit = kTextEdit.Get();
        g_textEditFilter = kTextEditFilter.Get();
        g_comboBegin = kComboBegin.Get();
        g_comboItem = kComboItem.Get();
        g_comboClose = kComboClose.Get();
        g_comboEnd = kComboEnd.Get();
        g_displayCount = kDisplayCount.Get();
        g_skinComboClosed = kSkinComboClosed.Get();
        g_skinComboClosedAlt = kSkinComboClosedAlt.Get();
        g_skinComboOpen = kSkinComboOpen.Get();
        g_skinComboOpenAlt = kSkinComboOpenAlt.Get();
        g_fillRect = kNkFillRect.Get();
        g_countWrappedLines = kCountWrappedLines.Get();
        g_drawWrappedLabel = kDrawWrappedLabel.Get();
        g_disableBegin = kDisableBegin.Get();
        g_disableEnd = kDisableEnd.Get();
        g_setSkin = kSetSkin.Get();
        g_skinManager = kSkinManager.Get();
        g_skinWindow = kSkinWindow.Get();
        g_skinWindowAlt = kSkinWindowAlt.Get();
        g_skinAltFlag = kSkinAltFlag.Get();
        g_skinContentPanel = kSkinContentPanel.Get();
        g_skinTextEdit = kSkinTextEdit.Get();
        g_lookupLocalized = kLookupLocalized.Get();
        g_transitionState = kTransitionGameState.Get();
        g_pauseMenuScreen = kPauseMenuScreen.Get();
        g_ppNkContext = kNkContextMenus.Get();

        bool ok = InstallHook(kBuildOptionsMenuScreen.Target(),
                               reinterpret_cast<void*>(&HookedBuildOptionsMenuScreen),
                               reinterpret_cast<void**>(&g_realBuildOptionsMenuScreen),
                               "BuildOptionsMenuScreen");
        ok = InstallHook(kDoScrollbarV.Target(),
                          reinterpret_cast<void*>(&HookedDoScrollbarV),
                          reinterpret_cast<void**>(&g_realDoScrollbarV),
                          "nk_do_scrollbarv (scrollbar arrows)") &&
             ok;
        return ok;
    }
};

}  // namespace

MOD_REGISTER(SettingsScreenMod)
