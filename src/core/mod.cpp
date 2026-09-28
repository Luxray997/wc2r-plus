// SPDX-License-Identifier: MIT
#include "core/mod.h"

#include "core/log.h"

ModRegistry& ModRegistry::Get() {
    static ModRegistry instance;
    return instance;
}

void ModRegistry::Register(IMod* mod) {
    mods_.push_back({mod});
}

void ModRegistry::InstallAll() {
    const Logger log{"loader"};
    for (Entry& e : mods_) {
        e.installed = e.mod->Install();
        if (e.installed) {
            log.Info("mod '%s' installed", e.mod->Name());
        } else {
            log.Error("mod '%s' failed to install", e.mod->Name());
        }
    }
}

void ModRegistry::TickAll(bool networked) {
    networked_ = networked;
    for (Entry& e : mods_) {
        if (e.installed) e.mod->OnTick();
    }
}
