// cronq for Windows.
//   cronq.exe   console build: the command line tool (same commands as the Linux `cronq` script/binary)
//   cronqw.exe  windowless build: the overlay itself (autostart, Start Menu, supervisor)
#include "app.h"
#include "browser.h"
#include "focus_guard.h"
#include "ghost.h"
#include <objbase.h>
#include "common.h"
#include "config.h"
#include "hotkey.h"
#include "input.h"
#include "ipc.h"
#include "overlay.h"
#include "raise.h"
#include "snap.h"
#include "version.h"
#include "crash_handler.h"

#include <shellapi.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace cronq;

namespace {

const char* const kVersion = CRONQ_VERSION_FULL;

// ---------------------------------------------------------------- helpers

std::vector<std::string> GetArgs() {
    std::vector<std::string> out;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return out;
    for (int i = 1; i < argc; i++) out.push_back(Narrow(argv[i]));
    LocalFree(argv);
    return out;
}

std::wstring SelfPath() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    return std::wstring(buf, n);
}

#ifndef CRONQ_UIACCESS
std::wstring DirOf(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring(L".") : path.substr(0, slash);
}

bool FileExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// The windowless overlay executable that sits next to cronq.exe; falls back to the running exe.
#endif

std::wstring OverlayExe() {
#ifdef CRONQ_UIACCESS
    return SelfPath();
#else
    std::wstring sibling = DirOf(SelfPath()) + L"\\cronqw.exe";
    return FileExists(sibling) ? sibling : SelfPath();
#endif
}

std::wstring Quote(const std::wstring& s) {
    std::wstring out = L"\"";
    for (wchar_t c : s) {
        if (c == L'"') out += L'\\';
        out += c;
    }
    out += L"\"";
    return out;
}

bool Spawn(const std::wstring& exe, const std::wstring& args, DWORD flags, HANDLE* processOut) {
    std::wstring cmd = Quote(exe) + L" " + args;
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, flags, nullptr, nullptr, &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    if (processOut)
        *processOut = pi.hProcess;
    else
        CloseHandle(pi.hProcess);
    return true;
}

bool HasShowFlag(const std::vector<std::string>& args) {
    for (const std::string& a : args) {
        if (a == "--show" || a == "show") return true;
    }
    return false;
}

void EnableDpiAwareness() {
    typedef BOOL(WINAPI * SetCtxFn)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    SetCtxFn setCtx =
        user32 ? reinterpret_cast<SetCtxFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"))
               : nullptr;
    if (setCtx) {
        if (setCtx(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)))) return;  // per-monitor v2
        if (setCtx(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-3)))) return;  // per-monitor v1
    }
    typedef BOOL(WINAPI * SetAwareFn)(void);
    SetAwareFn aware =
        user32 ? reinterpret_cast<SetAwareFn>(GetProcAddress(user32, "SetProcessDPIAware")) : nullptr;
    if (aware) aware();
}

// ---------------------------------------------------------------- daemon

BOOL WINAPI ConsoleCtrlHandler(DWORD) {
    HWND h = overlay::Hwnd();
    if (h) PostMessageW(h, WM_CRONQ_QUIT, 0, 0);
    return TRUE;
}

