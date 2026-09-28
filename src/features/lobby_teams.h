// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace lobby_teams {

bool IsInMatch(uint8_t player);

uint8_t TeammatesAtMatchStart(uint8_t player);

uint8_t LobbyTeam(uint8_t player);

}  // namespace lobby_teams
