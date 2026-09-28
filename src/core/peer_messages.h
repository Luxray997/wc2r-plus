// SPDX-License-Identifier: MIT
#pragma once

namespace peer_messages {

constexpr unsigned char kType = 0xE7;
constexpr int kHeaderLen = 5;

void WriteHeader(unsigned char* out, char subsystem);

using Handler = void (*)(unsigned senderNetSlot, const unsigned char* msg, int len);

bool Register(char subsystem, Handler handler);

bool Install();

int Broadcast(const void* msg, int len);
int SendTo(unsigned netSlot, const void* msg, int len);

int ListPeers(unsigned char* out, int max);

bool SessionConnected();
bool IsHost();

int LobbySlotOfNetSlot(unsigned netSlot);

}  // namespace peer_messages
