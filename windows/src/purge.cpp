#include "purge.h"

#include <shlobj.h>
#include <tlhelp32.h>

#include <cstdio>
#include <set>
#include <vector>

#include "purge_logic.h"

namespace cronq {
namespace purge {

namespace {

using namespace purge_logic;

bool g_dry = false;

void Step(const std::wstring& what) {
    // Console copy (tests / dry run) only; the windowless copy has no stdout.
    std::string line = Narrow(what) + "\n";
    fwrite(line.data(), 1, line.size(), stdout);
    fflush(stdout);
}

std::wstring Known(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, nullptr, &path)) && path) out = path;
    CoTaskMemFree(path);
    return out;
}

std::wstring Env(const wchar_t* name) {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(name, buf, MAX_PATH * 2);
    return (n && n < MAX_PATH * 2) ? std::wstring(buf, n) : std::wstring();
}

std::wstring SelfPath() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    return std::wstring(buf, n);
}

// Same resolution as DataDir()/ConfigDir() in common.cpp: the environment first, so the purge
// removes exactly the folders Cronq wrote to.
std::wstring LocalAppData() {
    std::wstring v = Env(L"LOCALAPPDATA");
    return v.empty() ? Known(FOLDERID_LocalAppData) : TrimSlashes(v);
}
std::wstring RoamingAppData() {
    std::wstring v = Env(L"APPDATA");
    return v.empty() ? Known(FOLDERID_RoamingAppData) : TrimSlashes(v);
}

bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

// Recursive delete without following directory junctions out of the tree.
bool DeleteTreeOnce(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return true;
    if (attr & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(path.c_str(), attr & ~FILE_ATTRIBUTE_READONLY);
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) return DeleteFileW(path.c_str()) != 0;
    if (!(attr & FILE_ATTRIBUTE_REPARSE_POINT)) {
        WIN32_FIND_DATAW fd;
        HANDLE find = FindFirstFileW((path + L"\\*").c_str(), &fd);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                const std::wstring name = fd.cFileName;
                if (name == L"." || name == L"..") continue;
                DeleteTreeOnce(path + L"\\" + name);
            } while (FindNextFileW(find, &fd));
            FindClose(find);
        }
    }
    return RemoveDirectoryW(path.c_str()) != 0;
}

// WebView2 helpers may hold files open for a moment after Cronq exits: retry for ~20 s.
void Remove(const std::wstring& path) {
    if (path.empty() || !Exists(path)) return;
    if (g_dry) {
        Step(L"would delete " + path);
        return;
    }
    for (int i = 0; i < 40 && Exists(path); ++i) {
        DeleteTreeOnce(path);
        if (Exists(path)) Sleep(500);
    }
    Step((Exists(path) ? L"FAILED to delete " : L"deleted ") + path);
}

void WaitForParent(DWORD pid) {
    if (!pid || g_dry) return;
    if (HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, pid)) {
        WaitForSingleObject(p, 20000);
        CloseHandle(p);
    }
}

// Every Cronq process of this user (supervisor included) and the WebView2 processes they started.
void StopProcesses() {
    if (g_dry) {
        Step(L"would stop Cronq processes");
        return;
    }
    const DWORD self = GetCurrentProcessId();
    for (int pass = 0; pass < 3; ++pass) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return;
        std::vector<std::pair<DWORD, DWORD>> all;  // pid, parent
        std::vector<std::wstring> names;
        std::set<DWORD> ours;
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
            all.emplace_back(pe.th32ProcessID, pe.th32ParentProcessID);
            names.emplace_back(pe.szExeFile);
            const std::wstring n = Lower(pe.szExeFile);
            if (pe.th32ProcessID != self && (n == L"cronq.exe" || n == L"cronqw.exe" || n == L"cronqw_uia.exe"))
                ours.insert(pe.th32ProcessID);
        }
        CloseHandle(snap);
        // Add descendants (msedgewebview2.exe browser, renderer, GPU ...) of Cronq processes.
        for (bool grew = true; grew;) {
            grew = false;
            for (const auto& p : all)
                if (p.first != self && !ours.count(p.first) && ours.count(p.second)) {
                    ours.insert(p.first);
                    grew = true;
                }
        }
        for (DWORD pid : ours)
            if (HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid)) {
                TerminateProcess(h, 0);
                CloseHandle(h);
            }
        Sleep(300);
    }
    Step(L"stopped Cronq processes");
}

