// SPDX-License-Identifier: MIT
#include <windows.h>

#include <bcrypt.h>
#include <commctrl.h>
#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>

#include <cstdio>
#include <string>
#include <vector>

#include "payload.h"
#include "wc2r_version.h"

namespace {

constexpr wchar_t kTitle[] = L"WarCraft 2 Remastered+ Setup";
constexpr wchar_t kProductName[] = L"WarCraft 2 Remastered+";
constexpr wchar_t kGameExe[] = L"Warcraft II.exe";
constexpr wchar_t kDllName[] = L"version.dll";
constexpr wchar_t kTempSuffix[] = L".wc2r-plus-new";
const wchar_t* const kDefaultGameDirs[] = {
    L"C:\\Program Files (x86)\\Warcraft II Remastered",
    L"C:\\Program Files\\Warcraft II Remastered",
};

std::wstring Widen(const char* s) {
    std::wstring out;
    while (s && *s) out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*s++)));
    return out;
}

const std::wstring kVersion = Widen(WC2R_PLUS_VERSION);
const std::wstring kGameVersion = Widen(WC2R_PLUS_GAME_VERSION);
constexpr DWORD kOurVersionMS = (WC2R_PLUS_VERSION_MAJOR << 16) | WC2R_PLUS_VERSION_MINOR;
constexpr DWORD kOurVersionLS = WC2R_PLUS_VERSION_PATCH << 16;

std::wstring Join(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    return (a.back() == L'\\' || a.back() == L'/') ? a + b : a + L"\\" + b;
}

bool IsFile(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool IsDir(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring TrimSlash(std::wstring p) {
    while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    return p;
}

std::wstring ErrorText(DWORD code) {
    wchar_t* buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    std::wstring text = buf ? buf : L"";
    if (buf) LocalFree(buf);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r')) text.pop_back();
    wchar_t num[32];
    swprintf_s(num, L" (error %lu)", code);
    return text + num;
}

bool Sha256(const void* data, size_t len, unsigned char out[32]) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return false;
    const bool ok =
        BCryptHash(alg, nullptr, 0, static_cast<PUCHAR>(const_cast<void*>(data)),
                   static_cast<ULONG>(len), out, 32) == 0;
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

bool ReadWholeFile(const std::wstring& path, std::vector<unsigned char>* out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart < 64ll * 1024 * 1024;
    if (ok) {
        out->resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ok = out->empty() ||
             (ReadFile(h, out->data(), static_cast<DWORD>(out->size()), &read, nullptr) &&
              read == out->size());
    }
    CloseHandle(h);
    return ok;
}

bool FileHashEquals(const std::wstring& path, const unsigned char want[32]) {
    std::vector<unsigned char> bytes;
    unsigned char got[32];
    return ReadWholeFile(path, &bytes) && Sha256(bytes.data(), bytes.size(), got) &&
           memcmp(got, want, 32) == 0;
}

struct FileVersion {
    bool present = false;
    DWORD ms = 0, ls = 0;
    std::wstring product;
};

FileVersion ReadVersion(const std::wstring& path) {
    FileVersion v;
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0) return v;
    std::vector<unsigned char> block(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, block.data())) return v;
    VS_FIXEDFILEINFO* fixed = nullptr;
    UINT len = 0;
    if (VerQueryValueW(block.data(), L"\\", reinterpret_cast<void**>(&fixed), &len) && fixed) {
        v.present = true;
        v.ms = fixed->dwFileVersionMS;
        v.ls = fixed->dwFileVersionLS;
    }
    struct Translation {
        WORD language, codePage;
    }* tr = nullptr;
    if (VerQueryValueW(block.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&tr),
                       &len) &&
        tr && len >= sizeof(Translation)) {
        wchar_t key[64];
        swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\ProductName", tr->language, tr->codePage);
        wchar_t* name = nullptr;
        if (VerQueryValueW(block.data(), key, reinterpret_cast<void**>(&name), &len) && name) {
            v.product = name;
        }
    }
    return v;
}

