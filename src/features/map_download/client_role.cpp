// SPDX-License-Identifier: MIT
#include "features/map_download/client_role.h"

#include <windows.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

#include <cstring>
#include <string>

#include "core/log.h"
#include "core/peer_messages.h"
#include "features/map_download/download_client.h"
#include "features/map_download/game_map.h"
#include "features/map_download/limits.h"
#include "core/map_index.h"
#include "features/map_download/map_store.h"
#include "features/map_download/outbox_send.h"
#include "core/sha256.h"
#include "features/peer_handshake.h"
#include "features/ui/game_list_message.h"
#include "target/addresses.h"

namespace map_download {
namespace client_role {
namespace {

const Logger kLog{"mapdl(client)"};

constexpr DWORD kHostCapWaitMs = 5000;
constexpr DWORD kFailedPopupDelayMs = 1000;

constexpr DWORD kIndexWaitMs = 6000;

constexpr DWORD kPromptClickDelayMs = 1000;

constexpr DWORD kNoPromptDeclineMs = 1500;

constexpr DWORD kRetryMs = kRequestMinIntervalMs + 500;
constexpr int kMaxRetries = 12;

DownloadClient g_client;

struct Run {
    std::string map;
    bool started = false;
    bool unsafeName = false;
    bool noRequestId = false;
    bool decided = false;
    bool mismatch = false;
    bool selectedChecked = false;
    bool stored = false;
    bool failLogged = false;
    uint32_t generation = 0;
    DWORD retryAt = 0;
    int retries = 0;
    DWORD failedAt = 0;
};
Run g_run;

bool WillRetry() { return g_client.Retryable() && g_run.retries < kMaxRetries; }
uint32_t g_generation = 0;
int g_answer = 0;
DWORD g_promptSeenMs = 0;

const char* g_pendingLeave = nullptr;
const char* g_pendingLeaveMsg = nullptr;

std::string g_heldMissName;
DWORD g_heldSince = 0;

std::string g_lookupMissName;

wire::SlotProgress g_progress[wire::kSlots] = {};

bool HostServes() { return (handshake::PeerCaps(0) & handshake::kCapMapDownload) != 0; }

bool SelectAndClearMiss(const std::string& gamePath) {
    if (!game_map::SelectByPath(gamePath)) return false;
    const std::string lobbyMap = game_map::LobbyMapName();
    if (g_heldMissName == lobbyMap) g_heldMissName.clear();
    if (g_lookupMissName == lobbyMap) g_lookupMissName.clear();
    g_run.mismatch = false;
    return true;
}

void LeaveLobby(const char* why, const char* message) {
    kLog.Info("leaving the lobby -- %s", why);
    g_heldMissName.clear();
    game::kTransitionGameState.Get()(0x16);
    void* session = *game::kNetSession.Get();
    kLog.Info("leave -- state 0x16 done, session %p", session);
    if (session) {
        game::kLeaveNetSession.Get()(session);
        *game::kMpLobbyLeftFlag.Get() = 0;
        game::kResetMpLobbyChatInput.Get()();
    }

    if (message) ui::ShowGameListMessage(message);
    kLog.Info("leave -- complete");
}

bool MakeReqId(uint8_t out[16]) {
    return BCryptGenRandom(nullptr, out, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}

void StartRun(DWORD now, const std::string& map) {
    if (g_client.state() == ClientState::Offered || g_client.state() == ClientState::Receiving ||
        g_client.state() == ClientState::Requested) {
        g_client.Abort(wire::Reason::NotCurrentMap);
        SendOutbox(&g_client.outbox());
    }
    const bool sameMap = g_run.map == map;
    const DWORD retryAt = sameMap ? g_run.retryAt : 0;
    const int retries = sameMap ? g_run.retries + 1 : 0;
    g_client = DownloadClient{};
    g_run = Run{};
    g_run.map = map;
    g_run.generation = ++g_generation;
    g_run.retryAt = retryAt;
    g_run.retries = retries;
    g_answer = 0;

    uint8_t reqId[16];
    if (!MakeReqId(reqId)) {
        kLog.Info("BCryptGenRandom failed -- not asking about '%s'", map.c_str());
        g_run.noRequestId = true;
        return;
    }
    if (!g_client.Start(now, reqId, map.c_str(), map.size())) {
        g_run.unsafeName = true;
        kLog.Info("'%s' is not a name we can request -- stock lookup only", map.c_str());
        return;
    }
    g_run.started = true;
    kLog.Info("asking the host about '%s'", map.c_str());
}

void DecideOffer(DWORD now, bool ask) {
    const uint8_t* sha = g_client.offeredSha();
    const std::string lobbyMap = game_map::LobbyMapName();
    if (!g_run.mismatch) {

        if (!g_run.selectedChecked) {
            g_run.selectedChecked = true;
            const std::string selected = game_map::SelectedMapPath();
            if (g_lookupMissName != lobbyMap && !selected.empty() &&
                game_map::FileHasSha(selected, sha)) {
                g_client.OnAlreadyHave();
                g_run.decided = true;
                kLog.Info("'%s' already selected with the host's hash", lobbyMap.c_str());
                return;
            }
        }

        if (map_index::GetState() != map_index::State::Ready &&
            now - g_client.offeredAtMs() < kIndexWaitMs) {
            return;
        }
        std::string local;
        if (map_index::FindBySha(sha, &local) && game_map::FileHasSha(local, sha) &&
            SelectAndClearMiss(local)) {
            g_client.OnAlreadyHave();
            g_run.decided = true;
            kLog.Info("'%s' found locally by hash at '%.160s' -- selected", lobbyMap.c_str(),
                      local.c_str());
            return;
        }

        g_run.mismatch = true;
        kLog.Info("no local file has the host's '%s' (%u bytes)%s", lobbyMap.c_str(),
                  g_client.offeredSize(),
                  g_lookupMissName == lobbyMap ? "" : " -- the same-named local map is DIFFERENT");
    }
    if (!ask) {
        g_client.OnConsent(now, true);
        g_run.decided = true;
        kLog.Info("downloading '%s' (ask is off)", lobbyMap.c_str());
        return;
    }
    const bool promptShown = g_promptSeenMs != 0 && now - g_promptSeenMs < 1000;
    if (g_answer != 0) {
        const bool yes = g_answer == 1;
        g_client.OnConsent(now, yes);
        g_run.decided = true;
        kLog.Info("player chose %s for '%s'", yes ? "DOWNLOAD" : "LEAVE", lobbyMap.c_str());
    } else if (!promptShown && now - g_client.offeredAtMs() >= kNoPromptDeclineMs) {

        g_client.Abort(wire::Reason::Timeout);
        g_run.decided = true;
        kLog.Info("no screen can show the prompt -- not downloading '%s'", lobbyMap.c_str());
    }
}

void StoreCompleted() {
    g_run.stored = true;
    std::wstring dir, full;
    const StoreResult r = StoreMap(g_client.name(), g_client.bytes().data(),
                                   g_client.bytes().size(), g_client.sha(), &dir, &full);
    std::string gamePath;
    map_index::ToGamePath(full, &gamePath);
    kLog.Info("download of '%s' complete (%zu bytes), store=%d at '%.160s' (err %lu)",
              g_client.name().c_str(), g_client.bytes().size(), static_cast<int>(r),
              gamePath.c_str(), r == StoreResult::WriteFailed ? GetLastError() : 0ul);
    if (r == StoreResult::Ok || r == StoreResult::AlreadyPresent) {
        map_index::Add(full);
        game_map::RegisterDirectory(dir);
        const bool ok = SelectAndClearMiss(gamePath);
        kLog.Info("selected the downloaded '%s' by path -- %s", g_client.name().c_str(),
                  ok ? "ok" : "FAILED");
        if (ok) return;

    }

    g_client.OnStoreFailed();
    SendOutbox(&g_client.outbox());
}

}  // namespace

void Reset() {
    g_client = DownloadClient{};
    g_run = Run{};
    g_answer = 0;
    g_pendingLeave = nullptr;
    g_pendingLeaveMsg = nullptr;
    memset(g_progress, 0, sizeof(g_progress));
}

void Tick(DWORD now, bool ask) {
    const std::string lobbyMap = game_map::LobbyMapName();
    if (lobbyMap.empty() || !HostServes()) return;

    const bool newMap = g_run.map != lobbyMap;

    const bool retry = !newMap && !g_run.started && WillRetry() && static_cast<int>(now - g_run.retryAt) >= 0;
    if (newMap || retry) StartRun(now, lobbyMap);
    if (!g_run.started) return;

    g_client.Tick(now);
    if (g_client.awaitingConsent() && !g_run.decided) DecideOffer(now, ask);
    SendOutbox(&g_client.outbox());

    if (g_client.state() == ClientState::Done && !g_run.stored) {
        StoreCompleted();
    } else if (g_client.state() == ClientState::Failed && !g_run.failLogged) {
        g_run.failLogged = true;
        g_run.failedAt = now;
        if (WillRetry()) {
            g_run.retryAt = now + kRetryMs;
            g_run.failLogged = false;
            g_run.started = false;
        }
        kLog.Info("'%s' ended, reason %d", g_run.map.c_str(), static_cast<int>(g_client.reason()));
    }
}

void OnHostMessage(DWORD now, unsigned sender, const wire::Message& m) {
    g_client.OnHostMessage(now, sender, m);
}

void OnHostProgress(const wire::SlotProgress slots[wire::kSlots]) {
    memcpy(g_progress, slots, sizeof(g_progress));
}

wire::SlotProgress SlotProgress(unsigned netSlot) { return g_progress[netSlot]; }

void LeaveIfMapUnobtainable(DWORD now, bool enabled) {
    if (g_heldMissName.empty()) return;
    if (g_heldMissName != game_map::LobbyMapName()) {
        g_heldMissName.clear();
        return;
    }
    const bool thisRun = g_run.map == g_heldMissName;
    const wire::Reason reason = g_client.reason();
    const char* why = nullptr;
    const char* msg = nullptr;
    wire::LeaveWhy leaveWhy = wire::LeaveWhy::HostCannotSend;
    if (!enabled) {

        if (!handshake::PeerRunsMod(0) && now - g_heldSince < kHostCapWaitMs) return;
        leaveWhy = wire::LeaveWhy::DownloadsOff;
        why = "map download is off";
        msg = "You do not have this map and map sharing is disabled";
    } else if (!HostServes() && (handshake::PeerRunsMod(0) || now - g_heldSince >= kHostCapWaitMs)) {

        why = "the host cannot send maps";

        msg = handshake::PeerRunsMod(0)
                  ? "You do not have this map and the host has sharing disabled"
                  : "You do not have this map and the host does not support map sharing";
    } else if (thisRun && g_run.noRequestId) {

        leaveWhy = wire::LeaveWhy::DownloadFailed;
        why = "no random request id could be made";
        msg = "Error while downloading map";
    } else if (thisRun && g_run.unsafeName) {
        leaveWhy = wire::LeaveWhy::BadName;
        why = "the map's name cannot be requested";

        msg = "Cannot download map with special characters";
    } else if (thisRun && g_client.state() == ClientState::Failed &&

               !WillRetry() && reason != wire::Reason::Declined && g_run.failedAt != 0 &&
               now - g_run.failedAt >= kFailedPopupDelayMs) {
        leaveWhy = wire::LeaveWhy::DownloadFailed;
        why = "the download failed";
        msg = "Error while downloading map";
    }
    if (!why) return;

    g_heldMissName.clear();
    if (*game::kMpLobbyPopup.Get() != 0) {

        kLog.Info("%s -- a popup is already open, leaving that to it", why);
        return;
    }

    if (g_promptSeenMs != 0 && now - g_promptSeenMs < 1000) {

        if (handshake::PeerRunsMod(0)) {
            uint8_t buf[wire::kMaxMessageLen];
            const size_t n = wire::EncodeLeaving(buf, sizeof(buf), leaveWhy);
            const int rc = n ? peer_messages::SendTo(0, buf, static_cast<int>(n)) : -1;
            kLog.Info("told the host why we are leaving (why %u, rc %d)",
                      static_cast<unsigned>(leaveWhy), rc);
        }
        g_pendingLeave = why;
        g_pendingLeaveMsg = msg;
        kLog.Info("%s -- leaving at the end of this frame", why);
        return;
    }

    *game::kMissingMapFlag.Get() = 1;
    kLog.Info("%s -- no screen can leave for us, showing the stock popup", why);
}

void NoteLookup(const char* name, bool found) {
    if (!name) return;
    if (!found) {
        g_lookupMissName.assign(name);

        *game::kMissingMapFlag.Get() = 0;
        g_heldMissName.assign(name);
        g_heldSince = GetTickCount();
        return;
    }
    if (g_heldMissName == name) g_heldMissName.clear();
    if (g_lookupMissName == name) g_lookupMissName.clear();
}

bool MapMissing() {
    const std::string lobbyMap = game_map::LobbyMapName();
    if (lobbyMap.empty()) return false;
    return g_lookupMissName == lobbyMap || (g_run.map == lobbyMap && g_run.mismatch);
}

DownloadPrompt Prompt(bool enabled, bool ask) {
    DownloadPrompt p;
    const DWORD now = GetTickCount();
    g_promptSeenMs = now ? now : 1;
    if (!MapMissing()) return p;

    if (*game::kMpLobbyPopup.Get() != 0) return p;
    p.mapName = game_map::LobbyMapName();
    p.generation = g_run.generation;
    const bool thisRun = g_run.map == p.mapName && g_run.started;

    if (!enabled) return p;
    if (!thisRun || g_client.state() == ClientState::Requested) {
        p.kind = DownloadPrompt::Kind::Waiting;
        return p;
    }
    switch (g_client.state()) {
        case ClientState::Offered: {
            p.sizeBytes = g_client.offeredSize();
            const DWORD shown = now - g_client.offeredAtMs();
            p.msLeft = shown < kConsentWaitMs ? kConsentWaitMs - shown : 0;
            if (g_run.decided || !ask) {
                p.kind = DownloadPrompt::Kind::Waiting;
            } else if (g_run.mismatch) {
                p.kind = DownloadPrompt::Kind::Asking;
                p.clickable = shown >= kPromptClickDelayMs;
            } else {
                p.kind = DownloadPrompt::Kind::Waiting;
            }
            return p;
        }
        case ClientState::Receiving:
            p.kind = DownloadPrompt::Kind::Downloading;
            p.sizeBytes = g_client.offeredSize();
            p.percent = g_client.percent();
            return p;
        case ClientState::Done:
            p.kind = DownloadPrompt::Kind::Downloading;
            p.percent = 100;
            return p;
        case ClientState::Failed:
            switch (g_client.reason()) {
                case wire::Reason::Declined:
                    return p;
                case wire::Reason::RateLimited:
                case wire::Reason::NotCurrentMap:
                    p.kind = DownloadPrompt::Kind::Waiting;
                    return p;
                default:

                    p.kind = DownloadPrompt::Kind::Waiting;
                    return p;
            }
        default:
            p.kind = DownloadPrompt::Kind::Waiting;
            return p;
    }
}

void AnswerPrompt(uint32_t generation, bool download) {
    if (generation != g_run.generation || g_run.decided || g_answer != 0) return;
    if (g_client.state() != ClientState::Offered) return;
    if (download) {
        if (GetTickCount() - g_client.offeredAtMs() < kPromptClickDelayMs) return;
        g_answer = 1;
        return;
    }

    g_answer = 2;
    g_run.decided = true;
    g_client.OnConsent(GetTickCount(), false);
    SendOutbox(&g_client.outbox());

    g_pendingLeave = "the player chose Leave on the download popup";
    g_pendingLeaveMsg = nullptr;
}

void PerformPendingLeave() {
    if (!g_pendingLeave) return;
    const char* why = g_pendingLeave;
    const char* msg = g_pendingLeaveMsg;
    g_pendingLeave = nullptr;
    g_pendingLeaveMsg = nullptr;
    LeaveLobby(why, msg);
}

}  // namespace client_role
}  // namespace map_download
