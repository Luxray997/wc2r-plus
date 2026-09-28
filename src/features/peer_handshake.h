// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace handshake {

constexpr unsigned kCapMapDownload = 1u << 2;

bool LobbySlotRunsMod(int lobbySlot);

bool PeerRunsMod(unsigned netSlot);
uint32_t PeerCaps(unsigned netSlot);

void SetLocalMapDownload(bool on);

}  // namespace handshake
