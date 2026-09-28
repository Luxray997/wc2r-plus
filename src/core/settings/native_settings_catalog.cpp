// SPDX-License-Identifier: MIT
#include "core/settings/native_settings_catalog.h"

#include <cstring>

#include <string>
#include <utility>
#include <vector>

#include "core/log.h"
#include "core/settings/native_settings_bridge.h"
#include "core/settings/settings_registry.h"

namespace {

const Logger kLog{"stock"};

struct Placement {
    const char* key;
    const char* category;
    const char* subcategory;
    const char* label;

    const char* description;

    bool hidden;
};

const Placement kPlacements[] = {

    {"music", "Sound", "Volume", "option_music_volume",
     "Background music volume, 0 (silent) to 100."},
    {"sfx", "Sound", "Volume", "option_sfx_volume",
     "Sound effects volume, 0 (silent) to 100."},
    {"remastered_music", "Sound", "Music Version", "option_musicversion_title",
     "Which soundtrack to play: the original DOS music, or the remastered version."},
    {"unitspeech", "Sound", "Effects", "option_sound_unit_speech",
     "Spoken alerts, such as the warning that you are under attack."},
    {"unitnoise", "Sound", "Effects", "option_sound_unit_acknowledgement",
     "The sounds units make when you select them or give them an order."},
    {"bldgnoise", "Sound", "Effects", "option_sound_building_sound",
     "The sounds buildings make when you select them."},
    {"dispmsgs", "Sound", "Effects", "option_sound_subtitles",
     "Show subtitles for spoken lines."},

    {"speed", "Speed", "Speeds", "option_speed_game_speed",
     "How fast the game world runs, 0 to 8. The game's own default is 6."},
    {"mscroll", "Speed", "Speeds", "option_speed_mouse_scroll",
     "How fast the view scrolls when the mouse is at a screen edge, 0 to 6."},
    {"kscroll", "Speed", "Speeds", "option_speed_key_scroll",
     "How fast the view scrolls from the keyboard, 0 to 6."},

    {"screen_mode", "Preferences", "Display", "option_display_mode",
     "Whether the game runs in a window or full screen."},
    {"display_monitor", "Preferences", "Display", "option_display_screen",
     "Which monitor the game opens on. The list is the game's own display count."},

    {"resolution_width", "Preferences", "Display", "Resolution Width",
     "Width in pixels, 640 to 3840. A value outside that range falls back to 1920, it is not "
     "clamped.",
     true},
    {"resolution_height", "Preferences", "Display", "Resolution Height",
     "Height in pixels, 480 to 2160. A value outside that range falls back to 1080, it is not "
     "clamped.",
     true},
    {"interface", "Preferences", "Mouse", "option_preference_mouse_interface",
     "Mouse scheme: on is the Warcraft II scheme, off the Warcraft I one."},

    {"mouse_size", "Preferences", "Mouse", "option_preference_mouse_size",
     "Size of the mouse cursor as a percentage of the cursor art, 15 to 100. Default 50.",
     true},
    {"mouse_trapped", "Preferences", "Mouse", "option_preference_mouse_trapped",
     "Keep the mouse cursor inside the game window."},
    {"ui_scale_mode", "Preferences", "UI Scale", "option_preference_ui_scale",
     "How the interface is scaled: Automatic, Fixed, or Classic."},
    {"ui_scale_fixed", "Preferences", "UI Scale", "UI Scale (Fixed)",
     "The interface scale factor, 1 to 4. Used only when UI scale is set to Fixed."},
    {"using_classic", "Preferences", "UI Scale", "Classic UI Scale",
     "A flag the game groups with classic UI scaling. What it changes on its own is not "
     "established."},
    {"ui_unlock_aspect", "Preferences", "UI Scale", "Use Window Aspect Ratio",
     "Unlocks the UI's aspect ratio so it follows the window instead of a fixed one."},

    {"name", "Preferences", "Player", "Player Name",
     "The multiplayer player name. Edit it in Warcraft2.ini: the game's buffer holds 12 characters "
     "and this screen has no writer for it.",
     true},
    {"tip", "Preferences", "Other", "option_preference_tips",
     "Show the game's tips."},
    {"intro", "Preferences", "Other", "option_preference_intro",
     "Play the intro movie on startup, after the Blizzard logo."},
    {"hide_objectives", "Preferences", "Other", "option_preference_hide_objectives",
     "Hide the mission objectives display."},

    {"show_healthbars", "Gameplay", "Health Bars", "option_gameplay_healthbar_show",
     "Draw health bars on units. Pairs with 'Only when damaged' to give the stock menu's three "
     "choices."},
    {"healthbars_only_when_damaged", "Gameplay", "Health Bars", "Only When Damaged",
     "Limit health bars to units that are not at full health. Needs health bars on."},
    {"healthbars_below", "Gameplay", "Health Bars", "option_gameplay_healthbar_location",
     "Whether each health bar is drawn above or below its unit."},
    {"resourcebar_align_left", "Gameplay", "Resource Bar", "Aligned Left",
     "Aligns the resource bar left. With 'Aligned Right' it encodes the stock left / center / "
     "right choice."},
    {"resourcebar_align_right", "Gameplay", "Resource Bar", "Aligned Right",
     "Aligns the resource bar right. With 'Aligned Left' it encodes the stock left / center / "
     "right choice."},
    {"portrait_style_scaled", "Gameplay", "Portraits", "Scaled",
     "Draws unit portraits scaled. Paired with 'Cropped'; the stock menu offers one or the other."},
    {"portrait_style_cropped", "Gameplay", "Portraits", "Cropped",
     "Draws unit portraits cropped. Paired with 'Scaled'; the stock menu offers one or the other."},
    {"show_button_hotkeys", "Gameplay", "Controls", "option_gameplay_show_button_hotkeys",
     "Print each command button's hotkey on the button itself."},
    {"use_grid_keys", "Gameplay", "Controls", "option_use_grid_keys",
     "Use a grid hotkey layout for the command buttons instead of the game's lettered keys."},

    {"center_on_cluster", "Gameplay", "Camera", "Center on Unit Cluster",
     "Center the view on a cluster of units rather than on one of them. Uses 'Max Cluster "
     "Distance'.",
     true},
    {"max_cluster_distance", "Gameplay", "Camera", "Max Cluster Distance",
     "How far apart units may be and still count as one cluster, 0 to 12.", true},

    {"tipnum", "Advanced", "Misc", "Next Tip Number",
     "Index of the next tip the game will show. The game writes this itself as tips are shown.",
     true},

    {"univbe", "Advanced", "Legacy", "Legacy: UniVBE",
     "DOS-era key: 1 forced the UniVBE video driver to load even on a VESA-capable card. Nothing in "
     "this build reads it.",
     true},
    {"vidmouse", "Advanced", "Legacy", "Legacy: Video Mouse",
     "DOS-era key: a mouse-and-video workaround, 0 off / 1 partial / 2 full. Nothing in this build "
     "reads it.",
     true},
};

uint32_t* g_pGameFlags = nullptr;

bool FlagOn(uint32_t mask) { return g_pGameFlags && (*g_pGameFlags & mask) != 0; }
void WriteFlags(uint32_t clearMask, uint32_t setMask) {
    if (!g_pGameFlags) return;
    *g_pGameFlags = (*g_pGameFlags & ~clearMask) | setMask;
}

constexpr uint32_t kFlagShowHealthbars = 0x00200000;
constexpr uint32_t kFlagHealthbarsNotFull = 0x00400000;

int GetHealthbarMode() {

    if (!FlagOn(kFlagShowHealthbars)) return 1;
    return FlagOn(kFlagHealthbarsNotFull) ? 2 : 0;
}
void SetHealthbarMode(int i) {
    if (i == 1) {
        WriteFlags(kFlagShowHealthbars | kFlagHealthbarsNotFull, 0);
    } else if (i == 2) {
        WriteFlags(0, kFlagShowHealthbars | kFlagHealthbarsNotFull);
    } else {
        WriteFlags(kFlagHealthbarsNotFull, kFlagShowHealthbars);
    }
}

constexpr uint32_t kFlagResourceLeft = 0x02000000;
constexpr uint32_t kFlagResourceRight = 0x04000000;

int GetResourceBarAlign() {

    if (FlagOn(kFlagResourceLeft)) return 0;
    return FlagOn(kFlagResourceRight) ? 2 : 1;
}
void SetResourceBarAlign(int i) {
    if (i == 0) {
        WriteFlags(kFlagResourceRight, kFlagResourceLeft);
    } else if (i == 2) {
        WriteFlags(kFlagResourceLeft, kFlagResourceRight);
    } else {
        WriteFlags(kFlagResourceLeft | kFlagResourceRight, 0);
    }
}

constexpr uint32_t kFlagPortraitScaled = 0x10000000;
constexpr uint32_t kFlagPortraitCropped = 0x20000000;

int GetPortraitStyle() { return FlagOn(kFlagPortraitScaled) ? 0 : 1; }
void SetPortraitStyle(int i) {

    WriteFlags(kFlagPortraitScaled | kFlagPortraitCropped,
                i == 0 ? kFlagPortraitScaled : kFlagPortraitCropped);
}

void ApplyOptionList(SettingDef& d) {
    struct OptionList {
        const char* key;
        const char* labels[4];
        const int values[4];
    };
    static const OptionList kLists[] = {

        {"screen_mode", {"option_windowed", "option_full_screen", nullptr}, {1, 0, -1}},
        {"remastered_music", {"option_musicversion_dos", "option_musicversion_remastered", nullptr},
         {-1}},
        {"healthbars_below",
         {"option_gameplay_healthbar_location_above", "option_gameplay_healthbar_location_below",
          nullptr},
         {-1}},

        {"ui_scale_mode",
         {"option_preference_ui_scale_classic", "option_preference_ui_scale_auto",
          "option_preference_ui_scale_fixed", nullptr},
         {2, 0, 1, -1}},
    };
    for (const OptionList& l : kLists) {
        if (_stricmp(l.key, d.key.c_str()) != 0) continue;
        d.type = SettingType::Int;
        d.input = InputType::Options;
        d.options.clear();
        for (int i = 0; i < 4 && l.labels[i]; ++i) d.options.push_back(l.labels[i]);
        if (l.values[0] >= 0) {
            d.optionValues.clear();
            for (int i = 0; i < 4 && l.values[i] >= 0; ++i) d.optionValues.push_back(l.values[i]);
        }
        d.numMin = 0;
        d.numMax = (double)(d.options.size() - 1);
        return;
    }
}

void HideNativeKey(SettingsRegistry& core, const char* key) {
    const int idx = core.Find("game", key);
    if (idx >= 0) core.SetHidden(idx, true);
}

void RegisterDerived(SettingsRegistry& core, const char* key, const char* category,
                      const char* subcategory, const char* label, const char* description,
                      std::vector<std::string> options, int (*get)(), void (*set)(int)) {
    SettingDef d;
    d.owner = "game";
    d.key = key;
    d.source = SettingSource::NativeDerived;
    d.type = SettingType::Int;
    d.input = InputType::Options;
    d.category = category;
    d.subcategory = subcategory;
    d.label = label;
    d.description = description;
    d.options = std::move(options);
    d.derivedGet = get;
    d.derivedSet = set;
    d.numMin = 0;
    d.numMax = (double)(d.options.size() - 1);
    core.Register(d);
}

const Placement* FindPlacement(const char* key) {
    for (const Placement& p : kPlacements) {
        if (_stricmp(p.key, key) == 0) return &p;
    }
    return nullptr;
}

void RegisterDerivedNativeSettings(SettingsRegistry& core, NativeSettingsBridge& native) {

    for (int i = 0; i < native.Count() && !g_pGameFlags; ++i) {
        const NativeSettingEntry* e = native.At(i);
        if (e && e->type == 2 && e->backing) g_pGameFlags = static_cast<uint32_t*>(e->backing);
    }
    if (!g_pGameFlags) {
        kLog.Warn("no flag entry in the table -- derived settings not registered");
        return;
    }

    RegisterDerived(core, "healthbar_mode", "Gameplay", "Health Bars",
                     "option_gameplay_healthbar_show",
                     "When to draw health bars on units.",
                     {"option_gameplay_healthbar_show_always",
                      "option_gameplay_healthbar_show_never",
                      "option_gameplay_healthbar_show_not_full"},
                     &GetHealthbarMode, &SetHealthbarMode);
    RegisterDerived(core, "resourcebar_align", "Gameplay", "Resource Bar",
                     "option_resourcebar_align_title",
                     "Where the resource bar sits along the top of the screen.",
                     {"option_resourcebar_align_left", "option_resourcebar_align_center",
                      "option_resourcebar_align_right"},
                     &GetResourceBarAlign, &SetResourceBarAlign);
    RegisterDerived(core, "portrait_style", "Gameplay", "Portraits",
                     "option_portrait_style_title",
                     "How unit portraits are fitted to their frame.",
                     {"option_portrait_style_scaled", "option_portrait_style_cropped"},
                     &GetPortraitStyle, &SetPortraitStyle);

    for (const char* key : {"show_healthbars", "healthbars_only_when_damaged",
                             "resourcebar_align_left", "resourcebar_align_right",
                             "portrait_style_scaled", "portrait_style_cropped"}) {
        HideNativeKey(core, key);
    }

    HideNativeKey(core, "using_classic");
}

}  // namespace

