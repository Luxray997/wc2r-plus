// SPDX-License-Identifier: MIT
#include "features/map_download.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "core/hook_utils.h"
#include "core/log.h"
#include "core/mod.h"
#include "core/named_window_hook.h"
#include "core/peer_messages.h"
#include "core/settings/mod_settings.h"
#include "features/map_download/client_role.h"
#include "features/map_download/game_map.h"
#include "features/map_download/host_role.h"
#include "core/map_index.h"
#include "features/map_download/map_store.h"
#include "features/map_download/wire.h"
#include "features/peer_handshake.h"
#include "target/addresses.h"

using namespace map_download;

namespace {

const Logger kLog{"mapdl"};

const char* const kKeyEnabled = "enabled";

const char* const kKeyAutoDownload = "auto_download";

constexpr bool kDefaultEnabled = true;
constexpr bool kDefaultAutoDownload = false;
const char* const kWindowMultiplayerLobby = "multiplayer_lobby_screen";
constexpr DWORD kLobbyGapMs = 2000;

constexpr DWORD kDoneShownMs = 1000;

ModSettings g_settings{"mapdownload"};

game::SelectMapByRegistryNameFn g_origSelectMap = nullptr;
game::LobbyStartGameButtonFn g_origStartButton = nullptr;
game::War2MpBNetCtorFn g_origCtor = nullptr;
bool g_startBlockedLogged = false;

DWORD g_lastSeen = 0;
bool g_wasHost = false;

DWORD g_doneSince[wire::kSlots] = {};

wire::SlotState g_lastSlotState[wire::kSlots] = {};

std::vector<DownloadEvent> g_events;

bool Enabled() { return g_settings.GetBool(kKeyEnabled); }

bool Ask() { return !g_settings.GetBool(kKeyAutoDownload); }
bool InLobby(DWORD now) { return g_lastSeen != 0 && now - g_lastSeen <= kLobbyGapMs; }

wire::SlotProgress NetSlotProgress(unsigned netSlot) {
    return peer_messages::IsHost() ? host_role::SlotProgress(netSlot)
                                   : client_role::SlotProgress(netSlot);
}

void ResetLobby() {
    host_role::Reset();
    client_role::Reset();
    memset(g_doneSince, 0, sizeof(g_doneSince));
    memset(g_lastSlotState, 0, sizeof(g_lastSlotState));
    g_events.clear();

    handshake::SetLocalMapDownload(Enabled());
}

void StartIndexOnce() {
    static bool done = false;
    if (done) return;
    const std::wstring maps = game_map::MapsRoot();
    if (maps.empty()) return;
    done = true;
    SetDownloadRoot(maps + L"\\Download");
    if (map_index::GetState() == map_index::State::Idle) game_map::ReloadMapIndex();
    std::string narrow;
    map_index::ToGamePath(maps, &narrow);
    kLog.Info("indexing maps under '%.200s' (downloads go to Download\\V<n>)", narrow.c_str());
}

void NoteEndedDownloads() {
    for (unsigned s = 1; s < wire::kSlots; ++s) {
        const wire::SlotProgress prog = NetSlotProgress(s);
        const wire::SlotState now = prog.state;
        const wire::SlotState was = g_lastSlotState[s];
        g_lastSlotState[s] = now;
        if (now == was) continue;

        if (now == wire::SlotState::LeftNoMap) {
            const int lobbySlot = peer_messages::LobbySlotOfNetSlot(s);
            if (lobbySlot < 0) continue;
            DownloadEvent e;
            e.lobbySlot = lobbySlot;
            e.kind = DownloadEvent::Kind::LeftNoMap;
            e.why = (prog.percent >= 1 && prog.percent <= 4)
                        ? static_cast<DownloadEvent::Why>(prog.percent)
                        : DownloadEvent::Why::None;
            g_events.push_back(e);
            kLog.Info("net slot %u (lobby slot %d) left without the map (why %u)", s, lobbySlot,
                      static_cast<unsigned>(prog.percent));
            continue;
        }

        if (was != wire::SlotState::Offered && was != wire::SlotState::Uploading) continue;
        if (now != wire::SlotState::NoMap && now != wire::SlotState::Stalled &&
            now != wire::SlotState::NoAnswer) {
            continue;
        }
        const int lobbySlot = peer_messages::LobbySlotOfNetSlot(s);
        if (lobbySlot < 0) continue;
        DownloadEvent e;
        e.lobbySlot = lobbySlot;
        e.kind = now == wire::SlotState::NoMap      ? DownloadEvent::Kind::Declined
                 : now == wire::SlotState::NoAnswer ? DownloadEvent::Kind::NoAnswer
                                                    : DownloadEvent::Kind::Stalled;
        g_events.push_back(e);
        kLog.Info("net slot %u (lobby slot %d) %s", s, lobbySlot,
                  e.kind == DownloadEvent::Kind::Declined   ? "declined the map download"
                  : e.kind == DownloadEvent::Kind::NoAnswer ? "did not answer the download prompt"
                                                            : "stalled while downloading");
    }
}

void* __fastcall War2MpBNetCtorDetour(void* self, void*  ) {
    void* r = g_origCtor(self);
    StartIndexOnce();
    return r;
}

void OnMapMsg(unsigned sender, const unsigned char* msg, int len) {
    if (!InLobby(GetTickCount())) return;
    wire::Message m;
    if (wire::Decode(msg, static_cast<size_t>(len), &m) != wire::DecodeFault::Ok) return;
    const DWORD now = GetTickCount();
    if (peer_messages::IsHost()) {
        host_role::OnMessage(now, sender, m);
    } else if (sender == 0 && m.kind == wire::Kind::Progress) {

        client_role::OnHostProgress(m.slots);
    } else if (sender == 0) {
        client_role::OnHostMessage(now, sender, m);
    }
}

void __cdecl StartGameButtonDetour() {
    if (peer_messages::IsHost() && host_role::AnyPending(GetTickCount())) {
        if (!g_startBlockedLogged) {
            g_startBlockedLogged = true;
            kLog.Info("Start held -- a download is pending (slot %d)",
                      host_role::PendingSlot(GetTickCount()));
        }

        void* ctx = *game::kNkContextMenus.Get();
        if (ctx) {
            const char* label = game::kLookupLocalized.Get()("customscenario_start");
            game::kDisableBegin.Get()(ctx);
            game::kDrawMenuButton.Get()(ctx, label);
            game::kDisableEnd.Get()(ctx);
        }
        return;
    }
    g_startBlockedLogged = false;
    if (g_origStartButton) g_origStartButton();
}

bool __cdecl SelectMapByRegistryNameDetour(const char* name) {
    const bool found = g_origSelectMap(name);
    kLog.Info("lookup '%.64s' -> %s", name ? name : "(null)", found ? "hit" : "MISS");

    if (!peer_messages::IsHost() || found) client_role::NoteLookup(name, found);
    return found;
}

named_window::Result OnMultiplayerLobbyWindow(void*) {
    const DWORD now = GetTickCount();
    g_settings.ReloadIfChanged();

    const bool freshLobby = !InLobby(now);
    g_lastSeen = now;
    if (freshLobby) ResetLobby();
    StartIndexOnce();
    if (!peer_messages::SessionConnected()) return named_window::Result::Continue;
    handshake::SetLocalMapDownload(Enabled());

    const bool host = peer_messages::IsHost();
    if (host != g_wasHost) {
        g_wasHost = host;
        ResetLobby();
    }
    if (host) {
        host_role::Tick(now, Enabled());
    } else {
        if (Enabled()) client_role::Tick(now, Ask());
        client_role::LeaveIfMapUnobtainable(now, Enabled());
    }
    NoteEndedDownloads();
    return named_window::Result::Continue;
}

class MapDownloadMod : public IMod {
public:
    const char* Name() const override { return "mapdownload"; }

