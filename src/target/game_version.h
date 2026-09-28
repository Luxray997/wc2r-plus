// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

constexpr uintptr_t kPreferredImageBase = 0x00400000;

constexpr uint32_t kExpectedSizeOfImage = 0x0062B000;

constexpr uintptr_t GameRva(uintptr_t staticAddr) {
    return staticAddr - kPreferredImageBase;
}
