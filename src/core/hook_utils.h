// SPDX-License-Identifier: MIT
#pragma once

#include "MinHook.h"
#include "core/log.h"

namespace hook_utils_detail {
inline bool& Live() {
    static bool live = false;
    return live;
}
}  // namespace hook_utils_detail

inline bool InstallHook(void* target, void* detour, void** outOriginal, const char* label) {
    MH_STATUS st = MH_CreateHook(target, detour, outOriginal);
    if (st == MH_OK) {
        st = hook_utils_detail::Live() ? MH_EnableHook(target) : MH_QueueEnableHook(target);
    }
    const Logger log{"hooks"};
    if (st == MH_OK) {
        log.Info("%s hooked at %p", label, target);
    } else {
        log.Error("%s NOT hooked at %p (MH_STATUS %d)", label, target, st);
    }
    return st == MH_OK;
}

inline bool ActivateQueuedHooks() {
    const MH_STATUS st = MH_ApplyQueued();
    hook_utils_detail::Live() = true;
    const Logger log{"hooks"};
    if (st == MH_OK) {
        log.Info("all hooks enabled");
    } else {
        log.Error("enabling the queued hooks failed (MH_STATUS %d)", st);
    }
    return st == MH_OK;
}
