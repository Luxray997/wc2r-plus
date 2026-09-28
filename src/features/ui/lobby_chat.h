// SPDX-License-Identifier: MIT
#pragma once

#include <windows.h>

#include "features/ui/screen_kit.h"
#include "target/addresses.h"

namespace ui {
namespace lobby_chat {

struct Binding {

    unsigned char** begin = nullptr;
    unsigned char** end = nullptr;

    void* input = nullptr;

    unsigned char* scrollRequest = nullptr;

    const unsigned int* frameSkin = nullptr;
    const unsigned int* textSkin = nullptr;

    game::CountWrappedLinesFn countLines = nullptr;
    game::LayoutRowDynamicFn rowDynamic = nullptr;
    game::DrawWrappedLabelFn wrappedLabel = nullptr;
    game::InputKeyPressedFn keyPressed = nullptr;
    game::LobbySendChatFn sendChat = nullptr;

    const char* (*seatedName)(int lobbySlot) = nullptr;

    ScreenTheme theme;
};

void Bind(const Binding& binding);

void OnFrame(DWORD now);

void DrawHistory(void* ctx, const Rect& r);
void DrawInput(void* ctx, const Rect& r);

float FontLineHeight(void* ctx);

}  // namespace lobby_chat
}  // namespace ui
