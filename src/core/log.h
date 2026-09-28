// SPDX-License-Identifier: MIT
#pragma once

class Logger {
public:
    constexpr explicit Logger(const char* tag) : tag_(tag) {}

    void Info(const char* fmt, ...) const;
    void Warn(const char* fmt, ...) const;
    void Error(const char* fmt, ...) const;
    void Debug(const char* fmt, ...) const;

private:
    const char* tag_;
};

void LogRaw(const char* fmt, ...);
