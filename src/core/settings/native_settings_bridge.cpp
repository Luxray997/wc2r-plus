// SPDX-License-Identifier: MIT
#include "core/settings/native_settings_bridge.h"

#include <windows.h>
#include <cstring>

#include "core/log.h"
#include "target/addresses.h"

namespace {

const Logger kLog{"native"};

using namespace game;

uintptr_t Base() { return reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)); }

uintptr_t ImageSize() {
    const uintptr_t base = Base();
    if (!base) return 0;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    return nt->OptionalHeader.SizeOfImage;
}

bool InModule(const void* p) {
    if (!p) return false;
    const uintptr_t base = Base();
    const uintptr_t size = ImageSize();
    if (!base || !size) return false;
    const uintptr_t addr = reinterpret_cast<uintptr_t>(p);
    return addr >= base && addr < base + size;
}

}  // namespace

NativeSettingsBridge& NativeSettingsBridge::Get() {
    static NativeSettingsBridge instance;
    return instance;
}

bool NativeSettingsBridge::Init() {
    if (available_) return true;

    auto* table = static_cast<NativeSettingEntry*>(kSettingsTable.Get());

    for (int i = 0; i < kEntryCount; ++i) {
        const NativeSettingEntry& e = table[i];
        if (e.type > 2) {
            kLog.Error("entry %d has type %u (expected 0..2) -- disabled", i,
                            e.type);
            return false;
        }
        if (!InModule(e.key) || !InModule(e.backing)) {
            kLog.Error("entry %d has out-of-module key=%p backing=%p -- disabled", i,
                            (const void*)e.key, e.backing);
            return false;
        }
        if (e.type == 1 && e.minValue > e.maxOrMask) {
            kLog.Error("entry %d '%s' has min %u > max %u -- disabled", i, e.key,
                            e.minValue, e.maxOrMask);
            return false;
        }
    }

    if (strcmp(table[0].key, "speed") != 0 || table[0].maxOrMask != 8) {
        kLog.Error("entry 0 is '%s' (max %u), expected 'speed' (max 8) -- disabled",
                        table[0].key, table[0].maxOrMask);
        return false;
    }

    table_ = table;
    persistFn_ = kPersistSettings.Get();
    applyMusicVolumeFn_ = kApplyMusicVolume.Get();
    restartMusicFn_ = kRestartMusic.Get();
    applyGameSpeedFn_ = kApplyGameSpeed.Get();
    rebuildMouseCursorsFn_ = kRebuildMouseCursors.Get();
    applyGridKeysFn_ = kApplyGridKeys.Get();
    applyUiScaleFn_ = kApplyUiScale.Get();
    applyUiAspectFn_ = kApplyUiAspect.Get();
    applyDisplayModeFn_ = kApplyDisplayMode.Get();
    available_ = true;
    kLog.Info("%d game settings available (table @ %p)", kEntryCount,
                    (void*)table_);
    return true;
}

const NativeSettingEntry* NativeSettingsBridge::At(int i) const {
    if (!available_ || i < 0 || i >= kEntryCount) return nullptr;
    return &table_[i];
}

uint32_t NativeSettingsBridge::GetUint(const NativeSettingEntry& e) const {
    if (!available_ || e.type != 1) return 0;
    return *reinterpret_cast<uint32_t*>(e.backing);
}

bool NativeSettingsBridge::GetFlag(const NativeSettingEntry& e) const {
    if (!available_ || e.type != 2) return false;
    return (*reinterpret_cast<uint32_t*>(e.backing) & e.maxOrMask) != 0;
}

const char* NativeSettingsBridge::GetString(const NativeSettingEntry& e) const {
    if (!available_ || e.type != 0) return "";
    return reinterpret_cast<const char*>(e.backing);
}

void NativeSettingsBridge::SetUint(const NativeSettingEntry& e, uint32_t v) {
    if (!available_ || e.type != 1) return;
    if (v < e.minValue) v = e.minValue;
    if (v > e.maxOrMask) v = e.maxOrMask;
    *reinterpret_cast<uint32_t*>(e.backing) = v;
}

void NativeSettingsBridge::SetFlag(const NativeSettingEntry& e, bool on) {
    if (!available_ || e.type != 2) return;
    uint32_t& flags = *reinterpret_cast<uint32_t*>(e.backing);
    if (on) {
        flags |= e.maxOrMask;
    } else {
        flags &= ~e.maxOrMask;
    }
}

int NativeSettingsBridge::FindKey(const char* key) const {
    if (!available_ || !key) return -1;
    for (int i = 0; i < kEntryCount; ++i) {
        if (table_[i].key && strcmp(table_[i].key, key) == 0) return i;
    }
    return -1;
}

void NativeSettingsBridge::Apply(const NativeSettingEntry& e) {
    if (!available_ || !e.key) return;

    void* scalarBlock = table_[0].backing;

    if (strcmp(e.key, "music") == 0) {
        if (applyMusicVolumeFn_ && scalarBlock) applyMusicVolumeFn_(scalarBlock);
    } else if (strcmp(e.key, "remastered_music") == 0) {
        if (restartMusicFn_) restartMusicFn_();
    } else if (strcmp(e.key, "speed") == 0) {
        if (applyGameSpeedFn_) applyGameSpeedFn_(GetUint(e), 0);
    } else if (strcmp(e.key, "mouse_size") == 0) {
        if (rebuildMouseCursorsFn_) rebuildMouseCursorsFn_();
    } else if (strcmp(e.key, "use_grid_keys") == 0) {
        if (applyGridKeysFn_) applyGridKeysFn_(GetUint(e) != 0 ? 1 : 0);
    } else if (strcmp(e.key, "ui_unlock_aspect") == 0) {
        if (applyUiAspectFn_) applyUiAspectFn_(GetFlag(e) ? 1 : 0);
    } else if (strcmp(e.key, "ui_scale_mode") == 0 || strcmp(e.key, "ui_scale_fixed") == 0) {

        const int modeIdx = FindKey("ui_scale_mode");
        const int fixedIdx = FindKey("ui_scale_fixed");
        if (applyUiScaleFn_ && modeIdx >= 0 && fixedIdx >= 0) {
            const uint32_t mode = GetUint(table_[modeIdx]);
            const uint32_t fixed = GetUint(table_[fixedIdx]);
            if (mode == 0) applyUiScaleFn_(1, 0);
            else if (mode == 1) applyUiScaleFn_(2, fixed);
            else if (mode == 2) applyUiScaleFn_(0, 0);
        }
    } else if (strcmp(e.key, "screen_mode") == 0 || strcmp(e.key, "display_monitor") == 0) {

        const int modeIdx = FindKey("screen_mode");
        const int monIdx = FindKey("display_monitor");
        if (applyDisplayModeFn_ && modeIdx >= 0 && monIdx >= 0) {
            applyDisplayModeFn_(GetUint(table_[modeIdx]) == 0 ? 1 : 0, GetUint(table_[monIdx]));
        }
    }

}

void NativeSettingsBridge::Persist() {
    if (!available_ || !persistFn_) return;
    persistFn_();
}