void RemoveRunValues(HKEY root, const wchar_t* subkey) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ | KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    std::vector<std::wstring> doomed;
    for (DWORD i = 0;; ++i) {
        wchar_t name[16384];
        DWORD nameLen = 16384, type = 0, dataLen = 0;
        LONG r = RegEnumValueW(key, i, name, &nameLen, nullptr, &type, nullptr, &dataLen);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS && r != ERROR_MORE_DATA) continue;
        std::wstring data;
        if ((type == REG_SZ || type == REG_EXPAND_SZ) && dataLen) {
            std::vector<wchar_t> buf(dataLen / sizeof(wchar_t) + 1, 0);
            DWORD len = static_cast<DWORD>(buf.size() * sizeof(wchar_t));
            nameLen = 16384;
            if (RegEnumValueW(key, i, name, &nameLen, nullptr, &type, reinterpret_cast<BYTE*>(buf.data()), &len) ==
                ERROR_SUCCESS)
                data = buf.data();
        }
        if (IsCronqRunValue(std::wstring(name, nameLen), data)) doomed.emplace_back(name, nameLen);
    }
    for (const auto& name : doomed) {
        if (g_dry)
            Step(L"would remove startup value " + name);
        else if (RegDeleteValueW(key, name.c_str()) == ERROR_SUCCESS)
            Step(L"removed startup value " + name);
    }
    RegCloseKey(key);
}

void RemoveShortcutsIn(const std::wstring& folder, bool startMenuFolderToo) {
    if (folder.empty()) return;
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((folder + L"\\*.lnk").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && IsCronqShortcut(fd.cFileName))
                Remove(folder + L"\\" + fd.cFileName);
        while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    if (startMenuFolderToo) {  // a Start Menu folder named Cronq only, never a Desktop folder
        const std::wstring menu = folder + L"\\Cronq";
        DWORD attr = GetFileAttributesW(menu.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) Remove(menu);
    }
}

void RemoveFromUserPath(const std::wstring& installDir) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_READ | KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(key, L"Path", nullptr, &type, nullptr, &size) == ERROR_SUCCESS && size) {
        std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1, 0);
        if (RegQueryValueExW(key, L"Path", nullptr, &type, reinterpret_cast<BYTE*>(buf.data()), &size) ==
            ERROR_SUCCESS) {
            std::wstring updated;  // read raw: other %VAR% entries stay unexpanded
            if (RemovePathEntry(buf.data(), installDir, &updated)) {
                if (g_dry) {
                    Step(L"would remove " + installDir + L" from user PATH");
                } else {
                    RegSetValueExW(key, L"Path", 0, REG_EXPAND_SZ, reinterpret_cast<const BYTE*>(updated.c_str()),
                                   static_cast<DWORD>((updated.size() + 1) * sizeof(wchar_t)));
                    DWORD_PTR ignored = 0;
                    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"Environment"),
                                        SMTO_ABORTIFHUNG, 2000, &ignored);
                    Step(L"removed from user PATH");
                }
            }
        }
    }
    RegCloseKey(key);
}

void RemoveKey(HKEY root, const wchar_t* subkey) {
    HKEY probe = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &probe) != ERROR_SUCCESS) return;
    RegCloseKey(probe);
    if (g_dry) {
        Step(std::wstring(L"would delete registry key ") + subkey);
        return;
    }
    if (RegDeleteTreeW(root, subkey) == ERROR_SUCCESS) {
        RegDeleteKeyW(root, subkey);
        Step(std::wstring(L"deleted registry key ") + subkey);
    }
}

