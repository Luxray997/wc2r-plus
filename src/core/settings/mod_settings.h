// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>

#include "core/settings/settings_registry.h"

class ModSettings {
public:

    explicit ModSettings(std::string modName);

    void BeginGroup(const char* category, const char* subcategory);

    void RegisterBool(const char* key, bool def, const char* label, const char* description = "");
    void RegisterInt(const char* key, int def, const char* label, const char* description = "",
                      int min = 0, int max = 0);
    void RegisterFloat(const char* key, float def, const char* label,
                        const char* description = "", float min = 0, float max = 0);
    void RegisterString(const char* key, const char* def, const char* label,
                         const char* description = "");

    void SetHidden(const char* key, bool hidden = true);

    void SetOptions(const char* key, std::vector<std::string> options);

    bool ReloadIfChanged();

    bool GetBool(const char* key) const;
    int GetInt(const char* key) const;
    float GetFloat(const char* key) const;
    std::string GetString(const char* key) const;

    double GetNumAt(int index) const;
    const std::string& GetStringAt(int index) const;
    void SetNumAt(int index, double value);
    void SetStringAt(int index, const std::string& value);

    bool Save();

    void SetChangeHandler(void (*handler)()) { changeHandler_ = handler; }

    const std::string& ModName() const { return modName_; }

    const std::string& ConfigPath() const;
    const std::vector<SettingDef>& Descriptors() const { return descs_; }

private:
    int IndexOf(const char* key) const;
    int CoreIndexOf(const char* key) const;
    int Add(SettingDef def);

    std::string modName_;
    mutable std::string jsonPath_;
    void (*changeHandler_)() = nullptr;

    std::string category_;
    std::string subcategory_;

    std::vector<SettingDef> descs_;
    std::vector<int> coreIndices_;

    bool everLoaded_ = false;
    unsigned long long lastWriteTime_ = 0;
    unsigned long lastStatTick_ = 0;
};

class ModSettingsList {
public:
    static ModSettingsList& Get();

    void Add(ModSettings* settings);
    const std::vector<ModSettings*>& All() const { return all_; }

private:
    std::vector<ModSettings*> all_;
};
