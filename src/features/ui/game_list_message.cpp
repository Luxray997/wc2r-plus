// SPDX-License-Identifier: MIT
#include "features/ui/game_list_message.h"

#include <functional>
#include <string>
#include <vector>

#include "core/log.h"
#include "target/addresses.h"

namespace ui {
namespace {

const Logger kLog{"gamelistmsg"};

std::vector<std::function<void()>> g_buttons;

void* Popup() { return game::kGameListPopup.Get(); }

void OkButton() {
    void* ctx = *game::kNkContextMenus.Get();
    if (!ctx) return;
    const char* label = game::kLookupLocalized.Get()("common_ok");
    if (game::kDrawMenuButton.Get()(ctx, label)) {
        game::kCloseNkPopup.Get()(Popup());
    }
}

}  // namespace

void ShowGameListMessage(const char* text) {
    if (!text || !*text) return;

    void* const popup = Popup();
    const std::string body(text);
    game::kAssignNkPopupText.Get()(popup, &body);

    unsigned int block[7] = {};
    game::kBuildStockPopupLayout.Get()(block);
    game::kAssignNkPopupLayout.Get()(
        popup, block[0], reinterpret_cast<float&>(block[1]), reinterpret_cast<float&>(block[2]),
        reinterpret_cast<float&>(block[3]), reinterpret_cast<float&>(block[4]),
        reinterpret_cast<float&>(block[5]), reinterpret_cast<float&>(block[6]));

    g_buttons.clear();
    g_buttons.emplace_back([] { OkButton(); });
    game::kAssignNkPopupButtons.Get()(popup, &g_buttons);

    game::kOpenNkPopup.Get()(popup);
    kLog.Info("showing '%.120s'", text);
}

}  // namespace ui
