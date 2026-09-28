// SPDX-License-Identifier: MIT
#include "features/chat.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <share.h>
#include <string>
#include <vector>

#include "core/hook_utils.h"
#include "core/log.h"
#include "core/match_start.h"
#include "core/paths.h"
#include "core/mod.h"
#include "core/settings/mod_settings.h"
#include "features/lobby_teams.h"
#include "features/team_colours.h"
#include "target/addresses.h"
#include "target/struct_offsets.h"

namespace {

using namespace game;

const Logger kLog{"chat"};

constexpr int kSlotSticky = 15;
constexpr int kSlotPrompt = 16;

constexpr size_t kScrollStyleSpan = nk::kScrollbarStride * 2;
constexpr size_t kScrollvOffset = nk::kCtxScrollV - nk::kCtxScrollH;

constexpr int kScrollToBottomY = 1000000;

PushMapMessageFn g_realPushMapMessage = nullptr;
DrawMapMessageListFn g_realDrawMapMessageList = nullptr;
WriteStickyMessageFn g_realWriteStickyMessage = nullptr;
BuildMapMessagesPanelFn g_realBuildMapMessagesPanel = nullptr;
DisplayChatOrGameMessageFn g_realDisplayChatOrGameMessage = nullptr;
ChatTextboxEventProcFn g_realChatTextboxEventProc = nullptr;
ChatTextboxEventProcFn g_realMinimapTerrainKeyProc = nullptr;
SendComposedChatFn g_realSendComposedChat = nullptr;
WriteChatPromptLineFn g_realWriteChatPromptLine = nullptr;
PauseCommandFn g_realHandlePause = nullptr;
PauseCommandFn g_realHandleResume = nullptr;

NkPanelAllocSpaceFn g_allocSpace = nullptr;
NkTextClampFn g_textClamp = nullptr;
NkWidgetTextFn g_widgetText = nullptr;

DrawMapMessageLineFn g_drawMapMessageLine = nullptr;
CountWrappedLinesFn g_countWrappedLines = nullptr;
OverrideNextRectFn g_overrideNextRect = nullptr;
DrawWrappedLabelFn g_drawWrappedLabel = nullptr;
ContentRegionSizeFn g_contentRegionSize = nullptr;
BeginNamedGroupFn g_beginNamedGroup = nullptr;
LayoutRowDynamicFn g_layoutRowDynamic = nullptr;
ScrollNamedGroupToFn g_scrollGroupTo = nullptr;
LayoutSpaceBeginFn g_layoutSpaceBegin = nullptr;
LayoutSpaceEndFn g_layoutSpaceEnd = nullptr;
EndGroupFn g_groupEnd = nullptr;
ExpireSlotFn g_expireSlot = nullptr;
GetClockMsFn g_getClockMs = nullptr;
NkFillRectFn g_nkFillRect = nullptr;

FILE* g_chatLog = nullptr;

constexpr uint32_t Rgba(int r, int g, int b, int a = 255) {
    return (uint32_t)(r & 0xff) | ((uint32_t)(g & 0xff) << 8) | ((uint32_t)(b & 0xff) << 16) |
           ((uint32_t)(a & 0xff) << 24);
}

constexpr uint32_t kHighlightColor = Rgba(255, 204, 0);

constexpr uint32_t kBlackNameColor = Rgba(0x20, 0x20, 0x20);

enum class Channel : uint8_t {
    System,
    All,
    Team,
    Allies,
    Enemies,
    Private,
    Count,
};

constexpr uint32_t kTextGrey = Rgba(0x92, 0x92, 0x92);
constexpr uint32_t kChannelColours[static_cast<int>(Channel::Count)] = {
    kHighlightColor,
    kTextGrey,
    Rgba(0x25, 0xe6, 0x5f),
    Rgba(0x69, 0xa7, 0xff),
    kTextGrey,
    kTextGrey,
};
constexpr const char* kChannelLabels[static_cast<int>(Channel::Count)] = {
    "", "[All]", "[Team]", "[Allies]", "[Enemies]", "[Private]",
};

constexpr uint8_t kTeamTargetMode = 8;

Channel ChannelOf(uint8_t targetMode) {
    switch (targetMode) {
        case kTeamTargetMode: return Channel::Team;
        case 3: return Channel::Allies;
        case 4: return Channel::Enemies;
        case 5: return Channel::Private;
        default: return Channel::All;
    }
}

uint8_t ComposeModeOf(Channel c) {
    return c == Channel::All ? 2 : c == Channel::Allies ? 3 : 6;
}

constexpr const char* kKeyEnabled = "enabled";

constexpr const char* kKeyLines = "lines";

constexpr const char* kKeyFade = "fade";

constexpr const char* kKeyClickUnfocus = "click_unfocus";
constexpr const char* kKeyBacklog = "backlog";
constexpr const char* kKeyTimestamps = "timestamps";
constexpr const char* kKeyLog = "log";

constexpr const char* kKeyBgAlpha = "bg";

constexpr const char* kKeyNameColours = "name_colours";

constexpr const char* kKeyChannels = "channels";

constexpr bool kAnchorBottom = true;

constexpr float kIndicatorWidth = 4.0f;

constexpr uint32_t kCursorColor = Rgba(166, 141, 105, 235);

ModSettings g_settings{"chat"};

constexpr bool kDefaultEnabled = true;
constexpr int kDefaultLines = 8;
constexpr int kDefaultFadeSeconds = 7;
constexpr bool kDefaultClickUnfocus = false;
constexpr int kDefaultBacklog = 100;
constexpr bool kDefaultTimestamps = false;
constexpr bool kDefaultLog = false;
constexpr int kDefaultBgAlpha = 100;
constexpr bool kDefaultNameColours = true;
constexpr bool kDefaultChannels = true;

void RegisterChatSettings() {
    g_settings.BeginGroup("Chat", "Behavior");
    g_settings.RegisterBool(kKeyEnabled, kDefaultEnabled, "Enabled",
                             "Master on/off switch for this mod.");

    g_settings.SetHidden(kKeyEnabled);

    g_settings.RegisterInt(kKeyLines, kDefaultLines, "History Height (Lines)",
                            "How tall the scrollable history box is, in wrapped lines.", 2, 20);
    g_settings.RegisterInt(kKeyFade, kDefaultFadeSeconds, "Fade Time",
                            "How long a message stays, in seconds, before fading out. Messages will "
                            "not fade if set to 0.", 0, 60);
    g_settings.RegisterBool(kKeyClickUnfocus, kDefaultClickUnfocus, "Click Closes Chat",
                             "Clicking discards your message and focuses the game, allowing hotkeys "
                             "to be pressed.");
    g_settings.RegisterInt(kKeyBacklog, kDefaultBacklog, "Saved Message Limit",
                            "How many messages to keep before the oldest are dropped.", 25, 500);
    g_settings.RegisterBool(kKeyTimestamps, kDefaultTimestamps, "Show Timestamps",
                             "Add a timestamp before each message.");
    g_settings.RegisterBool(kKeyLog, kDefaultLog, "Log Chat to Disk",
                             "Write messages to log files in the mod's logs folder.");

    g_settings.BeginGroup("Chat", "Style");
    g_settings.RegisterInt(kKeyBgAlpha, kDefaultBgAlpha, "Background Opacity",
                            "How opaque the background is, from 0 (invisible) to 255 (solid).", 0,
                            255);
    g_settings.RegisterBool(kKeyNameColours, kDefaultNameColours, "Color Player Names",
                             "Messages are drawn with sender's name as their in-game color.");
    g_settings.RegisterBool(kKeyChannels, kDefaultChannels, "Enhanced Chat Channels",
                             "Shows all messages with a channel indicating recipients, drawn with a "
                             "distinct color. Pressing tab while typing a message cycles channels. "
                             "Enter opens [Team], ctrl + Enter opens [Allies], and shift + Enter "
                             "opens [All]. [All] will be opened instead of opening an empty "
                             "channel.");
}

bool ChannelsOn() {
    return g_settings.GetBool(kKeyEnabled) && g_settings.GetBool(kKeyChannels);
}

struct NameSpan {
    size_t start;
    size_t len;
    uint8_t colourIndex;
};

struct Message {
    std::string text;
    int highlight;
    DWORD arrivedMs;

