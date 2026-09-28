// SPDX-License-Identifier: MIT
#include "features/map_preview_pud.h"

#include <cstring>

namespace map_preview_pud {

namespace {
constexpr uint8_t kUnitStartHuman = 0x5e;
constexpr uint8_t kUnitStartOrc = 0x5f;
constexpr uint8_t kUnitGoldMine = 0x5c;
constexpr uint8_t kUnitOilPatch = 0x5d;
}  // namespace

bool FindSection(const uint8_t* pud, size_t size, const char tag[4], const uint8_t** data, uint32_t* length) {
    size_t off = 0;
    bool found = false;
    while (size - off >= 8) {
        int32_t len;
        memcpy(&len, pud + off + 4, 4);

        if (len < 0 || static_cast<size_t>(len) > size - off - 8) break;
        if (memcmp(pud + off, tag, 4) == 0) {

            *data = pud + off + 8;
            *length = static_cast<uint32_t>(len);
            found = true;
        }
        off += 8 + static_cast<size_t>(len);
    }
    return found;
}

ParseFault Parse(const uint8_t* pud, size_t size, PudView* out) {
    *out = PudView();
    if (size < 8 || memcmp(pud, "TYPE", 4) != 0) return ParseFault::NotPud;

    const uint8_t* dim = nullptr;
    const uint8_t* era = nullptr;
    const uint8_t* mtxm = nullptr;
    uint32_t dimLen = 0, eraLen = 0, mtxmLen = 0;
    if (!FindSection(pud, size, "DIM ", &dim, &dimLen) || dimLen < 4 ||
        !FindSection(pud, size, "ERA ", &era, &eraLen) || eraLen < 2 ||
        !FindSection(pud, size, "MTXM", &mtxm, &mtxmLen)) {
        return ParseFault::MissingSection;
    }
    uint16_t w, h, eraIndex;
    memcpy(&w, dim, 2);
    memcpy(&h, dim + 2, 2);
    memcpy(&eraIndex, era, 2);
    out->w = w;
    out->h = h;
    out->era = eraIndex & 3;
    if (w == 0 || h == 0 || w > 256 || h > 256) return ParseFault::BadDims;
    if (mtxmLen < static_cast<uint32_t>(w) * h * 2) return ParseFault::ShortMtxm;
    out->mtxm = mtxm;
    FindSection(pud, size, "UNIT", &out->unit, &out->unitLen);
    return ParseFault::Ok;
}

void CollectMarkers(const PudView& v, std::vector<Marker>* out) {
    out->clear();
    for (uint32_t off = 0; v.unit && v.unitLen - off >= 8; off += 8) {
        uint16_t ux, uy;
        memcpy(&ux, v.unit + off, 2);
        memcpy(&uy, v.unit + off + 2, 2);
        const uint8_t type = v.unit[off + 4];
        const uint8_t owner = v.unit[off + 5];
        if (ux >= v.w || uy >= v.h) continue;
        const bool isStart = (type == kUnitStartHuman || type == kUnitStartOrc);
        const bool isRes = (type == kUnitGoldMine || type == kUnitOilPatch);
        if (isStart || isRes) out->push_back({ux, uy, owner, isStart, type == kUnitGoldMine});
    }
}

}  // namespace map_preview_pud
