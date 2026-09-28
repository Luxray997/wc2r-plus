// SPDX-License-Identifier: MIT
#include "features/map_download/map_name.h"

#include "features/map_download/limits.h"

namespace map_download {
namespace {

char Lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool EqualsIgnoreCase(const char* a, size_t length, const char* lowerLiteral) {
    size_t i = 0;
    for (; i < length; ++i) {
        if (lowerLiteral[i] == '\0' || Lower(a[i]) != lowerLiteral[i]) return false;
    }
    return lowerLiteral[i] == '\0';
}

constexpr const char* kDevices[] = {
    "con",  "prn",  "aux",  "nul",  "com0", "com1", "com2", "com3", "com4",  "com5",   "com6",
    "com7", "com8", "com9", "lpt0", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5",  "lpt6",   "lpt7",
    "lpt8", "lpt9", "conin$", "conout$", "clock$",
};

}  // namespace

NameFault ValidateMapName(const char* name, size_t length) {
    if (length == 0 || name == nullptr) return NameFault::Empty;
    if (length > kMaxNameLen) return NameFault::TooLong;

    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (c < 0x20 || c > 0x7e) return NameFault::NotPrintableAscii;
        switch (c) {
            case '\\': case '/': case ':': case '*': case '?': case '"': case '<': case '>': case '|':
                return NameFault::ForbiddenChar;
            default:
                break;
        }
    }

    if (name[0] == ' ' || name[0] == '.') return NameFault::LeadingSpaceOrDot;
    if (name[length - 1] == ' ' || name[length - 1] == '.') return NameFault::TrailingSpaceOrDot;

    if (length < 4 || !EqualsIgnoreCase(name + length - 4, 4, ".pud")) return NameFault::NotPud;
    if (length == 4) return NameFault::EmptyStem;

    size_t base = 0;
    while (base < length && name[base] != '.') ++base;
    while (base > 0 && name[base - 1] == ' ') --base;
    for (const char* device : kDevices) {
        if (EqualsIgnoreCase(name, base, device)) return NameFault::DeviceName;
    }
    return NameFault::Ok;
}

const char* NameFaultName(NameFault fault) {
    switch (fault) {
        case NameFault::Ok: return "ok";
        case NameFault::Empty: return "empty";
        case NameFault::TooLong: return "too long";
        case NameFault::NotPrintableAscii: return "not printable ASCII";
        case NameFault::ForbiddenChar: return "forbidden character";
        case NameFault::LeadingSpaceOrDot: return "starts with a space or dot";
        case NameFault::TrailingSpaceOrDot: return "ends with a space or dot";
        case NameFault::NotPud: return "not a .pud";
        case NameFault::EmptyStem: return "nothing before .pud";
        case NameFault::DeviceName: return "a Windows device name";
    }
    return "unknown fault";
}

}  // namespace map_download
