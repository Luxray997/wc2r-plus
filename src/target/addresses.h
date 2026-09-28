// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

#include "target/game_version.h"

namespace game {

uintptr_t Base();

template <typename Fn>
struct Fun {
    uintptr_t addr;
    Fn Get() const { return reinterpret_cast<Fn>(Base() + GameRva(addr)); }
    void* Target() const { return reinterpret_cast<void*>(Base() + GameRva(addr)); }
};

template <typename T>
struct Obj {
    uintptr_t addr;
    T* Get() const { return reinterpret_cast<T*>(Base() + GameRva(addr)); }
    uintptr_t Addr() const { return Base() + GameRva(addr); }
};

using TransitionGameStateFn = void(__cdecl*)(int newState);
constexpr Fun<TransitionGameStateFn> kTransitionGameState{0x004c48e0};

using ProcessNetworkTurnFn = int(__cdecl*)();
constexpr Fun<ProcessNetworkTurnFn> kProcessNetworkTurn{0x0049ea20};

using ProcessLocalMatchTickFn = int(__cdecl*)();
constexpr Fun<ProcessLocalMatchTickFn> kProcessLocalMatchTick{0x004d8d70};

constexpr Obj<uint32_t> kPlayerSelectedUnits{0x00933e18};

constexpr Obj<uint32_t> kTurnPlayerStatus{0x00916220};

constexpr Obj<uint32_t> kTurnSyncMismatch{0x009166fc};

constexpr Obj<uint32_t> kNetTurnCounter{0x00916210};

constexpr Obj<uint32_t> kLobbySlotByNetSlot{0x009164c0};

using GetNextPeerPacketFn = int(__cdecl*)(unsigned* sender, unsigned char** buf, int* len);
constexpr Fun<GetNextPeerPacketFn> kGetNextPeerPacket{0x0049d060};

using SelectMapByRegistryNameFn = bool(__cdecl*)(const char* name);
constexpr Fun<SelectMapByRegistryNameFn> kSelectMapByRegistryName{0x004f6dd0};

using KickLobbyPeerFn = void(__cdecl*)(unsigned netSlot, unsigned char reason);
constexpr Fun<KickLobbyPeerFn> kKickLobbyPeer{0x0049b5e0};

constexpr Obj<unsigned char> kMpLobbyPopup{0x0095d368};

constexpr Obj<unsigned int> kSkinMpLobbyPopup{0x0095d278};

using DrawNkPopupFn = void(__thiscall*)(void* popup);
constexpr Fun<DrawNkPopupFn> kDrawNkPopup{0x00626560};

constexpr Obj<unsigned char> kGameListPopup{0x0095d7e0};

using AssignNkPopupTextFn = void(__thiscall*)(void* popup, const void* text);
constexpr Fun<AssignNkPopupTextFn> kAssignNkPopupText{0x00627390};

using AssignNkPopupButtonsFn = void(__thiscall*)(void* popup, const void* v);
constexpr Fun<AssignNkPopupButtonsFn> kAssignNkPopupButtons{0x006273b0};

using AssignNkPopupLayoutFn = void(__thiscall*)(void* popup, unsigned int skin, float width, float height,
                                                float pad, float textTop, float bottom, float lineH);
constexpr Fun<AssignNkPopupLayoutFn> kAssignNkPopupLayout{0x006273d0};

using NkPopupToggleFn = void(__thiscall*)(void* popup);
constexpr Fun<NkPopupToggleFn> kOpenNkPopup{0x00627430};
constexpr Fun<NkPopupToggleFn> kCloseNkPopup{0x00627450};

constexpr Obj<unsigned int> kSkinPopupFrame{0x0095e790};

using BuildStockPopupLayoutFn = void(__cdecl*)(void* out7dwords);
constexpr Fun<BuildStockPopupLayoutFn> kBuildStockPopupLayout{0x00547280};

using LeaveNetSessionFn = void(__thiscall*)(void* session);
constexpr Fun<LeaveNetSessionFn> kLeaveNetSession{0x0051fe50};
using ResetMpLobbyChatInputFn = void(__cdecl*)();
constexpr Fun<ResetMpLobbyChatInputFn> kResetMpLobbyChatInput{0x00535c00};
constexpr Obj<unsigned char> kMpLobbyLeftFlag{0x0095d424};
constexpr Obj<unsigned int> kSkinPopupFrameAlt{0x0095e794};

constexpr Obj<void*> kMapDownloadSession{0x00916528};

using ScanDirectoryIntoMapRegistryFn = void(__cdecl*)(void* stdfsPath);
constexpr Fun<ScanDirectoryIntoMapRegistryFn> kScanDirectoryIntoMapRegistry{0x004f63d0};

using War2MpBNetCtorFn = void*(__thiscall*)(void* self);
constexpr Fun<War2MpBNetCtorFn> kWar2MpBNetCtor{0x00522fe0};

using ScanPudMetadataFromBufferFn = bool(__cdecl*)(const unsigned char* data, unsigned size);
constexpr Fun<ScanPudMetadataFromBufferFn> kScanPudMetadataFromBuffer{0x004d2ed0};
using SelectMapAndReadHeaderFn = int(__cdecl*)(const char* path, void* info72, int keepMapId);
constexpr Fun<SelectMapAndReadHeaderFn> kSelectMapAndReadHeader{0x00495ef0};

constexpr Obj<uint8_t> kMissingMapFlag{0x0091669d};

constexpr Obj<char> kMapsBaseDir{0x00964840};

using GameOperatorDeleteFn = void(__cdecl*)(void* p, size_t size);
constexpr Fun<GameOperatorDeleteFn> kGameOperatorDelete{0x007e91f3};

constexpr Obj<void> kSettingsTable{0x008cb498};

using PersistSettingsFn = void(__cdecl*)();
constexpr Fun<PersistSettingsFn> kPersistSettings{0x00589980};

using ApplyMusicVolumeFn = void(__cdecl*)(void* settingsBlock);
constexpr Fun<ApplyMusicVolumeFn> kApplyMusicVolume{0x004f99f0};

using RestartMusicFn = void(__cdecl*)();
constexpr Fun<RestartMusicFn> kRestartMusic{0x004f9d80};

using ApplyGameSpeedFn = void(__cdecl*)(uint32_t speed, char inMatch);
constexpr Fun<ApplyGameSpeedFn> kApplyGameSpeed{0x004c4910};

using RebuildMouseCursorsFn = void(__cdecl*)();
constexpr Fun<RebuildMouseCursorsFn> kRebuildMouseCursors{0x004b8000};

using ApplyGridKeysFn = void(__cdecl*)(char on);
constexpr Fun<ApplyGridKeysFn> kApplyGridKeys{0x004e86c0};

using ApplyUiScaleFn = void(__cdecl*)(uint32_t rule, uint32_t fixedFactor);
constexpr Fun<ApplyUiScaleFn> kApplyUiScale{0x005a1910};

using ApplyUiAspectFn = void(__cdecl*)(uint32_t on);
constexpr Fun<ApplyUiAspectFn> kApplyUiAspect{0x005a1960};

using ApplyDisplayModeFn = void(__cdecl*)(uint32_t fullscreen, uint32_t monitor);
constexpr Fun<ApplyDisplayModeFn> kApplyDisplayMode{0x005a1250};

using BeginNamedWindowFn = int(__cdecl*)(void* ctx, const char* name, float x, float y, float w,
                                         float h, unsigned int flags);
constexpr Fun<BeginNamedWindowFn> kBeginNamedWindow{0x005a9bf0};

using EndCurrentWindowFn = void(__cdecl*)(void* ctx);
constexpr Fun<EndCurrentWindowFn> kEndCurrentWindow{0x005aa7e0};

using GetUiSpaceWidthFn = float(__cdecl*)();
constexpr Fun<GetUiSpaceWidthFn> kGetUiSpaceWidth{0x00547260};

using OverrideNextRectFn = void(__cdecl*)(void* ctx, float x, float y, float w, float h);
constexpr Fun<OverrideNextRectFn> kOverrideNextRect{0x005ac550};

using NkLayoutWidgetSpaceFn = void(__cdecl*)(float* bounds, void* ctx, void* win, int modify);
constexpr Fun<NkLayoutWidgetSpaceFn> kNkLayoutWidgetSpace{0x005d1370};

using BeginNamedGroupFn = int(__cdecl*)(void* ctx, const char* name, unsigned int flags);
constexpr Fun<BeginNamedGroupFn> kBeginNamedGroup{0x005aca30};

using EndGroupFn = void(__cdecl*)(void* ctx);
constexpr Fun<EndGroupFn> kEndGroup{0x005acb70};

using LayoutSpaceBeginFn = void(__cdecl*)(void* ctx, int fmt, float height, int widgetCount);
constexpr Fun<LayoutSpaceBeginFn> kLayoutSpaceBegin{0x005ac490};

using LayoutSpaceEndFn = void(__cdecl*)(void* ctx);
constexpr Fun<LayoutSpaceEndFn> kLayoutSpaceEnd{0x005ac5c0};

using LayoutRowDynamicFn = void(__cdecl*)(void* ctx, float height, int cols);
constexpr Fun<LayoutRowDynamicFn> kLayoutRowDynamic{0x005aba00};

using DrawLabelFn = void(__cdecl*)(void* ctx, const char* text, int align);
constexpr Fun<DrawLabelFn> kDrawLabel{0x005aee20};

using DrawWrappedLabelFn = void(__cdecl*)(void* ctx, const char* text, uint32_t colour);
constexpr Fun<DrawWrappedLabelFn> kDrawWrappedLabel{0x005aeeb0};

using CountWrappedLinesFn = int(__cdecl*)(void* ctx, const char* text, int len, float wrapWidth);
constexpr Fun<CountWrappedLinesFn> kCountWrappedLines{0x00625730};

using NkFillRectFn = void(__cdecl*)(void* canvas, float x, float y, float w, float h,
                                    float rounding, uint32_t colour);
constexpr Fun<NkFillRectFn> kNkFillRect{0x005c44a0};

constexpr Obj<void*> kNkContextMenus{0x00965170};

using LookupLocalizedFn = const char*(__cdecl*)(const char* key);
constexpr Fun<LookupLocalizedFn> kLookupLocalized{0x0058ab00};

constexpr Obj<char> kSelectedMapPath{0x009196d0};

constexpr Obj<unsigned short> kSelectedMapId{0x009190e0};

constexpr Obj<void> kLobbyCreationMapPath{0x008ca7e8};

using CustomScenarioBuildFn = char(__cdecl*)();
constexpr Fun<CustomScenarioBuildFn> kCustomScenarioBuild{0x00527d90};

using LoadMapFileFn = unsigned int(__cdecl*)(unsigned short mapId, const char* path,
                                             void** outBuf, unsigned int* outLen);
constexpr Fun<LoadMapFileFn> kLoadMapFile{0x004d2670};

using GameFreeFn = void(__stdcall*)(void* p, const char* file, int line, int flag);
constexpr Fun<GameFreeFn> kGameFree{0x0054d820};

struct NkImage {
    void* handle;
    uint16_t w, h;
    uint16_t region[4];
};

using NkDrawImageFn = void(__cdecl*)(void* canvas, float x, float y, float w, float h,
                                     const NkImage* img, uint32_t colour);
constexpr Fun<NkDrawImageFn> kNkDrawImage{0x005c4b60};

struct GameTexture {
    int32_t w;
    int32_t h;
    uint32_t glName;
    int32_t format;
    int32_t filter;
};

constexpr int kGameTextureFormatRgba8 = 0;
constexpr int kGameTextureFilterNearest = 0;
constexpr int kGameTextureFilterLinear = 1;

using CreateGameTextureFn = GameTexture**(__cdecl*)(GameTexture** out, int w, int h,
                                                    const void* pixels, int format, int filter);
constexpr Fun<CreateGameTextureFn> kCreateGameTexture{0x00615dc0};

using GlTextureSubImage2DFn = void(__stdcall*)(uint32_t texture, int level, int xoffset,
                                               int yoffset, int width, int height, uint32_t format,
                                               uint32_t type, const void* pixels);
constexpr Obj<GlTextureSubImage2DFn> kGlTextureSubImage2DSlot{0x00965df0};

using GlTextureParameteriFn = void(__stdcall*)(uint32_t texture, uint32_t pname, int param);
constexpr Obj<GlTextureParameteriFn> kGlTextureParameteriSlot{0x00965dd0};

using PushMapMessageFn = void(__cdecl*)(const char* text, int highlight, unsigned durationMs);
constexpr Fun<PushMapMessageFn> kPushMapMessage{0x00614a90};

using DrawMapMessageListFn = void(__cdecl*)(void* ctx, int lineHeight);
constexpr Fun<DrawMapMessageListFn> kDrawMapMessageList{0x00614f50};

using WriteStickyMessageFn = void(__cdecl*)(const char* text, unsigned durationMs);
constexpr Fun<WriteStickyMessageFn> kWriteStickyMessage{0x00614b30};

using ResetMapMessageSlotsFn = void(__fastcall*)(void* unused);
constexpr Fun<ResetMapMessageSlotsFn> kResetMapMessageSlots{0x006149e0};

using BuildMapMessagesPanelFn = void(__cdecl*)(void* ctx, float x, float y, float w, float h,
                                               int lineHeight);
constexpr Fun<BuildMapMessagesPanelFn> kBuildMapMessagesPanel{0x00614c20};

using DrawMapMessageLineFn = int(__cdecl*)(void* ctx, const char* text, float x, float y, float w,
                                           float h, int highlight, int used, int lineHeight);
constexpr Fun<DrawMapMessageLineFn> kDrawMapMessageLine{0x00614dd0};

using ExpireSlotFn = void(__cdecl*)(void* slot, unsigned nowMs);
constexpr Fun<ExpireSlotFn> kExpireSlot{0x00614da0};

using GetClockMsFn = unsigned(__cdecl*)();
constexpr Fun<GetClockMsFn> kGetClockMs{0x00625940};

using ContentRegionSizeFn = uint64_t(__cdecl*)(void* ctx);
constexpr Fun<ContentRegionSizeFn> kContentRegionSize{0x005aad40};

using ScrollNamedGroupToFn = void(__cdecl*)(void* ctx, const char* name, int x, int y);
constexpr Fun<ScrollNamedGroupToFn> kScrollGroupTo{0x005ad330};

constexpr Obj<uint8_t> kMapMessageSlots{0x009b17a0};

using DisplayChatOrGameMessageFn = void(__cdecl*)(const char* text, uint8_t who, unsigned durationMs,
                                                  char targetMode);
constexpr Fun<DisplayChatOrGameMessageFn> kDisplayChatOrGameMessage{0x004d3160};

constexpr Obj<char> kPlayerNames{0x0091ada8};

constexpr Obj<uint8_t> kTeamColourReorder{0x00919390};

constexpr Obj<float> kTeamColourRgba{0x008c9640};

constexpr Obj<uint8_t*> kRenderPalette{0x00949e04};

using ChatTextboxEventProcFn = unsigned(__cdecl*)(void* dlg, uint8_t* evt);
constexpr Fun<ChatTextboxEventProcFn> kChatTextboxEventProc{0x004e9470};

using ToggleChatComposeFn = void(__cdecl*)(void* dlg, uint8_t* evt, char submit);
constexpr Fun<ToggleChatComposeFn> kToggleChatCompose{0x004ea320};

constexpr Fun<ChatTextboxEventProcFn> kMinimapTerrainKeyProc{0x004e96f0};

using SendComposedChatFn = void(__cdecl*)(const char* text);
constexpr Fun<SendComposedChatFn> kSendComposedChat{0x004ea590};

using WriteChatPromptLineFn = void(__cdecl*)(const char* format, const char* arg1, const char* arg2);
constexpr Fun<WriteChatPromptLineFn> kWriteChatPromptLine{0x00614b70};

constexpr Obj<uint8_t> kChatTargetMode{0x009347be};

constexpr Obj<uint8_t> kChatRecipientMask{0x00918be9};

constexpr Obj<uint8_t> kLocalPlayer{0x00918ccd};

constexpr Obj<uint8_t> kPlayerOwner{0x00918cac};

constexpr Obj<uint8_t> kDiplomacyStance{0x00919578};

using NkPanelAllocSpaceFn = void(__cdecl*)(float* boundsOut, void* ctx);
constexpr Fun<NkPanelAllocSpaceFn> kNkPanelAllocSpace{0x005d1ae0};

using NkTextClampFn = int(__cdecl*)(const void* font, const char* text, int len, float space,
                                    int* glyphs, float* width, const uint32_t* separators,
                                    int separatorCount);
constexpr Fun<NkTextClampFn> kNkTextClamp{0x005caa20};

constexpr Obj<uint32_t> kNkWrapSeparators{0x008cb9c4};

using NkWidgetTextFn = void(__cdecl*)(void* canvas, float x, float y, float w, float h,
                                      const char* text, int len, const void* textStyle,
                                      unsigned align, const void* font);
constexpr Fun<NkWidgetTextFn> kNkWidgetText{0x005d1ff0};

using NkTextWidthFn = float(__cdecl*)(void* userdata, float height, const char* text, int len);

using BuildOptionsMenuScreenFn = void(__cdecl*)(int variant);
constexpr Fun<BuildOptionsMenuScreenFn> kBuildOptionsMenuScreen{0x005429a0};

constexpr Obj<uint32_t> kPauseMenuScreen{0x0095dfdc};

using DrawMenuButtonFn = char(__cdecl*)(void* ctx, const char* text);
constexpr Fun<DrawMenuButtonFn> kDrawMenuButton{0x005453b0};

using DrawBannerFn = void(__cdecl*)();
constexpr Fun<DrawBannerFn> kDrawBanner{0x005453e0};

using SliderU32Fn = bool(__cdecl*)(uint32_t* value, uint32_t* minV, uint32_t* maxV, uint32_t* step);
constexpr Fun<SliderU32Fn> kSliderU32{0x00546250};

using FlagCheckboxFn = bool(__cdecl*)(const char* text, uint32_t* flags, uint32_t mask);
constexpr Fun<FlagCheckboxFn> kFlagCheckbox{0x005461d0};

using RadioFn = bool(__cdecl*)(const char* text, uint8_t* selected);
constexpr Fun<RadioFn> kRadio{0x00546160};

using TextEditFn = unsigned int(__cdecl*)(void* ctx, unsigned int flags, char* buffer, int maxLen,
                                          void* filter);
constexpr Fun<TextEditFn> kTextEdit{0x00545890};

constexpr Obj<void> kTextEditFilter{0x005c3230};

using DisableScopeFn = void(__cdecl*)(void* ctx);
constexpr Fun<DisableScopeFn> kDisableBegin{0x005ae470};
constexpr Fun<DisableScopeFn> kDisableEnd{0x005ae7e0};

using DisplayCountFn = int(__cdecl*)();
constexpr Fun<DisplayCountFn> kDisplayCount{0x00674110};

using ComboBeginFn = int(__cdecl*)(void* ctx, const char* text, int align, float w, float h);
constexpr Fun<ComboBeginFn> kComboBegin{0x005b7440};

using ComboItemFn = int(__cdecl*)(void* ctx, const char* text, int align);
constexpr Fun<ComboItemFn> kComboItem{0x005b7af0};

using ComboScopeFn = void(__cdecl*)(void* ctx);
constexpr Fun<ComboScopeFn> kComboClose{0x005b7be0};
constexpr Fun<ComboScopeFn> kComboEnd{0x005b7c00};

constexpr Obj<unsigned int> kSkinComboClosed{0x0095e354};
constexpr Obj<unsigned int> kSkinComboClosedAlt{0x0095e358};
constexpr Obj<unsigned int> kSkinComboOpen{0x0095e394};
constexpr Obj<unsigned int> kSkinComboOpenAlt{0x0095e398};

using SetSkinFn = unsigned int(__thiscall*)(void* manager, unsigned int skinId);
constexpr Fun<SetSkinFn> kSetSkin{0x0059a2b0};

constexpr Obj<void> kSkinManager{0x00965310};

constexpr Obj<unsigned int> kSkinWindow{0x0095dd34};
constexpr Obj<unsigned int> kSkinWindowAlt{0x0095dd38};

constexpr Obj<unsigned char> kSkinAltFlag{0x008c03c8};

constexpr Obj<unsigned int> kSkinContentPanel{0x0095dd5c};
constexpr Obj<unsigned int> kSkinTextEdit{0x0095d668};

using DoScrollbarVFn = float(__cdecl*)(void* state, void* out, float x, float y, float w, float h,
                                       int hasScrolling, float offset, float target, float step,
                                       float buttonPixelInc, void* style, void* in, void* font);
constexpr Fun<DoScrollbarVFn> kDoScrollbarV{0x005d61b0};

constexpr Obj<uint8_t*> kMapListBegin{0x0095c570};
constexpr Obj<uint8_t*> kMapListEnd{0x0095c574};

constexpr Obj<void> kFolderList{0x0095c57c};

constexpr Obj<uint32_t> kMapListMode{0x0095c59c};

constexpr Obj<uint32_t> kMapListModeAtSelect{0x0095c5a0};

constexpr Obj<uint8_t> kSelectedEntry{0x008c9c88};

using CopyMapListEntryFn = void*(__thiscall*)(void* dst, const void* src);
constexpr Fun<CopyMapListEntryFn> kCopyMapListEntry{0x00527bb0};

using RefreshBuiltInMapListFn = void(__cdecl*)(void* mapListVector);
constexpr Fun<RefreshBuiltInMapListFn> kRefreshBuiltInMapList{0x004bb960};

using RefreshMapListFromFolderFn = void(__cdecl*)(void* mapListVector, void* folderListVector,
                                                  const void* relativePath);
constexpr Fun<RefreshMapListFromFolderFn> kRefreshMapListFromFolder{0x004bb9c0};

using SelectBuiltInScenarioFn = int(__cdecl*)();
constexpr Fun<SelectBuiltInScenarioFn> kSelectBuiltInScenario{0x00527cc0};

using ValidateMapFileFn = char(__cdecl*)(const char* path);
constexpr Fun<ValidateMapFileFn> kValidateMapFile{0x004bbd70};

constexpr Obj<uint8_t> kPendingGameSetup{0x0091af50};

using DrawLobbyDropdownFn = char(__cdecl*)(const char* comboId, void* currentValue,
                                           const void* options, uint8_t* openFlag, float itemH);
constexpr Fun<DrawLobbyDropdownFn> kDrawLobbyDropdown{0x005454f0};

constexpr Fun<DrawLobbyDropdownFn> kDrawFolderDropdown{0x00545650};

constexpr Obj<void> kModeCurrent{0x008c9d98};
constexpr Obj<void> kModeOptions{0x0095c504};
constexpr Obj<void> kMapSizeCurrent{0x008c9db0};
constexpr Obj<void> kMapSizeOptions{0x0095c528};
constexpr Obj<void> kRaceCurrent{0x008c9d20};
constexpr Obj<void> kRaceOptions{0x0095c540};
constexpr Obj<void> kOpponentsCurrent{0x008c9d68};
constexpr Obj<void> kOpponentsOptions{0x0095c534};
constexpr Obj<void> kTilesetCurrent{0x008c9d80};
constexpr Obj<void> kTilesetOptions{0x0095c54c};
constexpr Obj<void> kResourcesCurrent{0x008c9d38};
constexpr Obj<void> kResourcesOptions{0x0095c51c};
constexpr Obj<void> kUnitsCurrent{0x008c9d50};
constexpr Obj<void> kUnitsOptions{0x0095c510};

constexpr Obj<void> kMapFolderCurrent{0x008c9cf0};
constexpr Obj<void> kMapFolderRelPath{0x008c9d08};

constexpr Obj<uint8_t> kComboOpenMode{0x0095c5a7};
constexpr Obj<uint8_t> kComboOpenMapSize{0x0095c5a8};
constexpr Obj<uint8_t> kComboOpenFolder{0x0095c599};
constexpr Obj<uint8_t> kComboOpenRace{0x0095c59a};
constexpr Obj<uint8_t> kComboOpenOpponents{0x0095c5a5};
constexpr Obj<uint8_t> kComboOpenTileset{0x0095c5a6};
constexpr Obj<uint8_t> kComboOpenResources{0x0095c59b};
constexpr Obj<uint8_t> kComboOpenUnits{0x0095c5a4};

using DrawLobbyNavStripFn = void(__cdecl*)(void* ctx, float x, float y, float w, float h,
                                           int unused6, int unused7, unsigned char* exitOut);
constexpr Fun<DrawLobbyNavStripFn> kDrawLobbyNavStrip{0x00527730};

using LobbyStartButtonFn = void(__thiscall*)(void* closure);
constexpr Fun<LobbyStartButtonFn> kLobbyStartButton{0x005290b0};

constexpr Obj<uint8_t> kLobbyLeavingFlag{0x0095c598};

using DestroyMapListEntryFn = void(__thiscall*)(void* entry);
constexpr Fun<DestroyMapListEntryFn> kDestroyMapListEntry{0x004baaa0};

using PreviousGameStateFn = unsigned short(__cdecl*)();
constexpr Fun<PreviousGameStateFn> kPreviousGameState{0x004c48b0};

using ButtonLabelFn = int(__cdecl*)(void* ctx, const char* label);
constexpr Fun<ButtonLabelFn> kButtonLabel{0x005af470};

using SpacingFn = void(__cdecl*)(void* ctx);
constexpr Fun<SpacingFn> kSpacing{0x005ac9e0};

using FormatLocalizedFn = const char*(__cdecl*)(const char* key, ...);
constexpr Fun<FormatLocalizedFn> kFormatLocalized{0x0058ac20};

using StringAssignFn = void*(__thiscall*)(void* str, const char* data, size_t len);
constexpr Fun<StringAssignFn> kStringAssign{0x00522640};

constexpr Obj<unsigned int> kSkinCustomScenario{0x0095c488};
constexpr Obj<unsigned int> kSkinLobbyLeftFrame{0x0095c49c};
constexpr Obj<unsigned int> kSkinLobbyRightFrame{0x0095c4a4};
constexpr Obj<unsigned int> kFontLobbyHeading{0x0095c4d4};

using SetFontFn = unsigned int(__thiscall*)(void* manager, unsigned int fontId);
constexpr Fun<SetFontFn> kSetFont{0x0059a300};

using LobbyCreationContentFn = char(__cdecl*)();
constexpr Fun<LobbyCreationContentFn> kLobbyCreationContent{0x00538e20};

using LobbyCreationInitFn = void(__cdecl*)();
constexpr Fun<LobbyCreationInitFn> kLobbyCreationInit{0x00538a80};

constexpr Obj<uint8_t> kLobbyCreationReady{0x0095d59c};

constexpr Obj<uint8_t*> kLobbyMapListBegin{0x0095d5e4};
constexpr Obj<uint8_t*> kLobbyMapListEnd{0x0095d5e8};

constexpr Obj<void> kLobbyFolderList{0x0095d5f0};

constexpr Obj<uint8_t> kLobbyCreationSelected{0x008ca7c8};

constexpr Obj<void> kLobbyFolderCurrent{0x008ca830};
constexpr Obj<void> kLobbyFolderRelPath{0x008ca848};
constexpr Obj<void> kLobbyMapSizeCurrent{0x008ca8ac};
constexpr Obj<void> kLobbyMapSizeOptions{0x0095d510};
constexpr Obj<void> kLobbyResourcesCurrent{0x008ca894};
constexpr Obj<void> kLobbyResourcesOptions{0x0095d504};
constexpr Obj<void> kLobbyUnitsCurrent{0x008ca87c};
constexpr Obj<void> kLobbyUnitsOptions{0x0095d4f8};
constexpr Obj<void> kLobbyTilesetCurrent{0x008ca8c4};
constexpr Obj<void> kLobbyTilesetOptions{0x0095d534};
constexpr Obj<void> kLobbyStartLocCurrent{0x008ca8dc};
constexpr Obj<void> kLobbyStartLocOptions{0x0095d540};

constexpr Obj<uint8_t> kLobbyComboOpenFolder{0x0095d59d};
constexpr Obj<uint8_t> kLobbyComboOpenMapSize{0x0095d624};
constexpr Obj<uint8_t> kLobbyComboOpenResources{0x0095d59f};
constexpr Obj<uint8_t> kLobbyComboOpenUnits{0x0095d59e};
constexpr Obj<uint8_t> kLobbyComboOpenTileset{0x0095d625};
constexpr Obj<uint8_t> kLobbyComboOpenStartLoc{0x0095d626};

constexpr Obj<uint32_t> kLobbyGameSpeed{0x0095d628};

constexpr Obj<char*> kLobbyNameBuffer{0x008ca860};
constexpr Obj<uint32_t> kLobbyNameLength{0x008ca864};
constexpr Obj<uint32_t> kLobbyNameCapacity{0x008ca868};

constexpr Obj<char> kLobbyPassword{0x0095d60c};
constexpr int kLobbyPasswordCapacity = 0x18;

using LobbyCreateButtonFn = void(__thiscall*)(void* closure);
constexpr Fun<LobbyCreateButtonFn> kLobbyCreateButton{0x00538710};

constexpr Obj<unsigned int> kSkinLobbyCreation{0x0095d470};
constexpr Obj<unsigned int> kSkinLobbyCreationLeft{0x0095d484};
constexpr Obj<unsigned int> kSkinLobbyCreationRight{0x0095d48c};
constexpr Obj<unsigned int> kFontLobbyCreationHeading{0x0095d4bc};

using LobbySlotRowFn = void(__cdecl*)(int slot, const float* ratios, float rowH, float spacerH);
constexpr Fun<LobbySlotRowFn> kMpLobbySlotRow{0x005348a0};

using LobbyLeaveButtonFn = void(__cdecl*)();
constexpr Fun<LobbyLeaveButtonFn> kMpLobbyLeaveButton{0x005361e0};

using LobbyStartGameButtonFn = void(__cdecl*)();
constexpr Fun<LobbyStartGameButtonFn> kMpLobbyStartGameButton{0x00534690};

using LobbySendChatFn = void(__cdecl*)();
constexpr Fun<LobbySendChatFn> kMpLobbySendChat{0x00538100};

using InputKeyPressedFn = int(__cdecl*)(void* ctx, int key);
constexpr Fun<InputKeyPressedFn> kInputKeyPressed{0x005c5f20};

using LayoutRowRatiosFn = void(__cdecl*)(void* ctx, int fmt, float height, int cols,
                                          const float* ratios);
constexpr Fun<LayoutRowRatiosFn> kLayoutRowRatios{0x005abcf0};

using LobbyMaxPlayersFn = unsigned char(__cdecl*)();
constexpr Fun<LobbyMaxPlayersFn> kMpLobbyMaxPlayers{0x004a0640};

constexpr Obj<unsigned char> kMpLobbyCountdownActive{0x0095d426};

using MillisecondClockFn = long long(__cdecl*)();
constexpr Fun<MillisecondClockFn> kMillisecondClock{0x00671bd0};

constexpr Obj<long long> kMpLobbyEnterMs{0x0095d240};

constexpr Obj<char> kMpLobbyGameName{0x00916534};
constexpr Obj<unsigned char> kMpLobbyPlayerCount{0x00916554};

constexpr Obj<unsigned char> kMpLobbyGameSpeed{0x00916556};
constexpr Obj<char> kMpLobbyHostName{0x00916564};
constexpr Obj<char> kMpLobbyMapNameText{0x0091657d};

constexpr Obj<unsigned int> kMpLobbyMatchFlags{0x00916560};

constexpr Obj<unsigned char> kMpLobbyFixedStartLocation{0x0091668a};

constexpr Obj<void> kMpLobbyResourceKeys{0x0095d314};
constexpr Obj<void> kMpLobbyTilesetKeys{0x0095d344};

constexpr Obj<void> kMpLobbyTeamOptions{0x0095d3c8};

using LobbyTeamChangeRequestFn = void(__cdecl*)(const unsigned char* packet, int senderNetSlot);
constexpr Fun<LobbyTeamChangeRequestFn> kHandleLobbyTeamChangeRequest{0x00497e60};

using LobbySlotSwapRequestFn = void(__cdecl*)(const unsigned char* packet, int senderNetSlot);
constexpr Fun<LobbySlotSwapRequestFn> kHandleLobbyExchangeTwoSlotsRequest{0x00497b60};

using LobbySlotDropdownFn = char(__cdecl*)(char disabled, char prevValue, const char* label,
                                            void* value, void* optionList, char* changedFlag,
                                            float rowH, char localize);
constexpr Fun<LobbySlotDropdownFn> kLobbySlotDropdown{0x005358e0};

constexpr Obj<unsigned char*> kMpLobbyChatBegin{0x0095d404};
constexpr Obj<unsigned char*> kMpLobbyChatEnd{0x0095d408};

constexpr Obj<void> kMpLobbyChatInput{0x008ca628};

constexpr Obj<unsigned char> kMpLobbyChatScrollRequest{0x008ca504};

constexpr Obj<void*> kNetSession{0x0095bbd4};

constexpr Obj<unsigned char> kMpLobbySlots{0x00916260};

constexpr Obj<unsigned char*> kMpLobbySlotNames{0x0095d3f8};

constexpr Obj<unsigned int> kSkinMultiplayerLobbyChat{0x0095d2a8};
constexpr Obj<unsigned int> kSkinMultiplayerLobbyText{0x0095d278};
constexpr Obj<unsigned int> kFontMultiplayerLobbyHeading{0x0095d410};

constexpr Obj<uint8_t> kDipAlliedChecks{0x0095c064};
constexpr Obj<uint8_t> kDipVisionChecks{0x0095c06c};

constexpr Obj<uint8_t> kDipAlliedVictoryCheck{0x008c9a34};

using DipCheckboxFn = bool(__cdecl*)(const char* label, uint8_t* value);
constexpr Fun<DipCheckboxFn> kDipCheckbox{0x00545370};

using SendDiplomacyFn = void(__cdecl*)();
constexpr Fun<SendDiplomacyFn> kSendDiplomacyFromUI{0x00526540};

using AlliancesAllowedFn = int(__cdecl*)();
constexpr Fun<AlliancesAllowedFn> kAlliancesAllowed{0x00486d50};

using DrawTeamColourIconFn = void(__cdecl*)(int player);
constexpr Fun<DrawTeamColourIconFn> kDrawTeamColourIcon{0x00525110};

using AlliancesFooterFn = void(__cdecl*)(void* ctx, float x, float y, float w, float h, int unusedA,
                                         int unusedB);
constexpr Fun<AlliancesFooterFn> kAlliancesFooter{0x00524ac0};

constexpr Obj<const uint8_t*> kDipNameTable{0x0095c048};

constexpr Obj<unsigned int> kSkinAlliances{0x0095beb4};
constexpr Obj<unsigned int> kSkinAlliancesAlt{0x0095beb8};
constexpr Obj<unsigned int> kFontAlliancesRow{0x0095bf0c};

constexpr Obj<uint8_t> kPeerTable{0x00916a40};

constexpr Obj<uint8_t> kPlayerMatchResult{0x0091aa84};

using BuildInMatchHudFn = void(__cdecl*)(char whichView);
constexpr Fun<BuildInMatchHudFn> kBuildInMatchHud{0x0052f5f0};

using HudBeginFrameFn = void(__cdecl*)();
constexpr Fun<HudBeginFrameFn> kHudBeginFrame{0x0059ab10};

using HudUiExtentFn = float(__cdecl*)();
constexpr Fun<HudUiExtentFn> kHudUiWidth{0x005a1ab0};
constexpr Fun<HudUiExtentFn> kHudUiHeight{0x005a1b00};

using HudAtlasLookupFn = bool(__thiscall*)(void* atlas, NkImage* out, uint32_t nameHash);
constexpr Fun<HudAtlasLookupFn> kHudAtlasLookup{0x00617450};

using HudFramePieceSizeFn = int*(__cdecl*)(int* outWH, const NkImage* img, int view);
constexpr Fun<HudFramePieceSizeFn> kHudFramePieceSize{0x0052f510};

using HudDrawImageFn = void(__cdecl*)(void* ctx, NkImage img);
constexpr Fun<HudDrawImageFn> kHudDrawImage{0x005aeee0};

using HudDrawTiledFn = void(__cdecl*)(NkImage img, int x, int y, int length, int horizontal,
                                      int view);
constexpr Fun<HudDrawTiledFn> kHudDrawTiled{0x0052de20};

using HudResourceCellFn = void(__cdecl*)(float x, float y, float w, float h, int slot, int slots,
                                         int icon, int amount, int view);
constexpr Fun<HudResourceCellFn> kHudResourceCell{0x0052ce20};

using HudFoodCellFn = void(__cdecl*)(float x, float y, float w, float h, int slot, int slots,
                                     int used, int cap, int view);
constexpr Fun<HudFoodCellFn> kHudFoodCell{0x0052d220};

using HudMenuButtonsFn = void(__cdecl*)(int view);
constexpr Fun<HudMenuButtonsFn> kHudMenuButtons{0x0052d930};

using HudMinimapTextureFn = void*(__cdecl*)();
constexpr Fun<HudMinimapTextureFn> kHudMinimapTexture1{0x0050e790};
constexpr Obj<void*> kHudMinimapTexture0{0x00949e00};

using HudUnitHasCargoFn = int(__cdecl*)(void* unit);
constexpr Fun<HudUnitHasCargoFn> kHudUnitHasCargo{0x004e6910};

using HudPortraitFn = void(__cdecl*)(float x, float y, float w, float h, int view);
constexpr Obj<HudPortraitFn> kHudPortraitByType{0x008c9f48};
using HudUnitInfoFn = void(__cdecl*)(int a, int x, int y, int c, int style, int view);
constexpr Fun<HudUnitInfoFn> kHudUnitInfo{0x0052e940};
using HudGroupInfoFn = void(__cdecl*)(int x, int y, int c, int style, int view);
constexpr Fun<HudGroupInfoFn> kHudGroupInfo{0x0052ec90};

using HudCommandButtonsFn = void*(__cdecl*)(int view);
constexpr Fun<HudCommandButtonsFn> kHudCommandButtons{0x0052caf0};
using HudCommandTooltipFn = void(__cdecl*)(void* button, int bottom, int view);
constexpr Fun<HudCommandTooltipFn> kHudCommandTooltip{0x0052e080};

using HudObjectivesLinesFn = int(__cdecl*)(void* ctx, int wrapWidth);
constexpr Fun<HudObjectivesLinesFn> kHudObjectivesLines{0x00546080};
using HudObjectivesDrawFn = void(__cdecl*)(void* ctx, int wrapWidth, float lineHeight);
constexpr Fun<HudObjectivesDrawFn> kHudObjectivesDraw{0x005457c0};

using DlgGetControlFn = uint8_t*(__cdecl*)(void* dlg, int id);
constexpr Fun<DlgGetControlFn> kDlgGetControl{0x004a3110};

constexpr Obj<void*> kHudCtx{0x0095c7a8};
constexpr Obj<void*> kHudSkinManager{0x0095c7ac};
constexpr Obj<void*> kHudCtxView0{0x00965178};
constexpr Obj<void*> kHudCtxView1{0x00965174};
constexpr Obj<void> kHudSkinManagerView0{0x00965490};
constexpr Obj<void> kHudSkinManagerView1{0x00965180};
constexpr Obj<uint8_t> kHudSkipFrame{0x0095c7b0};
constexpr Obj<unsigned int> kHudFont{0x0095c974};

constexpr Obj<unsigned int> kHudSkinView0{0x0095c990};
constexpr Obj<unsigned int> kHudSkinView0Alt{0x0095c994};
constexpr Obj<unsigned int> kHudSkinView1{0x0095c998};
constexpr Obj<unsigned int> kHudSkinView1Alt{0x0095c99c};

constexpr Obj<void*> kHudFrameAtlasView0{0x00939ccc};
constexpr Obj<void*> kHudFrameAtlasView1{0x00939cd0};

constexpr Obj<uint8_t> kHudSelectionCount{0x009342ea};
constexpr Obj<uint8_t*> kHudSelectedUnit{0x009342ec};
constexpr Obj<void*> kHudSelectionOther{0x009342f0};
constexpr Obj<uint32_t> kUnitTypeFlags{0x009185f0};

constexpr Obj<int> kHudGold{0x00934780};
constexpr Obj<int> kHudLumber{0x0093477c};
constexpr Obj<int> kHudOil{0x00934784};
constexpr Obj<uint16_t> kFoodA{0x0091b38c};
constexpr Obj<uint16_t> kFoodB{0x0091b6ac};
constexpr Obj<uint16_t> kFoodCap{0x0091b50c};

constexpr Obj<uint32_t> kGameSettingsFlags{0x00964cc0};

constexpr Obj<uint8_t> kMatchPauseFlags{0x0091c596};

constexpr Obj<uint8_t> kNetworkedMatch{0x00922f5b};
constexpr Obj<uint8_t> kHudNoObjectives{0x00918cce};

constexpr Obj<void*> kChatDialog{0x00934788};
constexpr Obj<uint8_t> kChatTargetSlot{0x00918beb};

using EditControlEventProcFn = unsigned(__cdecl*)(uint8_t* control, uint8_t* evt);
constexpr Fun<EditControlEventProcFn> kEditControlEventProc{0x004a6350};
using DlgEditRefreshFn = void(__cdecl*)(uint8_t* control);
constexpr Fun<DlgEditRefreshFn> kDlgEditRefresh{0x004a39e0};

using PauseCommandFn = void(__cdecl*)();
constexpr Fun<PauseCommandFn> kHandlePauseCommand{0x004d8c90};
constexpr Fun<PauseCommandFn> kHandleResumeCommand{0x004d8c20};
constexpr Obj<uint8_t> kCommandIssuer{0x00922f5a};

constexpr Obj<uint8_t> kPausesRemaining{0x009197fc};

}  // namespace game
