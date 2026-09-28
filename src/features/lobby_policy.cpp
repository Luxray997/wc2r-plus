// SPDX-License-Identifier: MIT
#include "features/lobby_policy.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "core/hook_utils.h"
#include "core/log.h"
#include "core/mod.h"
#include "core/named_window_hook.h"
#include "core/peer_messages.h"
#include "core/settings/mod_settings.h"
#include "features/peer_handshake.h"
#include "target/addresses.h"
#include "target/struct_offsets.h"

namespace {

const Logger kLog{"lobbypolicy"};

const char* const kKeyLockTeams = "lock_teams";
const char* const kKeyLockSlots = "lock_slots";
constexpr bool kDefaultLockTeams = false;
constexpr bool kDefaultLockSlots = false;

const char* const kWindowMultiplayerLobby = "multiplayer_lobby_screen";

constexpr char kSubsystem = 'L';
constexpr unsigned char kProtocolVersion = 1;
constexpr int kMessageLen = peer_messages::kHeaderLen + 2;
constexpr int kMaxMessageLen = kMessageLen + 1 + game::kLobbyPasswordCapacity;
constexpr unsigned char kFlagTeams = 1u << 0;
constexpr unsigned char kFlagSlots = 1u << 1;
constexpr DWORD kResendMs = 2000;

constexpr DWORD kLobbyGapMs = 2000;

constexpr DWORD kInLobbyMs = 500;

ModSettings g_settings{"lobbypolicy"};

void** g_netSession = nullptr;
void* g_teamOptions = nullptr;

game::LobbyTeamChangeRequestFn g_origTeamChange = nullptr;
game::LobbySlotSwapRequestFn g_origSlotSwap = nullptr;
game::LobbySlotDropdownFn g_origDropdown = nullptr;

lobby_policy::Locks g_locks;
bool g_passwordKnown = false;
std::string g_password;
bool g_createChoiceApplied = false;
bool g_sendNow = false;
DWORD g_lastSend = 0;
DWORD g_lastSeen = 0;

std::deque<lobby_policy::LockEvent> g_events;

bool IsHost() {
    if (!g_netSession || !*g_netSession) return false;
    return *(static_cast<const unsigned char*>(*g_netSession) + 0x18) != 0;
}

bool SenderIsHost(int senderNetSlot) { return senderNetSlot == 0; }

void Touch() {
    const DWORD now = GetTickCount();
    if (g_lastSeen == 0 || now - g_lastSeen > kLobbyGapMs) {
        g_locks = lobby_policy::Locks{};
        g_passwordKnown = false;
        g_password.clear();
        g_createChoiceApplied = false;
        g_sendNow = false;
        g_lastSend = 0;
        g_events.clear();
    }
    g_lastSeen = now ? now : 1;
}

void Adopt(const lobby_policy::Locks& locks) {
    const lobby_policy::Locks before = g_locks;
    g_locks = locks;
    if (before.teams != locks.teams) g_events.push_back({true, locks.teams});
    if (before.slots != locks.slots) g_events.push_back({false, locks.slots});
    while (g_events.size() > 16) g_events.pop_front();
    if (before.teams != locks.teams || before.slots != locks.slots) {
        kLog.Info("locks: team changes %s, slot swaps %s", locks.teams ? "LOCKED" : "unlocked",
                  locks.slots ? "LOCKED" : "unlocked");
    }
}

std::string HostPassword() {
    const char* p = game::kLobbyPassword.Get();
    return p ? std::string(p, strnlen(p, game::kLobbyPasswordCapacity - 1)) : std::string();
}

void ApplyPendingMove();
void DropPendingMove();

named_window::Result OnMultiplayerLobbyWindow(void*) {
    Touch();
    g_settings.ReloadIfChanged();
    if (!IsHost() || !peer_messages::SessionConnected()) return named_window::Result::Continue;
    const unsigned char* countdown = game::kMpLobbyCountdownActive.Get();
    if (countdown && *countdown) {
        DropPendingMove();
        return named_window::Result::Continue;
    }
    ApplyPendingMove();
    const DWORD now = GetTickCount();
    if (g_sendNow || g_lastSend == 0 || now - g_lastSend >= kResendMs) {
        unsigned char msg[kMaxMessageLen];
        peer_messages::WriteHeader(msg, kSubsystem);
        msg[peer_messages::kHeaderLen] = kProtocolVersion;
        msg[peer_messages::kHeaderLen + 1] = static_cast<unsigned char>(
            (g_locks.teams ? kFlagTeams : 0) | (g_locks.slots ? kFlagSlots : 0));
        const std::string password = HostPassword();
        msg[kMessageLen] = static_cast<unsigned char>(password.size());
        memcpy(msg + kMessageLen + 1, password.data(), password.size());
        const int len = kMessageLen + 1 + static_cast<int>(password.size());
        for (unsigned ns = 1; ns < 8; ++ns) {
            if (handshake::PeerRunsMod(ns)) peer_messages::SendTo(ns, msg, len);
        }
        g_sendNow = false;
        g_lastSend = now ? now : 1;
    }
    return named_window::Result::Continue;
}

void OnLocksMessage(unsigned sender, const unsigned char* m, int len) {
    if (g_lastSeen == 0 || GetTickCount() - g_lastSeen > kInLobbyMs) return;
    if (IsHost() || !SenderIsHost(static_cast<int>(sender))) return;
    if (len < kMessageLen || m[peer_messages::kHeaderLen] != kProtocolVersion) return;
    const unsigned char flags = m[peer_messages::kHeaderLen + 1];
    lobby_policy::Locks locks;
    locks.teams = (flags & kFlagTeams) != 0;
    locks.slots = (flags & kFlagSlots) != 0;
    if (len > kMessageLen) {
        const int n = m[kMessageLen];
        if (n < game::kLobbyPasswordCapacity && kMessageLen + 1 + n <= len) {

            g_password.clear();
            for (int i = 0; i < n; ++i) {
                const unsigned char c = m[kMessageLen + 1 + i];
                g_password.push_back(c >= 0x20 && c < 0x7f ? static_cast<char>(c) : '?');
            }
            g_passwordKnown = true;
        }
    }
    Adopt(locks);
}

constexpr char kNameColumnId[] = "slotType";
constexpr char kKickKey[] = "mp_kick_player";
constexpr unsigned char kSlotSeated = 0;

struct KeyVector {
    const char* const* begin;
    const char* const* end;
    const char* const* cap;
};

int NameCellSlot(const char* label) {
    if (!label || strncmp(label, kNameColumnId, sizeof(kNameColumnId) - 1) != 0) return -1;
    const char* digits = label + sizeof(kNameColumnId) - 1;
    if (*digits < '0' || *digits > '7' || digits[1] != '\0') return -1;
    return *digits - '0';
}

const unsigned char* SlotRecord(int slot) {
    return game::kMpLobbySlots.Get() + slot * game::wc2r::kLobbySlotStride;
}

std::string GameString(const void* str) {
    const auto* b = static_cast<const unsigned char*>(str);
    const size_t len = *reinterpret_cast<const uint32_t*>(b + game::msvc::kStrSize);
    const size_t cap = *reinterpret_cast<const uint32_t*>(b + game::msvc::kStrCapacity);
    const char* data = cap > game::msvc::kStrSsoMax ? *reinterpret_cast<const char* const*>(b)
                                              : reinterpret_cast<const char*>(b);
    return data ? std::string(data, len) : std::string();
}

struct MoveMenu {
    char labels[8][32];
    int target[8];
    const char* keys[1 + 8];
    int count = 0;
};
MoveMenu g_menus[8];

struct PendingMove {
    bool pending = false;
    int from = -1;
    int to = -1;
    std::string who;
};
PendingMove g_move;

void DropPendingMove() { g_move = PendingMove{}; }

void ApplyPendingMove() {
    if (!g_move.pending) return;
    PendingMove m = g_move;
    g_move = PendingMove{};
    const unsigned char* from = SlotRecord(m.from);
    const unsigned char* to = SlotRecord(m.to);
    const char* name = reinterpret_cast<const char*>(from + game::wc2r::kLobbySlotName);
    if (from[game::wc2r::kLobbySlotState] != kSlotSeated ||
        strncmp(name, m.who.c_str(), game::wc2r::kLobbySlotNameMax) != 0 ||
        to[game::wc2r::kLobbySlotState] != game::wc2r::kLobbySlotOpen) {
        kLog.Info("move of '%s' to slot %d dropped: the lobby changed first", m.who.c_str(),
                  m.to + 1);
        return;
    }
    const unsigned char packet[3] = {0x22, static_cast<unsigned char>(m.from),
                                     static_cast<unsigned char>(m.to)};
    g_origSlotSwap(packet, 0);
    kLog.Info("moved '%s' from slot %d to slot %d", m.who.c_str(), m.from + 1, m.to + 1);
}

char HostPlayerCell(int slot, char disabled, char prevValue, const char* label, void* value,
                    char* changedFlag, float rowH, char localize) {
    MoveMenu& menu = g_menus[slot];
    menu.count = 0;
    menu.keys[0] = kKickKey;
    for (int t = 0; t < 8; ++t) {
        if (t == slot || SlotRecord(t)[game::wc2r::kLobbySlotState] != game::wc2r::kLobbySlotOpen) continue;
        _snprintf_s(menu.labels[menu.count], sizeof(menu.labels[0]), _TRUNCATE, "Move to Slot %d",
                    t + 1);
        menu.target[menu.count] = t;
        menu.keys[1 + menu.count] = menu.labels[menu.count];
        ++menu.count;
    }
    const KeyVector keys{menu.keys, menu.keys + 1 + menu.count, menu.keys + 1 + menu.count};

    const std::string before = GameString(value);
    const char picked = g_origDropdown(disabled, prevValue, label, value,
                                       const_cast<KeyVector*>(&keys), changedFlag, rowH, localize);
    if (!picked) return picked;
    const std::string now = GameString(value);
    for (int i = 0; i < menu.count; ++i) {
        if (now != menu.labels[i]) continue;

        game::kStringAssign.Get()(value, before.c_str(), before.size());
        g_move.pending = true;
        g_move.from = slot;
        g_move.to = menu.target[i];
        g_move.who.assign(reinterpret_cast<const char*>(SlotRecord(slot) + game::wc2r::kLobbySlotName),
                          strnlen(reinterpret_cast<const char*>(SlotRecord(slot) +
                                                                game::wc2r::kLobbySlotName),
                                  game::wc2r::kLobbySlotNameMax));
        return 0;
    }
    return picked;
}

constexpr char kSlotCloseKey[] = "mp_slot_close";
constexpr char kClosedText[] = "Closed";

char SlotStateCell(char disabled, char prevValue, const char* label, void* value,
                   void* optionList, char* changedFlag, float rowH, char localize) {
    const std::string current = GameString(value);

    const char* keys[16];
    KeyVector filtered{};
    void* list = optionList;
    const auto* in = static_cast<const KeyVector*>(optionList);
    if (disabled == 0 && in && in->begin && in->end >= in->begin && in->end - in->begin <= 16) {
        int n = 0;
        for (const char* const* k = in->begin; k != in->end; ++k) {
            if (*k && current == *k) continue;
            keys[n++] = *k;
        }
        filtered = KeyVector{keys, keys + n, keys + n};
        list = &filtered;
    }

    const bool closed = current == kSlotCloseKey;
    if (closed) game::kStringAssign.Get()(value, kClosedText, sizeof(kClosedText) - 1);
    const char picked =
        g_origDropdown(disabled, prevValue, label, value, list, changedFlag, rowH, localize);
    if (closed && !picked) game::kStringAssign.Get()(value, current.c_str(), current.size());
    return picked;
}

bool IsKickOnlyList(const void* optionList) {
    const auto* v = static_cast<const KeyVector*>(optionList);
    return v && v->begin && v->end == v->begin + 1 && v->begin[0] &&
           strcmp(v->begin[0], kKickKey) == 0;
}

char __cdecl DropdownDetour(char disabled, char prevValue, const char* label, void* value,
                             void* optionList, char* changedFlag, float rowH, char localize) {
    Touch();
    if (optionList == g_teamOptions) {
        if (IsHost()) {
            disabled = 0;
        } else if (g_locks.teams) {
            disabled = 1;
        }
    }
    const int slot = NameCellSlot(label);
    if (slot >= 0 && disabled == 0) {
        if (IsHost() && value && IsKickOnlyList(optionList)) {
            return HostPlayerCell(slot, disabled, prevValue, label, value, changedFlag, rowH,
                                  localize);
        }
        if (!IsHost() && g_locks.slots) disabled = 1;
    }
    if (slot >= 0 && value) {
        return SlotStateCell(disabled, prevValue, label, value, optionList, changedFlag, rowH,
                             localize);
    }
    return g_origDropdown(disabled, prevValue, label, value, optionList, changedFlag, rowH,
                           localize);
}

void __cdecl TeamChangeDetour(const unsigned char* packet, int senderNetSlot) {
    Touch();
    if (g_locks.teams && !SenderIsHost(senderNetSlot)) {
        kLog.Info("refused a team change from net slot %d (team changes locked)", senderNetSlot);
        return;
    }
    g_origTeamChange(packet, senderNetSlot);
}

void __cdecl SlotSwapDetour(const unsigned char* packet, int senderNetSlot) {
    Touch();
    if (g_locks.slots && !SenderIsHost(senderNetSlot)) {
        kLog.Info("refused a slot swap from net slot %d (slot swaps locked)", senderNetSlot);
        return;
    }
    g_origSlotSwap(packet, senderNetSlot);
}

class LobbyPolicyMod : public IMod {
public:
    const char* Name() const override { return "lobbypolicy"; }

