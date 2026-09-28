// SPDX-License-Identifier: MIT
#include "features/peer_handshake.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "core/mod.h"
#include "core/log.h"
#include "core/named_window_hook.h"
#include "core/peer_messages.h"
#include "features/ui/screen_kit.h"
#include "target/addresses.h"
#include "target/struct_offsets.h"
#include "wc2r_version.h"

namespace {

const Logger kLog{"handshake"};

const char* const kWindowMultiplayerLobby = "multiplayer_lobby_screen";

constexpr char kSubsystem = 'H';
constexpr unsigned char kProtocolVersion = 1;
constexpr unsigned char kKindHello = 1;
constexpr unsigned char kKindReply = 2;
constexpr int kMessageLen = 35;
constexpr int kVersionLen = 16;

constexpr uint32_t kCapHandshake = 1u << 0;
constexpr uint32_t kCapMapDownloadBit = 1u << 2;
bool g_localMapDownload = false;

constexpr char kLoaderVersion[] = WC2R_PLUS_VERSION;
static_assert(sizeof(kLoaderVersion) - 1 <= kVersionLen, "the handshake carries kVersionLen bytes");

constexpr DWORD kLobbyGapMs = 2000;
constexpr DWORD kRosterCheckMs = 1000;

constexpr DWORD kInLobbyMs = 500;

struct Peer {
    bool known;
    bool replied;
    uint32_t nonce;
    uint32_t caps;
    uint32_t simHash;
    char version[kVersionLen + 1];
};

Peer g_peers[8];

constexpr DWORD kNonceChangeMinMs = 5000;
DWORD g_lastNonceChangeMs[8] = {};
DWORD g_lastCapsLogMs[8] = {};
uint32_t g_nonce = 0;
int g_selfNetSlot = -1;
bool g_helloPending = false;
bool g_toldBadVersion = false;
DWORD g_lastSeen = 0;
DWORD g_lastRosterCheck = 0;
unsigned char g_roster[8];
int g_rosterCount = -1;

uint32_t MakeNonce() {
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    uint32_t n = static_cast<uint32_t>(qpc.QuadPart) ^ static_cast<uint32_t>(qpc.QuadPart >> 32) ^
                 (GetTickCount() * 2654435761u) ^ GetCurrentProcessId();
    return n ? n : 1;
}

void EnsureLobby(DWORD now) {
    if (g_lastSeen != 0 && now - g_lastSeen <= kLobbyGapMs) {
        g_lastSeen = now;
        return;
    }
    memset(g_peers, 0, sizeof(g_peers));
    memset(g_lastNonceChangeMs, 0, sizeof(g_lastNonceChangeMs));
    g_nonce = MakeNonce();
    g_selfNetSlot = -1;
    g_helloPending = true;
    g_rosterCount = -1;
    g_lastRosterCheck = 0;
    g_lastSeen = now;
}

const char* SlotName(int lobbySlot) {
    if (lobbySlot < 0 || lobbySlot >= 8) return "";
    unsigned char* const* names = game::kMpLobbySlotNames.Get();
    if (!names || !*names) return "";
    return ui::StrData(*names + lobbySlot * game::wc2r::kSlotNameStride + game::wc2r::kSlotNameString);
}

void Put32(unsigned char* p, uint32_t v) {
    p[0] = static_cast<unsigned char>(v);
    p[1] = static_cast<unsigned char>(v >> 8);
    p[2] = static_cast<unsigned char>(v >> 16);
    p[3] = static_cast<unsigned char>(v >> 24);
}

uint32_t Get32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

unsigned char g_out[kMessageLen];

const unsigned char* Build(unsigned char kind) {
    memset(g_out, 0, sizeof(g_out));
    peer_messages::WriteHeader(g_out, kSubsystem);
    g_out[5] = kProtocolVersion;
    g_out[6] = kind;
    Put32(g_out + 7, g_nonce);
    Put32(g_out + 11, kCapHandshake | (g_localMapDownload ? kCapMapDownloadBit : 0));
    Put32(g_out + 15, 0);

    static_assert(19 + kVersionLen == kMessageLen, "the version is the last field");
    memcpy(g_out + 19, kLoaderVersion, sizeof(kLoaderVersion) - 1);
    return g_out;
}

void CheckRoster(DWORD now) {
    if (g_lastRosterCheck != 0 && now - g_lastRosterCheck < kRosterCheckMs) return;
    g_lastRosterCheck = now;

    unsigned char peers[8];
    const int count = peer_messages::ListPeers(peers, 8);
    if (count < 0) return;

    bool present[8] = {};
    for (int i = 0; i < count; ++i) {
        if (peers[i] < 8) present[peers[i]] = true;
    }
    bool someoneNew = false;
    if (g_rosterCount >= 0) {
        bool before[8] = {};
        for (int i = 0; i < g_rosterCount; ++i) {
            if (g_roster[i] < 8) before[g_roster[i]] = true;
        }
        for (int s = 0; s < 8; ++s) {
            if (present[s] && !before[s]) someoneNew = true;
        }
    }
    for (int s = 0; s < 8; ++s) {
        if (!present[s] && g_peers[s].known) {
            kLog.Info("net slot %d left", s);
            g_peers[s] = Peer{};
            g_lastNonceChangeMs[s] = 0;
        }
    }
    memcpy(g_roster, peers, static_cast<size_t>(count));
    g_rosterCount = count;
    if (someoneNew) g_helloPending = true;
}

named_window::Result OnMultiplayerLobbyWindow(void*) {
    const DWORD now = GetTickCount();
    EnsureLobby(now);
    if (!peer_messages::SessionConnected()) return named_window::Result::Continue;

    CheckRoster(now);
    if (g_helloPending) {
        g_helloPending = false;
        const int rc = peer_messages::Broadcast(Build(kKindHello), kMessageLen);
        kLog.Info("announced (nonce %08x, rc %d)", g_nonce, rc);
    }
    return named_window::Result::Continue;
}

void OnHandshake(unsigned sender, const unsigned char* m, int len) {

    if (g_lastSeen == 0 || GetTickCount() - g_lastSeen > kInLobbyMs) return;
    if (len < kMessageLen || m[5] != kProtocolVersion) {
        if (!g_toldBadVersion) {
            g_toldBadVersion = true;
            kLog.Info("ignoring version %u / len %d from net slot %u",
                           len > 5 ? m[5] : 0u, len, sender);
        }
        return;
    }
    const unsigned char kind = m[6];
    const uint32_t nonce = Get32(m + 7);

    if (nonce == g_nonce) {

        if (g_selfNetSlot < 0 && sender < 8) {
            g_selfNetSlot = static_cast<int>(sender);
            kLog.Info("our own announcement came back on net slot %u",
                           sender);
        }
        return;
    }
    if (sender >= 8 || nonce == 0) return;

    Peer& p = g_peers[sender];
    if (!p.known || p.nonce != nonce) {

        const DWORD now = GetTickCount();
        if (g_lastNonceChangeMs[sender] != 0 && now - g_lastNonceChangeMs[sender] < kNonceChangeMinMs) {
            return;
        }
        g_lastNonceChangeMs[sender] = now ? now : 1;
        p = Peer{};
        p.known = true;
        p.nonce = nonce;
        p.caps = Get32(m + 11);
        p.simHash = Get32(m + 15);
        memcpy(p.version, m + 19, kVersionLen);
        p.version[kVersionLen] = '\0';

        for (char& c : p.version) {
            if (c != '\0' && (c < 0x20 || c > 0x7e)) c = '?';
        }
        const int lobbySlot = peer_messages::LobbySlotOfNetSlot(sender);
        kLog.Info("net slot %u (lobby slot %d, '%s') runs wc2r-plus %s "
                       "(caps %08x, sim %08x, via %s)",
                       sender, lobbySlot, SlotName(lobbySlot), p.version, p.caps, p.simHash,
                       kind == kKindHello ? "hello" : "reply");
    } else if (p.caps != Get32(m + 11)) {

        p.caps = Get32(m + 11);
        const DWORD now = GetTickCount();
        if (now - g_lastCapsLogMs[sender] >= kNonceChangeMinMs) {
            g_lastCapsLogMs[sender] = now;
            kLog.Info("net slot %u now has caps %08x", sender, p.caps);
        }
    }

    if (kind == kKindHello && !p.replied) {
        p.replied = true;
        const int rc = peer_messages::SendTo(sender, Build(kKindReply), kMessageLen);
        kLog.Info("replied to net slot %u (rc %d)", sender, rc);
    }
}

class HandshakeMod : public IMod {
public:
    const char* Name() const override { return "handshake"; }

