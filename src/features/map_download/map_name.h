// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>

namespace map_download {

enum class NameFault {
    Ok = 0,
    Empty,
    TooLong,
    NotPrintableAscii,
    ForbiddenChar,
    LeadingSpaceOrDot,
    TrailingSpaceOrDot,
    NotPud,
    EmptyStem,
    DeviceName,
};

NameFault ValidateMapName(const char* name, size_t length);

const char* NameFaultName(NameFault fault);

}  // namespace map_download
