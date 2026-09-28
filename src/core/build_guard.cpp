// SPDX-License-Identifier: MIT
#include "core/build_guard.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "target/addresses.h"
#include "target/game_version.h"

namespace {

struct RelocSlot {
    size_t offset;
    uint32_t staticAddr;
};

struct Signature {
    const char* what;
    uintptr_t staticAddr;
    const uint8_t* bytes;
    size_t len;
    const RelocSlot* relocs;
    size_t relocCount;
};

#include "target/signatures.generated.h"
constexpr int kSignatureCount = (int)(sizeof(kSignatures) / sizeof(kSignatures[0]));

bool IsInRelocSlot(const Signature& sig, size_t i) {
    for (size_t r = 0; r < sig.relocCount; ++r) {
        if (i >= sig.relocs[r].offset && i < sig.relocs[r].offset + 4) return true;
    }
    return false;
}

void LogBytes(const char* label, const uint8_t* p, const Signature& sig) {
    char line[32 + 3 * 256];
    int used = snprintf(line, sizeof(line), "      %s:", label);
    for (size_t i = 0; i < sig.len && used + 4 < (int)sizeof(line); ++i) {
        used += IsInRelocSlot(sig, i) ? snprintf(line + used, sizeof(line) - used, " --")
                                      : snprintf(line + used, sizeof(line) - used, " %02x", p[i]);
    }
    LogRaw("%s\n", line);
}

uint32_t ReadSizeOfImage(uintptr_t base) {
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    return nt->OptionalHeader.SizeOfImage;
}

bool CheckSignature(const Signature& sig, uintptr_t base, uintptr_t delta) {
    const auto* actual =
        reinterpret_cast<const uint8_t*>(base + (sig.staticAddr - kPreferredImageBase));

    if (IsBadReadPtr(actual, sig.len)) {
        LogRaw("  [FAIL] %-22s @ %08x: address not readable\n", sig.what,
               (unsigned)sig.staticAddr);
        return false;
    }

    for (size_t i = 0; i < sig.len; ++i) {
        if (IsInRelocSlot(sig, i)) continue;
        if (actual[i] != sig.bytes[i]) {
            LogRaw("  [FAIL] %-22s @ %08x: byte mismatch at +%zu\n", sig.what,
                   (unsigned)sig.staticAddr, i);
            LogBytes("expected", sig.bytes, sig);
            LogBytes("actual  ", actual, sig);
            return false;
        }
    }

    for (size_t r = 0; r < sig.relocCount; ++r) {
        const RelocSlot& rel = sig.relocs[r];
        uint32_t got;
        memcpy(&got, actual + rel.offset, sizeof(got));
        const uint32_t want = (uint32_t)(rel.staticAddr + delta);
        if (got != want) {
            LogRaw("  [FAIL] %-22s @ %08x: reloc at +%zu is %08x, expected %08x "
                   "(static %08x + delta %08x)\n",
                   sig.what, (unsigned)sig.staticAddr, rel.offset, got, want, rel.staticAddr,
                   (unsigned)delta);
            return false;
        }
    }

    LogRaw("  [ ok ] %-22s @ %08x (%zu bytes, %zu reloc)\n", sig.what, (unsigned)sig.staticAddr,
           sig.len, sig.relocCount);
    return true;
}

}  // namespace

bool VerifyGameBuild() {
    const uintptr_t base = game::Base();
    const uintptr_t delta = base - kPreferredImageBase;

    LogRaw("--- build guard: expecting Warcraft II Remastered v1.0.2.2818 ---\n");
    LogRaw("  module base=%p  preferred base=%08x  reloc delta=%08x\n", reinterpret_cast<void*>(base),
           (unsigned)kPreferredImageBase, (unsigned)delta);
    const uint32_t sizeOfImage = ReadSizeOfImage(base);
    LogRaw("  SizeOfImage=0x%08x (expected 0x%08x)%s\n", sizeOfImage, kExpectedSizeOfImage,
           sizeOfImage == kExpectedSizeOfImage ? "" : "  <-- DIFFERS");

    int failed = 0;
    for (const Signature& sig : kSignatures) {
        if (!CheckSignature(sig, base, delta)) ++failed;
    }

    if (failed != 0) {
        LogRaw("*** BUILD GUARD FAILED (%d/%d signatures) -- REFUSING TO HOOK OR MODIFY ***\n"
               "*** This version of the mod supports Warcraft II Remastered v1.0.2.2818 only.\n"
               "*** If the game has updated, a new release of the mod is needed. The game runs\n"
               "*** unmodified meanwhile.\n",
               failed, kSignatureCount);
        return false;
    }

    LogRaw("  build guard PASSED (%d/%d) -- safe to hook\n", kSignatureCount, kSignatureCount);
    return true;
}
