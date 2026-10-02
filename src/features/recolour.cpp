// SPDX-License-Identifier: MIT
#include <cstring>
#include <string>

#include "core/log.h"
#include "core/mod.h"
#include "core/settings/mod_settings.h"
#include "features/hex_colour.h"
#include "features/team_colours.h"
#include "target/addresses.h"

namespace {

const Logger kLog{"recolour"};

constexpr int kBlack = team_colours::kBlack;
constexpr int kFloatsPerColour = 4;

constexpr uint8_t kBlackRampBase = 0xe4;
constexpr int kRampLength = 4;
constexpr int kPaletteStride = 4;

const char* const kKeyBlack = "black";
const char* const kKeyPinkHex = "pink_hex";
const char* const kKeyCyanHex = "cyan_hex";

constexpr int kDefaultBlack = 0;

struct Choice {
    const char* label;
    const char* key;
    const char* defaultHex;
};
const Choice kChoices[] = {
    {"Pink", kKeyPinkHex, "e87bdd"},
    {"Cyan", kKeyCyanHex, "00c6e0"},
};
constexpr int kChoiceCount = sizeof(kChoices) / sizeof(kChoices[0]);

ModSettings g_settings{"recolour"};

bool g_active = false;
uint8_t g_rgb[3] = {};

void RebuildTarget() {
    g_active = false;
    const int choice = g_settings.GetInt(kKeyBlack);
    if (choice >= 1 && choice <= kChoiceCount) {
        const Choice& c = kChoices[choice - 1];
        const std::string hex = g_settings.GetString(c.key);
        unsigned rgb;
        if (!hex_colour::Parse(hex, &rgb)) {
            kLog.Warn("%s \"%s\" is not six hex digits (e.g. %s) -- using %s", c.key, hex.c_str(),
                      c.defaultHex, c.defaultHex);
            hex_colour::Parse(c.defaultHex, &rgb);
        }
        g_rgb[0] = static_cast<uint8_t>((rgb >> 16) & 0xff);
        g_rgb[1] = static_cast<uint8_t>((rgb >> 8) & 0xff);
        g_rgb[2] = static_cast<uint8_t>(rgb & 0xff);
        g_active = true;
        kLog.Info("black -> %s (%06x)", c.label, rgb);
    } else {
        kLog.Info("black -> default");
    }
    team_colours::SetOverride(kBlack, g_active, team_colours::Pack(g_rgb[0], g_rgb[1], g_rgb[2]));
}

float* g_table = nullptr;
float g_stock[kFloatsPerColour] = {};

void ApplyHdTable() {
    float want[kFloatsPerColour];
    std::memcpy(want, g_stock, sizeof(want));
    if (g_active)
        for (int c = 0; c < 3; ++c) want[c] = g_rgb[c] / 255.0f;
    float* live = g_table + kBlack * kFloatsPerColour;
    if (std::memcmp(live, want, sizeof(want)) != 0) std::memcpy(live, want, sizeof(want));
}

struct Ramp {
    bool owned = false;
    uint8_t stock[kRampLength][3] = {};
    uint8_t written[kRampLength][3] = {};
};
Ramp g_ramp;

bool RampHolds(const uint8_t* live, const uint8_t (&rgb)[kRampLength][3]) {
    for (int k = 0; k < kRampLength; ++k)
        for (int c = 0; c < 3; ++c)
            if (live[k * kPaletteStride + c] != rgb[k][c]) return false;
    return true;
}

void WriteRamp(uint8_t* live, const uint8_t (&rgb)[kRampLength][3]) {
    for (int k = 0; k < kRampLength; ++k)
        for (int c = 0; c < 3; ++c) live[k * kPaletteStride + c] = rgb[k][c];
}

int Luma(const uint8_t* rgb) { return (rgb[0] * 299 + rgb[1] * 587 + rgb[2] * 114) / 1000; }

void BuildRamp(const uint8_t (&stock)[kRampLength][3], const uint8_t* rgb,
               uint8_t (&out)[kRampLength][3]) {
    const int base = Luma(stock[0]);
    for (int k = 0; k < kRampLength; ++k) {
        for (int c = 0; c < 3; ++c) {
            int v = rgb[c];
            if (k > 0 && base > 0) v = v * Luma(stock[k]) / base;
            out[k][c] = static_cast<uint8_t>(v > 255 ? 255 : v);
        }
    }
}

void ApplyPalette() {
    uint8_t* const palette = *game::kRenderPalette.Get();
    if (!palette) return;
    Ramp& r = g_ramp;
    uint8_t* live = palette + kBlackRampBase * kPaletteStride;
    if (r.owned && !RampHolds(live, r.written)) r.owned = false;
    if (g_active) {
        if (!r.owned)
            for (int k = 0; k < kRampLength; ++k)
                for (int c = 0; c < 3; ++c) r.stock[k][c] = live[k * kPaletteStride + c];
        uint8_t want[kRampLength][3];
        BuildRamp(r.stock, g_rgb, want);
        if (!r.owned || !RampHolds(live, want)) {
            WriteRamp(live, want);
            std::memcpy(r.written, want, sizeof(want));
            r.owned = true;
        }
    } else if (r.owned) {
        WriteRamp(live, r.stock);
        r.owned = false;
    }
}

class RecolourMod : public IMod {
public:
    const char* Name() const override { return "recolour"; }

    bool Install() override {
        g_settings.BeginGroup("Accessibility", "Team Colors");
        g_settings.RegisterInt(kKeyBlack, kDefaultBlack, "Black Team Color",
                               "Changes the black team color for increased map and minimap "
                               "visibility. Only displays locally.",
                               0, kChoiceCount);
        std::vector<std::string> options{"Default"};
        for (const Choice& c : kChoices) options.push_back(c.label);
        g_settings.SetOptions(kKeyBlack, options);

        for (const Choice& c : kChoices) {
            g_settings.RegisterString(c.key, c.defaultHex, c.key,
                                      "The color used for this choice, as six hex digits (RRGGBB), "
                                      "with no # or 0x. Settings file only.");
            g_settings.SetHidden(c.key);
        }

        g_table = game::kTeamColourRgba.Get();
        std::memcpy(g_stock, g_table + kBlack * kFloatsPerColour, sizeof(g_stock));

        if (g_settings.ReloadIfChanged()) RebuildTarget();
        return true;
    }

    void OnTick() override {
        if (g_settings.ReloadIfChanged()) RebuildTarget();
        ApplyHdTable();
        ApplyPalette();
    }
};

}  // namespace

MOD_REGISTER(RecolourMod)
