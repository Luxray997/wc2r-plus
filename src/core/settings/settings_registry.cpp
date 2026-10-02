// SPDX-License-Identifier: MIT
#include "core/settings/settings_registry.h"

#include "core/paths.h"

#include <utility>

#include <windows.h>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "core/log.h"

namespace {

const Logger kLog{"settings"};

Logger OwnerLog(const char* owner) { return Logger{owner}; }

const std::string kEmptyString;

bool FileExists(const std::string& path) {
    WIN32_FILE_ATTRIBUTE_DATA attr{};
    return !path.empty() &&
           GetFileAttributesExW(paths::Widen(path).c_str(), GetFileExInfoStandard, &attr) != 0;
}

bool ReadWholeFile(const std::string& path, std::string& out) {
    FILE* f = nullptr;
    if (path.empty() || _wfopen_s(&f, paths::Widen(path).c_str(), L"rb") != 0 || !f) return false;
    out.clear();
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

bool WriteWholeFileAtomic(const std::string& path, const std::string& text) {
    if (path.empty()) return false;
    const std::string tmp = path + ".tmp";
    FILE* f = nullptr;
    if (_wfopen_s(&f, paths::Widen(tmp).c_str(), L"wb") != 0 || !f) {
        kLog.Error("cannot write %s", tmp.c_str());
        return false;
    }
    const bool wrote = fwrite(text.data(), 1, text.size(), f) == text.size();
    const bool closed = fclose(f) == 0;
    if (!wrote || !closed) {
        kLog.Error("cannot write %s -- keeping the previous file", tmp.c_str());
        DeleteFileW(paths::Widen(tmp).c_str());
        return false;
    }
    if (!MoveFileExW(paths::Widen(tmp).c_str(), paths::Widen(path).c_str(),
                     MOVEFILE_REPLACE_EXISTING)) {
        kLog.Error("cannot replace %s (error %lu)", path.c_str(),
                        GetLastError());
        DeleteFileW(paths::Widen(tmp).c_str());
        return false;
    }
    return true;
}

}  // namespace

SettingsRegistry& SettingsRegistry::Get() {
    static SettingsRegistry instance;
    return instance;
}

int SettingsRegistry::Register(SettingDef def) {
    const int existing = Find(def.owner.c_str(), def.key.c_str());
    if (existing >= 0) return existing;

    def.id = def.owner + "." + def.key;
    if (def.category.empty()) def.category = def.owner;
    if (def.subcategory.empty()) def.subcategory = "General";
    if (def.label.empty()) def.label = def.key;

    Record rec;
    rec.def = std::move(def);
    rec.num = rec.def.numDefault;
    rec.str = rec.def.strDefault;
    records_.push_back(std::move(rec));
    return (int)records_.size() - 1;
}

int SettingsRegistry::Find(const char* owner, const char* key) const {
    for (size_t i = 0; i < records_.size(); ++i) {
        if (_stricmp(records_[i].def.owner.c_str(), owner) == 0 &&
            _stricmp(records_[i].def.key.c_str(), key) == 0) {
            return (int)i;
        }
    }
    return -1;
}

const SettingDef& SettingsRegistry::DefAt(int index) const {
    static const SettingDef kEmpty;
    if (index < 0 || index >= (int)records_.size()) return kEmpty;
    return records_[index].def;
}

std::vector<int> SettingsRegistry::IndicesFor(const char* owner) const {
    std::vector<int> out;
    for (size_t i = 0; i < records_.size(); ++i) {
        if (_stricmp(records_[i].def.owner.c_str(), owner) == 0) out.push_back((int)i);
    }
    return out;
}

int OptionIndexForValue(const SettingDef& def, double value) {
    const int v = (int)value;
    if (def.optionValues.empty()) {
        return (v >= 0 && v < (int)def.options.size()) ? v : 0;
    }
    for (int i = 0; i < (int)def.optionValues.size(); ++i) {
        if (def.optionValues[i] == v) return i;
    }
    return 0;
}

double ValueForOptionIndex(const SettingDef& def, int i) {
    if (i < 0) return 0.0;
    if (def.optionValues.empty()) return (double)i;
    if (i >= (int)def.optionValues.size()) return 0.0;
    return (double)def.optionValues[i];
}

double SettingsRegistry::GetNum(int index) const {
    if (index < 0 || index >= (int)records_.size()) return 0.0;
    const Record& rec = records_[index];

    if (rec.def.derivedGet) return (double)rec.def.derivedGet();

    if (rec.def.nativeEntry) {
        NativeSettingsBridge& native = NativeSettingsBridge::Get();
        if (rec.def.nativeEntry->type == 2) return native.GetFlag(*rec.def.nativeEntry) ? 1.0 : 0.0;
        return (double)native.GetUint(*rec.def.nativeEntry);
    }
    return rec.num;
}

const std::string& SettingsRegistry::GetStr(int index) const {
    if (index < 0 || index >= (int)records_.size()) return kEmptyString;
    const Record& rec = records_[index];
    if (rec.def.nativeEntry && rec.def.nativeEntry->type == 0) {

        rec.scratch = NativeSettingsBridge::Get().GetString(*rec.def.nativeEntry);
        return rec.scratch;
    }
    return rec.str;
}

void SettingsRegistry::SetNum(int index, double value) {
    if (index < 0 || index >= (int)records_.size()) return;
    const SettingDef& d = records_[index].def;

    if (!std::isfinite(value)) value = d.numDefault;
    if (d.type == SettingType::Bool) {
        value = (value != 0.0) ? 1.0 : 0.0;
    } else if (d.numMin != d.numMax) {
        if (value < d.numMin) value = d.numMin;
        if (value > d.numMax) value = d.numMax;
    }

    if (d.derivedSet) {

        d.derivedSet((int)value);
        return;
    }

    if (d.nativeEntry) {

        NativeSettingsBridge& native = NativeSettingsBridge::Get();
        if (d.nativeEntry->type == 2) {
            native.SetFlag(*d.nativeEntry, value != 0.0);
        } else {
            native.SetUint(*d.nativeEntry, (uint32_t)value);
        }
        native.Apply(*d.nativeEntry);
        return;
    }
    records_[index].num = value;
}

void SettingsRegistry::SetStr(int index, const std::string& value) {
    if (index < 0 || index >= (int)records_.size()) return;

    if (records_[index].def.nativeEntry) return;
    records_[index].str = value;
}

void SettingsRegistry::SetHidden(int index, bool hidden) {
    if (index < 0 || index >= (int)records_.size()) return;
    records_[index].def.hidden = hidden;
}

void SettingsRegistry::SetActiveWhen(int index, const char* boolKey) {
    if (index < 0 || index >= (int)records_.size() || !boolKey) return;
    records_[index].def.activeWhen = boolKey;
}

void SettingsRegistry::SetOptions(int index, std::vector<std::string> options) {
    if (index < 0 || index >= (int)records_.size() || options.empty()) return;
    SettingDef& d = records_[index].def;
    d.options = std::move(options);
    d.optionValues.clear();
    d.input = InputType::Options;
    d.type = SettingType::Int;
    d.numMin = 0;
    d.numMax = (double)(d.options.size() - 1);
}

void SettingsRegistry::ResetToDefault(int index) {
    if (index < 0 || index >= (int)records_.size()) return;
    records_[index].num = records_[index].def.numDefault;
    records_[index].str = records_[index].def.strDefault;
}

void SettingsRegistry::ResetOwnerToDefaults(const char* owner) {
    for (int i : IndicesFor(owner)) ResetToDefault(i);
}

bool SettingsRegistry::ApplyLoadedValue(const char* owner, const std::string& key,
                                     const json::Value& v) {
    const int idx = Find(owner, key.c_str());
    if (idx < 0) return false;

    switch (records_[idx].def.type) {
        case SettingType::Bool: SetNum(idx, v.AsBool() ? 1.0 : 0.0); break;
        case SettingType::Int:
        case SettingType::Float: SetNum(idx, v.AsNumber(records_[idx].def.numDefault)); break;
        case SettingType::String: SetStr(idx, v.AsString(records_[idx].def.strDefault)); break;
    }
    return true;
}

bool SettingsRegistry::LoadOwner(const char* owner, const std::string& jsonPath) {
    ResetOwnerToDefaults(owner);

    if (FileExists(jsonPath)) {
        std::string text;
        if (!ReadWholeFile(jsonPath, text)) return false;

        json::Value root;
        std::string error;
        int line = 0;
        if (!json::Value::Parse(text, root, error, line)) {
            OwnerLog(owner).Warn("%s is not valid JSON (%s) -- using defaults", jsonPath.c_str(),
                                 error.c_str());
            return false;
        }

        const json::Value* settings = root.Find("settings");
        if (!settings || !settings->IsObject()) {
            OwnerLog(owner).Warn("%s has no \"settings\" object -- using defaults",
                                 jsonPath.c_str());
            return false;
        }

        int applied = 0, unknown = 0;

        for (const auto& member : settings->Members()) {
            const json::Value& mv = member.second;
            const bool isSetting = !mv.IsObject() || mv.Find("value") != nullptr;
            if (isSetting) {
                const json::Value* val = mv.IsObject() ? mv.Find("value") : &mv;
                if (val && ApplyLoadedValue(owner, member.first, *val)) {
                    ++applied;
                } else {
                    ++unknown;
                    OwnerLog(owner).Warn("unknown setting '%s' in %s -- ignored",
                                         member.first.c_str(), jsonPath.c_str());
                }
                continue;
            }
            for (const auto& inner : mv.Members()) {
                const json::Value& iv = inner.second;
                const json::Value* val = iv.IsObject() ? iv.Find("value") : &iv;
                if (val && ApplyLoadedValue(owner, inner.first, *val)) {
                    ++applied;
                } else {
                    ++unknown;
                    OwnerLog(owner).Warn("unknown setting '%s' in %s -- ignored",
                                         inner.first.c_str(), jsonPath.c_str());
                }
            }
        }
        OwnerLog(owner).Info("loaded %d setting(s) from %s%s", applied, jsonPath.c_str(),
                             unknown ? " (some unknown keys ignored)" : "");
        return true;
    }

    OwnerLog(owner).Info("no config at %s -- using defaults", jsonPath.c_str());
    return false;
}

void SettingsRegistry::PersistNative() {
    for (const Record& rec : records_) {
        if (rec.def.nativeEntry) {
            NativeSettingsBridge::Get().Persist();
            return;
        }
    }
}

bool SettingsRegistry::HasNativeSettings() const {
    for (const Record& rec : records_) {
        if (rec.def.nativeEntry) return true;
    }
    return false;
}

bool SettingsRegistry::SaveOwner(const char* owner, const std::string& jsonPath) {
    json::Value settings = json::Value::Object();

    for (const int idx : IndicesFor(owner)) {
        const Record& rec = records_[idx];

        if (rec.def.source != SettingSource::ModJson) continue;

        json::Value entry = json::Value::Object();
        switch (rec.def.type) {
            case SettingType::Bool: entry.Set("value", json::Value(rec.num != 0.0)); break;
            case SettingType::Int:
            case SettingType::Float: entry.Set("value", json::Value(rec.num)); break;
            case SettingType::String: entry.Set("value", json::Value(rec.str)); break;
        }

        if (!rec.def.description.empty()) {
            entry.Set("description", json::Value(rec.def.description));
        }

        const json::Value* existing = settings.Find(rec.def.subcategory);
        json::Value group = existing ? *existing : json::Value::Object();
        group.Set(rec.def.key, entry);
        settings.Set(rec.def.subcategory, group);
    }

    json::Value root = json::Value::Object();
    root.Set("version", json::Value(1.0));
    root.Set("mod", json::Value(std::string(owner)));
    root.Set("settings", settings);

    if (!WriteWholeFileAtomic(jsonPath, root.Serialize())) return false;
    OwnerLog(owner).Info("saved to %s", jsonPath.c_str());
    return true;
}
