// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>

#include "json/json.h"
#include "core/settings/native_settings_bridge.h"

enum class SettingType { Bool, Int, Float, String };

enum class InputType { Toggle, Slider, Text, Options };

enum class SettingSource { ModJson, NativeIni, NativeDerived };

struct SettingDef {
    std::string owner;
    std::string key;
    std::string id;
    SettingType type = SettingType::Bool;
    InputType input = InputType::Toggle;
    SettingSource source = SettingSource::ModJson;

    std::string category;
    std::string subcategory;
    std::string label;
    std::string description;

    double numDefault = 0;
    double numMin = 0;
    double numMax = 0;
    std::string strDefault;

    std::vector<std::string> options;

    std::vector<int> optionValues;

    bool hidden = false;

    const NativeSettingEntry* nativeEntry = nullptr;

    int (*derivedGet)() = nullptr;
    void (*derivedSet)(int value) = nullptr;
};

int OptionIndexForValue(const SettingDef& def, double value);

double ValueForOptionIndex(const SettingDef& def, int i);

class SettingsRegistry {
public:
    static SettingsRegistry& Get();

    int Register(SettingDef def);

    int Find(const char* owner, const char* key) const;
    int Count() const { return (int)records_.size(); }
    const SettingDef& DefAt(int index) const;
    std::vector<int> IndicesFor(const char* owner) const;

    double GetNum(int index) const;
    const std::string& GetStr(int index) const;

    void SetNum(int index, double value);
    void SetStr(int index, const std::string& value);
    void ResetToDefault(int index);

    void SetHidden(int index, bool hidden);

    void SetOptions(int index, std::vector<std::string> options);

    bool LoadOwner(const char* owner, const std::string& jsonPath);

    bool SaveOwner(const char* owner, const std::string& jsonPath);

    void PersistNative();

    bool HasNativeSettings() const;

private:
    struct Record {
        SettingDef def;
        double num = 0;
        std::string str;

        mutable std::string scratch;
    };

    void ResetOwnerToDefaults(const char* owner);

    bool ApplyLoadedValue(const char* owner, const std::string& key, const json::Value& v);

    std::vector<Record> records_;
};
