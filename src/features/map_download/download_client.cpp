// SPDX-License-Identifier: MIT
#include "features/map_download/download_client.h"

#include <cstring>

#include "features/map_download/limits.h"
#include "features/map_download/map_name.h"
#include "features/map_download/pud_validator.h"

namespace map_download {
namespace {

void Push(std::vector<OutMessage>* outbox, size_t len, uint8_t (&scratch)[wire::kMaxMessageLen]) {
    if (len == 0) return;
    OutMessage m;
    m.to = 0;
    m.len = len;
    memcpy(m.bytes, scratch, len);
    outbox->push_back(m);
}

}  // namespace

bool DownloadClient::Start(uint32_t nowMs, const uint8_t reqId[16], const char* name, size_t nameLen) {
    if (state_ != ClientState::Idle) return false;
    if (ValidateMapName(name, nameLen) != NameFault::Ok) return false;

    memcpy(reqId_, reqId, wire::kReqIdLen);
    name_.assign(name, nameLen);
    startMs_ = nowMs;
    lastRequestMs_ = nowMs;
    requestRetries_ = 0;

    uint8_t scratch[wire::kMaxMessageLen];
    const size_t n = wire::EncodeRequest(scratch, sizeof(scratch), reqId_, name, nameLen);
    if (n == 0) return false;
    Push(&outbox_, n, scratch);
    state_ = ClientState::Requested;
    return true;
}

void DownloadClient::OnHostMessage(uint32_t nowMs, unsigned senderSlot, const wire::Message& m) {
    if (senderSlot != 0) { ++ignored_; return; }
    if (state_ == ClientState::Idle || state_ == ClientState::Done || state_ == ClientState::Failed ||
        state_ == ClientState::Present) {
        ++ignored_;
        return;
    }
    if (memcmp(m.reqId, reqId_, wire::kReqIdLen) != 0) { ++ignored_; return; }

    uint8_t scratch[wire::kMaxMessageLen];
    switch (m.kind) {
        case wire::Kind::Offer: {
            if (state_ != ClientState::Requested) { ++ignored_; return; }

            size_ = m.size;
            chunkLen_ = m.chunkLen;
            memcpy(sha_, m.sha256, kSha256Len);
            offeredMs_ = nowMs;
            state_ = ClientState::Offered;
            return;
        }
        case wire::Kind::Deny: {
            if (state_ != ClientState::Requested) { ++ignored_; return; }
            Fail(m.reason, false);
            return;
        }
        case wire::Kind::Cancel: {
            Fail(m.reason, false);
            return;
        }
        case wire::Kind::Chunk: {
            if (state_ != ClientState::Receiving) { ++ignored_; return; }

            if (m.offset != received_ || m.dataLen == 0 || m.dataLen > size_ - received_) {
                ++ignored_;
                return;
            }
            memcpy(buffer_.data() + received_, m.data, m.dataLen);
            received_ += m.dataLen;
            lastProgressMs_ = nowMs;
            const size_t n = wire::EncodeAck(scratch, sizeof(scratch), reqId_, received_);
            Push(&outbox_, n, scratch);
            if (received_ == size_) {
                uint8_t digest[kSha256Len];
                Sha256(buffer_.data(), buffer_.size(), digest);
                if (memcmp(digest, sha_, kSha256Len) != 0) {
                    Fail(wire::Reason::HashMismatch, true);
                    return;
                }
                if (ValidatePud(buffer_.data(), buffer_.size()).fault != PudFault::Ok) {
                    Fail(wire::Reason::InvalidMap, true);
                    return;
                }
                state_ = ClientState::Done;
                const size_t d = wire::EncodeReason(scratch, sizeof(scratch), wire::Kind::Done, reqId_,
                                                    wire::Reason::Ok);
                Push(&outbox_, d, scratch);
            }
            return;
        }
        default:
            ++ignored_;
            return;
    }
}

void DownloadClient::OnConsent(uint32_t nowMs, bool accept) {
    if (state_ != ClientState::Offered) return;
    uint8_t scratch[wire::kMaxMessageLen];
    if (!accept) {
        Fail(wire::Reason::Declined, true);
        return;
    }
    buffer_.assign(size_, 0);
    received_ = 0;
    acceptedMs_ = nowMs;
    lastProgressMs_ = nowMs;
    const size_t n = wire::EncodeAccept(scratch, sizeof(scratch), reqId_);
    Push(&outbox_, n, scratch);
    state_ = ClientState::Receiving;
}

void DownloadClient::Tick(uint32_t nowMs) {
    switch (state_) {
        case ClientState::Requested:
            if (nowMs - startMs_ >= kOfferWaitMs) {
                Fail(wire::Reason::Timeout, true);
            } else if (requestRetries_ < kRequestRetries && nowMs - lastRequestMs_ >= kRequestRetryMs) {
                uint8_t scratch[wire::kMaxMessageLen];
                const size_t n = wire::EncodeRequest(scratch, sizeof(scratch), reqId_, name_.data(), name_.size());
                Push(&outbox_, n, scratch);
                lastRequestMs_ = nowMs;
                ++requestRetries_;
            }
            return;
        case ClientState::Offered:
            if (nowMs - offeredMs_ >= kClientConsentGiveUpMs) Fail(wire::Reason::Declined, true);
            return;
        case ClientState::Receiving:
            if (nowMs - lastProgressMs_ >= kStallMs || nowMs - acceptedMs_ >= kUploadDeadlineMs) {
                Fail(wire::Reason::Timeout, true);
            }
            return;
        default:
            return;
    }
}

void DownloadClient::OnAlreadyHave() {
    if (state_ != ClientState::Offered) return;
    uint8_t scratch[wire::kMaxMessageLen];
    const size_t n = wire::EncodeHave(scratch, sizeof(scratch), reqId_);
    Push(&outbox_, n, scratch);
    state_ = ClientState::Present;
}

void DownloadClient::OnStoreFailed() {
    if (state_ != ClientState::Done) return;
    Fail(wire::Reason::StoreFailed, true);
}

void DownloadClient::Abort(wire::Reason reason) {
    if (state_ == ClientState::Done || state_ == ClientState::Failed || state_ == ClientState::Idle ||
        state_ == ClientState::Present) {
        return;
    }
    Fail(reason, true);
}

uint8_t DownloadClient::percent() const {
    if (state_ == ClientState::Done) return 100;
    if (state_ != ClientState::Receiving || size_ == 0) return 0;
    return static_cast<uint8_t>(static_cast<uint64_t>(received_) * 100 / size_);
}

bool DownloadClient::Retryable() const {
    if (state_ != ClientState::Failed) return false;
    return reason_ == wire::Reason::RateLimited || reason_ == wire::Reason::Busy ||
           reason_ == wire::Reason::NotCurrentMap ||
           (reason_ == wire::Reason::Timeout && failedFrom_ == ClientState::Requested);
}

void DownloadClient::Fail(wire::Reason reason, bool tellHost) {
    if (tellHost) {
        uint8_t scratch[wire::kMaxMessageLen];
        const size_t n = wire::EncodeReason(scratch, sizeof(scratch), wire::Kind::Cancel, reqId_, reason);
        Push(&outbox_, n, scratch);
    }
    failedFrom_ = state_;
    state_ = ClientState::Failed;
    reason_ = reason;
    buffer_.clear();
    received_ = 0;
}

}  // namespace map_download
