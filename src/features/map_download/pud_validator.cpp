// SPDX-License-Identifier: MIT
#include "features/map_download/pud_validator.h"

#include <initializer_list>

#include "features/map_download/limits.h"
#include "features/map_download/pud_rules.h"

namespace map_download {
namespace {

struct TagSpec {
    char tag[4];
    bool required;
    uint32_t length;
};

enum TagIndex : uint8_t {
    kType, kVer, kDesc, kOwnr, kEra, kErax, kDim, kUdta, kAlow, kUgrd, kSide,
    kSgld, kSlbr, kSoil, kAipl, kSign, kMtxm, kSqm, kOilm, kRegm, kUnit, kTagCount
};

constexpr TagSpec kTags[kTagCount] = {
    {{'T', 'Y', 'P', 'E'}, true, 16},  {{'V', 'E', 'R', ' '}, true, 2},
    {{'D', 'E', 'S', 'C'}, true, 32},  {{'O', 'W', 'N', 'R'}, true, 16},
    {{'E', 'R', 'A', ' '}, true, 2},   {{'E', 'R', 'A', 'X'}, false, 2},
    {{'D', 'I', 'M', ' '}, true, 4},   {{'U', 'D', 'T', 'A'}, true, 0},
    {{'A', 'L', 'O', 'W'}, false, 384}, {{'U', 'G', 'R', 'D'}, true, 782},
    {{'S', 'I', 'D', 'E'}, true, 16},  {{'S', 'G', 'L', 'D'}, true, 32},
    {{'S', 'L', 'B', 'R'}, true, 32},  {{'S', 'O', 'I', 'L'}, true, 32},
    {{'A', 'I', 'P', 'L'}, true, 16},  {{'S', 'I', 'G', 'N'}, false, 4},
    {{'M', 'T', 'X', 'M'}, true, 0},   {{'S', 'Q', 'M', ' '}, true, 0},
    {{'O', 'I', 'L', 'M'}, true, 0},   {{'R', 'E', 'G', 'M'}, true, 0},
    {{'U', 'N', 'I', 'T'}, true, 0},
};

constexpr uint32_t kUdtaLengths[] = {5696, 5950};

struct Section {
    bool present;
    uint32_t offset;
    uint32_t length;
};

uint16_t Read16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t Read32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

template <typename T, typename V, size_t N>
bool InSet(V value, const T (&set)[N]) {
    for (size_t i = 0; i < N; ++i) {
        if (static_cast<uint32_t>(set[i]) == static_cast<uint32_t>(value)) return true;
    }
    return false;
}

PudVerdict Verdict(PudFault fault, uint32_t offset, TagIndex tag) {
    PudVerdict v;
    v.fault = fault;
    v.offset = offset;
    for (int i = 0; i < 4; ++i) v.tag[i] = kTags[tag].tag[i];
    v.tag[4] = '\0';
    return v;
}

PudVerdict Verdict(PudFault fault, uint32_t offset) {
    PudVerdict v;
    v.fault = fault;
    v.offset = offset;
    v.tag[0] = '\0';
    return v;
}

int FindTag(const uint8_t* p) {
    for (int i = 0; i < kTagCount; ++i) {
        if (p[0] == static_cast<uint8_t>(kTags[i].tag[0]) &&
            p[1] == static_cast<uint8_t>(kTags[i].tag[1]) &&
            p[2] == static_cast<uint8_t>(kTags[i].tag[2]) &&
            p[3] == static_cast<uint8_t>(kTags[i].tag[3])) {
            return i;
        }
    }
    return -1;
}

template <size_t N>
constexpr bool RulesFit(const pud_rules::FieldRule (&rules)[N], uint32_t length) {
    for (size_t i = 0; i < N; ++i) {
        if (rules[i].offset + static_cast<uint32_t>(rules[i].size) * rules[i].count > length) return false;
    }
    return true;
}
static_assert(RulesFit(pud_rules::kUdtaRules, 5696), "a UDTA rule runs past the shorter UDTA");
static_assert(RulesFit(pud_rules::kUgrdRules, 782), "a UGRD rule runs past UGRD");

template <size_t N>
uint32_t FirstBadField(const uint8_t* section, const pud_rules::FieldRule (&rules)[N]) {
    for (size_t i = 0; i < N; ++i) {
        const pud_rules::FieldRule& r = rules[i];
        for (uint32_t k = 0; k < r.count; ++k) {
            const uint32_t at = r.offset + k * r.size;
            uint32_t v = 0;
            for (uint32_t b = 0; b < r.size; ++b) v |= static_cast<uint32_t>(section[at + b]) << (8 * b);
            const bool ok = r.check == pud_rules::FieldCheck::Mask ? (v & ~r.max) == 0
                                                                   : (v >= r.min && v <= r.max);
            if (!ok) return at;
        }
    }
    return 0;
}

template <typename T, size_t N>
bool AllBytesIn(const uint8_t* data, const Section& s, const T (&set)[N]) {
    for (uint32_t i = 0; i < s.length; ++i) {
        if (!InSet(data[s.offset + i], set)) return false;
    }
    return true;
}

}  // namespace

PudVerdict ValidatePud(const uint8_t* data, size_t size) {
    if (size < 8 || data == nullptr) return Verdict(PudFault::TooSmall, 0);
    if (size > kMaxMapBytes) return Verdict(PudFault::TooLarge, 0);
    const uint32_t total = static_cast<uint32_t>(size);

    Section sections[kTagCount] = {};
    uint32_t off = 0;
    bool first = true;
    while (off < total) {
        const uint32_t remaining = total - off;
        if (remaining < 8) return Verdict(PudFault::TruncatedHeader, off);
        const uint32_t length = Read32(data + off + 4);
        if (length > remaining - 8) return Verdict(PudFault::TruncatedSection, off);
        const int tag = FindTag(data + off);
        if (tag < 0) return Verdict(PudFault::UnknownTag, off);
        if (first && tag != kType) return Verdict(PudFault::FirstTagNotType, off);
        first = false;
        Section& s = sections[tag];
        if (s.present) return Verdict(PudFault::DuplicateTag, off, static_cast<TagIndex>(tag));
        s.present = true;
        s.offset = off + 8;
        s.length = length;
        off += 8 + length;
    }

    for (int i = 0; i < kTagCount; ++i) {
        const Section& s = sections[i];
        if (!s.present) {
            if (kTags[i].required) return Verdict(PudFault::MissingTag, 0, static_cast<TagIndex>(i));
            continue;
        }
        if (kTags[i].length != 0 && s.length != kTags[i].length) {
            return Verdict(PudFault::BadLength, s.offset, static_cast<TagIndex>(i));
        }
    }

    const Section& type = sections[kType];
    for (int i = 0; i < 10; ++i) {
        if (data[type.offset + i] != static_cast<uint8_t>(pud_rules::kTypePrefix[i])) {
            return Verdict(PudFault::BadType, type.offset, kType);
        }
    }

    if (!InSet(Read16(data + sections[kVer].offset), pud_rules::kVer)) {
        return Verdict(PudFault::BadVersion, sections[kVer].offset, kVer);
    }

    {
        const Section& desc = sections[kDesc];
        bool terminated = false;
        for (uint32_t i = 0; i < desc.length; ++i) {
            if (data[desc.offset + i] == 0) {
                terminated = true;
                break;
            }
        }
        if (!terminated) return Verdict(PudFault::DescNotTerminated, desc.offset, kDesc);
    }

    if (!AllBytesIn(data, sections[kOwnr], pud_rules::kOwnr)) {
        return Verdict(PudFault::BadOwner, sections[kOwnr].offset, kOwnr);
    }
    if (!InSet(Read16(data + sections[kEra].offset), pud_rules::kEra)) {
        return Verdict(PudFault::BadEra, sections[kEra].offset, kEra);
    }
    if (sections[kErax].present && !InSet(Read16(data + sections[kErax].offset), pud_rules::kErax)) {
        return Verdict(PudFault::BadEra, sections[kErax].offset, kErax);
    }

    const uint16_t width = Read16(data + sections[kDim].offset);
    const uint16_t height = Read16(data + sections[kDim].offset + 2);
    if (width != height || !InSet(width, pud_rules::kDim)) {
        return Verdict(PudFault::BadDimensions, sections[kDim].offset, kDim);
    }

    const uint32_t cells = static_cast<uint32_t>(width) * height;

    if (!InSet(sections[kUdta].length, kUdtaLengths)) {
        return Verdict(PudFault::BadLength, sections[kUdta].offset, kUdta);
    }
    if (!AllBytesIn(data, sections[kSide], pud_rules::kSide)) {
        return Verdict(PudFault::BadSide, sections[kSide].offset, kSide);
    }
    if (!AllBytesIn(data, sections[kAipl], pud_rules::kAipl)) {
        return Verdict(PudFault::BadAiPlayer, sections[kAipl].offset, kAipl);
    }

    for (TagIndex t : {kMtxm, kSqm, kRegm}) {
        if (sections[t].length != 2 * cells) return Verdict(PudFault::BadLength, sections[t].offset, t);
    }
    if (sections[kOilm].length != cells && sections[kOilm].length != 2 * cells) {
        return Verdict(PudFault::BadLength, sections[kOilm].offset, kOilm);
    }

    {
        const Section& mtxm = sections[kMtxm];
        for (uint32_t i = 0; i < mtxm.length; i += 2) {
            if ((Read16(data + mtxm.offset + i) >> 4) >= pud_rules::kTileRows) {
                return Verdict(PudFault::BadTile, mtxm.offset + i, kMtxm);
            }
        }
    }

    const Section& udta = sections[kUdta];
    const bool customUnits = Read16(data + udta.offset) == 0;
    const uint8_t* sizes = data + udta.offset + pud_rules::kUdtaSizeOffset;
    for (unsigned t = 0; t < pud_rules::kUdtaUnitTypes; ++t) {
        const uint16_t w = Read16(sizes + 4 * t);
        if (!InSet(w, pud_rules::kUnitSize) || Read16(sizes + 4 * t + 2) != w) {
            return Verdict(PudFault::BadUnitSize, udta.offset + pud_rules::kUdtaSizeOffset + 4 * t, kUdta);
        }
    }

    if (customUnits) {
        if (const uint32_t at = FirstBadField(data + udta.offset, pud_rules::kUdtaRules)) {
            return Verdict(PudFault::BadUnitData, udta.offset + at, kUdta);
        }
    }
    {
        const Section& ugrd = sections[kUgrd];
        if (Read16(data + ugrd.offset) == 0) {
            if (const uint32_t at = FirstBadField(data + ugrd.offset, pud_rules::kUgrdRules)) {
                return Verdict(PudFault::BadUpgradeData, ugrd.offset + at, kUgrd);
            }
        }
    }
    {
        const Section& sqm = sections[kSqm];
        for (uint32_t i = 0; i < sqm.length; i += 2) {
            if (!InSet(Read16(data + sqm.offset + i), pud_rules::kSqmValues)) {
                return Verdict(PudFault::BadTerrainFlags, sqm.offset + i, kSqm);
            }
        }
    }
    if (!AllBytesIn(data, sections[kOilm], pud_rules::kOilmValues)) {
        return Verdict(PudFault::BadOilMap, sections[kOilm].offset, kOilm);
    }

    const Section& unit = sections[kUnit];
    if (unit.length % 8 != 0 || unit.length / 8 > kMaxUnits) {
        return Verdict(PudFault::BadUnitCount, unit.offset, kUnit);
    }
    for (uint32_t i = 0; i < unit.length / 8; ++i) {
        const uint8_t* record = data + unit.offset + i * 8;
        const uint32_t at = unit.offset + i * 8;
        if (!InSet(record[4], pud_rules::kUnitType)) return Verdict(PudFault::BadUnitType, at, kUnit);

        uint32_t size = pud_rules::kFootprint[record[4]];
        if (customUnits) {
            const uint32_t own = Read16(sizes + 4 * record[4]);
            if (own > size) size = own;
        }
        if (size == 0) size = 1;
        if (Read16(record) + size > width || Read16(record + 2) + size > height) {
            return Verdict(PudFault::UnitOutsideMap, at, kUnit);
        }
        if (!InSet(record[5], pud_rules::kUnitOwner)) return Verdict(PudFault::BadUnitOwner, at, kUnit);
    }

    return Verdict(PudFault::Ok, 0);
}

const char* PudFaultName(PudFault fault) {
    switch (fault) {
        case PudFault::Ok: return "ok";
        case PudFault::TooSmall: return "too small";
        case PudFault::TooLarge: return "too large";
        case PudFault::TruncatedHeader: return "truncated section header";
        case PudFault::TruncatedSection: return "section runs past the end";
        case PudFault::UnknownTag: return "unknown section";
        case PudFault::DuplicateTag: return "duplicate section";
        case PudFault::FirstTagNotType: return "first section is not TYPE";
        case PudFault::MissingTag: return "required section missing";
        case PudFault::BadLength: return "section has the wrong length";
        case PudFault::BadType: return "TYPE is not a WAR2 MAP";
        case PudFault::BadVersion: return "unknown VER";
        case PudFault::DescNotTerminated: return "DESC is not NUL-terminated";
        case PudFault::BadOwner: return "OWNR value not seen in real maps";
        case PudFault::BadEra: return "ERA value not seen in real maps";
        case PudFault::BadDimensions: return "DIM not seen in real maps";
        case PudFault::BadSide: return "SIDE value not seen in real maps";
        case PudFault::BadAiPlayer: return "AIPL value not seen in real maps";
        case PudFault::BadUnitCount: return "UNIT length or count out of range";
        case PudFault::UnitOutsideMap: return "unit outside the map";
        case PudFault::BadUnitType: return "unit type not seen in real maps";
        case PudFault::BadUnitOwner: return "unit owner not seen in real maps";
        case PudFault::BadTile: return "tile code past the tileset";
        case PudFault::BadUnitSize: return "unit size not seen in real maps";
        case PudFault::BadUnitData: return "custom unit data outside what real maps hold";
        case PudFault::BadUpgradeData: return "custom upgrade data outside what real maps hold";
        case PudFault::BadTerrainFlags: return "SQM value not seen in real maps";
        case PudFault::BadOilMap: return "OILM value not seen in real maps";
    }
    return "unknown fault";
}

}  // namespace map_download
