// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace map_download {

constexpr uint32_t kMaxMapBytes = 262144;

constexpr uint16_t kMaxChunk = 960;
constexpr uint32_t kWindowChunks = 8;
constexpr uint32_t kHostChunksPerSecond = 64;
constexpr uint8_t kMaxNameLen = 64;
constexpr uint32_t kMaxUnits = 1024;

constexpr uint32_t kConsentWaitMs = 15000;

constexpr uint32_t kConsentGraceMs = 3000;
constexpr uint32_t kClientConsentGiveUpMs = kConsentWaitMs + 2 * kConsentGraceMs;

constexpr uint32_t kStallMs = 15000;

constexpr uint32_t kUploadDeadlineMs = 120000;
constexpr uint32_t kProgressBytes = kWindowChunks * kMaxChunk;
constexpr uint32_t kJoinGraceMs = 5000;
constexpr uint32_t kOfferWaitMs = 10000;

constexpr uint32_t kRequestRetryMs = 2500;
constexpr uint32_t kRequestRetries = 2;

constexpr uint32_t kRequestMinIntervalMs = 5000;

constexpr uint32_t kDenyMinIntervalMs = 5000;

constexpr uint32_t kUploadsPerStay = 32;
constexpr uint32_t kMaxCompletedSameMapPerStay = 2;

constexpr uint32_t kMaxUnusedOffersPerMapPerStay = 3;

constexpr uint32_t kMaxConcurrentUploads = 2;

constexpr uint32_t kProgressMinIntervalMs = 500;

}  // namespace map_download
