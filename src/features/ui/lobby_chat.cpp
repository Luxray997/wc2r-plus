// SPDX-License-Identifier: MIT
#include "features/ui/lobby_chat.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/log.h"
#include "features/lobby_policy.h"
#include "features/map_download.h"
#include "target/struct_offsets.h"

namespace ui {
namespace lobby_chat {
namespace {

using namespace game;

const Logger kLog{"mplobby"};

Binding b;

constexpr float kInsetX = 60.0f;
constexpr float kInsetTop = 20.0f;
constexpr float kInsetBottom = -20.0f;

constexpr unsigned int kNoticeColour = Rgba(232, 196, 96, 255);
constexpr int kSlotCount = 8;
constexpr DWORD kLobbyGapMs = 2000;
constexpr int kNkKeyEnter = 4;

float g_scroll = 0.0f;

float g_maxOffset = 0.0f;

float g_contentH = 0.0f;

float g_wrapW = 0.0f;
unsigned int g_lastSeq = 0;
bool g_seqValid = false;
bool g_wipeSeen = false;

unsigned int ReadSeq(void* ctx) {
    if (!ctx) return 0;
    return *reinterpret_cast<const unsigned int*>(static_cast<const uint8_t*>(ctx) + nk::kCtxSeq);
}

bool ScrollWasWiped(void* ctx) {
    const unsigned int seq = ReadSeq(ctx);
    const bool wiped = g_seqValid && (seq - g_lastSeq) > 1u;
    g_lastSeq = seq;
    g_seqValid = true;
    return wiped;
}

struct LocalNotice {
    std::string text;
    size_t anchor = 0;
};
std::vector<LocalNotice> g_notices;

std::string g_recentName[kSlotCount];
DWORD g_recentNameMs[kSlotCount] = {};
constexpr DWORD kRecentNameMs = 5000;

DWORD g_lastFrameMs = 0;

size_t ChatLineCount() {
    if (!b.begin || !b.end || !*b.begin || *b.end <= *b.begin) return 0;
    return static_cast<size_t>(*b.end - *b.begin) / msvc::kStrStride;
}

const char* NoticeName(int slot, DWORD now) {
    if (g_recentNameMs[slot] != 0 && now - g_recentNameMs[slot] < kRecentNameMs &&
        !g_recentName[slot].empty()) {
        return g_recentName[slot].c_str();
    }
    return "A player";
}

void CollectDownloadNotices() {
    const DWORD now = GetTickCount();
    map_download::DownloadEvent e;
    while (map_download::NextDownloadEvent(&e)) {
        if (e.lobbySlot < 0 || e.lobbySlot >= kSlotCount) continue;
        const char* who = NoticeName(e.lobbySlot, now);
        using Why = map_download::DownloadEvent::Why;
        const char* fmt = "%s disconnected while downloading map";
        switch (e.kind) {
            case map_download::DownloadEvent::Kind::Declined:
                fmt = "%s declined map download and was disconnected";
                break;
            case map_download::DownloadEvent::Kind::Stalled:
                break;
            case map_download::DownloadEvent::Kind::NoAnswer:
                fmt = "%s did not respond to map download and was disconnected";
                break;
            case map_download::DownloadEvent::Kind::LeftNoMap:

                fmt = e.why == Why::HostCannotSend
                          ? "%s does not have the map, and map sharing is disabled"
                      : e.why == Why::DownloadsOff
                          ? "%s does not have the map, and has map downloading disabled"
                      : e.why == Why::BadName
                          ? "%s could not download the map: its name cannot be requested"
                      : e.why == Why::DownloadFailed
                          ? "%s could not download the map: the download failed"
                          : "%s does not have the map";
                break;
        }
        char line[256];
        _snprintf_s(line, sizeof(line), _TRUNCATE, fmt, who);
        g_notices.push_back(LocalNotice{line, ChatLineCount()});

        if (g_notices.size() > 32) g_notices.erase(g_notices.begin());
    }
}

void CollectLockNotices() {
    lobby_policy::LockEvent e;
    while (lobby_policy::NextLockEvent(&e)) {
        const char* line = e.teams ? (e.locked ? "Teams locked by host"
                                               : "Teams unlocked by host")
                                   : (e.locked ? "Slots locked by host"
                                               : "Slots unlocked by host");
        g_notices.push_back(LocalNotice{line, ChatLineCount()});
        if (g_notices.size() > 32) g_notices.erase(g_notices.begin());
    }
}

}  // namespace

void Bind(const Binding& binding) { b = binding; }

float FontLineHeight(void* ctx) {
    if (!ctx) return kCaptionH;
    const uint8_t* const* fontPtr =
        reinterpret_cast<const uint8_t* const*>(static_cast<const uint8_t*>(ctx) + nk::kCtxFont);
    if (!*fontPtr) return kCaptionH;
    return *reinterpret_cast<const float*>(*fontPtr + nk::kUserFontHeight);
}

void OnFrame(DWORD now) {

    if (g_lastFrameMs == 0 || now - g_lastFrameMs > kLobbyGapMs) {
        g_notices.clear();
        for (auto& n : g_recentName) n.clear();
        memset(g_recentNameMs, 0, sizeof(g_recentNameMs));
    }
    if (b.seatedName) {
        for (int i = 0; i < kSlotCount; ++i) {
            const char* n = b.seatedName(i);
            if (!n || !n[0]) continue;
            g_recentName[i] = n;
            g_recentNameMs[i] = now ? now : 1;
        }
    }
    g_lastFrameMs = now;
    CollectDownloadNotices();
    CollectLockNotices();
}

void DrawHistory(void* ctx, const Rect& r) {

    PanelFrame(ctx, r, "chat_frame", b.frameSkin);

    const Rect inner{Fl(r.x + kInsetX), Fl(r.y + kInsetTop), Fl(r.w - kInsetX * 2.0f),
                     Fl(r.h - kInsetTop - kInsetBottom)};
    const bool wiped = ScrollWasWiped(ctx);
    if (!BeginList(ctx, inner, "chat_history")) return;

    void* panel = CurrentPanel(ctx);
    if (wiped) {

        SetScrollOffsetY(panel, g_scroll);
    } else {

        g_scroll = ScrollOffsetY(panel);
    }

    const unsigned int prevText = b.textSkin ? PushSkin(*b.textSkin) : 0;
    const float lineH = FontLineHeight(ctx);

    const float contentTop = NextRowTop(ctx);

    const float wrapW = (g_wrapW > 0.0f && g_wrapW <= inner.w) ? g_wrapW : inner.w;
    bool measured = false;
    auto row = [&](const char* text, int len, unsigned int colour) {
        const int lines = b.countLines(ctx, text, len, wrapW);
        b.rowDynamic(ctx, static_cast<float>(lines > 0 ? lines : 1) * lineH, 1);
        b.wrappedLabel(ctx, text, colour);
        if (!measured) {
            measured = true;
            Rect cell;
            if (LastRowFirstCell(ctx, &cell)) g_wrapW = cell.w;
        }
    };

    size_t notice = 0;
    auto noticesUpTo = [&](size_t chatLines) {
        for (; notice < g_notices.size() && g_notices[notice].anchor <= chatLines; ++notice) {
            const std::string& t = g_notices[notice].text;
            row(t.c_str(), static_cast<int>(t.size()), kNoticeColour);
        }
    };
    const uint8_t* begin = b.begin ? *b.begin : nullptr;
    const uint8_t* end = b.end ? *b.end : nullptr;
    size_t index = 0;
    if (begin && end && end > begin) {
        for (const uint8_t* s = begin; s + msvc::kStrStride <= end;
             s += msvc::kStrStride, ++index) {
            noticesUpTo(index);
            const char* text = StrData(s);
            const int len = *reinterpret_cast<const int*>(s + msvc::kStrSize);
            if (!text[0] || len <= 0) continue;
            row(text, len, ReadCtxColour(ctx, nk::kCtxTextColour));
        }
    }
    noticesUpTo(static_cast<size_t>(-1));
    const float contentH = NextRowTop(ctx) - contentTop;

    const float maxOffset = (contentH > inner.h) ? (contentH - inner.h) : 0.0f;

    const bool atBottom = g_scroll >= g_maxOffset - lineH * 0.5f;
    g_maxOffset = maxOffset;

    const float grew = (g_contentH > 0.0f) ? (contentH - g_contentH) : 0.0f;
    g_contentH = contentH;

    if (atBottom && grew > 0.0f) {
        g_scroll += grew;
        SetScrollOffsetY(panel, g_scroll);
    }
    if (b.scrollRequest) *b.scrollRequest = 0;

    if (wiped && !g_wipeSeen) {
        g_wipeSeen = true;
        kLog.Info("mid-frame nk_clear seen -- chat scroll restored to %.0f", g_scroll);
    }

    if (b.textSkin) PopSkin(prevText);
    EndList(ctx);
}

void DrawInput(void* ctx, const Rect& r) {
    if (!b.input) return;

    char* buffer = const_cast<char*>(StrData(b.input));
    const int size = *reinterpret_cast<const int*>(static_cast<uint8_t*>(b.input) + msvc::kStrSize);
    if (!buffer || size <= 1) return;

    const float sendW = Fl((std::min)(360.0f, r.w * 0.22f));
    const float editW = Fl(r.w - sendW - kGap);

    TextEditAt(ctx, Rect{r.x, r.y, editW, r.h}, buffer, size - 1, b.theme);

    bool send = ButtonAt(ctx, Rect{r.x + editW + kGap, r.y, sendW, r.h}, Localized("mp_chat_send"));
    if (!send && b.keyPressed) send = b.keyPressed(ctx, kNkKeyEnter) != 0;
    if (send && b.sendChat) b.sendChat();
}

}  // namespace lobby_chat
}  // namespace ui