    std::vector<NameSpan> names;
    Channel channel = Channel::System;
};

struct Sender {
    bool active = false;
    uint8_t who = 0;
    const char* raw = nullptr;
    char targetMode = 0;
};
Sender g_sender;

CRITICAL_SECTION g_lock;
bool g_lockReady = false;
std::deque<Message> g_backlog;

bool g_scrollToBottomPending = false;

class Guard {
public:
    Guard() { if (g_lockReady) EnterCriticalSection(&g_lock); }
    ~Guard() { if (g_lockReady) LeaveCriticalSection(&g_lock); }
};

std::wstring g_chatLogName;

void SyncChatLog() {
    const bool wanted = g_settings.GetBool(kKeyEnabled) && g_settings.GetBool(kKeyLog);
    if (!wanted) {
        if (g_chatLog) {
            fclose(g_chatLog);
            g_chatLog = nullptr;
        }
        return;
    }
    if (g_chatLog) return;
    if (g_chatLogName.empty()) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        wchar_t name[64];
        _snwprintf_s(name, _countof(name), _TRUNCATE, L"chat-%04u-%02u-%02u_%02u-%02u-%02u.log",
                     t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        g_chatLogName = name;
    }
    const std::wstring path = paths::Logs(g_chatLogName);

    g_chatLog = path.empty() ? nullptr : _wfsopen(path.c_str(), L"a", _SH_DENYWR);
    if (!g_chatLog) {
        kLog.Error("could not open the chat log in the logs folder");
        return;
    }
    setvbuf(g_chatLog, nullptr, _IONBF, 0);
}

void AppendChatLogLine(const char* text) {
    if (!g_chatLog) return;
    time_t now = time(nullptr);
    struct tm t;
    localtime_s(&t, &now);
    fprintf(g_chatLog, "[%04d-%02d-%02d %02d:%02d:%02d] %s\n", t.tm_year + 1900, t.tm_mon + 1,
            t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, text);
}

void CaptureMessage(const char* text, int highlight, Channel channel = Channel::System,
                    std::vector<NameSpan> names = {}) {
    if (!text || !text[0]) return;

    Message m;
    if (g_settings.GetBool(kKeyTimestamps)) {
        time_t now = time(nullptr);
        struct tm t;
        localtime_s(&t, &now);

        char stamp[16];
        _snprintf_s(stamp, sizeof(stamp), _TRUNCATE, "[%02d:%02d] ", t.tm_hour, t.tm_min);
        m.text = stamp;
    }
    for (NameSpan& n : names) n.start += m.text.size();
    m.names = std::move(names);
    m.text += text;
    m.highlight = highlight;
    m.channel = channel;

    m.arrivedMs = GetTickCount();

    AppendChatLogLine(m.text.c_str());

    Guard g;
    g_backlog.push_back(std::move(m));
    const int backlogCap = g_settings.GetInt(kKeyBacklog);
    while ((int)g_backlog.size() > backlogCap) g_backlog.pop_front();
    g_scrollToBottomPending = true;
}

const uint8_t* Slot(int index) {
    return kMapMessageSlots.Get() + index * wc2r::kMessageSlotStride;
}
uint8_t* MutableSlot(int index) {
    return kMapMessageSlots.Get() + index * wc2r::kMessageSlotStride;
}

void FillGroupBackground(void* ctx, int alpha) {
    if (alpha <= 0 || !g_nkFillRect) return;
    void* canvas = nk::WindowCanvas(ctx);
    auto* panel = static_cast<const uint8_t*>(nk::CurrentPanel(ctx));
    if (!canvas || !panel) return;

    float b[4];
    memcpy(b, panel + nk::kPanelBoundsX, sizeof(b));
    if (!(b[2] > 0.0f && b[3] > 0.0f)) return;
    g_nkFillRect(canvas, b[0], b[1], b[2], b[3], 0.0f, Rgba(0, 0, 0, alpha));
}

void FlattenScrollbarTroughAndCursor(uint8_t* sb, uint32_t trough, uint32_t cursor) {

    const uint32_t colorType = 0;
    for (size_t o : nk::kScrollbarTrough) {
        memcpy(sb + o, &colorType, sizeof(colorType));
        memcpy(sb + o + 4, &trough, sizeof(trough));
    }
    for (size_t o : nk::kScrollbarCursor) {
        memcpy(sb + o, &colorType, sizeof(colorType));
        memcpy(sb + o + 4, &cursor, sizeof(cursor));
    }

    const float none = 0.0f;
    memcpy(sb + nk::kScrollbarBorder, &none, sizeof(none));
    memcpy(sb + nk::kScrollbarBorderCursor, &none, sizeof(none));
}

void DrawScrollIndicator(void* ctx, float stripW) {
    if (!g_nkFillRect) return;
    void* canvas = nk::WindowCanvas(ctx);
    void* panel = nk::CurrentPanel(ctx);
    if (!canvas || !panel) return;

    float bounds[4];
    memcpy(bounds, static_cast<const uint8_t*>(panel) + nk::kPanelBoundsX, sizeof(bounds));
    const float atY = nk::PanelFloat(panel, nk::kPanelAtY);
    const float rowH = nk::PanelFloat(panel, nk::kPanelRowHeight);
    const float offsetY = nk::ScrollOffsetY(panel);

    const float contentH = (atY + rowH) - bounds[1];
    const float viewH = bounds[3];
    if (contentH <= viewH + 0.5f) return;

    const float maxOffset = contentH - viewH;
    float thumbH = viewH * (viewH / contentH);
    if (thumbH < stripW) thumbH = stripW;
    if (thumbH > viewH) thumbH = viewH;

    float frac = maxOffset > 0.0f ? offsetY / maxOffset : 0.0f;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    const float thumbY = bounds[1] + frac * (viewH - thumbH);
    const float thumbX = bounds[0] + bounds[2] - stripW;

    g_nkFillRect(canvas, thumbX, thumbY, stripW, thumbH, stripW * 0.5f, kCursorColor);
}

int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

uint32_t ApplyColourFactor(uint32_t colour, float factor) {
    if (factor == 1.0f) return colour;
    const uint32_t r = static_cast<uint8_t>(static_cast<int>((colour & 0xff) * factor));
    const uint32_t g = static_cast<uint8_t>(static_cast<int>(((colour >> 8) & 0xff) * factor));
    const uint32_t b = static_cast<uint8_t>(static_cast<int>(((colour >> 16) & 0xff) * factor));
    return (colour & 0xff000000u) | (b << 16) | (g << 8) | r;
}

uint32_t NameColour(uint8_t colourIndex) {
    uint32_t recoloured;
    if (team_colours::GetOverride(colourIndex, &recoloured)) return recoloured;
    return colourIndex == team_colours::kBlack ? kBlackNameColor
                                               : team_colours::kColours[colourIndex];
}

struct LineLook {
    bool names = true;
    float alpha = 1.0f;
    uint32_t band = 0;
};

uint32_t Faded(uint32_t colour, float alpha) {
    if (alpha >= 1.0f) return colour;
    const uint32_t a = static_cast<uint32_t>(static_cast<float>(colour >> 24) * alpha);
    return (colour & 0x00ffffffu) | (a << 24);
}

void DrawNamedLine(void* ctx, const Message& m, uint32_t colour, const LineLook& look = {}) {

    if (!nk::CurrentPanel(ctx)) return;
    void* canvas = nk::WindowCanvas(ctx);
    const auto* c = reinterpret_cast<const uint8_t*>(ctx);
    const auto* font = *reinterpret_cast<const uint8_t* const*>(c + nk::kCtxFont);
    if (!canvas || !font) return;
    void* fontUserdata = *reinterpret_cast<void* const*>(font);
    const float fontH = *reinterpret_cast<const float*>(font + nk::kUserFontHeight);
    const auto width = *reinterpret_cast<const NkTextWidthFn*>(font + nk::kUserFontWidth);
    if (!width) return;

    float b[4];
    g_allocSpace(b, ctx);

    if ((look.band >> 24) != 0 && g_nkFillRect) {
        g_nkFillRect(canvas, b[0], b[1], b[2], b[3], 0.0f, look.band);
    }

    const float padX = nk::CtxFloat(ctx, nk::kCtxTextPaddingX);
    const float padY = nk::CtxFloat(ctx, nk::kCtxTextPaddingY);
    const float factor = nk::CtxFloat(ctx, nk::kCtxTextColourFactor);

    uint32_t style[7];
    const float zero = 0.0f;
    memcpy(&style[0], &zero, 4);
    memcpy(&style[1], &zero, 4);
    memcpy(&style[2], c + nk::kCtxWindowBackground, 4);
    memcpy(&style[4], c + nk::kCtxTextExtra0, 4);
    memcpy(&style[5], c + nk::kCtxTextExtra1, 4);
    memcpy(&style[6], c + nk::kCtxTextExtra2, 4);
    const uint32_t textFg = Faded(ApplyColourFactor(colour, factor), look.alpha);

    const float boxW = b[2] > padX * 2.0f ? b[2] : padX * 2.0f;
    const float boxH = b[3] > padY * 2.0f ? b[3] : padY * 2.0f;
    const float bottom = b[1] + (boxH - padY * 2.0f);
    const float lineX = b[0] + padX;
    const float lineW = boxW - padX * 2.0f;
    const float lineH = padY * 2.0f + fontH;
    float lineY = b[1] + padY;

    const char* str = m.text.c_str();
    const int len = static_cast<int>(m.text.size());
    const uint32_t* seps = kNkWrapSeparators.Get();

    int done = 0;
    auto piece = [&](int from, int to, uint32_t fg) {
        if (to <= from) return;
        const float dx = from > done ? width(fontUserdata, fontH, str + done, from - done) : 0.0f;
        memcpy(&style[3], &fg, 4);
        g_widgetText(canvas, lineX + dx, lineY, lineW - dx, lineH, str + from, to - from, style,
                     0x11, font);
    };

    int glyphs = 0;
    float fitW = 0.0f;
    int fitting = g_textClamp(font, str, len, lineW, &glyphs, &fitW, seps, 1);
    while (done < len && fitting != 0 && lineY + lineH <= bottom) {

        const int lineEnd = done + fitting;
        int at = done;
        for (const NameSpan& n : m.names) {
            if (!look.names) break;
            const int from = ClampInt(static_cast<int>(n.start), done, lineEnd);
            const int to = ClampInt(static_cast<int>(n.start + n.len), done, lineEnd);
            if (to <= from) continue;
            piece(at, from, textFg);
            piece(from, to, Faded(ApplyColourFactor(NameColour(n.colourIndex), factor), look.alpha));
            at = to;
        }
        piece(at, lineEnd, textFg);
        done = lineEnd;
        lineY += padY * 2.0f + fontH;
        fitting = g_textClamp(font, str + done, len - done, lineW, &glyphs, &fitW, seps, 1);
    }
}

uint8_t NameColourIndexOf(uint8_t who) {
    const uint8_t index = kTeamColourReorder.Get()[who];
    return index < team_colours::kCount ? index : static_cast<uint8_t>(team_colours::kCount);
}

std::string PlayerName(uint8_t who) {
    if (who >= team_colours::kCount) return {};
    const char* name = kPlayerNames.Get() + who * wc2r::kPlayerNameStride;
    const size_t n = strnlen(name, wc2r::kPlayerNameStride);
    return n < wc2r::kPlayerNameStride ? std::string(name, n) : std::string();
}

bool IsNameByte(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' ||
           c >= 0x80;
}

std::vector<NameSpan> FindPlayerNames(const std::string& s) {
    struct Candidate {
        std::string name;
        uint8_t colourIndex;
    };
    std::vector<Candidate> humans;
    const uint8_t* owner = kPlayerOwner.Get();
    for (uint8_t i = 0; i < team_colours::kCount; ++i) {
        std::string name = PlayerName(i);
        const uint8_t colour = NameColourIndexOf(i);
        if (owner[i] == 0 && name.size() >= 2 && colour < team_colours::kCount) {
            humans.push_back({std::move(name), colour});
        }
    }
    std::vector<Candidate> candidates;
    for (const Candidate& c : humans) {
        int sharing = 0;
        for (const Candidate& o : humans) sharing += o.name == c.name;
        if (sharing == 1) candidates.push_back(c);
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.name.size() > b.name.size(); });