    bool Install() override {
        g_settings.BeginGroup("Multiplayer", "Map Sharing");
        g_settings.RegisterBool(kKeyEnabled, kDefaultEnabled, "Enable Map Sharing",
                                 "Allows sending and receiving maps in multiplayer games to other "
                                 "players with the mod.");
        g_settings.RegisterBool(kKeyAutoDownload, kDefaultAutoDownload, "Enable Auto Downloading",
                                "Automatically downloads missing maps when joining a multiplayer "
                                "lobby hosted by a player with sharing enabled.");
        g_settings.ReloadIfChanged();

        const bool lookup = InstallHook(game::kSelectMapByRegistryName.Target(),
                                        reinterpret_cast<void*>(&SelectMapByRegistryNameDetour),
                                        reinterpret_cast<void**>(&g_origSelectMap),
                                        "SelectMapByRegistryName (mapdownload)");
        const bool startHook = InstallHook(game::kMpLobbyStartGameButton.Target(),
                                           reinterpret_cast<void*>(&StartGameButtonDetour),
                                           reinterpret_cast<void**>(&g_origStartButton),
                                           "start game button (mapdownload Start lock)");
        const bool ctorHook = InstallHook(game::kWar2MpBNetCtor.Target(),
                                          reinterpret_cast<void*>(&War2MpBNetCtorDetour),
                                          reinterpret_cast<void**>(&g_origCtor),
                                          "War2MultiplayerBNet ctor (mapdownload index)");
        const bool recv = peer_messages::Register('M', &OnMapMsg);
        const bool window =
            named_window::Register(kWindowMultiplayerLobby, &OnMultiplayerLobbyWindow);
        kLog.Info("ready, %s, ask %s (receive %s, lobby %s)", Enabled() ? "ON" : "off",
                  Ask() ? "on" : "off", recv ? "ok" : "FAIL", window ? "ok" : "FAIL");
        return lookup && startHook && ctorHook && recv && window;
    }
};

}  // namespace

