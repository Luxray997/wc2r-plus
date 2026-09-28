// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>

#include "features/map_download/limits.h"

namespace map_download {
namespace wire {

constexpr uint8_t kType = 0xE7;
constexpr char kSubsystem = 'M';
constexpr uint8_t kVersion = 1;
constexpr size_t kHeaderLen = 23;
constexpr size_t kReqIdLen = 16;
constexpr size_t kShaLen = 32;
constexpr size_t kSlots = 8;

enum class Kind : uint8_t {
    Request = 1,
    Offer = 2,
    Deny = 3,
    Accept = 4,
    Chunk = 5,
    Ack = 6,
    Cancel = 7,
    Done = 8,
    Progress = 9,

    Have = 10,

    Leaving = 11,
};

enum class LeaveWhy : uint8_t {
    HostCannotSend = 1,
    DownloadsOff = 2,
    BadName = 3,
    DownloadFailed = 4,
    kCount
};

enum class Reason : uint8_t {
    Ok = 0,
    NotCurrentMap = 1,
    ServingOff = 2,
    MapUnservable = 3,
    RateLimited = 4,
    Busy = 5,
    Declined = 6,
    TooLarge = 7,
    BadName = 8,
    Timeout = 9,
    HashMismatch = 10,
    InvalidMap = 11,
    StoreFailed = 12,
    LeftLobby = 13,
    Countdown = 14,
    kCount
};

enum class SlotState : uint8_t {
    None = 0,
    Offered = 1,
    Uploading = 2,
    Done = 3,
    NoMap = 4,
    Stalled = 5,
    LeftNoMap = 6,
    NoAnswer = 7,
    kCount
};

enum class DecodeFault {
    Ok = 0,
    TooShort,
    NotOurs,
    UnsupportedVersion,
    UnknownKind,
    BadLength,
    BadField,
};

struct SlotProgress {
    SlotState state;
    uint8_t percent;
};

struct Message {
    Kind kind;
    uint8_t reqId[kReqIdLen];

    const char* name;
    uint8_t nameLen;

    uint32_t size;
    uint8_t sha256[kShaLen];
    uint16_t chunkLen;

    Reason reason;

    uint32_t offset;
    const uint8_t* data;
    uint16_t dataLen;

    uint32_t nextOffset;

    SlotProgress slots[kSlots];

    LeaveWhy why;
};

DecodeFault Decode(const uint8_t* buf, size_t len, Message* out);

size_t EncodeRequest(uint8_t* buf, size_t cap, const uint8_t* reqId, const char* name, size_t nameLen);
size_t EncodeOffer(uint8_t* buf, size_t cap, const uint8_t* reqId, uint32_t size, const uint8_t* sha256,
                   uint16_t chunkLen);
size_t EncodeReason(uint8_t* buf, size_t cap, Kind kind, const uint8_t* reqId, Reason reason);
size_t EncodeAccept(uint8_t* buf, size_t cap, const uint8_t* reqId);
size_t EncodeChunk(uint8_t* buf, size_t cap, const uint8_t* reqId, uint32_t offset, const uint8_t* data,
                   uint16_t dataLen);
size_t EncodeAck(uint8_t* buf, size_t cap, const uint8_t* reqId, uint32_t nextOffset);
size_t EncodeProgress(uint8_t* buf, size_t cap, const SlotProgress slots[kSlots]);
size_t EncodeHave(uint8_t* buf, size_t cap, const uint8_t* reqId);
size_t EncodeLeaving(uint8_t* buf, size_t cap, LeaveWhy why);

constexpr size_t kMaxMessageLen = kHeaderLen + 4 + 2 + kMaxChunk;

const char* DecodeFaultName(DecodeFault fault);

}  // namespace wire
}  // namespace map_download