    std::vector<NameSpan> spans;
    for (const Candidate& c : candidates) {
        for (size_t at = s.find(c.name); at != std::string::npos; at = s.find(c.name, at + 1)) {
            const size_t end = at + c.name.size();
            if (at > 0 && IsNameByte(static_cast<unsigned char>(s[at - 1]))) continue;
            if (end < s.size() && IsNameByte(static_cast<unsigned char>(s[end]))) continue;
            bool overlaps = false;
            for (const NameSpan& o : spans) overlaps |= at < o.start + o.len && o.start < end;
            if (!overlaps) spans.push_back({at, c.name.size(), c.colourIndex});
        }
    }
    std::sort(spans.begin(), spans.end(),
              [](const NameSpan& a, const NameSpan& b) { return a.start < b.start; });
    return spans;
}

void SquareWhisperBrackets(std::string& line, const std::vector<NameSpan>& names) {
    if (line.empty() || line[0] != '<') return;
    for (const NameSpan& n : names) {
        const size_t close = n.start + n.len;
        if (close < line.size() && line[close] == '>' && line.find('>') == close) {
            line[0] = '[';
            line[close] = ']';
            return;
        }
    }
}

uint8_t AlliedMask() {
    const uint8_t me = *kLocalPlayer.Get();
    if (me >= team_colours::kCount) return 0;
    const uint8_t* row = kDiplomacyStance.Get() + me * wc2r::kDiplomacyRowStride;
    uint8_t mask = 0;
    for (uint8_t i = 0; i < team_colours::kCount; ++i) {
        if (i != me && row[i] != 0 && lobby_teams::IsInMatch(i)) {
            mask |= static_cast<uint8_t>(1u << i);
        }
    }
    return mask;
}

