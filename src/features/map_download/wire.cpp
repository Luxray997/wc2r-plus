// SPDX-License-Identifier: MIT
#include "features/map_download/wire.h"

#include <cstring>

#include "features/map_download/limits.h"

namespace map_download {
namespace wire {
namespace {

constexpr uint8_t kMagic[3] = {'W', '2', 'R'};

constexpr size_t kOfferLen = kHeaderLen + 4 + kShaLen + 2;
constexpr size_t kReasonLen = kHeaderLen + 1;
constexpr size_t kAcceptLen = kHeaderLen;
constexpr size_t kChunkFixedLen = kHeaderLen + 4 + 2;
constexpr size_t kAckLen = kHeaderLen + 4;
constexpr size_t kProgressLen = kHeaderLen + 2 * kSlots;
constexpr size_t kRequestFixedLen = kHeaderLen + 1;

uint16_t Get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t Get32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
void Put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}
void Put32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

size_t PutHeader(uint8_t* buf, size_t cap, size_t total, Kind kind, const uint8_t* reqId) {
    if (buf == nullptr || cap < total) return 0;
    buf[0] = kType;
    memcpy(buf + 1, kMagic, sizeof(kMagic));
    buf[4] = static_cast<uint8_t>(kSubsystem);
    buf[5] = kVersion;
    buf[6] = static_cast<uint8_t>(kind);
    if (reqId) {
        memcpy(buf + 7, reqId, kReqIdLen);
    } else {
        memset(buf + 7, 0, kReqIdLen);
    }
    return total;
}

bool AllZero(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (p[i] != 0) return false;
    }
    return true;
}

}  // namespace

DecodeFault Decode(const uint8_t* buf, size_t len, Message* out) {
    if (buf == nullptr || out == nullptr || len < 7) return DecodeFault::TooShort;
    if (buf[0] != kType || memcmp(buf + 1, kMagic, sizeof(kMagic)) != 0 ||
        buf[4] != static_cast<uint8_t>(kSubsystem)) {
        return DecodeFault::NotOurs;
    }
    if (buf[5] != kVersion) return DecodeFault::UnsupportedVersion;
    if (len < kHeaderLen) return DecodeFault::TooShort;

    memset(out, 0, sizeof(*out));
    out->kind = static_cast<Kind>(buf[6]);
    memcpy(out->reqId, buf + 7, kReqIdLen);
    const uint8_t* p = buf + kHeaderLen;

    switch (out->kind) {
        case Kind::Request: {
            if (len < kRequestFixedLen) return DecodeFault::BadLength;
            const uint8_t nameLen = p[0];
            if (nameLen == 0 || nameLen > kMaxNameLen) return DecodeFault::BadField;
            if (len != kRequestFixedLen + nameLen) return DecodeFault::BadLength;
            out->nameLen = nameLen;
            out->name = reinterpret_cast<const char*>(p + 1);
            return DecodeFault::Ok;
        }
        case Kind::Offer: {
            if (len != kOfferLen) return DecodeFault::BadLength;
            out->size = Get32(p);
            memcpy(out->sha256, p + 4, kShaLen);
            out->chunkLen = Get16(p + 4 + kShaLen);
            if (out->size == 0 || out->size > kMaxMapBytes) return DecodeFault::BadField;
            if (out->chunkLen == 0 || out->chunkLen > kMaxChunk) return DecodeFault::BadField;
            return DecodeFault::Ok;
        }
        case Kind::Deny:
        case Kind::Cancel:
        case Kind::Done: {
            if (len != kReasonLen) return DecodeFault::BadLength;
            if (p[0] >= static_cast<uint8_t>(Reason::kCount)) return DecodeFault::BadField;
            out->reason = static_cast<Reason>(p[0]);
            return DecodeFault::Ok;
        }
        case Kind::Leaving: {
            if (len != kReasonLen) return DecodeFault::BadLength;
            if (!AllZero(out->reqId, kReqIdLen)) return DecodeFault::BadField;
            if (p[0] == 0 || p[0] >= static_cast<uint8_t>(LeaveWhy::kCount)) return DecodeFault::BadField;
            out->why = static_cast<LeaveWhy>(p[0]);
            return DecodeFault::Ok;
        }
        case Kind::Have:
        case Kind::Accept:
            return len == kAcceptLen ? DecodeFault::Ok : DecodeFault::BadLength;
        case Kind::Chunk: {
            if (len < kChunkFixedLen) return DecodeFault::BadLength;
            out->offset = Get32(p);
            out->dataLen = Get16(p + 4);
            if (out->dataLen == 0 || out->dataLen > kMaxChunk) return DecodeFault::BadField;
            if (len != kChunkFixedLen + out->dataLen) return DecodeFault::BadLength;
            if (out->offset >= kMaxMapBytes) return DecodeFault::BadField;
            out->data = p + 6;
            return DecodeFault::Ok;
        }
        case Kind::Ack: {
            if (len != kAckLen) return DecodeFault::BadLength;
            out->nextOffset = Get32(p);
            if (out->nextOffset > kMaxMapBytes) return DecodeFault::BadField;
            return DecodeFault::Ok;
        }
        case Kind::Progress: {
            if (len != kProgressLen) return DecodeFault::BadLength;
            if (!AllZero(out->reqId, kReqIdLen)) return DecodeFault::BadField;
            for (size_t i = 0; i < kSlots; ++i) {
                const uint8_t state = p[2 * i];
                const uint8_t percent = p[2 * i + 1];
                if (state >= static_cast<uint8_t>(SlotState::kCount) || percent > 100) {
                    return DecodeFault::BadField;
                }
                out->slots[i].state = static_cast<SlotState>(state);
                out->slots[i].percent = percent;
            }
            return DecodeFault::Ok;
        }
    }
    return DecodeFault::UnknownKind;
}

