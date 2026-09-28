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

enum class ClientState {
    Idle,
    Requested,
    Offered,
    Receiving,
    Done,
    Present,
    Failed,
};

class DownloadClient {
 public:

    bool Start(uint32_t nowMs, const uint8_t reqId[16], const char* name, size_t nameLen);

    void OnHostMessage(uint32_t nowMs, unsigned senderSlot, const wire::Message& m);

    void OnConsent(uint32_t nowMs, bool accept);

    void OnAlreadyHave();

    void OnStoreFailed();

    void Tick(uint32_t nowMs);
    void Abort(wire::Reason reason);

    ClientState state() const { return state_; }
    bool awaitingConsent() const { return state_ == ClientState::Offered; }
    uint8_t percent() const;
    wire::Reason reason() const { return reason_; }

    bool Retryable() const;
    uint32_t offeredSize() const { return size_; }
    const uint8_t* offeredSha() const { return sha_; }
    uint32_t offeredAtMs() const { return offeredMs_; }
    unsigned ignored() const { return ignored_; }

    const std::vector<uint8_t>& bytes() const { return buffer_; }
    const std::string& name() const { return name_; }
    const uint8_t* sha() const { return sha_; }

    std::vector<OutMessage>& outbox() { return outbox_; }

 private:
    void Fail(wire::Reason reason, bool tellHost);
    ClientState failedFrom_ = ClientState::Idle;

    ClientState state_ = ClientState::Idle;
    wire::Reason reason_ = wire::Reason::Ok;
    uint8_t reqId_[wire::kReqIdLen] = {};
    std::string name_;
    uint32_t size_ = 0;
    uint16_t chunkLen_ = 0;
    uint8_t sha_[kSha256Len] = {};
    std::vector<uint8_t> buffer_;
    uint32_t received_ = 0;

    uint32_t startMs_ = 0;
    uint32_t lastRequestMs_ = 0;
    uint32_t requestRetries_ = 0;
    uint32_t offeredMs_ = 0;
    uint32_t acceptedMs_ = 0;
    uint32_t lastProgressMs_ = 0;
    unsigned ignored_ = 0;

    std::vector<OutMessage> outbox_;
};

}  // namespace map_download
