// SPDX-License-Identifier: MIT
#pragma once

namespace named_window {

enum class Result {
    Continue,
    Suppress,
};

using Handler = Result (*)(void* ctx);

bool Register(const char* windowName, Handler handler);

bool Install();

}  // namespace named_window