std::wstring VersionText(DWORD ms, DWORD ls, bool fourParts) {
    wchar_t buf[64];
    if (fourParts) {
        swprintf_s(buf, L"%u.%u.%u.%u", HIWORD(ms), LOWORD(ms), HIWORD(ls), LOWORD(ls));
    } else {
        swprintf_s(buf, L"%u.%u.%u", HIWORD(ms), LOWORD(ms), HIWORD(ls));
    }
    return buf;
}

std::wstring X86(const std::wstring& game) { return Join(game, L"x86"); }
std::wstring DllPath(const std::wstring& game) { return Join(X86(game), kDllName); }
std::wstring MapsDownload(const std::wstring& game) {
    return Join(Join(X86(game), L"Maps"), L"Download");
}

std::wstring GameDirFor(const std::wstring& picked) {
    const std::wstring p = TrimSlash(picked);
    if (IsFile(Join(X86(p), kGameExe))) return p;
    if (IsFile(Join(p, kGameExe))) {
        const size_t slash = p.find_last_of(L"\\/");
        if (slash != std::wstring::npos && _wcsicmp(p.c_str() + slash + 1, L"x86") == 0) {
            return p.substr(0, slash);
        }
    }
    return std::wstring();
}

std::wstring FindDefaultGameDir() {
    for (const wchar_t* dir : kDefaultGameDirs) {
        const std::wstring game = GameDirFor(dir);
        if (!game.empty()) return game;
    }
    return std::wstring();
}

bool GameRunning() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W e{};
    e.dwSize = sizeof(e);
    bool running = false;
    for (BOOL more = Process32FirstW(snap, &e); more; more = Process32NextW(snap, &e)) {
        if (_wcsicmp(e.szExeFile, kGameExe) == 0) {
            running = true;
            break;
        }
    }
    CloseHandle(snap);
    return running;
}

std::wstring ModDataDir() {
    PWSTR saved = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &saved) != S_OK) {
        if (saved) CoTaskMemFree(saved);
        return std::wstring();
    }
    const std::wstring dir = Join(Join(saved, L"Warcraft2Remastered"), L"wc2r-plus");
    CoTaskMemFree(saved);
    return dir;
}

enum class Mod { None, UpToDate, Older, Newer, Damaged, Foreign };

struct State {
    std::wstring game;
    Mod mod = Mod::None;
    std::wstring installed;
    FileVersion gameExe;
    bool gameSupported = false;
};

unsigned char g_payloadHash[32];

State Inspect(const std::wstring& game) {
    State s;
    s.game = game;
    s.gameExe = ReadVersion(Join(X86(game), kGameExe));
    s.gameSupported = s.gameExe.present && s.gameExe.ms == WC2R_PLUS_GAME_VERSION_MS &&
                      s.gameExe.ls == WC2R_PLUS_GAME_VERSION_LS;

    const std::wstring dll = DllPath(game);
    if (!IsFile(dll)) {
        s.mod = Mod::None;
        return s;
    }
    const FileVersion v = ReadVersion(dll);
    if (!v.present || v.product != kProductName) {
        s.mod = Mod::Foreign;
        return s;
    }
    s.installed = VersionText(v.ms, v.ls, false);
    const bool same = v.ms == kOurVersionMS && HIWORD(v.ls) == HIWORD(kOurVersionLS);
    if (same) {
        s.mod = FileHashEquals(dll, g_payloadHash) ? Mod::UpToDate : Mod::Damaged;
    } else if (v.ms < kOurVersionMS || (v.ms == kOurVersionMS && v.ls < kOurVersionLS)) {
        s.mod = Mod::Older;
    } else {
        s.mod = Mod::Newer;
    }
    return s;
}

