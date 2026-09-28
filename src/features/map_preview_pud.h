// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace map_preview_pud {

bool FindSection(const uint8_t* pud, size_t size, const char tag[4], const uint8_t** data, uint32_t* length);

struct PudView {
    uint16_t w = 0, h = 0;
    uint16_t era = 0;
    const uint8_t* mtxm = nullptr;
    const uint8_t* unit = nullptr;
    uint32_t unitLen = 0;
};

enum class ParseFault { Ok, NotPud, MissingSection, BadDims, ShortMtxm };

ParseFault Parse(const uint8_t* pud, size_t size, PudView* out);

struct Marker {
    int x, y, owner;
    bool isStart, isGold;
};
void CollectMarkers(const PudView& v, std::vector<Marker>* out);

}  // namespace map_preview_pud