    bool Install() override {
        g_settings.BeginGroup("Multiplayer", "Lobby");
        g_settings.RegisterBool(kKeyLockTeams, kDefaultLockTeams, "Lock Team Changes",
                                 "The Create Multiplayer Game screen's last choice.");
        g_settings.SetHidden(kKeyLockTeams);
        g_settings.RegisterBool(kKeyLockSlots, kDefaultLockSlots, "Lock Slot Swaps",
                                 "The Create Multiplayer Game screen's last choice.");
        g_settings.SetHidden(kKeyLockSlots);
        g_settings.ReloadIfChanged();

        g_netSession = game::kNetSession.Get();
        g_teamOptions = game::kMpLobbyTeamOptions.Get();

        const bool a = InstallHook(game::kLobbySlotDropdown.Target(),
                                    reinterpret_cast<void*>(&DropdownDetour),
                                    reinterpret_cast<void**>(&g_origDropdown), "LobbySlotDropdown");
        const bool b = InstallHook(game::kHandleLobbyTeamChangeRequest.Target(),
                                    reinterpret_cast<void*>(&TeamChangeDetour),
                                    reinterpret_cast<void**>(&g_origTeamChange),
                                    "LobbyTeamChangeRequest");
        const bool c = InstallHook(game::kHandleLobbyExchangeTwoSlotsRequest.Target(),
                                    reinterpret_cast<void*>(&SlotSwapDetour),
                                    reinterpret_cast<void**>(&g_origSlotSwap),
                                    "LobbyExchangeTwoSlotsRequest");
        const bool d = peer_messages::Register(kSubsystem, &OnLocksMessage);
        const bool e = named_window::Register(kWindowMultiplayerLobby, &OnMultiplayerLobbyWindow);

        kLog.Info("ready");
        return a && b && c && d && e;
    }
};

}  // namespace