int RegisterNativeSettingsCatalog() {
    NativeSettingsBridge& native = NativeSettingsBridge::Get();
    if (!native.Init()) {
        kLog.Warn("the game's settings table failed validation -- no game "
                             "categories will be shown");
        return 0;
    }

    SettingsRegistry& core = SettingsRegistry::Get();
    int registered = 0;
    int unplaced = 0;
    int hidden = 0;

    for (int i = 0; i < native.Count(); ++i) {
        const NativeSettingEntry* e = native.At(i);
        if (!e || !e->key) continue;

        SettingDef d;
        d.owner = "game";
        d.key = e->key;
        d.source = SettingSource::NativeIni;
        d.nativeEntry = e;

        const Placement* p = FindPlacement(e->key);
        if (p) {
            d.category = p->category;
            d.subcategory = p->subcategory;
            d.label = p->label;
            d.description = p->description;
            d.hidden = p->hidden;
        } else {

            d.category = "Advanced";
            d.subcategory = "Unsorted";
            d.label = e->key;

            d.description = "In Warcraft2.ini but not in the mod loader's placement table, so it "
                             "has no label or description yet.";
            ++unplaced;
        }

        switch (e->type) {
            case 1:

                if (e->minValue == 0 && e->maxOrMask == 1) {
                    d.type = SettingType::Bool;
                    d.input = InputType::Toggle;
                    d.numDefault = (double)e->defOrCap;
                    break;
                }
                d.type = SettingType::Int;
                d.input = InputType::Slider;
                d.numMin = (double)e->minValue;
                d.numMax = (double)e->maxOrMask;
                d.numDefault = (double)e->defOrCap;
                break;
            case 2:
                d.type = SettingType::Bool;
                d.input = InputType::Toggle;
                d.numDefault = 0;
                break;
            default:
                d.type = SettingType::String;
                d.input = InputType::Text;
                break;
        }

        if (p) ApplyOptionList(d);

        core.Register(d);
        ++registered;
    }

    RegisterDerivedNativeSettings(core, native);

    for (int i : core.IndicesFor("game")) {
        if (core.DefAt(i).hidden) ++hidden;
    }

    kLog.Info("registered %d game setting(s), %d hidden%s", registered, hidden,
                    unplaced ? " (some unplaced -- shown under Advanced)" : "");
    if (unplaced) {
        kLog.Info("%d key(s) are not in the placement table", unplaced);
    }
    return registered;
}
