// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>

namespace game {

namespace nk {

constexpr size_t kCtxCurrentWindow = 0x3d28;
constexpr size_t kCtxSeq = 0x3d34;

constexpr size_t kCtxMouseX = 0x1bc;
constexpr size_t kCtxMouseY = 0x1c0;

constexpr size_t kCtxMouseButtons = 0x17c;
constexpr size_t kMouseButtonStride = 0x10;
constexpr size_t kMouseButtonDown = 0x00;
constexpr size_t kMouseButtonClicked = 0x04;
constexpr int kMouseButtonLeft = 0;
constexpr int kMouseButtonRight = 2;

constexpr size_t kCtxFont = 0x1e0;
constexpr size_t kUserFontHeight = 0x04;

constexpr size_t kUserFontWidth = 0x08;

constexpr size_t kCtxTextExtra0 = 0x210;
constexpr size_t kCtxTextPaddingX = 0x214;
constexpr size_t kCtxTextPaddingY = 0x218;
constexpr size_t kCtxTextExtra1 = 0x21c;
constexpr size_t kCtxTextExtra2 = 0x220;
constexpr size_t kCtxTextColourFactor = 0x224;
constexpr size_t kCtxWindowBackground = 0x1f9c;

constexpr size_t kCtxTextColour = 0x20c;
constexpr size_t kCtxSpacingY = 0x2004;
constexpr size_t kCtxScrollbarSize = 0x2008;

constexpr size_t kCtxGroupPaddingY = 0x201c;

constexpr size_t kCtxScrollH = 0x13d0;
constexpr size_t kCtxScrollV = 0x1648;

constexpr size_t kCtxStyleEdit = 0x1058;
constexpr size_t kStyleEditCursorTextNormal = 0x2e4;
constexpr size_t kStyleEditCursorTextHover = 0x2e8;
constexpr size_t kStyleEditTextNormal = 0x2ec;
constexpr size_t kStyleEditTextHover = 0x2f0;
constexpr size_t kStyleEditTextActive = 0x2f4;
constexpr size_t kStyleEditSelectedTextNormal = 0x300;
constexpr size_t kStyleEditSelectedTextHover = 0x304;

constexpr size_t kCtxTextEditCursor = 0x2864;
constexpr size_t kCtxTextEditSelectStart = 0x2868;
constexpr size_t kCtxTextEditSelectEnd = 0x286c;

constexpr size_t kWindowBounds = 0x4c;
constexpr size_t kWindowBuffer = 0x64;
constexpr size_t kWindowPanel = 0x8c;

constexpr size_t kPanelFlags = 0x04;
constexpr size_t kPanelBoundsX = 0x08;
constexpr size_t kPanelBoundsY = 0x0c;
constexpr size_t kPanelBoundsW = 0x10;
constexpr size_t kPanelBoundsH = 0x14;
constexpr size_t kPanelOffsetY = 0x1c;

constexpr size_t kPanelAtY = 0x24;
constexpr size_t kPanelClipX = 0x3c;
constexpr size_t kPanelClipY = 0x40;
constexpr size_t kPanelClipW = 0x44;
constexpr size_t kPanelClipH = 0x48;
constexpr size_t kPanelRowIndex = 0x68;
constexpr size_t kPanelRowHeight = 0x6c;
constexpr size_t kPanelRowItemOffset = 0x84;

constexpr size_t kScrollbarStride = 0x278;
constexpr size_t kScrollbarTrough[3] = {0x00, 0x20, 0x40};
constexpr size_t kScrollbarCursor[3] = {0x64, 0x84, 0xa4};
constexpr size_t kScrollbarBorder = 0xc8;
constexpr size_t kScrollbarBorderCursor = 0xd0;
constexpr size_t kScrollbarShowButtons = 0xe8;
constexpr size_t kScrollbarDecButton = 0xec;

constexpr uint32_t kStyleItemColour = 0;
constexpr uint32_t kStyleItemNineSlice = 2;
constexpr size_t kNineSliceInsetL = 4 + 16 + 0;
constexpr size_t kNineSliceInsetR = 4 + 16 + 4;

constexpr unsigned int kWindowNoScrollbar = 0x20;
constexpr unsigned int kWindowNoInput = 0x1000;

constexpr unsigned int kWindowHidden = 0x2000;

constexpr size_t kWindowFlags = 0x48;

inline void* CurrentWindow(void* ctx) {
    if (!ctx) return nullptr;
    return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(ctx) + kCtxCurrentWindow);
}

inline void* CurrentPanel(void* ctx) {
    void* win = CurrentWindow(ctx);
    if (!win) return nullptr;
    return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(win) + kWindowPanel);
}

