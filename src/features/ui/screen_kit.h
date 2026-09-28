// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "target/struct_offsets.h"

namespace ui {

constexpr float kSideMargin = 500.0f;
constexpr float kContentTop = 360.0f;
constexpr float kFooterH = 135.0f;
constexpr float kFooterBottomGap = 230.0f;

constexpr float kFramePad = 100.0f;
constexpr float kRowH = 120.0f;
constexpr float kCaptionH = 90.0f;
constexpr float kNameH = 120.0f;
constexpr float kGap = 30.0f;

constexpr unsigned int Rgba(unsigned r, unsigned g, unsigned b, unsigned a) {
    return (a << 24) | (b << 16) | (g << 8) | r;
}

constexpr unsigned int kPlaceholderColour = Rgba(150, 142, 128, 200);

struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
};

constexpr float kFrameBorderAboveBottom = 50.0f;

Rect LobbyFrame(const Rect& window);

inline float Fl(float v) { return static_cast<float>(static_cast<int>(v)); }

inline float FooterButtonWidth(const Rect& window) { return Fl((window.w - 1040.0f) / 3.0f); }

constexpr int kAlignCentre = 0x12;
constexpr int kAlignLeft = 0x11;
constexpr int kAlignRight = 0x14;
constexpr int kAlignCaption = 0x21;

constexpr unsigned int kGroupFlags = 0x20;
constexpr unsigned int kGroupPlain = 0;
constexpr unsigned int kTextEditFlags = 0x260;

constexpr unsigned short kBuiltInIdBase = 0x4c4;

struct ScreenTheme {
    const unsigned int* screenSkin = nullptr;
    const unsigned int* leftFrameSkin = nullptr;
    const unsigned int* rightFrameSkin = nullptr;
    const unsigned int* textEditSkin = nullptr;
    const unsigned int* headingFont = nullptr;
};

bool Resolve();
bool Ready();

const char* StrData(const void* s);
std::string GameStr(const void* s);

void StrSet(void* s, const char* value);

struct KeyList {
    const char* const* begin = nullptr;
    const char* const* end = nullptr;
    size_t size() const { return begin && end > begin ? static_cast<size_t>(end - begin) : 0; }
};
KeyList Keys(const void* vec);

std::vector<std::string> FolderNames(const void* folderVec, bool skipParentEntry);

unsigned int PushSkin(unsigned int skinId);
void PopSkin(unsigned int previous);
unsigned int PushFont(unsigned int fontId);
void PopFont(unsigned int previous);

float ReadCtxFloat(void* ctx, size_t offset);
void SetRowSpacing(void* ctx, float spacing);
unsigned int ReadCtxColour(void* ctx, size_t offset);
void WriteCtxColour(void* ctx, size_t offset, unsigned int v);

using game::nk::CurrentPanel;
using game::nk::WindowCanvas;
using game::nk::PanelFloat;
using game::nk::ScrollOffsetY;

void SetScrollOffsetY(void* panel, float value);

float NextRowTop(void* ctx);

bool LastRowFirstCell(void* ctx, Rect* out);

bool WindowRect(void* ctx, Rect* out);

void SetRect(void* ctx, const Rect& r);
void LabelAt(void* ctx, const Rect& r, const char* text, int align);

void Label(void* ctx, const char* text, int align);
void HeadingAt(void* ctx, const Rect& r, const char* text, int align, const ScreenTheme& theme);
bool ButtonAt(void* ctx, const Rect& r, const char* text, bool greyed = false);

void LockedBoxAt(void* ctx, const Rect& r, const char* text, const char* id);
void WrappedLabelAt(void* ctx, const Rect& r, const char* text);

void PanelFrame(void* ctx, const Rect& r, const char* name, const unsigned int* skin);

bool TextEditAt(void* ctx, const Rect& r, char* buf, int maxLen, const ScreenTheme& theme);

bool FilterFieldAt(void* ctx, const Rect& r, char* buf, int maxLen, const char* placeholder,
                    const ScreenTheme& theme);

void DrawUpArrow(void* ctx, const Rect& button, bool greyed);

void DrawReloadIcon(void* ctx, const Rect& button, bool greyed);

bool BeginList(void* ctx, const Rect& r, const char* name, unsigned int flags = kGroupPlain);
void EndList(void* ctx);

bool ListRowButton(void* ctx, const char* label);

bool ContainsNoCase(const char* haystack, const char* needle);

void FillRect(void* canvas, float x, float y, float w, float h, float rounding, unsigned int col);

const char* Localized(const char* key);

const char* FormatLocalized(const char* key, const char* arg);

void DrawBanner();

int TilesetIndexForKey(const char* key);

bool Dropdown(const char* comboId, void* current, const void* options, uint8_t* openFlag);

bool FolderDropdown(const char* comboId, void* current, const void* options, uint8_t* openFlag);

}  // namespace ui
