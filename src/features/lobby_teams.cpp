// SPDX-License-Identifier: MIT
#include "features/lobby_teams.h"

#include "features/team_colours.h"
#include "target/addresses.h"
#include "target/struct_offsets.h"

namespace lobby_teams {

using namespace game;

bool IsInMatch(uint8_t player) {
    const uint8_t owner = kPlayerOwner.Get()[player];
    return owner == 0 || owner == 1;
}

uint8_t LobbyTeam(uint8_t player) {
    return kMpLobbySlots.Get()[player * wc2r::kLobbySlotStride + wc2r::kLobbySlotTeam];
}

uint8_t TeammatesAtMatchStart(uint8_t player) {
    if (player >= team_colours::kCount) return 0;
    const uint8_t* row = kDiplomacyStance.Get() + player * wc2r::kDiplomacyRowStride;
    const uint8_t team = LobbyTeam(player);
    uint8_t mask = 0;
    for (uint8_t i = 0; i < team_colours::kCount; ++i) {
        if (i == player || !IsInMatch(i) || row[i] == 0) continue;
        if (LobbyTeam(i) == team) mask |= static_cast<uint8_t>(1u << i);
    }
    return mask;
}

}  // namespace lobby_teams