uint8_t g_teamMask = 0;

void DrawBacklogRows(void* ctx, float w, float lineHeight) {
    bool needScroll;
    {
        Guard g;
        const bool nameColours = g_settings.GetBool(kKeyNameColours);
        for (const Message& m : g_backlog) {
            int n = g_countWrappedLines(ctx, m.text.c_str(), (int)m.text.size(), w);
            if (n < 1) n = 1;
            g_layoutRowDynamic(ctx, (float)n * lineHeight, 1);
            const uint32_t colour =
                m.highlight ? kHighlightColor : kChannelColours[static_cast<int>(m.channel)];
            if (nameColours && !m.names.empty()) {
                DrawNamedLine(ctx, m, colour);
            } else {
                g_drawWrappedLabel(ctx, m.text.c_str(), colour);
            }
        }

        g_layoutRowDynamic(ctx, 0.01f, 1);

        needScroll = g_scrollToBottomPending;
        g_scrollToBottomPending = false;
    }
    if (needScroll) g_scrollGroupTo(ctx, "messages_history", 0, kScrollToBottomY);
}

Channel g_promptChannel = Channel::System;

int DrawLineInColour(void* ctx, const char* text, float x, float y, float w, float h, uint32_t colour,
                     int used, int lineHeight) {
    const int n = g_countWrappedLines(ctx, text, static_cast<int>(strlen(text)), w);
    const int total = used + (n < 1 ? 1 : n);
    if (total >= 0x12) return 0x11;
    g_overrideNextRect(ctx, x, (y + h) - static_cast<float>(total * lineHeight), w,
                       static_cast<float>(n) * static_cast<float>(lineHeight));
    g_drawWrappedLabel(ctx, text, colour);
    return total;
}