    bool Install() override {

        const bool receive = peer_messages::Register(kSubsystem, &OnHandshake);
        const bool window = named_window::Register(kWindowMultiplayerLobby,
                                                   &OnMultiplayerLobbyWindow);
        kLog.Info("ready");
        return receive && window;
    }
};

}  // namespace

namespace handshake {

bool PeerRunsMod(unsigned netSlot) {
    if (netSlot >= 8) return false;
    if (g_lastSeen == 0 || GetTickCount() - g_lastSeen > kLobbyGapMs) return false;
    return g_peers[netSlot].known;
}

uint32_t PeerCaps(unsigned netSlot) {
    if (netSlot >= 8 || !g_peers[netSlot].known) return 0;

    if (g_lastSeen == 0 || GetTickCount() - g_lastSeen > kLobbyGapMs) return 0;
    return g_peers[netSlot].caps;
}

void SetLocalMapDownload(bool on) {
    if (g_localMapDownload != on) {
        g_localMapDownload = on;
        g_helloPending = true;
    }
}

bool LobbySlotRunsMod(int lobbySlot) {
    if (lobbySlot < 0 || lobbySlot >= 8) return false;
    if (g_lastSeen == 0 || GetTickCount() - g_lastSeen > kLobbyGapMs) return false;
    for (int s = 0; s < 8; ++s) {
        if (!g_peers[s].known && s != g_selfNetSlot) continue;
        if (peer_messages::LobbySlotOfNetSlot(static_cast<unsigned>(s)) == lobbySlot) return true;
    }
    return false;
}

}  // namespace handshake

MOD_REGISTER(HandshakeMod)
