// SPDX-License-Identifier: MIT
#pragma once

#include <cstdio>
#include <string>

namespace paths {

const std::wstring& Root();

std::wstring Config(const std::wstring& file);
std::wstring Logs(const std::wstring& file);
std::wstring Resources(const std::wstring& file);

std::wstring Widen(const std::string& utf8);
std::string Narrow(const std::wstring& wide);

FILE* OpenShared(const std::string& utf8Path, const wchar_t* mode, int shareFlag);

}  // namespace paths
