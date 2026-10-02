// SPDX-License-Identifier: MIT
#include "core/settings/mod_settings.h"

#include "core/paths.h"

#include <windows.h>
#include <cstring>

namespace {

constexpr DWORD kStatIntervalMs = 500;

unsigned long long ToU64(const FILETIME& ft) {
    return (static_cast<unsigned long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

unsigned long long LastWriteTime(const std::string& path) {
    WIN32_FILE_ATTRIBUTE_DATA attr{};
    if (path.empty() ||
        !GetFileAttributesExW(paths::Widen(path).c_str(), GetFileExInfoStandard, &attr)) {
        return 0;
    }
    return ToU64(attr.ftLastWriteTime);
}

}  // namespace

ModSettingsList& ModSettingsList::Get() {
    static ModSettingsList instance;
    return instance;
}

void ModSettingsList::Add(ModSettings* settings) {
    all_.push_back(settings);
}

ModSettings::ModSettings(std::string modName) : modName_(std::move(modName)) {

    ModSettingsList::Get().Add(this);
}

void ModSettings::BeginGroup(const char* category, const char* subcategory) {
    category_ = category ? category : "";
    subcategory_ = subcategory ? subcategory : "";
}

int ModSettings::IndexOf(const char* key) const {
    for (size_t i = 0; i < descs_.size(); ++i) {
        if (_stricmp(descs_[i].key.c_str(), key) == 0) return (int)i;
    }
    return -1;
}

int ModSettings::CoreIndexOf(const char* key) const {
    const int i = IndexOf(key);
    return i >= 0 ? coreIndices_[i] : -1;
}

int ModSettings::Add(SettingDef def) {
    def.owner = modName_;
    def.category = category_;
    def.subcategory = subcategory_;

    const int coreIndex = SettingsRegistry::Get().Register(def);

    descs_.push_back(SettingsRegistry::Get().DefAt(coreIndex));
    coreIndices_.push_back(coreIndex);
    return coreIndex;
}

void ModSettings::RegisterBool(const char* key, bool def, const char* label,
                                const char* description) {
    SettingDef d;
    d.key = key;
    d.type = SettingType::Bool;
    d.input = InputType::Toggle;
    d.label = label;
    d.description = description;
    d.numDefault = def ? 1.0 : 0.0;
    Add(d);
}

void ModSettings::RegisterInt(const char* key, int def, const char* label,
                               const char* description, int min, int max) {
    SettingDef d;
    d.key = key;
    d.type = SettingType::Int;

    d.input = (min != max) ? InputType::Slider : InputType::Text;
    d.label = label;
    d.description = description;
    d.numDefault = (double)def;
    d.numMin = (double)min;
    d.numMax = (double)max;
    Add(d);
}

void ModSettings::RegisterFloat(const char* key, float def, const char* label,
                                 const char* description, float min, float max) {
    SettingDef d;
    d.key = key;
    d.type = SettingType::Float;
    d.input = (min != max) ? InputType::Slider : InputType::Text;
    d.label = label;
    d.description = description;
    d.numDefault = (double)def;
    d.numMin = (double)min;
    d.numMax = (double)max;
    Add(d);
}

void ModSettings::RegisterString(const char* key, const char* def, const char* label,
                                  const char* description) {
    SettingDef d;
    d.key = key;
    d.type = SettingType::String;
    d.input = InputType::Text;
    d.label = label;
    d.description = description;
    d.strDefault = def ? def : "";
    Add(d);
}

void ModSettings::SetHidden(const char* key, bool hidden) {
    const int i = IndexOf(key);
    if (i < 0) return;
    descs_[i].hidden = hidden;
    SettingsRegistry::Get().SetHidden(coreIndices_[i], hidden);
}

void ModSettings::SetActiveWhen(const char* key, const char* boolKey) {
    const int i = IndexOf(key);
    if (i < 0 || IndexOf(boolKey) < 0) return;
    descs_[i].activeWhen = boolKey;
    SettingsRegistry::Get().SetActiveWhen(coreIndices_[i], boolKey);
}

void ModSettings::SetOptions(const char* key, std::vector<std::string> options) {
    const int i = IndexOf(key);
    if (i < 0 || options.empty()) return;
    descs_[i].options = options;
    descs_[i].optionValues.clear();
    descs_[i].input = InputType::Options;
    SettingsRegistry::Get().SetOptions(coreIndices_[i], std::move(options));
}

bool ModSettings::ReloadIfChanged() {
    const DWORD now = GetTickCount();
    if (everLoaded_ && lastStatTick_ != 0 && now - lastStatTick_ < kStatIntervalMs) return false;
    lastStatTick_ = now;

    const unsigned long long writeTime = LastWriteTime(ConfigPath());
    if (everLoaded_ && writeTime == lastWriteTime_) return false;

    const bool loaded = SettingsRegistry::Get().LoadOwner(modName_.c_str(), ConfigPath());
    everLoaded_ = true;
    lastWriteTime_ = writeTime;
    if (changeHandler_) changeHandler_();

    (void)loaded;
    return true;
}

bool ModSettings::GetBool(const char* key) const {
    const int i = CoreIndexOf(key);
    return i >= 0 ? SettingsRegistry::Get().GetNum(i) != 0.0 : false;
}

int ModSettings::GetInt(const char* key) const {
    const int i = CoreIndexOf(key);
    return i >= 0 ? (int)SettingsRegistry::Get().GetNum(i) : 0;
}

float ModSettings::GetFloat(const char* key) const {
    const int i = CoreIndexOf(key);
    return i >= 0 ? (float)SettingsRegistry::Get().GetNum(i) : 0.0f;
}

std::string ModSettings::GetString(const char* key) const {
    const int i = CoreIndexOf(key);
    return i >= 0 ? SettingsRegistry::Get().GetStr(i) : std::string();
}

double ModSettings::GetNumAt(int index) const {
    if (index < 0 || index >= (int)coreIndices_.size()) return 0.0;
    return SettingsRegistry::Get().GetNum(coreIndices_[index]);
}

const std::string& ModSettings::GetStringAt(int index) const {
    static const std::string kEmpty;
    if (index < 0 || index >= (int)coreIndices_.size()) return kEmpty;
    return SettingsRegistry::Get().GetStr(coreIndices_[index]);
}

void ModSettings::SetNumAt(int index, double value) {
    if (index < 0 || index >= (int)coreIndices_.size()) return;
    SettingsRegistry::Get().SetNum(coreIndices_[index], value);
}

void ModSettings::SetStringAt(int index, const std::string& value) {
    if (index < 0 || index >= (int)coreIndices_.size()) return;
    SettingsRegistry::Get().SetStr(coreIndices_[index], value);
}

bool ModSettings::Save() {
    lastStatTick_ = 0;
    const bool saved = SettingsRegistry::Get().SaveOwner(modName_.c_str(), ConfigPath());
    if (changeHandler_) changeHandler_();
    return saved;
}

const std::string& ModSettings::ConfigPath() const {
    if (jsonPath_.empty()) jsonPath_ = paths::Narrow(paths::Config(paths::Widen(modName_) + L".json"));
    return jsonPath_;
}
