// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace map_download {
namespace pud_rules {

constexpr uint16_t kDim[] = {14, 18, 32, 64, 96, 128};
constexpr uint16_t kVer[] = {17, 19};
constexpr uint16_t kEra[] = {0, 1, 2};
constexpr uint16_t kErax[] = {3};
constexpr uint8_t kOwnr[] = {2, 3, 4, 5, 6, 7};
constexpr uint8_t kSide[] = {0, 1, 2};
constexpr uint8_t kAipl[] = {0, 1, 23, 25, 26, 28, 31, 67};
constexpr uint8_t kUnitType[] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14,  15,  16,  17,  18, 19,
    20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 35,  38,  39,  40,  41, 42,
    43, 44, 45, 46, 47, 49, 50, 51, 52, 53, 55, 56, 57, 58, 59,  60,  61,  62,  63, 64,
    65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,  80,  81,  82,  83, 84,
    85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104};
constexpr uint8_t kUnitOwner[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 15};

constexpr uint16_t kTileRows = 158;

constexpr uint32_t kUdtaSizeOffset = 2448;
constexpr unsigned kUdtaUnitTypes = 110;
constexpr uint16_t kUnitSize[] = {0, 1, 2, 3, 4};

constexpr uint8_t kFootprint[kUdtaUnitTypes] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 2, 2, 3, 3, 3, 3, 2, 2,
    3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    4, 4, 4, 4, 3, 3, 1, 1, 2, 2, 2, 2, 2, 4, 2, 1, 1, 1, 1, 2, 3, 4};

enum class FieldCheck : uint8_t { Range, Mask };
struct FieldRule {
    uint16_t offset;
    uint8_t size;
    uint16_t count;
    FieldCheck check;
    uint32_t min;
    uint32_t max;
};

constexpr FieldRule kUdtaRules[] = {
    {2, 2, 110, FieldCheck::Range, 0, 15},
    {1238, 4, 110, FieldCheck::Range, 0, 9},
    {1678, 2, 110, FieldCheck::Range, 0, 65535},
    {1898, 1, 110, FieldCheck::Range, 0, 255},
    {2008, 1, 110, FieldCheck::Range, 0, 255},
    {2118, 1, 110, FieldCheck::Range, 0, 255},
    {2228, 1, 110, FieldCheck::Range, 0, 255},
    {2338, 1, 110, FieldCheck::Range, 0, 255},
    {2448, 2, 220, FieldCheck::Range, 0, 4},
    {2888, 2, 220, FieldCheck::Range, 0, 127},
    {3328, 1, 110, FieldCheck::Range, 0, 255},
    {3438, 1, 110, FieldCheck::Range, 0, 255},
    {3548, 1, 110, FieldCheck::Range, 0, 255},
    {3658, 1, 110, FieldCheck::Range, 0, 255},
    {3768, 1, 110, FieldCheck::Range, 0, 1},
    {3878, 1, 110, FieldCheck::Range, 0, 255},
    {3988, 1, 110, FieldCheck::Range, 0, 255},
    {4098, 1, 110, FieldCheck::Range, 0, 255},
    {4208, 1, 110, FieldCheck::Range, 0, 1},
    {4318, 1, 110, FieldCheck::Range, 0, 1},
    {4428, 1, 110, FieldCheck::Range, 0, 29},
    {4538, 1, 110, FieldCheck::Range, 0, 2},
    {4648, 1, 110, FieldCheck::Range, 0, 255},
    {4758, 1, 110, FieldCheck::Range, 0, 50},
    {4868, 1, 58, FieldCheck::Range, 0, 6},
    {4926, 2, 110, FieldCheck::Range, 0, 32768},
    {5146, 1, 110, FieldCheck::Mask, 0, 0x7},
    {5256, 4, 110, FieldCheck::Mask, 0, 0xfffffff},
};
constexpr FieldRule kUgrdRules[] = {
    {2, 1, 52, FieldCheck::Range, 0, 250},
    {54, 2, 52, FieldCheck::Range, 0, 50000},
    {158, 2, 52, FieldCheck::Range, 0, 25555},
    {262, 2, 52, FieldCheck::Range, 0, 25555},
    {366, 2, 52, FieldCheck::Range, 6, 169},
    {470, 2, 52, FieldCheck::Range, 0, 20},
    {574, 4, 52, FieldCheck::Mask, 0, 0x1fffff},
};
constexpr uint16_t kSqmValues[] = {0, 1, 2, 17, 64, 129, 130, 137, 141, 513, 576, 2445, 2817};
constexpr uint8_t kOilmValues[] = {0, 248, 249, 250, 251, 252, 253, 254, 255};

constexpr char kTypePrefix[10] = {'W', 'A', 'R', '2', ' ', 'M', 'A', 'P', 0, 0};

}  // namespace pud_rules
}  // namespace map_download
