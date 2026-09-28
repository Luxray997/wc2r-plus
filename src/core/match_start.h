// SPDX-License-Identifier: MIT
#pragma once

namespace match_start {

using Handler = void (*)();

bool Register(Handler handler);

bool Install();

}  // namespace match_start