int RunDaemon(bool forceVisible) {
    crash_handler::Install();
    HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(apartment)) {
        LogErr("MAIN", "CoInitializeEx hr=0x%08lX", apartment);
        return 1;
    }
    struct Apartment {
        ~Apartment() { CoUninitialize(); }
    } apartmentScope;
    EnableDpiAwareness();
    Log("MAIN", "=== Starting Cronq (%s) ===", kVersion);

    if (!config::Init()) {
        LogErr("MAIN", "Failed to initialize configuration.");
        return 1;
    }
    const AppConfig& cfg = config::Get();
    bool startVisible = cfg.startVisible || forceVisible;

    // Single instance: the pipe can only be created once per user.
    if (!ipc::StartServer()) return 0;  // exit 0 so a supervisor does not restart-loop

    raise::Init();

    if (!overlay::Init(startVisible)) {
        LogErr("MAIN", "Failed to initialize overlay window.");
        ipc::StopServer();
        return 1;
    }
    ipc::SetTarget(overlay::Hwnd());

    input::Init(overlay::Hwnd());
    input::SetKeyMode(cfg.defaultKeyMode);
    ghost::Init();
    snap::Init();

    if (!hotkey::Init(overlay::Hwnd())) {
        LogErr("MAIN", "Failed to initialize global hotkey system.");
        snap::Cleanup();
        ghost::Cleanup();
        input::Cleanup();
        raise::Cleanup();
        ipc::StopServer();
        overlay::Cleanup();
        return 1;
    }
    focus_guard::Init(overlay::Hwnd());
    browser::Init(overlay::Hwnd());
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    if (!startVisible) {
        Log("MAIN", "Cronq started in BACKGROUND mode (hidden). Press Alt+%c to show it, or run: cronq show",
            (char)toupper(cfg.toggleKey));
    }

    Log("MAIN", "Entering main loop.");
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Log("MAIN", "Shutdown: performing clean teardown...");
    ghost::Cleanup();
    snap::Cleanup();
    input::Cleanup();
    hotkey::Cleanup();
    focus_guard::Cleanup();
    browser::Cleanup();
    raise::Cleanup();
    ipc::StopServer();  // waits (briefly) for an in-flight reply such as KILL's "OK"
    overlay::Cleanup();
    Log("MAIN", "Clean exit completed.");
    return 0;
}

// ---------------------------------------------------------------- supervisor

void AppendCrashLog(DWORD code) {
    std::wstring path = DataDir() + L"\\crash.log";
    FILE* f = _wfopen(path.c_str(), L"a");
    if (!f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] Cronq crashed/exited with code %lu. Restarting in 2s...\n",
            t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, (unsigned long)code);
    fclose(f);
}

int RunSupervisor(const std::vector<std::string>& passthrough) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\CronqSupervisor");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) return 0;  // another supervisor is already watching

    std::wstring args = L"--daemon";
    for (const std::string& a : passthrough) args += L" " + Quote(Widen(a));
    std::wstring exe = OverlayExe();

    for (;;) {
        HANDLE proc = nullptr;
        if (!Spawn(exe, args, 0, &proc)) {
            AppendCrashLog(GetLastError());
            Sleep(2000);
            continue;
        }
        WaitForSingleObject(proc, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(proc, &code);
        CloseHandle(proc);

        // Clean exit (including `cronq kill` and Ctrl+C): stop supervising.
        if (code == 0 || code == 0xC000013A /* STATUS_CONTROL_C_EXIT */) break;
        AppendCrashLog(code);
        Sleep(2000);
    }
    if (mutex) CloseHandle(mutex);
    return 0;
}

// ---------------------------------------------------------------- CLI

int Remote(const std::string& cmd, const char* okMessage, bool printResponse = false) {
    std::string resp;
    if (!ipc::Send(cmd, &resp)) {
        fprintf(stderr, "Error: Cronq is not running.\n");
        return 1;
    }
    if (printResponse)
        printf("%s", resp.c_str());
    else if (okMessage)
        printf("%s\n", okMessage);
    return 0;
}