MOD_REGISTER(MapDownloadMod)

namespace map_download {

bool LobbyMapMissing() {
    if (peer_messages::IsHost() || !InLobby(GetTickCount())) return false;
    return client_role::MapMissing();
}

DownloadPrompt LobbyDownloadPrompt() {
    if (peer_messages::IsHost() || !InLobby(GetTickCount())) return DownloadPrompt{};
    return client_role::Prompt(Enabled(), Ask());
}

void AnswerDownloadPrompt(uint32_t generation, bool download) {
    client_role::AnswerPrompt(generation, download);
}

void PerformPendingLobbyLeave() { client_role::PerformPendingLeave(); }

bool NextDownloadEvent(DownloadEvent* out) {
    if (!out || g_events.empty()) return false;
    *out = g_events.front();
    g_events.erase(g_events.begin());
    return true;
}

int PendingDownloadSlots(int* out, int max) {
    if (!out || max <= 0 || !InLobby(GetTickCount())) return 0;
    int n = 0;
    for (unsigned s = 1; s < wire::kSlots && n < max; ++s) {
        const wire::SlotState st = NetSlotProgress(s).state;
        if (st != wire::SlotState::Offered && st != wire::SlotState::Uploading) continue;
        const int lobbySlot = peer_messages::LobbySlotOfNetSlot(s);
        if (lobbySlot >= 0) out[n++] = lobbySlot;
    }
    return n;
}

SlotDownloadStatus LobbySlotDownloadStatus(int lobbySlot) {
    SlotDownloadStatus out;
    const DWORD now = GetTickCount();
    if (lobbySlot < 0 || lobbySlot >= static_cast<int>(wire::kSlots) || !InLobby(now)) return out;
    for (unsigned s = 0; s < wire::kSlots; ++s) {
        if (peer_messages::LobbySlotOfNetSlot(s) != lobbySlot) continue;
        const wire::SlotProgress p = NetSlotProgress(s);
        if (p.state != wire::SlotState::Done) g_doneSince[s] = 0;
        switch (p.state) {
            case wire::SlotState::Offered:
                out.kind = SlotDownload::Deciding;
                break;
            case wire::SlotState::Uploading:
                out.kind = SlotDownload::Downloading;
                out.percent = p.percent > 100 ? 100 : p.percent;
                break;
            case wire::SlotState::Done:
                if (g_doneSince[s] == 0) g_doneSince[s] = now ? now : 1;
                if (now - g_doneSince[s] < kDoneShownMs) {
                    out.kind = SlotDownload::Done;
                    out.percent = 100;
                }
                break;
            case wire::SlotState::NoMap:
                out.kind = SlotDownload::NoMap;
                break;
            default:
                break;
        }
        return out;
    }
    return out;
}

}  // namespace map_download
