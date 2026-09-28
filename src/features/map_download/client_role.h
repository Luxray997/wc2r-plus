// SPDX-License-Identifier: MIT
#pragma once

#include <windows.h>

#include <cstdint>

#include "features/map_download.h"
#include "features/map_download/wire.h"

namespace map_download {
namespace client_role {

void Reset();

void Tick(DWORD now, bool ask);

void OnHostMessage(DWORD now, unsigned sender, const wire::Message& m);

void OnHostProgress(const wire::SlotProgress slots[wire::kSlots]);
wire::SlotProgress SlotProgress(unsigned netSlot);

void LeaveIfMapUnobtainable(DWORD now, bool enabled);

void NoteLookup(const char* name, bool found);

bool MapMissing();

DownloadPrompt Prompt(bool enabled, bool ask);
void AnswerPrompt(uint32_t generation, bool download);

void PerformPendingLeave();

}  // namespace client_role
}  // namespace map_download
