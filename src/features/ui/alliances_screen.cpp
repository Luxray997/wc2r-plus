// SPDX-License-Identifier: MIT
#include "features/ui/alliances_screen.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "core/log.h"
#include "core/match_start.h"
#include "core/mod.h"
#include "core/named_window_hook.h"
#include "core/settings/mod_settings.h"
#include "features/lobby_teams.h"
#include "features/team_colours.h"
#include "features/ui/screen_kit.h"
#include "target/addresses.h"
#include "target/struct_offsets.h"

namespace {

using namespace game;
using namespace ui;

const Logger kLog{"alliances"};

constexpr const char* kKeyEnabled = "enabled";

constexpr bool kDefaultEnabled = true;

ModSettings g_settings{"alliances"};

constexpr const char* kWindowName = "alliances_menu";

constexpr int kPlayers = team_colours::kCount;

constexpr Rect kTitleRect{0.0f, 70.0f, 1650.0f, 120.0f};
constexpr float kFooterX = 150.0f;
constexpr float kFooterW = 1350.0f;
constexpr float kFooterH = 135.0f;
constexpr float kFooterBottomGap = 230.0f;

constexpr float kContentX = 150.0f;
constexpr float kContentTop = 210.0f;
constexpr float kContentW = 1350.0f;
constexpr float kContentFooterGap = 98.0f;

constexpr float kHeaderRatios[4] = {0.02f, 0.48f, 0.2f, 0.3f};
constexpr float kPlayerRatios[4] = {0.1f, 0.47f, 0.25f, 0.25f};
constexpr float kHeaderRowH = 180.0f;
constexpr float kVisionLineH = 78.0f;

constexpr float kPlayerRowH = 120.0f;
constexpr float kTeamHeaderH = 70.0f;

constexpr unsigned int kTeammateColour = Rgba(0x25, 0xe6, 0x5f, 255);
constexpr unsigned int kLeftColour = Rgba(0xe6, 0x40, 0x40, 255);

bool g_ready = false;
LayoutSpaceBeginFn g_spaceBegin = nullptr;
LayoutSpaceEndFn g_spaceEnd = nullptr;
EndCurrentWindowFn g_endWindow = nullptr;
BeginNamedGroupFn g_beginGroup = nullptr;
EndGroupFn g_endGroup = nullptr;
LayoutRowRatiosFn g_rowRatios = nullptr;
LayoutRowDynamicFn g_rowDynamic = nullptr;
DrawLabelFn g_label = nullptr;
SpacingFn g_spacing = nullptr;
DisableScopeFn g_disableBegin = nullptr;
DisableScopeFn g_disableEnd = nullptr;
DipCheckboxFn g_checkbox = nullptr;
SendDiplomacyFn g_sendDiplomacy = nullptr;
AlliancesAllowedFn g_alliancesAllowed = nullptr;
DrawTeamColourIconFn g_colourIcon = nullptr;
AlliancesFooterFn g_footer = nullptr;

struct Roster {
    bool valid = false;
    bool teamGame = false;
    uint8_t present = 0;
    uint8_t human = 0;
    uint8_t team[kPlayers] = {};
    uint8_t teammatesOfMe = 0;
    uint8_t me = 0xff;
    std::string name[kPlayers];
};

Roster g_roster;

std::string FixedName(const char* p, size_t max) {
    const size_t n = strnlen(p, max);
    return n < max ? std::string(p, n) : std::string();
}

void OnMatchStart() {
    Roster r;
    r.me = *kLocalPlayer.Get();
    const uint8_t* owner = kPlayerOwner.Get();
    const uint8_t* slots = kMpLobbySlots.Get();
    for (uint8_t i = 0; i < kPlayers; ++i) {
        if (!lobby_teams::IsInMatch(i)) continue;
        const uint8_t bit = static_cast<uint8_t>(1u << i);
        r.present |= bit;
        if (owner[i] == 0) r.human |= bit;
        r.team[i] = lobby_teams::LobbyTeam(i);
        if (lobby_teams::TeammatesAtMatchStart(i)) r.teamGame = true;

        r.name[i] = FixedName(kPlayerNames.Get() + i * wc2r::kPlayerNameStride,
                              wc2r::kPlayerNameStride);
        if (r.name[i].empty()) {
            r.name[i] = FixedName(
                reinterpret_cast<const char*>(slots + i * wc2r::kLobbySlotStride +
                                              wc2r::kLobbySlotName),
                wc2r::kLobbySlotNameMax);
        }
    }
    if (r.me < kPlayers) r.teammatesOfMe = lobby_teams::TeammatesAtMatchStart(r.me);
    r.valid = true;

    kLog.Info("match start: player %u, present 0x%02x, humans 0x%02x, %s, my teammates 0x%02x",
              static_cast<unsigned>(r.me), static_cast<unsigned>(r.present),
              static_cast<unsigned>(r.human), r.teamGame ? "team game" : "no teams",
              static_cast<unsigned>(r.teammatesOfMe));
    g_roster = std::move(r);
}

struct ClanKey {
    const char* name;
    const char* key;
};
constexpr ClanKey kClanKeys[] = {
    {"Nation of Azeroth", "clan_human_blue"},       {"Stormreaver Clan", "clan_orc_blue"},
    {"Nation of Lordaeron", "clan_human_white"},    {"Dragonmaw Clan", "clan_orc_white"},
    {"Nation of Stromgarde", "clan_human_red"},     {"Blackrock Clan", "clan_orc_red"},
    {"Nation of Kul Tiras", "clan_human_green"},    {"Bleeding Hollow Clan", "clan_orc_green"},
    {"Nation of Gilneas", "clan_human_black"},      {"Black Tooth Grin Clan", "clan_orc_black"},
    {"Nation of Dalaran", "clan_human_purple"},     {"Twilight's Hammer Clan", "clan_orc_purple"},
    {"Nation of Alterac", "clan_human_orange"},     {"Burning Blade Clan", "clan_orc_orange"},
    {"Alliance Traitors", "clan_human_yellow"},     {"Horde Traitors", "clan_orc_yellow"},
    {"Flowerpicker Clan", "clan_expansion_blue"},   {"Shattered Hand Clan", "clan_expansion_white"},
    {"Warsong Clan", "clan_expansion_red"},         {"Bonechewer Clan", "clan_expansion_green"},
    {"Shadowmoon Clan", "clan_expansion_black"},    {"Thunderlord Clan", "clan_expansion_purple"},
    {"Laughing Skull Clan", "clan_expansion_yellow"},
};

std::string DisplayName(const std::string& raw) {
    for (const ClanKey& c : kClanKeys) {
        if (raw == c.name) return Localized(c.key);
    }
    return raw;
}

std::string StockName(uint8_t player) {
    const uint8_t* table = *kDipNameTable.Get();
    if (!table || player == *kLocalPlayer.Get()) return std::string();
    return GameStr(table + player * msvc::kStrStride);
}

struct Row {
    uint8_t player;
    std::string name;
    unsigned int colour;
    bool checkboxes;
};

struct Group {
    uint8_t team;
    std::vector<Row> rows;
};

bool HasLeft(uint8_t player) {
    if (!(g_roster.human & (1u << player))) return false;
    if (kPlayerMatchResult.Get()[player] == 1) return true;
    const uint32_t* lobbySlotOf = kLobbySlotByNetSlot.Get();
    const uint8_t* peers = kPeerTable.Get();
    for (int ns = 0; ns < kPlayers; ++ns) {
        if ((lobbySlotOf[ns] & 0xff) != player) continue;
        if (peers[ns * wc2r::kPeerStride] == wc2r::kPeerDeparted) return true;
    }
    return false;
}

bool IsOut(uint8_t player) {
    if (!(g_roster.present & (1u << player))) return false;
    const uint8_t result = kPlayerMatchResult.Get()[player];
    return result == 1 || result == 2 || HasLeft(player);
}

std::vector<Group> BuildGroups() {
    const uint8_t me = *kLocalPlayer.Get();
    std::vector<Group> groups;
    for (uint8_t i = 0; i < kPlayers; ++i) {
        Row row;
        row.player = i;
        const std::string stock = StockName(i);
        row.checkboxes = !stock.empty();
        const bool listed = (g_roster.present & (1u << i)) != 0;
        if (!listed && !row.checkboxes) continue;
        row.name = DisplayName(!stock.empty() ? stock : g_roster.name[i]);
        if (row.name.empty()) row.name = "?";

        if (IsOut(i)) {
            row.colour = kLeftColour;
        } else if (g_roster.teamGame && (i == me || (g_roster.teammatesOfMe & (1u << i)))) {
            row.colour = kTeammateColour;
        } else {
            row.colour = 0;
        }

        const uint8_t team = g_roster.teamGame ? g_roster.team[i] : 0;
        auto it = std::find_if(groups.begin(), groups.end(),
                               [team](const Group& g) { return g.team == team; });
        if (it == groups.end()) {
            groups.push_back(Group{team, {}});
            it = groups.end() - 1;
        }

        if (i == me) {
            it->rows.insert(it->rows.begin(), std::move(row));
        } else {
            it->rows.push_back(std::move(row));
        }
    }

    const uint8_t myTeam = (me < kPlayers && g_roster.teamGame) ? g_roster.team[me] : 0;
    std::stable_sort(groups.begin(), groups.end(), [myTeam](const Group& a, const Group& b) {
        if ((a.team == myTeam) != (b.team == myTeam)) return a.team == myTeam;
        return a.team < b.team;
    });
    return groups;
}

void DrawHeaderRow(void* ctx) {
    g_rowRatios(ctx, 0, kHeaderRowH, 4, kHeaderRatios);
    g_spacing(ctx);
    if (!g_alliancesAllowed()) g_disableBegin(ctx);
    if (g_checkbox(Localized("mp_dip_victory"), kDipAlliedVictoryCheck.Get())) g_sendDiplomacy();
    g_disableEnd(ctx);
    g_label(ctx, Localized("mp_dip_allies"), kAlignCentre);

    if (g_beginGroup(ctx, "shared_vision_title", kGroupFlags)) {

        const std::string caption = Localized("mp_dip_vision");
        const size_t space = caption.find(' ');
        if (space == std::string::npos) {
            g_rowDynamic(ctx, kHeaderRowH, 1);
            g_label(ctx, caption.c_str(), kAlignCentre);
        } else {
            g_rowDynamic(ctx, kVisionLineH, 1);
            g_label(ctx, caption.substr(0, space).c_str(), kAlignCentre);
            g_label(ctx, caption.substr(space + 1).c_str(), 0x0a);
        }
        g_endGroup(ctx);
    }
}

void DrawPlayerRow(void* ctx, const Row& row, float rowH) {
    g_rowRatios(ctx, 0, rowH, 4, kPlayerRatios);
    const unsigned int prevFont = PushFont(*kFontAlliancesRow.Get());

    g_colourIcon(row.player);

    const unsigned int prevColour = ReadCtxColour(ctx, nk::kCtxTextColour);
    if (row.colour) WriteCtxColour(ctx, nk::kCtxTextColour, row.colour);
    g_label(ctx, row.name.c_str(), kAlignLeft);
    if (row.colour) WriteCtxColour(ctx, nk::kCtxTextColour, prevColour);

    if (row.checkboxes) {
        if (kPlayerOwner.Get()[row.player] == 1) g_disableBegin(ctx);
        if (g_checkbox("", kDipAlliedChecks.Get() + row.player)) g_sendDiplomacy();
        if (g_checkbox("", kDipVisionChecks.Get() + row.player)) g_sendDiplomacy();
        g_disableEnd(ctx);
    } else {
        g_spacing(ctx);
        g_spacing(ctx);
    }
    PopFont(prevFont);
}

void DrawScreen(void* ctx, const Rect& window) {
    const bool alt = *kSkinAltFlag.Get() == 1;
    const unsigned int prevSkin = PushSkin(alt ? *kSkinAlliancesAlt.Get() : *kSkinAlliances.Get());

    const std::vector<Group> groups = BuildGroups();
    int players = 0;
    for (const Group& g : groups) players += static_cast<int>(g.rows.size());
    const int headers = g_roster.teamGame ? static_cast<int>(groups.size()) : 0;

    const float contentH = Fl(window.h - kFooterBottomGap - kContentFooterGap) - kContentTop;

    uint8_t leftMask = 0;
    for (uint8_t i = 0; i < kPlayers; ++i) {
        if (HasLeft(i)) leftMask |= static_cast<uint8_t>(1u << i);
    }
    static int loggedFor = -1;
    const int shape = (players * 16 + headers) << 8 | leftMask;
    if (loggedFor != shape) {
        loggedFor = shape;
        kLog.Info("drawing our screen: %d players in %d team headers, left 0x%02x (content %.0f, "
                  "scrolling)",
                  players, headers, static_cast<unsigned>(leftMask), contentH);
    }

    g_spaceBegin(ctx, 1, 0.0f, 1000);
    SetRect(ctx, Rect{0.0f, 0.0f, window.w, window.h - kFooterBottomGap});
    if (g_beginGroup(ctx, "main", kGroupFlags)) {
        g_spaceBegin(ctx, 1, 0.0f, 1000);
        SetRect(ctx, kTitleRect);
        g_label(ctx, Localized("mp_dip_title"), kAlignCentre);

        SetRect(ctx, Rect{kContentX, kContentTop, kContentW, contentH});

        float* scrollbarY = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(ctx) +
                                                     nk::kCtxScrollbarSize + sizeof(float));
        const float savedScrollbarY = *scrollbarY;
        *scrollbarY = 0.0f;
        const int contentOpen = g_beginGroup(ctx, "main_content", kGroupPlain);
        *scrollbarY = savedScrollbarY;
        if (contentOpen) {
            DrawHeaderRow(ctx);
            const std::string teamWord = Localized("mp_lobby_team");
            for (const Group& g : groups) {
                if (headers) {
                    g_rowDynamic(ctx, kTeamHeaderH, 1);
                    const std::string caption = teamWord + " " + std::to_string(g.team);
                    g_label(ctx, caption.c_str(), kAlignLeft);
                }
                for (const Row& row : g.rows) DrawPlayerRow(ctx, row, kPlayerRowH);
            }
            const float endScrollbarY = *scrollbarY;
            *scrollbarY = 0.0f;
            g_endGroup(ctx);
            *scrollbarY = endScrollbarY;
        }
        g_spaceEnd(ctx);
        g_endGroup(ctx);
    }

