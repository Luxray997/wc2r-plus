add_library(version_proxy SHARED
    core/build_guard.cpp
    core/hooks.cpp
    core/log.cpp
    core/map_index.cpp
    core/match_start.cpp
    core/mod.cpp
    core/named_window_hook.cpp
    core/paths.cpp
    core/peer_messages.cpp
    core/sha256.cpp
    core/settings/mod_settings.cpp
    core/settings/native_settings_bridge.cpp
    core/settings/native_settings_catalog.cpp
    core/settings/settings_registry.cpp
    core/settings/settings_screen.cpp

    features/chat.cpp
    features/lobby_policy.cpp
    features/lobby_teams.cpp
    features/map_preview.cpp
    features/map_preview_pud.cpp
    features/map_download.cpp
    features/map_download/client_role.cpp
    features/map_download/game_map.cpp
    features/map_download/host_role.cpp
    features/map_download/outbox_send.cpp
    features/map_download/pud_validator.cpp
    features/map_download/map_name.cpp
    features/map_download/wire.cpp
    features/map_download/download_client.cpp
    features/map_download/download_host.cpp
    features/map_download/map_store.cpp
    features/net_watch.cpp
    features/peer_handshake.cpp
    features/recolour.cpp
    features/team_colours.cpp

    features/ui/alliances_screen.cpp
    features/ui/custom_scenario_screen.cpp
    features/ui/in_match_hud.cpp
    features/ui/lobby_creation_screen.cpp
    features/ui/lobby_chat.cpp
    features/ui/main_menu_version.cpp
    features/ui/map_browser.cpp
    features/ui/map_panel.cpp
    features/ui/game_list_message.cpp
    features/ui/multiplayer_lobby_screen.cpp
    features/ui/screen_kit.cpp

    json/json.cpp

    target/base.cpp
    target/version_proxy.cpp

    ${CMAKE_BINARY_DIR}/generated/version.rc
)

set_target_properties(version_proxy PROPERTIES
    OUTPUT_NAME "version"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

target_include_directories(version_proxy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(version_proxy PRIVATE minhook shell32 ole32)