bool AbsoluteGroupRegion(void* ctx, float* w, float* h) {
    const uint64_t packed = g_contentRegionSize(ctx);
    float rw, rh;
    {
        const uint32_t lo = (uint32_t)(packed & 0xffffffffu);
        const uint32_t hi = (uint32_t)(packed >> 32);
        memcpy(&rw, &lo, 4);
        memcpy(&rh, &hi, 4);
    }
    float padding;
    memcpy(&padding, reinterpret_cast<const uint8_t*>(ctx) + nk::kCtxGroupPaddingY, 4);
    *w = rw;
    *h = rh - padding * 2.0f;
    return *w > 8.0f && *w < 20000.0f && *h > 0.0f && *h < 20000.0f;
}

constexpr DWORD kFadeOutMs = 1000;

float Visibility(const Message& m, DWORD now, DWORD holdMs) {
    const DWORD age = now - m.arrivedMs;
    if (age < holdMs) return 1.0f;
    if (age >= holdMs + kFadeOutMs) return 0.0f;
    return 1.0f - static_cast<float>(age - holdMs) / static_cast<float>(kFadeOutMs);
}

bool Revealed() {
    return g_settings.GetInt(kKeyFade) <= 0 || *kChatTargetMode.Get() != 0 ||
           (*kMatchPauseFlags.Get() & 1) != 0;
}

void DrawRecentLines(void* ctx, int lineHeight) {
    float w, h;
    if (!AbsoluteGroupRegion(ctx, &w, &h)) return;
    const DWORD now = GetTickCount();
    const DWORD holdMs = static_cast<DWORD>(g_settings.GetInt(kKeyFade)) * 1000u;
    const bool nameColours = g_settings.GetBool(kKeyNameColours);
    const int bgAlpha = g_settings.GetInt(kKeyBgAlpha);
    const int maxLines = static_cast<int>(h) / lineHeight;

    Guard g;
    int used = 0;
    for (auto it = g_backlog.rbegin(); it != g_backlog.rend(); ++it) {
        const float alpha = Visibility(*it, now, holdMs);
        if (alpha <= 0.0f) break;
        int n = g_countWrappedLines(ctx, it->text.c_str(), static_cast<int>(it->text.size()), w);
        if (n < 1) n = 1;
        if (used + n > maxLines) break;
        used += n;
        g_overrideNextRect(ctx, 0.0f, h - static_cast<float>(used * lineHeight), w,
                           static_cast<float>(n * lineHeight));
        LineLook look;
        look.names = nameColours;
        look.alpha = alpha;
        look.band = Rgba(0, 0, 0, static_cast<int>(static_cast<float>(bgAlpha) * alpha));
        DrawNamedLine(ctx, *it,
                      it->highlight ? kHighlightColor : kChannelColours[static_cast<int>(it->channel)],
                      look);
    }
}

void __cdecl HookedDrawMapMessageList(void* ctx, int lineHeight) {
    if (!g_settings.GetBool(kKeyEnabled) || !ctx || lineHeight <= 0) {
        g_realDrawMapMessageList(ctx, lineHeight);
        return;
    }

    float w, h;
    if (!AbsoluteGroupRegion(ctx, &w, &h)) {
        g_realDrawMapMessageList(ctx, lineHeight);
        return;
    }

    const char* prompt = reinterpret_cast<const char*>(Slot(kSlotPrompt));
    int used = g_promptChannel != Channel::System
                   ? DrawLineInColour(ctx, prompt, 0.0f, 0.0f, w, h,
                                      kChannelColours[static_cast<int>(g_promptChannel)], 0,
                                      lineHeight)
                   : g_drawMapMessageLine(ctx, prompt, 0.0f, 0.0f, w, h,
                                          Slot(kSlotPrompt)[wc2r::kMessageSlotHighlight], 0,
                                          lineHeight);
    g_drawMapMessageLine(ctx, reinterpret_cast<const char*>(Slot(kSlotSticky)), 0.0f, 0.0f, w, h,
                         Slot(kSlotSticky)[wc2r::kMessageSlotHighlight], used, lineHeight);
}

bool g_inPauseHandler = false;

void __cdecl HookedWriteStickyMessage(const char* text, unsigned durationMs) {
    if (g_inPauseHandler && g_settings.GetBool(kKeyEnabled)) return;
    g_realWriteStickyMessage(text, durationMs);
}

size_t FindSenderName(const std::string& line, const std::string& name) {
    for (size_t at = line.find(name); at != std::string::npos; at = line.find(name, at + 1)) {
        const size_t end = at + name.size();
        if (at > 0 && IsNameByte(static_cast<unsigned char>(line[at - 1]))) continue;
        if (line.compare(end, 2, ": ") == 0) return at;
    }
    return std::string::npos;
}

