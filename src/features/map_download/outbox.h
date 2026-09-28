// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>

#include "features/map_download/wire.h"

namespace map_download {

constexpr uint8_t kToAllModded = 0xFF;

struct OutMessage {
    uint8_t to;
    uint8_t bytes[wire::kMaxMessageLen];
    size_t len;
};

}  // namespace map_download