int Help(const char* prog) {
    printf("Usage: cronq [OPTIONS] or %s [OPTIONS]\n", prog);
    printf("Quick commands:\n");
    printf("  cronq                 Bring up overlay (launch if not running)\n");
    printf("  cronq kill            Kill running overlay daemon\n");
    printf("Options / Subcommands:\n");
    printf("  --show, show                  Show / bring up overlay\n");
    printf("  --hide, hide                  Hide running overlay\n");
    printf("  --toggle, toggle              Toggle visibility of running overlay\n");
    printf("  --kill, kill                  Kill running overlay daemon\n");
    printf("  --status, status              Query running overlay status\n");
    printf("  --snap, snap                  Capture screenshot (excluding overlay) to clipboard & disk\n");
    printf("  --mode [m|e]                  Set or cycle typing mode (mirror / exclusive)\n");
    printf("  --ghost [on|off]              Set or toggle GHOST click-through mode\n");
    printf("  --app <name>                  Switch app view (home, chatgpt, browser)\n");
    printf("  --load <url>                  Load URL in browser\n");
    printf("  --back                        Navigate back in current app (or home if at start)\n");
    printf("  --home                        Return to two-app home screen\n");
    printf("  --reload                      Reload active webview\n");
    printf("  --reset, reset                Reset overlay to new-like state (clear history, reload apps)\n");
    printf("  --opacity <val>               Set overlay opacity (0.20 to 1.00)\n");
    printf("  --size <w> <h>                Set overlay dimensions in pixels\n");
    printf("  --fullscreen                  Toggle fullscreen mode\n");
    printf("  --default-size                Reset overlay to default dimensions (360x640)\n");
    printf("  --get-geometry                Print current overlay geometry (X, Y, W, H)\n");
    printf("  --get-entry                   Print current text from Test Input\n");
    printf("  --has-modal                   Query if an internal modal popup is displayed\n");
    printf("  --close-modal                 Close internal modal popup if displayed\n");
    printf("  --ghost-write                 Trigger Ghost Writer keyboard injection\n");
    printf("  --paste                       Trigger paste into overlay (entry or active webview)\n");
    printf("  --reload-config               Reload configuration from config.ini\n");
    printf("  --version                     Print the version\n");
    printf("  --help, -h                    Show this help text\n");
    printf("Windows only:\n");
    printf("  --daemon                      Run the overlay in this process (no background launch)\n");
    printf("  --supervise                   Run the overlay and restart it if it crashes\n");
    return 0;
}

#ifdef CRONQ_CONSOLE
// cronq.exe: start the overlay in the background (hidden console-less process) and wait for it.
int LaunchBackground(bool show) {
    std::wstring args = L"--daemon";
    if (show) args += L" --show";
    if (!Spawn(OverlayExe(), args, DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr)) {
        fprintf(stderr, "Cronq: Failed to start (error %lu).\n", GetLastError());
        return 1;
    }
    for (int i = 0; i < 30; i++) {
        std::string resp;
        if (ipc::Send("STATUS", &resp)) {
            printf("Cronq: Started and brought up.\n");
            return 0;
        }
        Sleep(100);
    }
    printf("Cronq: Failed to start. Check %s for details.\n", Narrow(DataDir() + L"\\cronq.log").c_str());
    return 1;
}
#endif

int StartOrShow(const std::vector<std::string>& args) {
    bool show = args.empty() || HasShowFlag(args);
    std::string resp;
    if (show && ipc::Send("SHOW", &resp)) {
        printf("Cronq: %s\n", args.empty() ? "Brought up." : "Overlay shown.");
        return 0;
    }
#ifdef CRONQ_CONSOLE
    return LaunchBackground(true);
#else
    return RunDaemon(true);
#endif
}

