// SPDX-License-Identifier: MIT
#include "features/net_watch.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>

#include "core/log.h"
#include "core/mod.h"
#include "target/addresses.h"

namespace {

using namespace game;

const Logger kLog{"netwatch"};

constexpr int kNetSlots = 8;
constexpr DWORD kStallLogMs = 1500;

void FormatSlots(const uint32_t* status, char* out, size_t size) {
    size_t used = 0;
    out[0] = '\0';
    for (int i = 0; i < kNetSlots && used < size; ++i) {
        if (status[i] == 0) continue;
        int n = snprintf(out + used, size - used, "%s%d=%05x", used ? " " : "", i, status[i]);
        if (n < 0) break;
        used += static_cast<size_t>(n);
    }
    if (out[0] == '\0') snprintf(out, size, "(none)");
}

class NetWatchMod : public IMod {
public:
    const char* Name() const override { return "netwatch"; }

    bool Install() override {
        kLog.Info("ready (read-only; logs sync mismatches and turn stalls in networked matches)");
        return true;
    }

    void OnTick() override {
        if (!ModRegistry::Get().InNetworkedMatch()) return;

        const uint32_t turn = *kNetTurnCounter.Get();
        const uint32_t* status = kTurnPlayerStatus.Get();
        const DWORD now = GetTickCount();
        char slots[128];

        if (!seenMatch_ || turn < lastTurn_) {
            seenMatch_ = true;
            lastTurn_ = turn;
            lastAdvance_ = now;
            stallLogged_ = false;
            lastMismatchTurn_ = UINT32_MAX;
            FormatSlots(status, slots, sizeof(slots));
            kLog.Info("networked match, turn %u, slots %s", turn, slots);
        }

        if (turn != lastTurn_) {
            if (stallLogged_) {
                kLog.Info("turns resumed after %lu ms (turn %u)",
                          static_cast<unsigned long>(now - lastAdvance_), turn);
            }
            stallLogged_ = false;
            lastTurn_ = turn;
            lastAdvance_ = now;
        } else if (!stallLogged_ && now - lastAdvance_ >= kStallLogMs) {
            FormatSlots(status, slots, sizeof(slots));
            kLog.Warn("no turn for %lu ms at turn %u -- slots %s",
                      static_cast<unsigned long>(now - lastAdvance_), turn, slots);
            stallLogged_ = true;
        }

        if (*kTurnSyncMismatch.Get() != 0 && turn != lastMismatchTurn_) {
            lastMismatchTurn_ = turn;
            FormatSlots(status, slots, sizeof(slots));
            kLog.Error("SYNC CHECKSUM MISMATCH at turn %u -- slots %s -- the simulations have "
                       "diverged and the game drops the minority side", turn, slots);
        }
    }

private:
    bool seenMatch_ = false;
    bool stallLogged_ = false;
    uint32_t lastTurn_ = 0;
    uint32_t lastMismatchTurn_ = UINT32_MAX;
    DWORD lastAdvance_ = 0;
};

}  // namespace

MOD_REGISTER(NetWatchMod)