void __cdecl HookedPushMapMessage(const char* text, int highlight, unsigned durationMs) {
    if (!text) {
        g_realPushMapMessage(text, highlight, durationMs);
        return;
    }

    const std::string name = g_sender.active ? PlayerName(g_sender.who) : std::string();
    if (!name.empty() && !ChannelsOn()) {

        std::string line = text;
        std::vector<NameSpan> names;
        const uint8_t colour = NameColourIndexOf(g_sender.who);
        const size_t at = FindSenderName(line, name);
        if (at != std::string::npos && colour < team_colours::kCount) {
            names.push_back({at, name.size(), colour});
        }
        CaptureMessage(line.c_str(), highlight, Channel::All, std::move(names));
    } else if (!name.empty() && g_sender.raw) {
        const Channel channel = ChannelOf(static_cast<uint8_t>(g_sender.targetMode));
        std::string line = kChannelLabels[static_cast<int>(channel)];
        line += ' ';
        std::vector<NameSpan> names;
        const uint8_t colour = NameColourIndexOf(g_sender.who);
        if (colour < team_colours::kCount) names.push_back({line.size(), name.size(), colour});
        line += name;
        line += ": ";
        line += g_sender.raw;
        CaptureMessage(line.c_str(), highlight, channel, std::move(names));
    } else {
        std::string line = text;
        std::vector<NameSpan> names = FindPlayerNames(line);
        SquareWhisperBrackets(line, names);
        CaptureMessage(line.c_str(), highlight, Channel::System, std::move(names));
    }

    g_realPushMapMessage(text, highlight, durationMs);
}

void __cdecl HookedDisplayChatOrGameMessage(const char* text, uint8_t who, unsigned durationMs,
                                            char targetMode) {
    const Sender saved = g_sender;
    g_sender.active = who < team_colours::kCount;
    g_sender.who = who;
    g_sender.raw = text;
    g_sender.targetMode = targetMode;
    g_realDisplayChatOrGameMessage(text, who, durationMs, targetMode);
    g_sender = saved;
}

Channel g_compose = Channel::System;

constexpr uint8_t kScanEnter = 0x1c;
constexpr uint8_t kScanTab = 0x0f;

bool Composing() {
    return ChannelsOn() && *kChatTargetMode.Get() != 0 && g_compose != Channel::System;
}

void ComposeOn(Channel c) {
    g_compose = c;
    *kChatTargetMode.Get() = ComposeModeOf(c);
}

void OnComposeOpened(uint8_t modifiers) {
    const uint8_t mode = *kChatTargetMode.Get();
    if (mode == 0 || mode == 1) {
        g_compose = Channel::System;
        return;
    }
    if (modifiers & wc2r::kModShift) {
        ComposeOn(Channel::All);
    } else if (modifiers & wc2r::kModCtrl) {

        ComposeOn(AlliedMask() ? Channel::Allies : Channel::All);
    } else {

        ComposeOn(g_teamMask ? Channel::Team : Channel::All);
    }
}

void CycleComposeChannel() {
    static const Channel kOrder[] = {Channel::Team, Channel::All, Channel::Allies};
    int at = 0;
    while (kOrder[at] != g_compose && at < 2) ++at;
    for (int step = 1; step <= 3; ++step) {
        const Channel next = kOrder[(at + step) % 3];
        if (next == Channel::Team && !g_teamMask) continue;
        if (next == Channel::Allies && !AlliedMask()) continue;
        ComposeOn(next);
        break;
    }
    kLog.Info("compose channel -> %s", kChannelLabels[static_cast<int>(g_compose)]);
}

unsigned __cdecl HookedChatTextboxEventProc(void* dlg, uint8_t* evt) {
    if (!g_settings.GetBool(kKeyEnabled) || !evt) return g_realChatTextboxEventProc(dlg, evt);

    uint16_t number;
    memcpy(&number, evt + wc2r::kEventNumber, sizeof(number));
    const uint8_t scan = evt[wc2r::kEventScan];

    if (Composing() && scan == kScanTab && number == 1) return 1;

    const bool opening = *kChatTargetMode.Get() == 0 && number == 0 && scan == kScanEnter;
    const unsigned handled = g_realChatTextboxEventProc(dlg, evt);
    if (opening && ChannelsOn()) OnComposeOpened(evt[wc2r::kEventModifiers]);
    return handled;
}

unsigned __cdecl HookedMinimapTerrainKeyProc(void* dlg, uint8_t* evt) {
    if (g_settings.GetBool(kKeyEnabled) && evt && Composing() && evt[wc2r::kEventScan] == kScanTab) {
        uint16_t number;
        memcpy(&number, evt + wc2r::kEventNumber, sizeof(number));
        if (number == 0) {
            CycleComposeChannel();
            return 1;
        }
    }
    return g_realMinimapTerrainKeyProc(dlg, evt);
}

void CloseComposeOnClick(void* ctx) {
    if (!g_settings.GetBool(kKeyClickUnfocus) || *kChatTargetMode.Get() == 0) return;
    const bool left = nk::MousePressed(ctx, nk::kMouseButtonLeft);
    if (!left && !nk::MousePressed(ctx, nk::kMouseButtonRight)) return;
    void* dlg = *kChatDialog.Get();
    if (!dlg) return;
    uint8_t unread[16] = {};
    kToggleChatCompose.Get()(dlg, unread, 0);
    kLog.Info("compose closed by a %s click", left ? "left" : "right");
}

void LocalNotice(const char* text) {
    CaptureMessage(text, 1, Channel::System);
}

void __cdecl HookedSendComposedChat(const char* text) {
    if (!g_settings.GetBool(kKeyEnabled) || !Composing()) {
        g_realSendComposedChat(text);
        return;
    }
    uint8_t* mode = kChatTargetMode.Get();
    if (g_compose == Channel::Allies) {

        if (!AlliedMask()) {
            LocalNotice("You have no allies and your message was not sent.");
            return;
        }
        *mode = 3;
        g_realSendComposedChat(text);
        return;
    }
    if (g_compose == Channel::Team && g_teamMask) {

        uint8_t* mask = kChatRecipientMask.Get();
        const uint8_t saved = *mask;
        *mask = g_teamMask;
        *mode = kTeamTargetMode;
        g_realSendComposedChat(text);
        *mask = saved;
        return;
    }

    *mode = 2;
    g_realSendComposedChat(text);
}