int RunCli(const std::vector<std::string>& args) {
    for (size_t i = 0; i < args.size(); i++) {
        const std::string& a = args[i];
        auto is = [&](const char* x) { return a == x; };

        if (is("--kill") || is("kill") || is("--stop") || is("stop") || is("--quit") || is("quit")) {
            std::string resp;
            if (ipc::Send("KILL", &resp))
                printf("Cronq: Killed.\n");
            else
                printf("Cronq is not running.\n");
            return 0;
        } else if (is("--toggle") || is("toggle")) {
            return Remote("TOGGLE", "Cronq: Toggled visibility.");
        } else if (is("--hide") || is("hide")) {
            return Remote("HIDE", "Cronq: Overlay hidden.");
        } else if (is("--mode")) {
            std::string cmd = "MODE";
            if (i + 1 < args.size() && args[i + 1][0] != '-') {
                std::string m = args[++i];
                if (m == "m")
                    m = "mirror";
                else if (m == "e")
                    m = "exclusive";
                cmd += " " + m;
            }
            std::string resp;
            if (!ipc::Send(cmd, &resp)) {
                fprintf(stderr, "Error: Cronq is not running.\n");
                return 1;
            }
            printf("Cronq: %s\n", Trim(resp).c_str());
            return 0;
        } else if (is("--ghost")) {
            std::string cmd = "GHOST";
            if (i + 1 < args.size() && args[i + 1][0] != '-') cmd += " " + args[++i];
            std::string resp;
            if (!ipc::Send(cmd, &resp)) {
                fprintf(stderr, "Error: Cronq is not running.\n");
                return 1;
            }
            printf("Cronq: %s\n", Trim(resp).c_str());
            return 0;
        } else if (is("--load")) {
            if (i + 1 >= args.size()) {
                fprintf(stderr, "Error: --load requires a URL argument.\n");
                return 1;
            }
            const std::string& url = args[++i];
            std::string okMsg = "Cronq: Loaded '" + url + "'.";
            return Remote("LOAD " + url, okMsg.c_str());
        } else if (is("--app")) {
            if (i + 1 >= args.size()) {
                fprintf(stderr, "Error: --app requires an app name (home, chatgpt, browser).\n");
                return 1;
            }
            const std::string& name = args[++i];
            std::string okMsg = "Cronq: Switched to '" + name + "'.";
            return Remote("APP " + name, okMsg.c_str());
        } else if (is("--back")) {
            return Remote("BACK", nullptr);
        } else if (is("--home")) {
            return Remote("HOME", nullptr);
        } else if (is("--reload")) {
            return Remote("RELOAD", nullptr);
        } else if (is("--get-entry")) {
            return Remote("GET_TEST_ENTRY", nullptr, true);
        } else if (is("--set-entry") && i + 1 < args.size()) {
            return Remote("SET_TEST_ENTRY " + args[i + 1], nullptr);
        } else if (is("--clear-entry")) {
            return Remote("CLEAR_TEST_ENTRY", nullptr);
        } else if (is("--has-modal")) {
            return Remote("HAS_MODAL", nullptr, true);
        } else if (is("--close-modal")) {
            return Remote("CLOSE_MODAL", nullptr);
        } else if (is("--snap") || is("snap")) {
            return Remote("SNAP", nullptr);
        } else if (is("--ghost-write") || is("--ghost-writer")) {
            return Remote("GHOST_WRITE", nullptr);
        } else if (is("--paste")) {
            return Remote("PASTE", nullptr);
        } else if (is("--reset") || is("--refresh-all") || is("reset")) {
            return Remote("UNIVERSAL_RESET", "Cronq: Universal reset triggered (new state).");
        } else if (is("--reload-config")) {
            return Remote("RELOAD_CONFIG", "Cronq: Configuration reloaded.");
        } else if (is("--opacity") && i + 1 < args.size()) {
            return Remote("SET_OPACITY " + args[i + 1], nullptr);
        } else if (is("--size") && i + 2 < args.size()) {
            return Remote("SET_SIZE " + args[i + 1] + " " + args[i + 2], nullptr);
        } else if (is("--get-geometry")) {
            return Remote("GET_GEOMETRY", nullptr, true);
        } else if (is("--status") || is("status")) {
            return Remote("STATUS", nullptr, true);
        } else if (is("--fullscreen")) {
            return Remote("FULLSCREEN", nullptr);
        } else if (is("--default-size")) {
            return Remote("DEFAULT_SIZE", nullptr);
        } else if (is("--version")) {
            printf("%s\n", kVersion);
            return 0;
        } else if (is("--help") || is("-h")) {
            return Help("cronq");
        }
    }
    return StartOrShow(args);
}

}  // namespace

int main() {
    std::vector<std::string> args = GetArgs();

#ifdef CRONQ_CONSOLE
    SetConsoleOutputCP(CP_UTF8);
#endif

    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == "--supervise") {
            std::vector<std::string> rest(args.begin() + (long)i + 1, args.end());
            return RunSupervisor(rest);
        }
        if (args[i] == "--daemon") {
            std::string resp;
            if (HasShowFlag(args) && ipc::Send("SHOW", &resp)) return 0;  // already running: just show it
            return RunDaemon(HasShowFlag(args));
        }
    }
    return RunCli(args);
}
