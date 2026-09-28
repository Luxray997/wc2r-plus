// SPDX-License-Identifier: MIT
#pragma once

#include <windows.h>

#include "features/map_download/wire.h"

namespace map_download {
namespace host_role {

void Reset();

void Tick(DWORD now, bool serving);

void OnMessage(DWORD now, unsigned sender, const wire::Message& m);

wire::SlotProgress SlotProgress(unsigned netSlot);

bool AnyPending(DWORD now);
int PendingSlot(DWORD now);

}  // namespace host_role
}  // namespace map_download
