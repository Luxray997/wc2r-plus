// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "features/map_download/outbox.h"
#include "features/map_download/sha256.h"
#include "features/map_download/wire.h"

namespace map_download {

class DownloadHost {
 public:
    void SetServing(bool on) { serving_ = on; }

    void SetServableMap(const char* name, size_t nameLen, const uint8_t* bytes, size_t size);

    void ClearServableMap();

    void NotePeerJoined(unsigned slot, uint32_t nowMs, bool canDownload = true);
    void NotePeerLeft(unsigned slot);
    void Reset();

    void OnClientMessage(uint32_t nowMs, unsigned senderSlot, const wire::Message& m);
    void Tick(uint32_t nowMs);

    void SetCountdown(bool running) { countdown_ = running; }

    bool AnyPending(uint32_t nowMs) const;

    int pendingSlot(uint32_t nowMs) const;

    std::vector<OutMessage>& outbox() { return outbox_; }
    std::vector<uint8_t>& disconnects() { return disconnects_; }

    wire::SlotState slotState(unsigned slot) const;
    uint8_t slotPercent(unsigned slot) const;

 private:
    enum class Up { None, Offered, Uploading, Done, Failed, Stalled, LeftNoMap, NoAnswer };
    struct Slot {
        Up state = Up::None;
        uint8_t reqId[wire::kReqIdLen] = {};
        uint32_t sent = 0;
        uint32_t acked = 0;
        uint32_t offeredMs = 0;
        uint32_t acceptedMs = 0;
        uint32_t lastProgressMs = 0;
        uint32_t progressAcked = 0;
        uint32_t joinedMs = 0;
        bool present = false;
        bool inGrace = false;
        uint8_t leftWhy = 0;
        uint32_t lastRequestMs = 0;
        bool offered = false;
        uint32_t uploads = 0;
        uint32_t completedSameSha = 0;
        uint8_t completedSha[kSha256Len] = {};
        uint32_t unusedOffers = 0;
        uint8_t unusedSha[kSha256Len] = {};
        uint32_t lastDenyMs = 0;
        bool deniedOnce = false;
    };

    void Deny(uint32_t nowMs, unsigned slot, const uint8_t reqId[16], wire::Reason reason);
    void CancelOpenTransfers(wire::Reason reason);
    void EndUpload(unsigned slot, Up newState);
    void CountCompleted(Slot& s);
    void CountUnusedOffer(Slot& s);
    void Kick(unsigned slot, wire::Reason tellClient);
    int ConcurrentUploads() const;
    bool AnyUploading() const;
    wire::SlotState WireState(Up u) const;
    void MarkChanged() { progressDirty_ = true; }

    Slot slots_[wire::kSlots];
    bool serving_ = true;

    bool GraceCanHold() const { return serving_ && hasMap_; }
    bool countdown_ = false;

    bool hasMap_ = false;
    std::vector<uint8_t> map_;
    std::string mapName_;
    uint8_t mapSha_[kSha256Len] = {};

    double tokens_ = 0.0;
    uint32_t lastFillMs_ = 0;

    bool progressDirty_ = true;
    uint32_t lastProgressMs_ = 0;

    std::vector<OutMessage> outbox_;
    std::vector<uint8_t> disconnects_;
    bool clockStarted_ = false;
};

}  // namespace map_download
