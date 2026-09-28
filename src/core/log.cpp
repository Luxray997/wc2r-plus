// SPDX-License-Identifier: MIT
#include "core/log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <share.h>

#include "core/paths.h"

namespace {

constexpr unsigned long long kMaxLogBytes = 5ull * 1024 * 1024;

FILE* File() {
    static FILE* f = [] {
        const std::wstring path = paths::Logs(L"wc2r-plus.log");
        if (path.empty()) return static_cast<FILE*>(nullptr);
        WIN32_FILE_ATTRIBUTE_DATA attr{};
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attr)) {
            const unsigned long long size =
                (static_cast<unsigned long long>(attr.nFileSizeHigh) << 32) | attr.nFileSizeLow;
            if (size > kMaxLogBytes) {
                MoveFileExW(path.c_str(), paths::Logs(L"wc2r-plus.1.log").c_str(),
                            MOVEFILE_REPLACE_EXISTING);
            }
        }
        FILE* opened = _wfsopen(path.c_str(), L"a", _SH_DENYWR);
        if (opened) setvbuf(opened, nullptr, _IONBF, 0);
        return opened;
    }();
    return f;
}

constexpr long kMaxSessionBytes = 20 * 1024 * 1024;
volatile long g_sessionBytes = 0;

bool Admit(FILE* f, size_t bytes) {

    if (g_sessionBytes > kMaxSessionBytes) return false;
    const long before = InterlockedExchangeAdd(&g_sessionBytes, static_cast<long>(bytes));
    if (before + static_cast<long>(bytes) <= kMaxSessionBytes) return true;
    if (before <= kMaxSessionBytes) {
        fputs("*** log limit for this session reached; nothing more is written until the next "
              "launch ***\n", f);
    }
    return false;
}

void Write(const char* tag, const char* level, const char* fmt, va_list args) {
    FILE* f = File();
    if (!f) return;

    SYSTEMTIME t;
    GetLocalTime(&t);
    char line[1024];
    int used = snprintf(line, sizeof(line), "[%02u:%02u:%02u.%03u] %s: %s", t.wHour, t.wMinute,
                        t.wSecond, t.wMilliseconds, tag, level);
    if (used < 0) return;
    if (used < (int)sizeof(line)) {
        const int n = vsnprintf(line + used, sizeof(line) - used, fmt, args);
        used = n < 0 ? used : used + n;
    }
    if (used > (int)sizeof(line) - 2) used = (int)sizeof(line) - 2;
    line[used++] = '\n';
    if (Admit(f, static_cast<size_t>(used))) fwrite(line, 1, used, f);
}

}  // namespace

#define LOGGER_LEVEL(Name, prefix)                  \
    void Logger::Name(const char* fmt, ...) const { \
        va_list args;                               \
        va_start(args, fmt);                        \
        Write(tag_, prefix, fmt, args);             \
        va_end(args);                               \
    }

LOGGER_LEVEL(Info, "")
LOGGER_LEVEL(Warn, "WARNING: ")
LOGGER_LEVEL(Error, "ERROR: ")
LOGGER_LEVEL(Debug, "DEBUG: ")

#undef LOGGER_LEVEL

void LogRaw(const char* fmt, ...) {
    FILE* f = File();
    if (!f) return;
    char text[2048];
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    if (n <= 0) return;
    const size_t len = n < static_cast<int>(sizeof(text)) ? static_cast<size_t>(n) : sizeof(text) - 1;
    if (Admit(f, len)) fwrite(text, 1, len, f);
}