void RemoveInstallDir(const std::wstring& installDir) {
    const std::wstring local = LocalAppData();
    const std::wstring standard = local.empty() ? std::wstring() : local + L"\\Programs\\Cronq";
    if (RemoveWholeInstallDir(installDir, standard)) {
        Remove(installDir);
        return;
    }
    // Portable / developer folder: only Cronq's own files.
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((installDir + L"\\*").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> own;
    do
        if (IsOwnFile(fd.cFileName)) own.push_back(installDir + L"\\" + fd.cFileName);
    while (FindNextFileW(find, &fd));
    FindClose(find);
    for (const auto& p : own) Remove(p);
    if (!g_dry) RemoveDirectoryW(installDir.c_str());  // only succeeds if nothing else is left
}

void RemoveTempLeftovers(const std::wstring& self) {
    std::wstring temp = Env(L"TEMP");
    if (temp.empty()) return;
    temp = TrimSlashes(temp);
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((temp + L"\\cronq*").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> items;
    do items.push_back(temp + L"\\" + fd.cFileName);
    while (FindNextFileW(find, &fd));
    FindClose(find);
    // Compare file names: %TEMP% may be spelled in short (8.3) form in one path and long in the other.
    const std::wstring selfName = self.substr(self.find_last_of(L"\\/") + 1);
    for (const auto& p : items)
        if (!EqualsNoCase(p.substr(p.find_last_of(L"\\/") + 1), selfName)) Remove(p);
}

// A running executable cannot delete itself: a short-lived cmd.exe removes this copy afterwards.
void DeleteSelfLater(const std::wstring& self) {
    if (g_dry) return;
    wchar_t sys[MAX_PATH] = {};
    GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(sys) + L"\\cmd.exe\" /d /c ping -n 3 127.0.0.1 >nul & del /f /q \"" + self + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, sys, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

}  // namespace

bool Launch() {
    std::wstring self = SelfPath();
    std::wstring dir = self.substr(0, self.find_last_of(L"\\/"));
    // Prefer the windowless overlay executable so no console window flashes.
    std::wstring source = Exists(dir + L"\\cronqw.exe") ? dir + L"\\cronqw.exe" : self;
    wchar_t tmp[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, tmp)) return false;
    const std::wstring copy = std::wstring(tmp) + L"cronq-purge-" + std::to_wstring(GetTickCount64()) + L".exe";
    if (!CopyFileW(source.c_str(), copy.c_str(), FALSE)) {
        LogErr("KILL", "Could not copy the purge helper (%lu).", GetLastError());
        return false;
    }
    std::wstring cmd = L"\"" + copy + L"\" --purge \"" + dir + L"\" " + std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    DWORD flags = CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB;
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, flags, nullptr, tmp, &si, &pi);
    if (!ok) {  // the surrounding job may forbid breakaway
        buf.assign(cmd.begin(), cmd.end());
        buf.push_back(0);
        ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, flags & ~CREATE_BREAKAWAY_FROM_JOB, nullptr,
                            tmp, &si, &pi);
    }
    if (!ok) {
        LogErr("KILL", "Could not start the purge helper (%lu).", GetLastError());
        DeleteFileW(copy.c_str());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

int Run(const std::wstring& installDirIn, DWORD parentPid, bool dryRun) {
    g_dry = dryRun;
    const std::wstring installDir = TrimSlashes(installDirIn);
    const std::wstring self = SelfPath();
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // 1. Stop everything.
    WaitForParent(parentPid);
    StopProcesses();

    // 2. Startup entries (HKLM ones only succeed for an administrator; harmless otherwise).
    RemoveRunValues(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run");
    RemoveRunValues(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce");
    RemoveRunValues(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run");
    RemoveRunValues(HKEY_LOCAL_MACHINE, L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run");
    RemoveShortcutsIn(Known(FOLDERID_Startup), false);

    // 3. Shortcuts.
    RemoveShortcutsIn(Known(FOLDERID_Programs), true);
    RemoveShortcutsIn(Known(FOLDERID_CommonPrograms), true);
    RemoveShortcutsIn(Known(FOLDERID_Desktop), false);
    RemoveShortcutsIn(Known(FOLDERID_PublicDesktop), false);

    // 4. User PATH and installer registration.
    RemoveFromUserPath(installDir);
    RemoveKey(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Cronq.Windows_is1");
    RemoveKey(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Cronq.Windows_is1");
    RemoveKey(HKEY_CURRENT_USER, L"Software\\Cronq");

    // 5. Data (settings, logins, logs, snaps; Pictures\cronq from versions before 1.2) and the program.
    const std::wstring local = LocalAppData(), roaming = RoamingAppData(), pictures = Known(FOLDERID_Pictures);
    if (!local.empty()) Remove(local + L"\\cronq");
    if (!roaming.empty()) Remove(roaming + L"\\cronq");
    if (!pictures.empty()) Remove(pictures + L"\\cronq");
    RemoveTempLeftovers(self);
    if (!installDir.empty()) RemoveInstallDir(installDir);

    // 6. This helper itself.
    DeleteSelfLater(self);
    Step(L"done");
    if (SUCCEEDED(com)) CoUninitialize();
    return 0;
}

}  // namespace purge
}  // namespace cronq
