// SPDX-License-Identifier: MIT
#include "features/map_download/host_role.h"

#include <cstring>
#include <string>

#include "core/log.h"
#include "features/map_download/download_host.h"
#include "features/map_download/game_map.h"
#include "features/map_download/map_index.h"
#include "features/map_download/outbox_send.h"
#include "features/map_download/pud_validator.h"
#include "features/map_download/sha256.h"
#include "features/peer_handshake.h"
#include "target/addresses.h"

namespace map_download {
namespace host_role {
namespace {

const Logger kLog{"mapdl(host)"};

DownloadHost g_host;

std::string g_servedName;
uint8_t g_servedSha[kSha256Len] = {};

std::string g_attemptedName;

struct ServeKey {
    std::string name, path;
    uint64_t size = 0, mtime = 0;
    bool operator==(const ServeKey& o) const {
        return name == o.name && path == o.path && size == o.size && mtime == o.mtime;
    }
};
ServeKey g_attemptedKey;
DWORD g_lastServeStatMs = 0;
constexpr DWORD kServeStatIntervalMs = 1000;

bool g_peerNoted[wire::kSlots] = {};

void ForgetServedMap() {
    g_host.ClearServableMap();
    g_servedName.clear();
}

void UpdateServableMap() {
    const std::string name = game_map::LobbyMapName();
    if (name.empty()) {

        if (!g_servedName.empty()) ForgetServedMap();
        g_attemptedName.clear();
        return;
    }
    const DWORD now = GetTickCount();
    if (name == g_attemptedName && now - g_lastServeStatMs < kServeStatIntervalMs) return;
    g_lastServeStatMs = now;

    ServeKey key;
    key.name = name;
    key.path = game_map::SelectedMapPath();
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!key.path.empty() && GetFileAttributesExA(key.path.c_str(), GetFileExInfoStandard, &fa)) {
        key.size = (static_cast<uint64_t>(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
        key.mtime = (static_cast<uint64_t>(fa.ftLastWriteTime.dwHighDateTime) << 32) |
                    fa.ftLastWriteTime.dwLowDateTime;
    }
    if (name == g_attemptedName && key == g_attemptedKey) return;
    const bool sameName = name == g_attemptedName;
    g_attemptedName = name;
    g_attemptedKey = key;

    const std::string& path = key.path;
    std::string bytes;
    std::wstring finalPath;
    const game_map::ServeRead read =
        path.empty() ? game_map::ServeRead::Unreadable
                     : game_map::ReadServableFile(path, &bytes, &finalPath);
    if (read != game_map::ServeRead::Ok) {
        std::string finalNarrow;
        ToGamePath(finalPath, &finalNarrow);
        if (read == game_map::ServeRead::OutsideMaps) {
            kLog.Info("selected map '%s' resolves to '%.200s', outside the game's Maps folders -- "
                      "not serving", name.c_str(), finalNarrow.c_str());
        } else {
            kLog.Info("selected map '%s' path '%.120s' unreadable -- not serving", name.c_str(),
                      path.c_str());
        }
        ForgetServedMap();
        return;
    }
    const PudVerdict v = ValidatePud(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    if (v.fault != PudFault::Ok) {
        kLog.Info("selected map '%s' failed validation (%s) -- not serving", name.c_str(),
                  PudFaultName(v.fault));
        ForgetServedMap();
        return;
    }
    uint8_t sha[kSha256Len];
    Sha256(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), sha);

    if (sameName && g_servedName == name && memcmp(sha, g_servedSha, kSha256Len) == 0) return;
    if (sameName && g_servedName == name) {
        kLog.Info("'%s' changed on disk -- re-validated, open transfers cancelled", name.c_str());
    }
    g_host.SetServableMap(name.c_str(), name.size(),
                          reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    memcpy(g_servedSha, sha, kSha256Len);
    g_servedName = name;
    kLog.Info("serving '%s' (%zu bytes, validated)", name.c_str(), bytes.size());
}

void TrackPeers(DWORD now) {
    for (unsigned s = 1; s < wire::kSlots; ++s) {
        const bool modded = handshake::PeerRunsMod(s);
        if (modded && !g_peerNoted[s]) {
            g_peerNoted[s] = true;
            g_host.NotePeerJoined(s, now,
                                  (handshake::PeerCaps(s) & handshake::kCapMapDownload) != 0);
        } else if (!modded && g_peerNoted[s]) {
            g_peerNoted[s] = false;
            g_host.NotePeerLeft(s);
        }
    }
}

}  // namespace

void Reset() {
    g_host.Reset();
    g_servedName.clear();
    g_attemptedName.clear();
    memset(g_peerNoted, 0, sizeof(g_peerNoted));
}

void Tick(DWORD now, bool serving) {
    g_host.SetServing(serving);
    if (serving) {
        UpdateServableMap();
    } else if (!g_attemptedName.empty()) {
        ForgetServedMap();
        g_attemptedName.clear();
    }

    TrackPeers(now);

    const unsigned char* countdown = game::kMpLobbyCountdownActive.Get();
    g_host.SetCountdown(countdown && *countdown != 0);
    g_host.Tick(now);
    SendOutbox(&g_host.outbox());
    for (uint8_t slot : g_host.disconnects()) {

        if (slot == 0 || slot >= wire::kSlots) continue;
        kLog.Info("kicking net slot %u (stalled, declined or no answer)", slot);
        game::kKickLobbyPeer.Get()(slot, 0);
    }
    g_host.disconnects().clear();
}

void OnMessage(DWORD now, unsigned sender, const wire::Message& m) {
    g_host.OnClientMessage(now, sender, m);
}

wire::SlotProgress SlotProgress(unsigned netSlot) {
    return wire::SlotProgress{g_host.slotState(netSlot), g_host.slotPercent(netSlot)};
}

bool AnyPending(DWORD now) { return g_host.AnyPending(now); }
int PendingSlot(DWORD now) { return g_host.pendingSlot(now); }

}  // namespace host_role
}  // namespace map_download