inline void* WindowCanvas(void* ctx) {
    void* win = CurrentWindow(ctx);
    if (!win) return nullptr;
    return reinterpret_cast<uint8_t*>(win) + kWindowBuffer;
}

inline float CtxFloat(const void* ctx, size_t offset) {
    if (!ctx) return 0.0f;
    return *reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(ctx) + offset);
}

inline float PanelFloat(const void* panel, size_t offset) {
    if (!panel) return 0.0f;
    return *reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(panel) + offset);
}

inline float ScrollOffsetY(const void* panel) {
    if (!panel) return 0.0f;
    const unsigned int* p = *reinterpret_cast<const unsigned int* const*>(
        reinterpret_cast<const uint8_t*>(panel) + kPanelOffsetY);
    return p ? static_cast<float>(*p) : 0.0f;
}

inline bool MousePressed(const void* ctx, int button) {
    if (!ctx) return false;
    const auto* b = reinterpret_cast<const uint8_t*>(ctx) + kCtxMouseButtons +
                    static_cast<size_t>(button) * kMouseButtonStride;
    return *reinterpret_cast<const int*>(b + kMouseButtonDown) != 0 &&
           *reinterpret_cast<const unsigned*>(b + kMouseButtonClicked) != 0;
}

}  // namespace nk

namespace msvc {

constexpr size_t kStrSize = 0x10;
constexpr size_t kStrCapacity = 0x14;
constexpr size_t kStrStride = 0x18;
constexpr unsigned int kStrSsoMax = 15;

}  // namespace msvc

namespace wc2r {

constexpr size_t kSessionIsHost = 0x18;
constexpr size_t kSessionConnected = 0x19;
constexpr size_t kSessionTransport = 0x68;

constexpr int kVtSendToPeer = 0x3c / 4;
constexpr int kVtBroadcast = 0x40 / 4;
constexpr int kVtEnumPeers = 0x48 / 4;

constexpr size_t kLobbySlotStride = 0x26;
constexpr size_t kLobbySlotState = 0x08;
constexpr unsigned char kLobbySlotAbsent = 3;

constexpr unsigned char kLobbySlotOpen = 5;

constexpr size_t kLobbySlotTeam = 0x0a;

constexpr size_t kLobbySlotName = 0x0d;
constexpr size_t kLobbySlotNameMax = kLobbySlotStride - kLobbySlotName;

constexpr size_t kSlotNameStride = 0x20;
constexpr size_t kSlotNameString = 0x08;

constexpr size_t kEntryStride = 0x68;
constexpr size_t kEntryIndex = 0x00;
constexpr size_t kEntryPlayers = 0x04;
constexpr size_t kEntryDim = 0x06;
constexpr size_t kEntryName = 0x08;
constexpr size_t kEntryPath = 0x20;
constexpr size_t kEntryFolder = 0x38;
constexpr size_t kEntryDesc = 0x50;

constexpr size_t kMessageSlotStride = 0xD0;
constexpr size_t kMessageSlotHighlight = 0xCC;

constexpr size_t kPlayerNameStride = 0x38;

constexpr size_t kPeerStride = 0x22;
constexpr uint8_t kPeerDeparted = 2;

constexpr size_t kDiplomacyRowStride = 0x10;

constexpr size_t kEventNumber = 0x00;
constexpr size_t kEventScan = 0x0c;
constexpr size_t kEventModifiers = 0x0d;
constexpr size_t kEventChar = 0x0e;
constexpr unsigned char kModShift = 0x03;
constexpr unsigned char kModCtrl = 0x04;

constexpr size_t kDlgEditText = 0x14;

constexpr size_t kDlgEditChanged = 0x36;
constexpr size_t kDlgEditMaxLen = 0xfe;

constexpr size_t kUnitTypeByte = 0x27;

constexpr size_t kPendingSetupName = 0x10;
constexpr size_t kPendingSetupNameCapacity = 0xdc;

constexpr size_t kMaxGamePath = 0x104;

}  // namespace wc2r

}  // namespace game