DWORD WritePayload(const std::wstring& game) {
    const std::wstring dll = DllPath(game);
    const std::wstring tmp = dll + kTempSuffix;
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return GetLastError();
    DWORD written = 0;
    const bool ok = WriteFile(h, kPayload, kPayloadSize, &written, nullptr) &&
                    written == kPayloadSize && FlushFileBuffers(h);
    DWORD err = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(h);
    if (err == ERROR_SUCCESS && !FileHashEquals(tmp, g_payloadHash)) err = ERROR_CRC;
    if (err == ERROR_SUCCESS &&
        !MoveFileExW(tmp.c_str(), dll.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        err = GetLastError();
    }
    if (err != ERROR_SUCCESS) DeleteFileW(tmp.c_str());
    return err;
}

HANDLE PinPlainFolder(const std::wstring& dir) {
    HANDLE h = CreateFileW(dir.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return h;
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(h, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

DWORD DeleteTree(const std::wstring& dir) {
    const DWORD attrs = GetFileAttributesW(dir.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) return ERROR_SUCCESS;
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
        return RemoveDirectoryW(dir.c_str()) ? ERROR_SUCCESS : GetLastError();
    }
    HANDLE pin = PinPlainFolder(dir);
    if (pin == INVALID_HANDLE_VALUE) return ERROR_CANT_ACCESS_FILE;
    DWORD err = ERROR_SUCCESS;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(Join(dir, L"*").c_str(), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            const std::wstring p = Join(dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                err = DeleteTree(p);
            } else {
                SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
                if (!DeleteFileW(p.c_str())) err = GetLastError();
            }
        } while (err == ERROR_SUCCESS && FindNextFileW(f, &fd));
        FindClose(f);
    }
    CloseHandle(pin);
    if (err != ERROR_SUCCESS) return err;
    return RemoveDirectoryW(dir.c_str()) ? ERROR_SUCCESS : GetLastError();
}

bool EndsWithNoCase(const wchar_t* s, const wchar_t* suffix) {
    const size_t n = wcslen(s), m = wcslen(suffix);
    return n >= m && _wcsicmp(s + n - m, suffix) == 0;
}

bool IsVersionFolderName(const wchar_t* name) {
    if (name[0] != L'V' || !name[1]) return false;
    for (const wchar_t* p = name + 1; *p; ++p) {
        if (*p < L'0' || *p > L'9') return false;
    }
    return true;
}

DWORD DeleteDownloadedMaps(const std::wstring& game) {
    const std::wstring root = MapsDownload(game);
    if (!IsDir(root)) return ERROR_SUCCESS;
    HANDLE pinRoot = PinPlainFolder(root);
    if (pinRoot == INVALID_HANDLE_VALUE) return ERROR_ACCESS_DENIED;
    DWORD result = ERROR_SUCCESS;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(Join(root, L"V*").c_str(), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                !IsVersionFolderName(fd.cFileName)) {
                continue;
            }
            const std::wstring dir = Join(root, fd.cFileName);
            HANDLE pin = PinPlainFolder(dir);
            if (pin == INVALID_HANDLE_VALUE) continue;
            WIN32_FIND_DATAW inner;
            HANDLE g = FindFirstFileW(Join(dir, L"*").c_str(), &inner);
            if (g != INVALID_HANDLE_VALUE) {
                do {
                    if (inner.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
                        continue;
                    }

                    if (!EndsWithNoCase(inner.cFileName, L".pud") &&
                        !EndsWithNoCase(inner.cFileName, L".pud.part")) {
                        continue;
                    }
                    const std::wstring file = Join(dir, inner.cFileName);
                    SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL);
                    if (!DeleteFileW(file.c_str()) && result == ERROR_SUCCESS) result = GetLastError();
                } while (FindNextFileW(g, &inner));
                FindClose(g);
            }
            CloseHandle(pin);
            RemoveDirectoryW(dir.c_str());
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    CloseHandle(pinRoot);
    RemoveDirectoryW(root.c_str());
    return result;
}

DWORD RemoveDll(const std::wstring& game) {
    const std::wstring dll = DllPath(game);
    if (IsFile(dll)) {
        if (ReadVersion(dll).product != kProductName) return ERROR_ACCESS_DENIED;
        if (!DeleteFileW(dll.c_str())) return GetLastError();
    }
    return ERROR_SUCCESS;
}

DWORD RunElevated(const std::wstring& args) {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = self;
    sei.lpParameters = args.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) return GetLastError();
    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD code = ERROR_GEN_FAILURE;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    return code;
}

std::wstring Quote(const std::wstring& s) {
    size_t trailing = 0;
    while (trailing < s.size() && s[s.size() - 1 - trailing] == L'\\') ++trailing;
    return L"\"" + s + std::wstring(trailing, L'\\') + L"\"";
}

