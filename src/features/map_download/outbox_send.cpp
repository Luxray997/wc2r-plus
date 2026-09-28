// SPDX-License-Identifier: MIT
#include "features/map_download/outbox_send.h"

#include "core/peer_messages.h"
#include "features/map_download/wire.h"
#include "features/peer_handshake.h"

namespace map_download {

void SendOutbox(std::vector<OutMessage>* box) {
    for (const OutMessage& m : *box) {
        if (m.to == kToAllModded) {
            for (unsigned s = 1; s < wire::kSlots; ++s) {
                if (handshake::PeerRunsMod(s)) {
                    peer_messages::SendTo(s, m.bytes, static_cast<int>(m.len));
                }
            }
        } else {
            peer_messages::SendTo(m.to, m.bytes, static_cast<int>(m.len));
        }
    }
    box->clear();
}

}  // namespace map_download