size_t EncodeRequest(uint8_t* buf, size_t cap, const uint8_t* reqId, const char* name, size_t nameLen) {
    if (reqId == nullptr || name == nullptr || nameLen == 0 || nameLen > kMaxNameLen) return 0;
    const size_t total = kRequestFixedLen + nameLen;
    if (!PutHeader(buf, cap, total, Kind::Request, reqId)) return 0;
    buf[kHeaderLen] = static_cast<uint8_t>(nameLen);
    memcpy(buf + kHeaderLen + 1, name, nameLen);
    return total;
}

size_t EncodeHave(uint8_t* buf, size_t cap, const uint8_t* reqId) {
    if (reqId == nullptr) return 0;
    if (!PutHeader(buf, cap, kHeaderLen, Kind::Have, reqId)) return 0;
    return kHeaderLen;
}

size_t EncodeOffer(uint8_t* buf, size_t cap, const uint8_t* reqId, uint32_t size, const uint8_t* sha256,
                   uint16_t chunkLen) {
    if (reqId == nullptr || sha256 == nullptr || size == 0 || size > kMaxMapBytes || chunkLen == 0 ||
        chunkLen > kMaxChunk) {
        return 0;
    }
    if (!PutHeader(buf, cap, kOfferLen, Kind::Offer, reqId)) return 0;
    Put32(buf + kHeaderLen, size);
    memcpy(buf + kHeaderLen + 4, sha256, kShaLen);
    Put16(buf + kHeaderLen + 4 + kShaLen, chunkLen);
    return kOfferLen;
}

size_t EncodeLeaving(uint8_t* buf, size_t cap, LeaveWhy why) {
    if (why == static_cast<LeaveWhy>(0) || static_cast<uint8_t>(why) >= static_cast<uint8_t>(LeaveWhy::kCount)) {
        return 0;
    }
    if (!PutHeader(buf, cap, kReasonLen, Kind::Leaving, nullptr)) return 0;
    buf[kHeaderLen] = static_cast<uint8_t>(why);
    return kReasonLen;
}

size_t EncodeReason(uint8_t* buf, size_t cap, Kind kind, const uint8_t* reqId, Reason reason) {
    if (reqId == nullptr || (kind != Kind::Deny && kind != Kind::Cancel && kind != Kind::Done) ||
        static_cast<uint8_t>(reason) >= static_cast<uint8_t>(Reason::kCount)) {
        return 0;
    }
    if (!PutHeader(buf, cap, kReasonLen, kind, reqId)) return 0;
    buf[kHeaderLen] = static_cast<uint8_t>(reason);
    return kReasonLen;
}

size_t EncodeAccept(uint8_t* buf, size_t cap, const uint8_t* reqId) {
    if (reqId == nullptr) return 0;
    return PutHeader(buf, cap, kAcceptLen, Kind::Accept, reqId);
}

size_t EncodeChunk(uint8_t* buf, size_t cap, const uint8_t* reqId, uint32_t offset, const uint8_t* data,
                   uint16_t dataLen) {
    if (reqId == nullptr || data == nullptr || dataLen == 0 || dataLen > kMaxChunk ||
        offset >= kMaxMapBytes) {
        return 0;
    }
    const size_t total = kChunkFixedLen + dataLen;
    if (!PutHeader(buf, cap, total, Kind::Chunk, reqId)) return 0;
    Put32(buf + kHeaderLen, offset);
    Put16(buf + kHeaderLen + 4, dataLen);
    memcpy(buf + kChunkFixedLen, data, dataLen);
    return total;
}

size_t EncodeAck(uint8_t* buf, size_t cap, const uint8_t* reqId, uint32_t nextOffset) {
    if (reqId == nullptr || nextOffset > kMaxMapBytes) return 0;
    if (!PutHeader(buf, cap, kAckLen, Kind::Ack, reqId)) return 0;
    Put32(buf + kHeaderLen, nextOffset);
    return kAckLen;
}

size_t EncodeProgress(uint8_t* buf, size_t cap, const SlotProgress slots[kSlots]) {
    if (slots == nullptr) return 0;
    for (size_t i = 0; i < kSlots; ++i) {
        if (static_cast<uint8_t>(slots[i].state) >= static_cast<uint8_t>(SlotState::kCount) ||
            slots[i].percent > 100) {
            return 0;
        }
    }
    if (!PutHeader(buf, cap, kProgressLen, Kind::Progress, nullptr)) return 0;
    for (size_t i = 0; i < kSlots; ++i) {
        buf[kHeaderLen + 2 * i] = static_cast<uint8_t>(slots[i].state);
        buf[kHeaderLen + 2 * i + 1] = slots[i].percent;
    }
    return kProgressLen;
}

const char* DecodeFaultName(DecodeFault fault) {
    switch (fault) {
        case DecodeFault::Ok: return "ok";
        case DecodeFault::TooShort: return "too short";
        case DecodeFault::NotOurs: return "not a map-download message";
        case DecodeFault::UnsupportedVersion: return "unsupported version";
        case DecodeFault::UnknownKind: return "unknown kind";
        case DecodeFault::BadLength: return "wrong length for its kind";
        case DecodeFault::BadField: return "field out of range";
    }
    return "unknown fault";
}

}  // namespace wire
}  // namespace map_download
