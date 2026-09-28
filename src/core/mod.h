// SPDX-License-Identifier: MIT
#pragma once

#include <vector>

class IMod {
public:
    virtual ~IMod() = default;

    virtual const char* Name() const = 0;

    virtual bool Install() = 0;

    virtual void OnTick() {}
};

class ModRegistry {
public:
    static ModRegistry& Get();

    void Register(IMod* mod);

    void InstallAll();

    void TickAll(bool networked);

    bool InNetworkedMatch() const { return networked_; }

private:
    struct Entry {
        IMod* mod;
        bool installed = false;
    };
    std::vector<Entry> mods_;
    bool networked_ = false;
};

#define MOD_REGISTER(type)                                                        \
    static type g_##type##_instance;                                             \
    namespace {                                                                  \
    struct type##Registrar {                                                     \
        type##Registrar() { ModRegistry::Get().Register(&g_##type##_instance); } \
    } g_##type##_registrar;                                                      \
    }
