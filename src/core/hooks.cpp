// SPDX-License-Identifier: MIT
#include <windows.h>

#include "MinHook.h"
#include "wc2r_version.h"
#include "core/build_guard.h"
#include "core/hook_utils.h"
#include "core/log.h"
#include "core/match_start.h"
#include "core/mod.h"
#include "core/named_window_hook.h"
#include "core/peer_messages.h"
#include "target/addresses.h"

void InitVersionProxy();

namespace {

using namespace game;

const Logger kLog{"loader"};

ProcessNetworkTurnFn g_realProcessNetworkTurn = nullptr;
ProcessLocalMatchTickFn g_realProcessLocalMatchTick = nullptr;

int __cdecl HookedProcessNetworkTurn() {
    const int result = g_realProcessNetworkTurn();
    ModRegistry::Get().TickAll(true);
    return result;
}

int __cdecl HookedProcessLocalMatchTick() {
    const int result = g_realProcessLocalMatchTick();
    ModRegistry::Get().TickAll(false);
    return result;
}

DWORD WINAPI InstallHooksThread(LPVOID) {
    LogRaw("\n=== " WC2R_PLUS_NAME " " WC2R_PLUS_VERSION " attached, base=%p ===\n",
           reinterpret_cast<void*>(Base()));

    if (!VerifyGameBuild()) return 1;

    if (MH_Initialize() != MH_OK) {
        kLog.Error("MH_Initialize failed");
        return 1;
    }

    InstallHook(kProcessNetworkTurn.Target(), reinterpret_cast<void*>(&HookedProcessNetworkTurn),
                reinterpret_cast<void**>(&g_realProcessNetworkTurn), "ProcessNetworkTurn");
    InstallHook(kProcessLocalMatchTick.Target(),
                reinterpret_cast<void*>(&HookedProcessLocalMatchTick),
                reinterpret_cast<void**>(&g_realProcessLocalMatchTick), "ProcessLocalMatchTick");
    named_window::Install();
    peer_messages::Install();
    match_start::Install();

    ModRegistry::Get().InstallAll();

    ActivateQueuedHooks();
    return 0;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);

        InitVersionProxy();
        CreateThread(nullptr, 0, &InstallHooksThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
