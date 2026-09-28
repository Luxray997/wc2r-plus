// SPDX-License-Identifier: MIT
#include "features/team_colours.h"

namespace team_colours {
namespace {

bool g_active[kCount] = {};
uint32_t g_packed[kCount] = {};

}  // namespace

void SetOverride(int colourIndex, bool active, uint32_t packed) {
    if (colourIndex < 0 || colourIndex >= kCount) return;
    g_active[colourIndex] = active;
    g_packed[colourIndex] = packed;
}

bool GetOverride(int colourIndex, uint32_t* packed) {
    if (colourIndex < 0 || colourIndex >= kCount || !g_active[colourIndex]) return false;
    *packed = g_packed[colourIndex];
    return true;
}

}  // namespace team_colours
