// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace team_colours {

constexpr int kCount = 8;
constexpr int kBlack = 5;

constexpr uint32_t Pack(unsigned r, unsigned g, unsigned b) {
    return (0xffu << 24) | (b << 16) | (g << 8) | r;
}

constexpr uint32_t kColours[kCount] = {
    Pack(196, 40, 40),
    Pack(48, 72, 200),
    Pack(0, 152, 152),
    Pack(128, 48, 168),
    Pack(232, 132, 24),
    Pack(32, 32, 32),
    Pack(236, 236, 236),
    Pack(232, 212, 48),
};

void SetOverride(int colourIndex, bool active, uint32_t packed);
bool GetOverride(int colourIndex, uint32_t* packed);

}  // namespace team_colours
