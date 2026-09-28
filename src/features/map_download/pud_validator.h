// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>

namespace map_download {

enum class PudFault : uint8_t {
    Ok = 0,
    TooSmall,
    TooLarge,
    TruncatedHeader,
    TruncatedSection,
    UnknownTag,
    DuplicateTag,
    FirstTagNotType,
    MissingTag,
    BadLength,
    BadType,
    BadVersion,
    DescNotTerminated,
    BadOwner,
    BadEra,
    BadDimensions,
    BadSide,
    BadAiPlayer,
    BadUnitCount,
    UnitOutsideMap,
    BadUnitType,
    BadUnitOwner,
    BadTile,
    BadUnitSize,
    BadUnitData,
    BadUpgradeData,
    BadTerrainFlags,
    BadOilMap,
};

struct PudVerdict {
    PudFault fault;
    uint32_t offset;
    char tag[5];
};

PudVerdict ValidatePud(const uint8_t* data, size_t size);

const char* PudFaultName(PudFault fault);

}  // namespace map_download
