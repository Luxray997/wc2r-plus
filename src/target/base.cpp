// SPDX-License-Identifier: MIT
#include <windows.h>

#include "target/addresses.h"

namespace game {

uintptr_t Base() {
    static const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    return base;
}

}  // namespace game
