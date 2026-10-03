// SPDX-License-Identifier: MIT
#pragma once

#include <cctype>
#include <cstdlib>
#include <string>

namespace hex_colour {

inline bool Parse(const std::string& s, unsigned* rgb) {
    if (s.size() != 6) return false;
    for (char c : s)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    *rgb = static_cast<unsigned>(std::strtoul(s.c_str(), nullptr, 16));
    return true;
}

}  // namespace hex_colour
