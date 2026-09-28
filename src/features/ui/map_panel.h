// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

#include "features/ui/map_browser.h"
#include "features/ui/screen_kit.h"

namespace ui {
namespace map_panel {

Rect PreviewBox(const Rect& column);

void DrawColumn(void* ctx, const Rect& column, const uint8_t* selectedEntry,
                const ScreenTheme& theme, MapBrowser* browser);

void DrawDetails(void* ctx, const Rect& r, const uint8_t* selectedEntry);

}  // namespace map_panel
}  // namespace ui
