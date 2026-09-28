// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>

namespace map_download {

constexpr size_t kSha256Len = 32;

void Sha256(const uint8_t* data, size_t len, uint8_t out[kSha256Len]);

}  // namespace map_download