namespace lobby_policy {

Locks Current() {
    Touch();
    return g_locks;
}

void SetLocks(const Locks& locks) {
    Touch();
    if (!IsHost()) return;
    Adopt(locks);
    g_sendNow = true;
}

void ApplyCreateChoiceOnce() {
    Touch();
    if (g_createChoiceApplied || !IsHost()) return;
    g_createChoiceApplied = true;
    SetLocks(CreateChoice());
}

Locks CreateChoice() {
    g_settings.ReloadIfChanged();
    Locks l;
    l.teams = g_settings.GetBool(kKeyLockTeams);
    l.slots = g_settings.GetBool(kKeyLockSlots);
    return l;
}

void SetCreateChoice(const Locks& locks) {
    const std::vector<SettingDef>& defs = g_settings.Descriptors();
    for (int i = 0; i < static_cast<int>(defs.size()); ++i) {
        if (defs[i].key == kKeyLockTeams) g_settings.SetNumAt(i, locks.teams ? 1.0 : 0.0);
        if (defs[i].key == kKeyLockSlots) g_settings.SetNumAt(i, locks.slots ? 1.0 : 0.0);
    }
    g_settings.Save();
}

bool Password(std::string* out) {
    Touch();
    if (IsHost()) {
        *out = HostPassword();
        return true;
    }
    *out = g_password;
    return g_passwordKnown;
}

bool NextLockEvent(LockEvent* out) {
    Touch();
    if (g_events.empty()) return false;
    *out = g_events.front();
    g_events.pop_front();
    return true;
}

}  // namespace lobby_policy

MOD_REGISTER(LobbyPolicyMod)
