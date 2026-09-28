// SPDX-License-Identifier: MIT
#pragma once

#include <vector>

#include "features/map_download/outbox.h"

namespace map_download {

void SendOutbox(std::vector<OutMessage>* box);

}  // namespace map_download