void __cdecl HookedWriteChatPromptLine(const char* format, const char* arg1, const char* arg2) {
    if (*kChatTargetMode.Get() == 0) g_compose = Channel::System;

    if (!g_settings.GetBool(kKeyEnabled) || !format || !Composing() ||
        *kChatTargetMode.Get() == 5) {
        g_promptChannel = Channel::System;
        g_realWriteChatPromptLine(format, arg1, arg2);
        return;
    }
    std::string line = kChannelLabels[static_cast<int>(g_compose)];
    line += ' ';
    if (arg1) line += arg1;
    g_promptChannel = g_compose;

    g_realWriteChatPromptLine("%s", line.c_str(), nullptr);
}

void AnnouncePause(bool paused) {
    const uint8_t who = *kCommandIssuer.Get();
    const std::string name = PlayerName(who);
    if (name.empty()) return;
    std::vector<NameSpan> names;
    const uint8_t colour = NameColourIndexOf(who);
    if (colour < team_colours::kCount) names.push_back({0, name.size(), colour});

    std::string line = name;
    int left = -1;
    if (!paused) {
        line += " resumed the game.";
    } else {
        line += " paused the game.";
        if (*kNetworkedMatch.Get()) {
            left = kPausesRemaining.Get()[who];
            char count[40];
            _snprintf_s(count, sizeof(count), _TRUNCATE, " (%d pause%s remaining)", left,
                        left == 1 ? "" : "s");
            line += count;
        }
    }
    kLog.Info("%s by player %u (local %u)%s", paused ? "pause" : "resume",
              static_cast<unsigned>(who), static_cast<unsigned>(*kLocalPlayer.Get()),
              left >= 0 ? (", " + std::to_string(left) + " left").c_str() : "");
    CaptureMessage(line.c_str(), 1, Channel::System, std::move(names));
}

void __cdecl HookedHandlePause() {
    const bool before = (*kMatchPauseFlags.Get() & 1) != 0;
    g_inPauseHandler = true;
    g_realHandlePause();
    g_inPauseHandler = false;
    if (g_settings.GetBool(kKeyEnabled) && !before && (*kMatchPauseFlags.Get() & 1)) {
        AnnouncePause(true);
    }
}

void __cdecl HookedHandleResume() {
    const bool before = (*kMatchPauseFlags.Get() & 1) != 0;
    g_inPauseHandler = true;
    g_realHandleResume();
    g_inPauseHandler = false;
    if (g_settings.GetBool(kKeyEnabled) && before && !(*kMatchPauseFlags.Get() & 1)) {
        AnnouncePause(false);
    }
}

void OnMatchStart() {
    {
        Guard g;
        g_backlog.clear();
        g_scrollToBottomPending = false;
    }
    g_teamMask = lobby_teams::TeammatesAtMatchStart(*kLocalPlayer.Get());
    g_compose = Channel::System;
    g_promptChannel = Channel::System;
    kLog.Info("match start: backlog cleared; player %u, teammates mask 0x%02x",
              static_cast<unsigned>(*kLocalPlayer.Get()), static_cast<unsigned>(g_teamMask));
    if (g_chatLog) fprintf(g_chatLog, "\n=== new match ===\n");
}

void __cdecl HookedBuildMapMessagesPanel(void* ctx, float x, float y, float w, float h,
                                         int lineHeight) {

    g_settings.ReloadIfChanged();

    if (!g_settings.GetBool(kKeyEnabled) || !ctx || lineHeight <= 0) {
        g_realBuildMapMessagesPanel(ctx, x, y, w, h, lineHeight);
        return;
    }

    CloseComposeOnClick(ctx);

    unsigned now = g_getClockMs();
    for (int i = 0; i < 15; ++i) g_expireSlot(MutableSlot(i), now);
    g_expireSlot(MutableSlot(kSlotSticky), now);

    float stripH = 2.0f * (float)lineHeight;
    if (stripH > h) stripH = h;

    const char* promptText = reinterpret_cast<const char*>(Slot(kSlotPrompt));
    const char* stickyText = reinterpret_cast<const char*>(Slot(kSlotSticky));
    const bool stripHasContent = promptText[0] != '\0' || stickyText[0] != '\0';

    float historyH = (float)g_settings.GetInt(kKeyLines) * (float)lineHeight;
    const float historyMax = h - stripH;
    if (historyH > historyMax) historyH = historyMax;
    if (historyH < 0.0f) historyH = 0.0f;

    const float panelH = historyH + stripH;
    const float top = kAnchorBottom ? (y + h - panelH) : y;

    uint8_t savedScrollStyle[kScrollStyleSpan];
    auto* scrollStyleField = reinterpret_cast<uint8_t*>(ctx) + nk::kCtxScrollH;
    memcpy(savedScrollStyle, scrollStyleField, kScrollStyleSpan);
    FlattenScrollbarTroughAndCursor(scrollStyleField + kScrollvOffset, 0  ,
                                    0  );
    const int noButtons = 0;
    memcpy(scrollStyleField + kScrollvOffset + nk::kScrollbarShowButtons, &noButtons,
           sizeof(noButtons));

    float savedScrollbar[2];
    auto* scrollbarField = reinterpret_cast<uint8_t*>(ctx) + nk::kCtxScrollbarSize;
    memcpy(savedScrollbar, scrollbarField, sizeof(savedScrollbar));

    const float scrollbarPair[2] = {kIndicatorWidth, 0.0f};
    memcpy(scrollbarField, scrollbarPair, sizeof(scrollbarPair));

    static bool wasRevealed = true;
    const bool revealed = Revealed();
    if (revealed && !wasRevealed) {
        Guard g;
        g_scrollToBottomPending = true;
    }
    wasRevealed = revealed;

    if (!revealed && historyH > (float)lineHeight * 0.5f) {
        g_overrideNextRect(ctx, x, top, w, historyH);
        if (g_beginNamedGroup(ctx, "messages_recent", 0x20)) {
            g_layoutSpaceBegin(ctx, 1, 0, 1000);
            DrawRecentLines(ctx, lineHeight);
            g_layoutSpaceEnd(ctx);
            g_groupEnd(ctx);
        }
    }

    if (revealed && historyH > (float)lineHeight * 0.5f) {
        g_overrideNextRect(ctx, x, top, w, historyH);
        if (g_beginNamedGroup(ctx, "messages_history", 0)) {
            FillGroupBackground(ctx, g_settings.GetInt(kKeyBgAlpha));
            DrawBacklogRows(ctx, w, (float)lineHeight);

            DrawScrollIndicator(ctx, kIndicatorWidth);
            g_groupEnd(ctx);
        }
    }

    if (stripHasContent) {
        g_overrideNextRect(ctx, x, top + historyH, w, stripH);
        if (g_beginNamedGroup(ctx, "messages", 0x20)) {
            g_layoutSpaceBegin(ctx, 1, 0, 1000);
            FillGroupBackground(ctx, g_settings.GetInt(kKeyBgAlpha));
            HookedDrawMapMessageList(ctx, lineHeight);
            g_layoutSpaceEnd(ctx);
            g_groupEnd(ctx);
        }
    }

    memcpy(scrollStyleField, savedScrollStyle, kScrollStyleSpan);
    memcpy(scrollbarField, savedScrollbar, sizeof(savedScrollbar));
}