DWORD Install(const std::wstring& game) {
    DWORD err = WritePayload(game);
    if (err == ERROR_ACCESS_DENIED) err = RunElevated(L"--elevated-install " + Quote(game));
    return err;
}

DWORD Uninstall(const std::wstring& game, bool maps, bool data) {
    DWORD err = RemoveDll(game);
    if (err == ERROR_ACCESS_DENIED && ReadVersion(DllPath(game)).product == kProductName) {
        err = RunElevated(L"--elevated-uninstall " + Quote(game));
    }

    if (err == ERROR_SUCCESS && maps) err = DeleteDownloadedMaps(game);
    if (err == ERROR_SUCCESS && data) {
        const std::wstring dir = ModDataDir();
        if (!dir.empty()) err = DeleteTree(dir);
    }
    return err;
}

int Message(PCWSTR icon, const std::wstring& instruction, const std::wstring& content,
            TASKDIALOG_COMMON_BUTTON_FLAGS buttons = TDCBF_OK_BUTTON) {
    int pressed = 0;
    TaskDialog(nullptr, nullptr, kTitle, instruction.c_str(), content.c_str(), buttons, icon,
               &pressed);
    return pressed;
}

std::wstring PickFolder() {
    std::wstring result;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg)))) {
        return result;
    }
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(L"Select the \"Warcraft II Remastered\" folder");
    if (SUCCEEDED(dlg->Show(nullptr))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

std::wstring AskForGameDir(bool notFound) {
    for (;;) {
        const TASKDIALOG_BUTTON buttons[] = {{100, L"Browse..."}};
        TASKDIALOGCONFIG c{};
        c.cbSize = sizeof(c);
        c.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
        c.pszWindowTitle = kTitle;
        c.pszMainIcon = notFound ? TD_ERROR_ICON : nullptr;
        c.pszMainInstruction = notFound ? L"Can't find installation directory"
                                        : L"Choose the game's folder";
        c.pszContent =
            notFound
                ? L"The game was not found in C:\\Program Files (x86) or C:\\Program Files.\n\n"
                  L"Click Browse and select the \"Warcraft II Remastered\" folder -- the one that "
                  L"contains the \"x86\" folder. Battle.net shows where it is under the game's "
                  L"settings (the gear beside Play), \"Show in Explorer\"."
                : L"Click Browse and select the \"Warcraft II Remastered\" folder -- the one that "
                  L"contains the \"x86\" folder.";
        c.pButtons = buttons;
        c.cButtons = 1;
        c.dwCommonButtons = TDCBF_CANCEL_BUTTON;
        int pressed = 0;
        TaskDialogIndirect(&c, &pressed, nullptr, nullptr);
        if (pressed != 100) return std::wstring();
        const std::wstring picked = PickFolder();
        if (picked.empty()) continue;
        const std::wstring game = GameDirFor(picked);
        if (!game.empty()) return game;
        Message(TD_ERROR_ICON, L"That is not the \"Warcraft II Remastered\" folder",
                L"The game was not found in:\n" + picked +
                    L"\n\nSelect the \"Warcraft II Remastered\" folder -- the one that contains "
                    L"x86\\Warcraft II.exe.");
        notFound = false;
    }
}

bool WaitForGameClosed() {
    while (GameRunning()) {
        if (Message(TD_WARNING_ICON, L"Warcraft II is running",
                    L"Close the game, then try again.",
                    TDCBF_RETRY_BUTTON | TDCBF_CANCEL_BUTTON) != IDRETRY) {
            return false;
        }
    }
    return true;
}

void ReportInstall(DWORD err, const std::wstring& verb) {
    if (err == ERROR_SUCCESS) {
        Message(TD_INFORMATION_ICON, std::wstring(kProductName) + L" " + kVersion + L" is " + verb,
                std::wstring(L"Start the game from Battle.net as usual. The main menu shows \"") +
                    kProductName +
                    L" Version " + kVersion + L"\" when it is running.");
    } else if (err == ERROR_CANCELLED) {
        Message(TD_WARNING_ICON, L"Nothing was changed",
                L"Administrator rights are needed to write to the game's folder.");
    } else {
        Message(TD_ERROR_ICON, L"The mod could not be written", ErrorText(err));
    }
}

void RunUninstall(const std::wstring& game) {
    const TASKDIALOG_BUTTON buttons[] = {{200, L"Uninstall"}};
    const TASKDIALOG_BUTTON radios[] = {
        {300, L"Keep my settings and downloaded maps"},
        {301, L"Also delete my settings and logs"},
        {302, L"Also delete my settings, logs and the maps downloaded from other players"},
    };
    TASKDIALOGCONFIG c{};
    c.cbSize = sizeof(c);
    c.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
    c.pszWindowTitle = kTitle;
    c.pszMainInstruction = L"Uninstall WarCraft 2 Remastered+?";
    c.pszContent = L"The game will run exactly as it did before the mod.";
    c.pButtons = buttons;
    c.cButtons = 1;
    c.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    c.pRadioButtons = radios;
    c.cRadioButtons = 3;
    c.nDefaultRadioButton = 300;
    int pressed = 0, radio = 300;
    TaskDialogIndirect(&c, &pressed, &radio, nullptr);
    if (pressed != 200 || !WaitForGameClosed()) return;
    const DWORD err = Uninstall(game, radio == 302, radio >= 301);
    if (err == ERROR_SUCCESS) {
        Message(TD_INFORMATION_ICON, L"WarCraft 2 Remastered+ is uninstalled", L"");
    } else if (err == ERROR_CANCELLED) {
        Message(TD_WARNING_ICON, L"Nothing was removed",
                L"Administrator rights are needed to change the game's folder.");
    } else {
        Message(TD_ERROR_ICON, L"Uninstalling did not finish", ErrorText(err));
    }
}

bool MainDialog(std::wstring* game) {
    const State s = Inspect(*game);

    std::wstring instruction, status, primary;
    bool canUninstall = true;
    switch (s.mod) {
        case Mod::None:
            instruction = L"Install WarCraft 2 Remastered+ " + kVersion;
            status = L"The mod is not installed.";
            primary = L"Install\nPut version " + kVersion + L" into the game's folder.";
            canUninstall = false;
            break;
        case Mod::UpToDate:
            instruction = L"WarCraft 2 Remastered+ " + kVersion + L" is installed";
            status = L"The installed mod is this version, and intact.";
            primary = L"Reinstall\nWrite version " + kVersion + L" again.";
            break;
        case Mod::Older:
            instruction = L"Update WarCraft 2 Remastered+";
            status = L"Version " + s.installed + L" is installed.";
            primary = L"Update to " + kVersion + L"\nYour settings are kept.";
            break;
        case Mod::Newer:
            instruction = L"A newer version is installed";
            status = L"Version " + s.installed + L" is installed, which is newer than this setup (" +
                     kVersion + L").";
            primary = L"Install " + kVersion + L" anyway\nReplace the newer version.";
            break;
        case Mod::Damaged:
            instruction = L"The installed mod is damaged";
            status = L"Version " + s.installed +
                     L" is installed, but the file does not match this version: it has been "
                     L"changed or corrupted.";
            primary = L"Repair\nWrite a good copy of version " + kVersion + L".";
            break;
        case Mod::Foreign:
            instruction = L"Another version.dll is installed";
            status = L"The game's folder already has a version.dll that is not WarCraft 2 "
                     L"Remastered+ -- another mod, or an early test build of this one.";
            primary = L"Replace it\nInstall WarCraft 2 Remastered+ " + kVersion + L" in its place.";
            canUninstall = false;
            break;
    }

    std::wstring content = L"Game folder: " + *game + L"\n" + status;
    if (!s.gameSupported) {
        content += L"\n\nThis version of the mod supports game version " + kGameVersion +
                   L", and this game is " +
                   (s.gameExe.present ? VersionText(s.gameExe.ms, s.gameExe.ls, true)
                                      : std::wstring(L"an unknown version")) +
                   L". The mod stays inactive on any other version until a matching release is "
                   L"installed.";
    }

    std::vector<TASKDIALOG_BUTTON> buttons;
    buttons.push_back({1001, primary.c_str()});
    if (canUninstall) buttons.push_back({1002, L"Uninstall\nRemove the mod from the game."});
    buttons.push_back({1003, L"Choose a different game folder"});

    TASKDIALOGCONFIG c{};
    c.cbSize = sizeof(c);
    c.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION;
    c.pszWindowTitle = kTitle;
    c.pszMainIcon = s.mod == Mod::Damaged || !s.gameSupported ? TD_WARNING_ICON : nullptr;
    c.pszMainInstruction = instruction.c_str();
    c.pszContent = content.c_str();
    c.pButtons = buttons.data();
    c.cButtons = static_cast<UINT>(buttons.size());
    c.dwCommonButtons = TDCBF_CLOSE_BUTTON;
    int pressed = 0;
    TaskDialogIndirect(&c, &pressed, nullptr, nullptr);

    if (pressed == 1003) {
        const std::wstring other = AskForGameDir(false);
        if (!other.empty()) *game = other;
        return true;
    }
    if (pressed == 1002) {
        RunUninstall(*game);
        return false;
    }
    if (pressed == 1001) {
        if (!WaitForGameClosed()) return true;
        const bool update = s.mod == Mod::Older || s.mod == Mod::Newer;
        ReportInstall(Install(*game), s.mod == Mod::Damaged ? L"repaired"
                                      : update           ? L"installed (updated)"
                                                         : L"installed");
        return false;
    }
    return false;
}

int Status(const std::wstring& given) {

    const HANDLE inherited = GetStdHandle(STD_OUTPUT_HANDLE);
    const bool redirected = inherited && inherited != INVALID_HANDLE_VALUE &&
                            GetFileType(inherited) != FILE_TYPE_UNKNOWN;
    FILE* out = nullptr;
    if (!redirected && AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen_s(&out, "CONOUT$", "w", stdout);
    }
    const std::wstring game = given.empty() ? FindDefaultGameDir() : GameDirFor(given);
    wprintf(L"setup version   %ls (supports game %ls)\n", kVersion.c_str(), kGameVersion.c_str());
    if (game.empty()) {
        wprintf(L"game            NOT FOUND%ls%ls\n", given.empty() ? L"" : L" at ", given.c_str());
        return 2;
    }
    const State s = Inspect(game);
    static const wchar_t* const kNames[] = {L"not installed", L"up to date", L"older",
                                            L"newer", L"damaged", L"foreign version.dll"};
    wprintf(L"game            %ls\n", game.c_str());
    wprintf(L"game version    %ls (%ls)\n",
            s.gameExe.present ? VersionText(s.gameExe.ms, s.gameExe.ls, true).c_str() : L"unknown",
            s.gameSupported ? L"supported" : L"NOT supported");
    wprintf(L"mod             %ls%ls%ls\n", kNames[static_cast<int>(s.mod)],
            s.installed.empty() ? L"" : L", installed version ", s.installed.c_str());
    wprintf(L"mod data        %ls\n", ModDataDir().c_str());
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {

    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!Sha256(kPayload, kPayloadSize, g_payloadHash)) return 1;

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const std::wstring verb = argc >= 2 ? argv[1] : L"";
    const std::wstring arg = argc >= 3 ? argv[2] : L"";

    if (verb.rfind(L"--elevated-", 0) == 0) {
        const std::wstring game = GameDirFor(arg);
        if (game.empty()) return static_cast<int>(ERROR_PATH_NOT_FOUND);
        if (verb == L"--elevated-install") return static_cast<int>(WritePayload(game));
        if (verb == L"--elevated-uninstall") return static_cast<int>(RemoveDll(game));
        return static_cast<int>(ERROR_INVALID_PARAMETER);
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    int rc = 0;
    if (verb == L"--status") {
        rc = Status(arg);
    } else {
        std::wstring game = FindDefaultGameDir();
        if (game.empty()) game = AskForGameDir(true);
        while (!game.empty() && MainDialog(&game)) {
        }
    }
    CoUninitialize();
    LocalFree(argv);
    return rc;
}
