// SPDX-License-Identifier: MIT
using HMODULE_ = void*;
using UINT_ = unsigned int;

extern "C" {
__declspec(dllimport) HMODULE_ __stdcall LoadLibraryA(const char*);
__declspec(dllimport) void* __stdcall GetProcAddress(HMODULE_, const char*);
__declspec(dllimport) UINT_ __stdcall GetSystemDirectoryA(char*, UINT_);
__declspec(dllimport) char* __stdcall lstrcatA(char*, const char*);
}

namespace {
constexpr int kMaxPath = 260;
}  // namespace

extern "C" {
void* g_real_GetFileVersionInfoA = nullptr;
void* g_real_GetFileVersionInfoByHandle = nullptr;
void* g_real_GetFileVersionInfoExA = nullptr;
void* g_real_GetFileVersionInfoExW = nullptr;
void* g_real_GetFileVersionInfoSizeA = nullptr;
void* g_real_GetFileVersionInfoSizeExA = nullptr;
void* g_real_GetFileVersionInfoSizeExW = nullptr;
void* g_real_GetFileVersionInfoSizeW = nullptr;
void* g_real_GetFileVersionInfoW = nullptr;
void* g_real_VerFindFileA = nullptr;
void* g_real_VerFindFileW = nullptr;
void* g_real_VerInstallFileA = nullptr;
void* g_real_VerInstallFileW = nullptr;
void* g_real_VerLanguageNameA = nullptr;
void* g_real_VerLanguageNameW = nullptr;
void* g_real_VerQueryValueA = nullptr;
void* g_real_VerQueryValueW = nullptr;
}

void InitVersionProxy() {
    char sysDir[kMaxPath] = {};
    GetSystemDirectoryA(sysDir, kMaxPath);
    lstrcatA(sysDir, "\\version.dll");
    HMODULE_ h = LoadLibraryA(sysDir);
    if (!h) return;

#define RESOLVE(name) g_real_##name = GetProcAddress(h, #name)
    RESOLVE(GetFileVersionInfoA);
    RESOLVE(GetFileVersionInfoByHandle);
    RESOLVE(GetFileVersionInfoExA);
    RESOLVE(GetFileVersionInfoExW);
    RESOLVE(GetFileVersionInfoSizeA);
    RESOLVE(GetFileVersionInfoSizeExA);
    RESOLVE(GetFileVersionInfoSizeExW);
    RESOLVE(GetFileVersionInfoSizeW);
    RESOLVE(GetFileVersionInfoW);
    RESOLVE(VerFindFileA);
    RESOLVE(VerFindFileW);
    RESOLVE(VerInstallFileA);
    RESOLVE(VerInstallFileW);
    RESOLVE(VerLanguageNameA);
    RESOLVE(VerLanguageNameW);
    RESOLVE(VerQueryValueA);
    RESOLVE(VerQueryValueW);
#undef RESOLVE
}

#define STUB(name)                                     \
    extern "C" __declspec(naked) void name() {          \
        __asm { jmp dword ptr [g_real_##name] }          \
    }

STUB(GetFileVersionInfoA)
STUB(GetFileVersionInfoByHandle)
STUB(GetFileVersionInfoExA)
STUB(GetFileVersionInfoExW)
STUB(GetFileVersionInfoSizeA)
STUB(GetFileVersionInfoSizeExA)
STUB(GetFileVersionInfoSizeExW)
STUB(GetFileVersionInfoSizeW)
STUB(GetFileVersionInfoW)
STUB(VerFindFileA)
STUB(VerFindFileW)
STUB(VerInstallFileA)
STUB(VerInstallFileW)
STUB(VerLanguageNameA)
STUB(VerLanguageNameW)
STUB(VerQueryValueA)
STUB(VerQueryValueW)

#undef STUB

#pragma comment(linker, "/export:GetFileVersionInfoA=_GetFileVersionInfoA")
#pragma comment(linker, "/export:GetFileVersionInfoByHandle=_GetFileVersionInfoByHandle")
#pragma comment(linker, "/export:GetFileVersionInfoExA=_GetFileVersionInfoExA")
#pragma comment(linker, "/export:GetFileVersionInfoExW=_GetFileVersionInfoExW")
#pragma comment(linker, "/export:GetFileVersionInfoSizeA=_GetFileVersionInfoSizeA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeExA=_GetFileVersionInfoSizeExA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeExW=_GetFileVersionInfoSizeExW")
#pragma comment(linker, "/export:GetFileVersionInfoSizeW=_GetFileVersionInfoSizeW")
#pragma comment(linker, "/export:GetFileVersionInfoW=_GetFileVersionInfoW")
#pragma comment(linker, "/export:VerFindFileA=_VerFindFileA")
#pragma comment(linker, "/export:VerFindFileW=_VerFindFileW")
#pragma comment(linker, "/export:VerInstallFileA=_VerInstallFileA")
#pragma comment(linker, "/export:VerInstallFileW=_VerInstallFileW")
#pragma comment(linker, "/export:VerLanguageNameA=_VerLanguageNameA")
#pragma comment(linker, "/export:VerLanguageNameW=_VerLanguageNameW")
#pragma comment(linker, "/export:VerQueryValueA=_VerQueryValueA")
#pragma comment(linker, "/export:VerQueryValueW=_VerQueryValueW")
