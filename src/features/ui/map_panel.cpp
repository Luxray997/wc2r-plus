// SPDX-License-Identifier: MIT
#include "features/ui/map_panel.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "target/struct_offsets.h"

namespace ui {
namespace map_panel {

using game::wc2r::kEntryDesc;
using game::wc2r::kEntryDim;
using game::wc2r::kEntryName;
using game::wc2r::kEntryPlayers;

Rect PreviewBox(const Rect& column) {
    const float available = column.h - kNameH - kGap - kRowH - kGap;
    const float side = Fl((std::max)(0.0f, (std::min)(column.w, available)));
    return Rect{Fl(column.x + (column.w - side) * 0.5f), column.y + kNameH + kGap, side, side};
}

void DrawColumn(void* ctx, const Rect& column, const uint8_t* selectedEntry,
                const ScreenTheme& theme, MapBrowser* browser) {

    const char* name = selectedEntry ? StrData(selectedEntry + kEntryName) : "";
    HeadingAt(ctx, Rect{column.x, column.y, column.w, kNameH},
              name[0] ? name : "No map selected", kAlignCentre, theme);

    const Rect box = PreviewBox(column);
    if (ButtonAt(ctx, Rect{box.x, box.y + box.h + kGap, box.w, kRowH}, "Change map") && browser) {
        browser->Open();
    }
}

void DrawDetails(void* ctx, const Rect& r, const uint8_t* selectedEntry) {
    const char* name = selectedEntry ? StrData(selectedEntry + kEntryName) : "";
    if (!selectedEntry || !name[0]) return;

    float y = r.y;
    const unsigned dim = *reinterpret_cast<const uint16_t*>(selectedEntry + kEntryDim);
    char line[128];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "%u x %u", dim, dim);
    LabelAt(ctx, Rect{r.x, y, r.w, kCaptionH}, line, kAlignLeft);
    y += kCaptionH;

    char players[16];
    _snprintf_s(players, sizeof(players), _TRUNCATE, "%u",
                static_cast<unsigned>(selectedEntry[kEntryPlayers]));
    LabelAt(ctx, Rect{r.x, y, r.w, kCaptionH},
            FormatLocalized("customscenario_maxplayers", players), kAlignLeft);
    y += kCaptionH + kGap;

    const char* desc = StrData(selectedEntry + kEntryDesc);
    if (desc[0] && strcmp(desc, name) != 0 && y + kCaptionH < r.y + r.h) {
        WrappedLabelAt(ctx, Rect{r.x, y, r.w, (r.y + r.h) - y}, desc);
    }
}

}  // namespace map_panel
}  // namespace ui
