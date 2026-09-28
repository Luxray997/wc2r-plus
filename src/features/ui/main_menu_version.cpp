// SPDX-License-Identifier: MIT
#include "features/ui/main_menu_version.h"

#include <cstdint>
#include <cstring>

#include "core/log.h"
#include "core/mod.h"
#include "core/named_window_hook.h"
#include "target/addresses.h"
#include "target/struct_offsets.h"
#include "wc2r_version.h"

namespace {

using namespace game;

const Logger kLog{"mainmenu"};

constexpr char kWindow[] = "main_menu";

constexpr char kText[] = WC2R_PLUS_NAME " Version " WC2R_PLUS_VERSION;

constexpr float kBuildLabelX = 20.0f;
constexpr float kBuildLabelHeight = 120.0f;
constexpr unsigned kAlignLeftMiddle = 0x11;

NkWidgetTextFn g_widgetText = nullptr;

uint32_t ApplyColourFactor(uint32_t colour, float factor) {
    if (factor == 1.0f) return colour;
    const uint32_t r = static_cast<uint8_t>(static_cast<int>((colour & 0xff) * factor));
    const uint32_t g = static_cast<uint8_t>(static_cast<int>(((colour >> 8) & 0xff) * factor));
    const uint32_t b = static_cast<uint8_t>(static_cast<int>(((colour >> 16) & 0xff) * factor));
    return (colour & 0xff000000u) | (b << 16) | (g << 8) | r;
}

named_window::Result OnMainMenu(void* ctx) {
    void* win = nk::CurrentWindow(ctx);
    void* canvas = nk::WindowCanvas(ctx);
    const auto* c = static_cast<const uint8_t*>(ctx);
    const auto* font = ctx ? *reinterpret_cast<const uint8_t* const*>(c + nk::kCtxFont) : nullptr;
    if (!win || !canvas || !font || !g_widgetText) return named_window::Result::Continue;

    float bounds[4];
    memcpy(bounds, static_cast<const uint8_t*>(win) + nk::kWindowBounds, sizeof(bounds));
    const float fontH = *reinterpret_cast<const float*>(font + nk::kUserFontHeight);
    const float padY = nk::CtxFloat(ctx, nk::kCtxTextPaddingY);
    const float lineH = fontH + padY * 2.0f;

    uint32_t style[7];
    const float zero = 0.0f;
    memcpy(&style[0], &zero, 4);
    memcpy(&style[1], &zero, 4);
    memcpy(&style[2], c + nk::kCtxWindowBackground, 4);
    uint32_t colour;
    memcpy(&colour, c + nk::kCtxTextColour, 4);
    colour = ApplyColourFactor(colour, nk::CtxFloat(ctx, nk::kCtxTextColourFactor));
    memcpy(&style[3], &colour, 4);
    memcpy(&style[4], c + nk::kCtxTextExtra0, 4);
    memcpy(&style[5], c + nk::kCtxTextExtra1, 4);
    memcpy(&style[6], c + nk::kCtxTextExtra2, 4);

    g_widgetText(canvas, bounds[0] + kBuildLabelX, bounds[1] + lineH, bounds[2] - kBuildLabelX,
                 kBuildLabelHeight, kText, static_cast<int>(sizeof(kText) - 1), style,
                 kAlignLeftMiddle, font);
    return named_window::Result::Continue;
}

class MainMenuVersionMod : public IMod {
public:
    const char* Name() const override { return "mainmenu"; }

    bool Install() override {
        g_widgetText = kNkWidgetText.Get();
        const bool ok = named_window::Register(kWindow, &OnMainMenu);
        kLog.Info("ready: \"%s\"", kText);
        return ok;
    }
};

}  // namespace

MOD_REGISTER(MainMenuVersionMod)
