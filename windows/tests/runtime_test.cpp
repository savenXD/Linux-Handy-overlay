#include "common.h"
#include "ipc.h"
#include <psapi.h>
#include <cstdio>
#include <string>
using namespace cronq;
namespace {
HWND edit = nullptr;
void Pump(DWORD ms) {
    ULONGLONG end = GetTickCount64() + ms;
    do {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        Sleep(2);
    } while (GetTickCount64() < end);
}
std::string Command(const std::string& c) {
    std::string r;
    if (!ipc::Send(c, &r)) return "IPC_FAILED";
    return r;
}
std::wstring Text() {
    wchar_t buf[8192]{};
    GetWindowTextW(edit, buf, 8192);
    return buf;
}
void Key(WORD vk, bool down) {
    INPUT i{};
    i.type = INPUT_KEYBOARD;
    i.ki.wVk = vk;
    i.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &i, sizeof(i));
    Pump(30);
}
LRESULT CALLBACK Receiver(HWND w, UINT m, WPARAM a, LPARAM b) { return DefWindowProcW(w, m, a, b); }
}  // namespace
int main(int argc, char** argv) {
    HWND overlay = FindWindowW(L"CronqOverlayWindow", nullptr);
    if (!overlay) {
        std::puts("FAIL: daemon not running");
        return 1;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(overlay, &pid);
    if (argc > 1 && std::string(argv[1]) == "--metrics") {
        HANDLE p = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        PROCESS_MEMORY_COUNTERS mem{};
        mem.cb = sizeof(mem);
        DWORD handles = 0;
        if (!p || !GetProcessMemoryInfo(p, &mem, sizeof(mem)) || !GetProcessHandleCount(p, &handles) ||
            !mem.WorkingSetSize) {
            if (p) CloseHandle(p);
            std::puts("FAIL: process metrics unavailable");
            return 1;
        }
        std::printf("%llu %lu\n", static_cast<unsigned long long>(mem.WorkingSetSize / 1024), handles);
        CloseHandle(p);
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--ipc-stress") {
        const char* commands[] = {"STATUS",          "SET_SIZE 360 400", "SET_OPACITY 0.8", "MODE exclusive",
                                  "GHOST on",        "GET_GEOMETRY",     "GHOST off",       "MODE mirror",
                                  "SET_OPACITY 1.0", "SET_SIZE 360 280"};
        for (int n = 0; n < 5000; ++n) {
            std::string reply;
            if (!ipc::Send(commands[n % 10], &reply) || reply.empty()) {
                std::printf("FAIL: IPC iteration %d error %lu\n", n, GetLastError());
                return 1;
            }
        }
        std::puts("runtime_test: 5000 IPC requests passed");
        return 0;
    }
    const bool instant = argc > 1 && std::string(argv[1]) == "--instant";
    int failed = 0;
    auto check = [&](const char* name, bool ok) {
        std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failed;
    };
    WNDCLASSW wc{};
    wc.lpfnWndProc = Receiver;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"CronqTestReceiver";
    RegisterClassW(&wc);
    HWND window = CreateWindowW(wc.lpszClassName, L"Cronq isolated test receiver", WS_OVERLAPPEDWINDOW, 600,
                                100, 400, 300, nullptr, nullptr, wc.hInstance, nullptr);
    edit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL, 0, 0, 380, 250,
                         window, nullptr, wc.hInstance, nullptr);
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    SetFocus(edit);
    Pump(200);
    Command("RESET");
    Command("SHOW");
    Pump(100);
    check("test receiver has focus", GetForegroundWindow() == window);
    if (instant) {
        const std::string text =
            std::string(512, 'x') + " caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xF0\x9F\x98\x80";
        Command("SET_TEST_ENTRY " + text);
        Command("GHOST_WRITE");
        Pump(1400);
        check("Instant mode types long Unicode text in batches", Text() == Widen(text));
        Command("RESET");
        Command("SHOW");
        DestroyWindow(window);
        std::printf("runtime_test: %d failure(s)\n", failed);
        return failed ? 1 : 0;
    }
    Command("SET_TEST_ENTRY abc");
    Command("GHOST_WRITE");
    Pump(1800);
    check("Ghost Writer types exact text into external edit", Text() == L"abc");
    SetWindowTextW(edit, L"");
    Command("SET_TEST_ENTRY abcdefghijklmnopqrstuvwxyz");
    Command("GHOST_WRITE");
    Pump(550);
    Key(VK_MENU, true);
    Key('W', true);
    Key('W', false);
    Key(VK_MENU, false);
    Pump(200);
    auto cancelled = Text();
    Pump(1000);
    check("Alt+W cancels during self-echo guard", Text() == cancelled && cancelled.size() < 26);
    check("cancel releases held letter",
          !(GetAsyncKeyState('A') & 0x8000) && !(GetAsyncKeyState('B') & 0x8000));
    Command("SET_TEST_ENTRY zzzzzzzzzzzzzzzzzzzz");
    Command("GHOST_WRITE");
    Pump(450);
    Command("RESET");
    Pump(100);
    auto resetText = Text();
    Pump(700);
    check("Reset cancels active writer", Text() == resetText);
    SetWindowTextW(edit, L"");
    Command("SET_TEST_ENTRY ffffffffffffffffffffffffffffffff");
    Command("GHOST_WRITE");
    Pump(600);
    HWND other = CreateWindowW(L"STATIC", L"Other external test window", WS_OVERLAPPEDWINDOW, 700, 200, 200,
                               100, nullptr, nullptr, wc.hInstance, nullptr);
    ShowWindow(other, SW_SHOW);
    SetForegroundWindow(other);
    Pump(100);
    check("external focus change delivered", GetForegroundWindow() == other);
    SetForegroundWindow(window);
    SetFocus(edit);
    Pump(100);
    auto afterFocus = Text();
    Pump(900);
    check("focus loss cancels even after focus returns", Text() == afterFocus);
    DestroyWindow(other);
    Command("RESET");
    Command("MODE exclusive");
    RECT rc{};
    GetWindowRect(overlay, &rc);
    SetCursorPos(rc.left + 20, rc.top + 70);
    INPUT mouse[2]{};
    mouse[0].type = INPUT_MOUSE;
    mouse[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    mouse[1] = mouse[0];
    mouse[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, mouse, sizeof(INPUT));
    Pump(100);
    check("click activates exclusive target", Command("STATUS").find("TARGET=OVERLAY") != std::string::npos);
    Command("HIDE");
    check("hide releases exclusive target", Command("STATUS").find("TARGET=NONE") != std::string::npos);
    Command("RESET");
    Command("SHOW");
    DestroyWindow(window);
    std::printf("runtime_test: %d failure(s)\n", failed);
    return failed ? 1 : 0;
}
