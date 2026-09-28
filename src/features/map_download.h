// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string>

namespace map_download {

enum class SlotDownload : uint8_t {
    None = 0,
    Deciding,
    Downloading,
    Done,
    NoMap,
};

struct SlotDownloadStatus {
    SlotDownload kind = SlotDownload::None;
    uint8_t percent = 0;
};

SlotDownloadStatus LobbySlotDownloadStatus(int lobbySlot);

bool LobbyMapMissing();

struct DownloadPrompt {
    enum class Kind : uint8_t {
        None,
        Waiting,
        Asking,
        Downloading,

    };
    Kind kind = Kind::None;
    std::string mapName;
    uint32_t sizeBytes = 0;
    uint8_t percent = 0;
    uint32_t msLeft = 0;
    bool clickable = false;
    uint32_t generation = 0;
};
DownloadPrompt LobbyDownloadPrompt();

void AnswerDownloadPrompt(uint32_t generation, bool download);

void PerformPendingLobbyLeave();

struct DownloadEvent {
    enum class Kind : uint8_t {
        Declined,
        Stalled,
        LeftNoMap,
        NoAnswer,
    };
    enum class Why : uint8_t { None, HostCannotSend, DownloadsOff, BadName, DownloadFailed };
    int lobbySlot = -1;
    Kind kind = Kind::Declined;
    Why why = Why::None;
};

bool NextDownloadEvent(DownloadEvent* out);

int PendingDownloadSlots(int* out, int max);

}  // namespace map_download
