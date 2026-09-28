// SPDX-License-Identifier: MIT
#include "core/named_window_hook.h"

#include <cstring>

#include "core/hook_utils.h"
#include "target/addresses.h"

namespace named_window {
namespace {

constexpr int kMaxHandlers = 8;

struct Entry {
    const char* name;
    Handler handler;
};

Entry g_entries[kMaxHandlers];
int g_count = 0;

game::BeginNamedWindowFn g_real = nullptr;

int __cdecl HookedBeginNamedWindow(void* ctx, const char* name, float x, float y, float w, float h,
                                    unsigned int flags) {
    const int opened = g_real(ctx, name, x, y, w, h, flags);
    if (!opened || !name) return opened;

    bool suppress = false;
    for (int i = 0; i < g_count; ++i) {
        if (strcmp(name, g_entries[i].name) != 0) continue;
        if (g_entries[i].handler(ctx) == Result::Suppress) suppress = true;
    }
    return suppress ? 0 : opened;
}

}  // namespace

bool Register(const char* windowName, Handler handler) {
    if (!windowName || !handler || g_count >= kMaxHandlers) return false;
    g_entries[g_count++] = {windowName, handler};
    return true;
}

bool Install() {
    return InstallHook(game::kBeginNamedWindow.Target(),
                       reinterpret_cast<void*>(&HookedBeginNamedWindow),
                       reinterpret_cast<void**>(&g_real), "NuiBeginNamedWindow");
}

}  // namespace named_window