class ChatMod : public IMod {
public:
    const char* Name() const override { return "chat"; }

    bool Install() override {
        RegisterChatSettings();
        g_settings.SetChangeHandler(&SyncChatLog);

        InitializeCriticalSection(&g_lock);
        g_lockReady = true;

        g_drawMapMessageLine = kDrawMapMessageLine.Get();
        g_countWrappedLines = kCountWrappedLines.Get();
        g_overrideNextRect = kOverrideNextRect.Get();
        g_drawWrappedLabel = kDrawWrappedLabel.Get();
        g_contentRegionSize = kContentRegionSize.Get();
        g_beginNamedGroup = kBeginNamedGroup.Get();
        g_layoutRowDynamic = kLayoutRowDynamic.Get();
        g_scrollGroupTo = kScrollGroupTo.Get();
        g_layoutSpaceBegin = kLayoutSpaceBegin.Get();
        g_layoutSpaceEnd = kLayoutSpaceEnd.Get();
        g_groupEnd = kEndGroup.Get();
        g_expireSlot = kExpireSlot.Get();
        g_getClockMs = kGetClockMs.Get();
        g_nkFillRect = kNkFillRect.Get();
        g_allocSpace = kNkPanelAllocSpace.Get();
        g_textClamp = kNkTextClamp.Get();
        g_widgetText = kNkWidgetText.Get();

        g_settings.ReloadIfChanged();

        InstallHook(kPushMapMessage.Target(), reinterpret_cast<void*>(&HookedPushMapMessage),
                    reinterpret_cast<void**>(&g_realPushMapMessage), "PushMapMessage");
        InstallHook(kDrawMapMessageList.Target(),
                    reinterpret_cast<void*>(&HookedDrawMapMessageList),
                    reinterpret_cast<void**>(&g_realDrawMapMessageList), "DrawMapMessageList");
        match_start::Register(&OnMatchStart);
        InstallHook(kWriteStickyMessage.Target(),
                    reinterpret_cast<void*>(&HookedWriteStickyMessage),
                    reinterpret_cast<void**>(&g_realWriteStickyMessage), "WriteStickyMapMessage");
        InstallHook(kBuildMapMessagesPanel.Target(),
                    reinterpret_cast<void*>(&HookedBuildMapMessagesPanel),
                    reinterpret_cast<void**>(&g_realBuildMapMessagesPanel),
                    "BuildMapMessagesPanel");
        InstallHook(kDisplayChatOrGameMessage.Target(),
                    reinterpret_cast<void*>(&HookedDisplayChatOrGameMessage),
                    reinterpret_cast<void**>(&g_realDisplayChatOrGameMessage),
                    "DisplayChatOrGameMessage");
        InstallHook(kChatTextboxEventProc.Target(),
                    reinterpret_cast<void*>(&HookedChatTextboxEventProc),
                    reinterpret_cast<void**>(&g_realChatTextboxEventProc), "ChatTextboxEventProc");
        InstallHook(kMinimapTerrainKeyProc.Target(),
                    reinterpret_cast<void*>(&HookedMinimapTerrainKeyProc),
                    reinterpret_cast<void**>(&g_realMinimapTerrainKeyProc), "MinimapTerrainKeyProc");
        InstallHook(kSendComposedChat.Target(), reinterpret_cast<void*>(&HookedSendComposedChat),
                    reinterpret_cast<void**>(&g_realSendComposedChat),
                    "SendComposedChatToRecipients");
        InstallHook(kWriteChatPromptLine.Target(),
                    reinterpret_cast<void*>(&HookedWriteChatPromptLine),
                    reinterpret_cast<void**>(&g_realWriteChatPromptLine), "WriteChatPromptLine");
        InstallHook(kHandlePauseCommand.Target(), reinterpret_cast<void*>(&HookedHandlePause),
                    reinterpret_cast<void**>(&g_realHandlePause), "HandlePauseGameCommand");
        InstallHook(kHandleResumeCommand.Target(), reinterpret_cast<void*>(&HookedHandleResume),
                    reinterpret_cast<void**>(&g_realHandleResume), "HandleResumeGameCommand");
        return true;
    }
};

}  // namespace

MOD_REGISTER(ChatMod)
