// SPDX-License-Identifier: MIT
#include "core/peer_messages.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "core/hook_utils.h"
#include "core/log.h"
#include "target/addresses.h"
#include "target/struct_offsets.h"

namespace peer_messages {
namespace {

const Logger kLog{"peer_messages"};

constexpr unsigned char kMagic[3] = {'W', '2', 'R'};
constexpr int kMaxHandlers = 8;

constexpr int kMaxConsumedPerCall = 64;

constexpr unsigned char kStockFileTransferType = 0x2a;
constexpr unsigned kDropLogEvery = 100;

struct GameByteVector {
    uint8_t* begin;
    uint8_t* end;
    uint8_t* cap;
};

using BroadcastFn = int(__thiscall*)(void* self, const void* buf, int len);
using SendToPeerFn = int(__thiscall*)(void* self, int peer, const void* buf, int len);
using EnumPeersFn = GameByteVector*(__thiscall*)(void* self, GameByteVector* out);

struct Entry {
    char subsystem;
    Handler handler;
};
Entry g_handlers[kMaxHandlers];
int g_handlerCount = 0;

unsigned g_droppedFileTransfer[9];

game::GetNextPeerPacketFn g_origGetNext = nullptr;

const unsigned char* Session() {
    void* const* slot = game::kNetSession.Get();
    return slot ? static_cast<const unsigned char*>(*slot) : nullptr;
}

void* Transport() {
    const unsigned char* session = Session();
    return session ? *reinterpret_cast<void* const*>(session + game::wc2r::kSessionTransport) : nullptr;
}

bool IsOurs(const unsigned char* buf, int len) {
    return buf && len >= kHeaderLen && buf[0] == kType && memcmp(buf + 1, kMagic, sizeof(kMagic)) == 0;
}

void Dispatch(unsigned sender, const unsigned char* buf, int len) {
    for (int i = 0; i < g_handlerCount; ++i) {
        if (g_handlers[i].subsystem == static_cast<char>(buf[4])) {
            g_handlers[i].handler(sender, buf, len);
            return;
        }
    }

}

void DropStockFileTransfer(unsigned sender, int len) {
    const unsigned slot = sender < 8 ? sender : 8;
    const unsigned count = ++g_droppedFileTransfer[slot];
    if (count == 1 || count % kDropLogEvery == 0) {
        kLog.Info("dropped stock 0x2a (legacy file transfer) from net slot %u (len %d, %u from "
                  "that slot so far)", sender, len, count);
    }
}

int __cdecl GetNextPeerPacketDetour(unsigned* sender, unsigned char** buf, int* len) {
    for (int i = 0; i < kMaxConsumedPerCall; ++i) {
        const int got = g_origGetNext(sender, buf, len);
        if (!got) return got;
        if (!IsOurs(*buf, *len)) {
            if (*buf && *len >= 1 && (*buf)[0] == kStockFileTransferType) {
                DropStockFileTransfer(*sender, *len);
                continue;
            }
            return got;
        }
        Dispatch(*sender, *buf, *len);
    }
    return 0;
}

}  // namespace

void WriteHeader(unsigned char* out, char subsystem) {
    out[0] = kType;
    memcpy(out + 1, kMagic, sizeof(kMagic));
    out[4] = static_cast<unsigned char>(subsystem);
}

bool Register(char subsystem, Handler handler) {
    if (!handler || g_handlerCount >= kMaxHandlers) return false;
    for (int i = 0; i < g_handlerCount; ++i) {
        if (g_handlers[i].subsystem == subsystem) return false;
    }
    g_handlers[g_handlerCount++] = {subsystem, handler};
    return true;
}

bool Install() {
    return InstallHook(game::kGetNextPeerPacket.Target(),
                       reinterpret_cast<void*>(&GetNextPeerPacketDetour),
                       reinterpret_cast<void**>(&g_origGetNext), "GetNextPeerPacket");
}

int Broadcast(const void* msg, int len) {
    void* transport = Transport();
    if (!transport) return -1;
    void** vtable = *static_cast<void***>(transport);
    return reinterpret_cast<BroadcastFn>(vtable[game::wc2r::kVtBroadcast])(transport, msg, len);
}

int SendTo(unsigned netSlot, const void* msg, int len) {
    void* transport = Transport();
    if (!transport) return -1;
    void** vtable = *static_cast<void***>(transport);
    return reinterpret_cast<SendToPeerFn>(vtable[game::wc2r::kVtSendToPeer])(transport,
                                                                 static_cast<int>(netSlot), msg,
                                                                 len);
}

int ListPeers(unsigned char* out, int max) {
    void* transport = Transport();
    if (!transport) return -1;
    void** vtable = *static_cast<void***>(transport);

    GameByteVector peers = {};
    reinterpret_cast<EnumPeersFn>(vtable[game::wc2r::kVtEnumPeers])(transport, &peers);
    int count = 0;
    if (peers.begin && peers.end > peers.begin) {
        const int n = static_cast<int>(peers.end - peers.begin);
        for (; count < n && count < max; ++count) out[count] = peers.begin[count];
    }

    if (peers.begin) {
        const size_t capacity = static_cast<size_t>(peers.cap - peers.begin);
        if (capacity < 0x1000) game::kGameOperatorDelete.Get()(peers.begin, capacity);
    }
    return count;
}

bool SessionConnected() {
    const unsigned char* session = Session();
    return session && session[game::wc2r::kSessionConnected] != 0 && Transport() != nullptr;
}

bool IsHost() {
    const unsigned char* session = Session();
    return session && session[game::wc2r::kSessionIsHost] != 0;
}

int LobbySlotOfNetSlot(unsigned netSlot) {
    if (netSlot >= 8) return -1;

    const unsigned slot = game::kLobbySlotByNetSlot.Get()[netSlot] & 0xffu;
    return slot < 8 ? static_cast<int>(slot) : -1;
}

}  // namespace peer_messages
