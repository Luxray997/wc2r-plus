// SPDX-License-Identifier: MIT
#include "features/map_download/download_host.h"

#include <cstring>

#include "features/map_download/limits.h"

namespace map_download {
namespace {

void PushTo(std::vector<OutMessage>* outbox, uint8_t to, size_t len,
            const uint8_t (&scratch)[wire::kMaxMessageLen]) {
    if (len == 0) return;
    OutMessage m;
    m.to = to;
    m.len = len;
    memcpy(m.bytes, scratch, len);
    outbox->push_back(m);
}

}  // namespace

void DownloadHost::CancelOpenTransfers(wire::Reason reason) {
    for (unsigned i = 1; i < wire::kSlots; ++i) {
        Slot& s = slots_[i];
        if (s.state != Up::Offered && s.state != Up::Uploading) continue;
        uint8_t scratch[wire::kMaxMessageLen];
        const size_t n = wire::EncodeReason(scratch, sizeof(scratch), wire::Kind::Cancel, s.reqId, reason);
        PushTo(&outbox_, static_cast<uint8_t>(i), n, scratch);
        s.state = Up::None;
        MarkChanged();
    }
}

void DownloadHost::ClearServableMap() {
    CancelOpenTransfers(wire::Reason::NotCurrentMap);
    hasMap_ = false;
}

void DownloadHost::SetServableMap(const char* name, size_t nameLen, const uint8_t* bytes, size_t size) {

    CancelOpenTransfers(wire::Reason::NotCurrentMap);
    map_.assign(bytes, bytes + size);
    mapName_.assign(name, nameLen);
    Sha256(map_.data(), map_.size(), mapSha_);
    hasMap_ = true;
}

void DownloadHost::NotePeerJoined(unsigned slot, uint32_t nowMs, bool canDownload) {
    if (slot == 0 || slot >= wire::kSlots) return;
    Slot& s = slots_[slot];
    s = Slot();
    s.present = true;
    s.joinedMs = nowMs;
    s.inGrace = canDownload;
    MarkChanged();
}

void DownloadHost::NotePeerLeft(unsigned slot) {
    if (slot >= wire::kSlots) return;
    slots_[slot] = Slot();
    MarkChanged();
}

void DownloadHost::Reset() {
    for (unsigned i = 0; i < wire::kSlots; ++i) slots_[i] = Slot();

    outbox_.clear();
    disconnects_.clear();
    hasMap_ = false;
    map_.clear();
    mapName_.clear();
    countdown_ = false;
    progressDirty_ = true;
    MarkChanged();
}

void DownloadHost::Deny(uint32_t nowMs, unsigned slot, const uint8_t reqId[16], wire::Reason reason) {
    Slot& s = slots_[slot];
    if (s.deniedOnce && nowMs - s.lastDenyMs < kDenyMinIntervalMs) return;
    s.deniedOnce = true;
    s.lastDenyMs = nowMs;
    uint8_t scratch[wire::kMaxMessageLen];
    const size_t n = wire::EncodeReason(scratch, sizeof(scratch), wire::Kind::Deny, reqId, reason);
    PushTo(&outbox_, static_cast<uint8_t>(slot), n, scratch);
}

void DownloadHost::CountCompleted(Slot& s) {
    if (memcmp(s.completedSha, mapSha_, kSha256Len) != 0) {
        memcpy(s.completedSha, mapSha_, kSha256Len);
        s.completedSameSha = 0;
    }
    ++s.completedSameSha;
}

void DownloadHost::CountUnusedOffer(Slot& s) {
    if (memcmp(s.unusedSha, mapSha_, kSha256Len) != 0) {
        memcpy(s.unusedSha, mapSha_, kSha256Len);
        s.unusedOffers = 0;
    }
    ++s.unusedOffers;
}

int DownloadHost::ConcurrentUploads() const {
    int n = 0;
    for (unsigned i = 0; i < wire::kSlots; ++i) {
        if (slots_[i].state == Up::Uploading) ++n;
    }
    return n;
}

void DownloadHost::OnClientMessage(uint32_t nowMs, unsigned senderSlot, const wire::Message& m) {
    if (senderSlot == 0 || senderSlot >= wire::kSlots) return;
    Slot& s = slots_[senderSlot];

    switch (m.kind) {
        case wire::Kind::Leaving: {

            if (!s.present || s.state == Up::LeftNoMap) return;
            s.state = Up::LeftNoMap;
            s.leftWhy = static_cast<uint8_t>(m.why);
            s.inGrace = false;
            MarkChanged();
            return;
        }
        case wire::Kind::Request: {
            if (!s.present) return;
            if (countdown_) { Deny(nowMs, senderSlot, m.reqId, wire::Reason::Countdown); return; }
            if (!serving_) { Deny(nowMs, senderSlot, m.reqId, wire::Reason::ServingOff); return; }
            if (!hasMap_) { Deny(nowMs, senderSlot, m.reqId, wire::Reason::MapUnservable); return; }

            if (m.nameLen != mapName_.size() ||
                memcmp(m.name, mapName_.data(), m.nameLen) != 0) {
                Deny(nowMs, senderSlot, m.reqId, wire::Reason::NotCurrentMap);
                return;
            }
            if (s.state == Up::Offered || s.state == Up::Uploading) {

                if (memcmp(m.reqId, s.reqId, wire::kReqIdLen) != 0) {
                    Deny(nowMs, senderSlot, m.reqId, wire::Reason::Busy);
                }
                return;
            }
            if (s.offered && nowMs - s.lastRequestMs < kRequestMinIntervalMs) {
                Deny(nowMs, senderSlot, m.reqId, wire::Reason::RateLimited);
                return;
            }
            const bool sameMapDoneTwice = s.completedSameSha >= kMaxCompletedSameMapPerStay &&
                                          memcmp(s.completedSha, mapSha_, kSha256Len) == 0;
            const bool offersUsedUp = s.unusedOffers >= kMaxUnusedOffersPerMapPerStay &&
                                      memcmp(s.unusedSha, mapSha_, kSha256Len) == 0;
            if (s.uploads >= kUploadsPerStay || sameMapDoneTwice || offersUsedUp) {
                Deny(nowMs, senderSlot, m.reqId, wire::Reason::RateLimited);
                return;
            }
            if (ConcurrentUploads() >= static_cast<int>(kMaxConcurrentUploads)) {
                Deny(nowMs, senderSlot, m.reqId, wire::Reason::Busy);
                return;
            }
            memcpy(s.reqId, m.reqId, wire::kReqIdLen);
            s.state = Up::Offered;
            s.offeredMs = nowMs;
            s.lastRequestMs = nowMs;
            s.offered = true;
            s.inGrace = false;
            uint8_t scratch[wire::kMaxMessageLen];
            const size_t n = wire::EncodeOffer(scratch, sizeof(scratch), s.reqId,
                                               static_cast<uint32_t>(map_.size()), mapSha_, kMaxChunk);
            PushTo(&outbox_, static_cast<uint8_t>(senderSlot), n, scratch);
            MarkChanged();
            return;
        }
        case wire::Kind::Accept: {
            if (s.state != Up::Offered || memcmp(m.reqId, s.reqId, wire::kReqIdLen) != 0) return;
            if (ConcurrentUploads() >= static_cast<int>(kMaxConcurrentUploads)) {

                uint8_t scratch[wire::kMaxMessageLen];
                const size_t n = wire::EncodeReason(scratch, sizeof(scratch), wire::Kind::Cancel,
                                                    s.reqId, wire::Reason::Busy);
                PushTo(&outbox_, static_cast<uint8_t>(senderSlot), n, scratch);
                s.state = Up::None;
                MarkChanged();
                return;
            }
            s.state = Up::Uploading;
            ++s.uploads;
            s.acceptedMs = nowMs;
            s.lastProgressMs = nowMs;
            s.progressAcked = 0;
            s.sent = 0;
            s.acked = 0;
            MarkChanged();
            return;
        }
        case wire::Kind::Ack: {
            if (s.state != Up::Uploading || memcmp(m.reqId, s.reqId, wire::kReqIdLen) != 0) return;

            if (m.nextOffset <= s.acked || m.nextOffset > s.sent) return;
            s.acked = m.nextOffset;
            if (s.acked == map_.size() || s.acked - s.progressAcked >= kProgressBytes) {
                s.lastProgressMs = nowMs;
                s.progressAcked = s.acked;
            }
            if (s.acked == map_.size()) {
                CountCompleted(s);
                EndUpload(senderSlot, Up::Done);
            } else {
                MarkChanged();
            }
            return;
        }
        case wire::Kind::Cancel: {
            if ((s.state == Up::Offered || s.state == Up::Uploading) &&
                memcmp(m.reqId, s.reqId, wire::kReqIdLen) == 0) {

                if (s.state == Up::Offered && m.reason == wire::Reason::Declined) {
                    Kick(senderSlot, wire::Reason::Declined);
                } else {
                    if (s.state == Up::Offered) CountUnusedOffer(s);
                    EndUpload(senderSlot, Up::Failed);
                }
            }
            return;
        }
        case wire::Kind::Done: {
            if (s.state == Up::Uploading && memcmp(m.reqId, s.reqId, wire::kReqIdLen) == 0) {

                CountCompleted(s);
                EndUpload(senderSlot, Up::Done);
            }
            return;
        }
        case wire::Kind::Have: {

            if (s.state != Up::Offered || memcmp(m.reqId, s.reqId, wire::kReqIdLen) != 0) return;
            CountUnusedOffer(s);
            s.state = Up::None;
            s.inGrace = false;
            MarkChanged();
            return;
        }
        default:
            return;
    }
}

void DownloadHost::EndUpload(unsigned slot, Up newState) {
    Slot& s = slots_[slot];
    s.state = newState;
    MarkChanged();
}

void DownloadHost::Kick(unsigned slot, wire::Reason tellClient) {
    disconnects_.push_back(static_cast<uint8_t>(slot));
    uint8_t scratch[wire::kMaxMessageLen];
    const size_t n = wire::EncodeReason(scratch, sizeof(scratch), wire::Kind::Cancel, slots_[slot].reqId,
                                        tellClient);
    PushTo(&outbox_, static_cast<uint8_t>(slot), n, scratch);

    const bool offered = slots_[slot].state == Up::Offered;
    EndUpload(slot, tellClient == wire::Reason::Declined ? Up::Failed
                    : offered                             ? Up::NoAnswer
                                                          : Up::Stalled);
}

void DownloadHost::Tick(uint32_t nowMs) {
    if (!clockStarted_) {
        clockStarted_ = true;
        lastFillMs_ = nowMs;
        lastProgressMs_ = nowMs;
    }

    for (unsigned i = 1; i < wire::kSlots; ++i) {
        Slot& s = slots_[i];
        if (s.state == Up::Offered) {
            if (nowMs - s.offeredMs >= kConsentWaitMs + kConsentGraceMs) Kick(i, wire::Reason::Timeout);
        } else if (s.state == Up::Uploading) {

            if (s.sent > s.acked && nowMs - s.lastProgressMs >= kStallMs) {
                Kick(i, wire::Reason::Timeout);
            } else if (nowMs - s.acceptedMs >= kUploadDeadlineMs) {
                Kick(i, wire::Reason::Timeout);
            }
        }
        if (s.inGrace && nowMs - s.joinedMs >= kJoinGraceMs) {
            s.inGrace = false;
            MarkChanged();
        }
    }

    const uint32_t elapsed = nowMs - lastFillMs_;
    lastFillMs_ = nowMs;
    tokens_ += static_cast<double>(elapsed) * kHostChunksPerSecond / 1000.0;
    if (tokens_ > kHostChunksPerSecond) tokens_ = kHostChunksPerSecond;

    bool sentSomething = true;
    while (tokens_ >= 1.0 && sentSomething) {
        sentSomething = false;
        for (unsigned i = 1; i < wire::kSlots && tokens_ >= 1.0; ++i) {
            Slot& s = slots_[i];
            if (s.state != Up::Uploading) continue;
            if (s.sent >= map_.size()) continue;
            if (s.sent - s.acked >= kWindowChunks * kMaxChunk) continue;
            const uint32_t remaining = static_cast<uint32_t>(map_.size()) - s.sent;
            const uint16_t len = static_cast<uint16_t>(remaining < kMaxChunk ? remaining : kMaxChunk);
            uint8_t scratch[wire::kMaxMessageLen];
            const size_t n = wire::EncodeChunk(scratch, sizeof(scratch), s.reqId, s.sent,
                                               map_.data() + s.sent, len);
            PushTo(&outbox_, static_cast<uint8_t>(i), n, scratch);
            s.sent += len;
            tokens_ -= 1.0;
            sentSomething = true;
        }
    }

    const bool due = nowMs - lastProgressMs_ >= kProgressMinIntervalMs;
    if ((progressDirty_ || due) && (progressDirty_ || AnyUploading())) {
        wire::SlotProgress prog[wire::kSlots];
        for (unsigned i = 0; i < wire::kSlots; ++i) {
            prog[i].state = WireState(slots_[i].state);
            prog[i].percent = slotPercent(i);
        }
        uint8_t scratch[wire::kMaxMessageLen];
        const size_t n = wire::EncodeProgress(scratch, sizeof(scratch), prog);
        PushTo(&outbox_, kToAllModded, n, scratch);
        progressDirty_ = false;
        lastProgressMs_ = nowMs;
    }
}

bool DownloadHost::AnyPending(uint32_t nowMs) const {
    for (unsigned i = 1; i < wire::kSlots; ++i) {
        const Slot& s = slots_[i];
        if (s.state == Up::Offered || s.state == Up::Uploading) return true;
        if (GraceCanHold() && s.inGrace && nowMs - s.joinedMs < kJoinGraceMs) return true;
    }
    return false;
}

int DownloadHost::pendingSlot(uint32_t nowMs) const {
    for (unsigned i = 1; i < wire::kSlots; ++i) {
        if (slots_[i].state == Up::Uploading) return static_cast<int>(i);
    }
    for (unsigned i = 1; i < wire::kSlots; ++i) {
        if (slots_[i].state == Up::Offered) return static_cast<int>(i);
    }
    for (unsigned i = 1; i < wire::kSlots; ++i) {
        if (GraceCanHold() && slots_[i].inGrace && nowMs - slots_[i].joinedMs < kJoinGraceMs) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

wire::SlotState DownloadHost::WireState(Up u) const {
    switch (u) {
        case Up::None: return wire::SlotState::None;
        case Up::Offered: return wire::SlotState::Offered;
        case Up::Uploading: return wire::SlotState::Uploading;
        case Up::Done: return wire::SlotState::Done;
        case Up::Failed: return wire::SlotState::NoMap;
        case Up::Stalled: return wire::SlotState::Stalled;
        case Up::LeftNoMap: return wire::SlotState::LeftNoMap;
        case Up::NoAnswer: return wire::SlotState::NoAnswer;
    }
    return wire::SlotState::None;
}

wire::SlotState DownloadHost::slotState(unsigned slot) const {
    return slot < wire::kSlots ? WireState(slots_[slot].state) : wire::SlotState::None;
}

uint8_t DownloadHost::slotPercent(unsigned slot) const {
    if (slot >= wire::kSlots) return 0;
    const Slot& s = slots_[slot];
    if (s.state == Up::Done) return 100;
    if (s.state == Up::LeftNoMap) return s.leftWhy;
    if (s.state == Up::Uploading && !map_.empty()) {
        return static_cast<uint8_t>(static_cast<uint64_t>(s.acked) * 100 / map_.size());
    }
    return 0;
}

bool DownloadHost::AnyUploading() const {
    for (unsigned i = 1; i < wire::kSlots; ++i) {
        if (slots_[i].state == Up::Uploading || slots_[i].state == Up::Offered) return true;
    }
    return false;
}

}  // namespace map_download
