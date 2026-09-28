// SPDX-License-Identifier: MIT
#include "core/match_start.h"

#include "core/hook_utils.h"
#include "target/addresses.h"

namespace match_start {
namespace {

constexpr int kMaxHandlers = 8;

Handler g_handlers[kMaxHandlers];
int g_count = 0;

game::ResetMapMessageSlotsFn g_real = nullptr;

void __fastcall HookedResetMapMessageSlots(void* unused) {
    for (int i = 0; i < g_count; ++i) g_handlers[i]();
    g_real(unused);
}

}  // namespace

bool Register(Handler handler) {
    if (!handler || g_count >= kMaxHandlers) return false;
    g_handlers[g_count++] = handler;
    return true;
}

bool Install() {
    return InstallHook(game::kResetMapMessageSlots.Target(),
                       reinterpret_cast<void*>(&HookedResetMapMessageSlots),
                       reinterpret_cast<void**>(&g_real), "ResetMapMessageSlots (match start)");
}

}  // namespace match_start
