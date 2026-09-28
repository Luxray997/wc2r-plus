// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

struct NativeSettingEntry {
    uint32_t type;
    const char* key;
    uint32_t minValue;
    uint32_t maxOrMask;
    uint32_t defOrCap;
    void* backing;
};

static_assert(sizeof(NativeSettingEntry) == 24, "must match the game's 24-byte table stride");

class NativeSettingsBridge {
public:
    static NativeSettingsBridge& Get();

    bool Init();

    bool Available() const { return available_; }
    int Count() const { return available_ ? kEntryCount : 0; }

    const NativeSettingEntry* At(int i) const;

    uint32_t GetUint(const NativeSettingEntry& e) const;
    bool GetFlag(const NativeSettingEntry& e) const;
    const char* GetString(const NativeSettingEntry& e) const;

    void SetUint(const NativeSettingEntry& e, uint32_t v);
    void SetFlag(const NativeSettingEntry& e, bool on);

    void Apply(const NativeSettingEntry& e);

    void Persist();

private:

    static constexpr int kEntryCount = 39;

    NativeSettingEntry* table_ = nullptr;
    void (*persistFn_)() = nullptr;

    int FindKey(const char* key) const;

    void (*applyMusicVolumeFn_)(void* settingsBlock) = nullptr;
    void (*restartMusicFn_)() = nullptr;
    void (*applyGameSpeedFn_)(uint32_t speed, char inMatch) = nullptr;
    void (*rebuildMouseCursorsFn_)() = nullptr;
    void (*applyGridKeysFn_)(char on) = nullptr;
    void (*applyUiScaleFn_)(uint32_t rule, uint32_t fixedFactor) = nullptr;
    void (*applyUiAspectFn_)(uint32_t on) = nullptr;
    void (*applyDisplayModeFn_)(uint32_t fullscreen, uint32_t monitor) = nullptr;
    bool available_ = false;
};