    g_footer(ctx, kFooterX, window.h - kFooterBottomGap, kFooterW, kFooterH, 0, 0);
    g_spaceEnd(ctx);

    g_endWindow(ctx);
    PopSkin(prevSkin);
}

named_window::Result OnAlliancesWindow(void* ctx) {
    g_settings.ReloadIfChanged();

    Rect window;
    const bool haveWindow = ctx && WindowRect(ctx, &window);

    if (!g_ready || !g_roster.valid || !haveWindow || !g_settings.GetBool(kKeyEnabled)) {
        return named_window::Result::Continue;
    }
    DrawScreen(ctx, window);
    return named_window::Result::Suppress;
}

void RegisterScreenSettings() {
    g_settings.BeginGroup("UI", "Enhanced UIs");

    g_settings.RegisterBool(kKeyEnabled, kDefaultEnabled, "Enhanced Alliances UI",
                            "The Alliances screen groups players by team, shows teammates in "
                            "green, and shows defeated and disconnected players in red.");
}

class AlliancesScreenMod : public IMod {
public:
    const char* Name() const override { return "alliances"; }

    bool Install() override {
        RegisterScreenSettings();
        g_settings.ReloadIfChanged();

        const bool kitReady = ui::Resolve();
        g_spaceBegin = kLayoutSpaceBegin.Get();
        g_spaceEnd = kLayoutSpaceEnd.Get();
        g_endWindow = kEndCurrentWindow.Get();
        g_beginGroup = kBeginNamedGroup.Get();
        g_endGroup = kEndGroup.Get();
        g_rowRatios = kLayoutRowRatios.Get();
        g_rowDynamic = kLayoutRowDynamic.Get();
        g_label = kDrawLabel.Get();
        g_spacing = kSpacing.Get();
        g_disableBegin = kDisableBegin.Get();
        g_disableEnd = kDisableEnd.Get();
        g_checkbox = kDipCheckbox.Get();
        g_sendDiplomacy = kSendDiplomacyFromUI.Get();
        g_alliancesAllowed = kAlliancesAllowed.Get();
        g_colourIcon = kDrawTeamColourIcon.Get();
        g_footer = kAlliancesFooter.Get();

        g_ready = kitReady && g_spaceBegin && g_spaceEnd && g_endWindow && g_beginGroup &&
                  g_endGroup && g_rowRatios && g_rowDynamic && g_label && g_spacing &&
                  g_disableBegin && g_disableEnd && g_checkbox && g_sendDiplomacy &&
                  g_alliancesAllowed && g_colourIcon && g_footer;

        const bool atStart = match_start::Register(&OnMatchStart);
        const bool registered = named_window::Register(kWindowName, &OnAlliancesWindow);
        kLog.Info("%s, replacement screen %s",
                  g_ready ? "ready" : "NOT ready (an address failed to resolve)",
                  g_settings.GetBool(kKeyEnabled) ? "ON" : "off");
        return atStart && registered;
    }
};

}  // namespace

MOD_REGISTER(AlliancesScreenMod)
