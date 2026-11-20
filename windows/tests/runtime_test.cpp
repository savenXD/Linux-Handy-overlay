#include "common.h"
#include "ipc.h"
#include <psapi.h>
#include <cstdio>
#include <string>
using namespace cronq;
namespace {
HWND edit = nullptr;
bool altWReceived = false, recordExternalKeys = false;
int externalKeyEvents = 0;
WNDPROC originalEditProc = nullptr;
LRESULT CALLBACK ObserveEdit(HWND w, UINT m, WPARAM a, LPARAM b) {
    if (recordExternalKeys && (m==WM_KEYDOWN || m==WM_SYSKEYDOWN || m==WM_KEYUP || m==WM_SYSKEYUP)) ++externalKeyEvents;
    return CallWindowProcW(originalEditProc,w,m,a,b);
}
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
LRESULT CALLBACK Receiver(HWND w, UINT m, WPARAM a, LPARAM b) {
    if (m == WM_SYSKEYDOWN && a == 'W') { altWReceived = true; return 0; }
    return DefWindowProcW(w, m, a, b);
}
void Key(WORD vk, bool down) {
    INPUT event{};
    event.type = INPUT_KEYBOARD;
    event.ki.wVk = vk;
    event.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &event, sizeof(event));
    Pump(40);
}
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
        const char* commands[] = {"STATUS",          "SET_SIZE 360 400", "SET_OPACITY 0.8", "MODE overlay",
                                  "GHOST on",        "GET_GEOMETRY",     "GHOST off",       "MODE normal",
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
    originalEditProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(edit,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(ObserveEdit)));
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    SetFocus(edit);
    Pump(200);
    Command("RESET");
    Command("SHOW");
    Pump(100);
    check("test receiver has focus", GetForegroundWindow() == window);
    // Commands for retired tools must fail instead of recreating their views or work.
    check("retired typing command rejected", Command("GHOST_WRITE") == "UNKNOWN_COMMAND\n");
    check("retired navigation command rejected", Command("LOAD https://example.com") == "UNKNOWN_COMMAND\n");
    check("retired app names rejected", Command("APP browser") == "UNKNOWN_APP\n" &&
          Command("APP ghost") == "UNKNOWN_APP\n");
    check("rejected commands preserve Home", Command("STATUS").find("APP=home") != std::string::npos);
    SetFocus(window);
    Key(VK_MENU, true); Key('W', true); Key('W', false); Key(VK_MENU, false);
    check("Alt+W reaches the external application", altWReceived);
    SetFocus(edit);
    Command("MODE normal");
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
    check("click inside does not enable typing", Command("STATUS").find("TARGET=NONE") != std::string::npos);
    check("overlay click preserves foreground focus", GetForegroundWindow() == window && GetFocus() == edit);
    SetWindowTextW(edit,L"");
    Key('N',true); Key('N',false);
    wchar_t content[64]{}; GetWindowTextW(edit,content,64);
    for (int n=0; n<20 && !content[0]; ++n) { Pump(10); GetWindowTextW(edit,content,64); }
    if (std::wstring(content)!=L"n") std::printf("normal typing actual UTF-16: %04x %04x; foreground=%p focus=%p\n",unsigned(content[0]),unsigned(content[1]),static_cast<void*>(GetForegroundWindow()),static_cast<void*>(GetFocus()));
    check("normal typing reaches external edit", std::wstring(content)==L"n");
    Key(VK_MENU,true); Key('M',true); Key('M',false); Key(VK_MENU,false);
    check("Alt+M enables overlay typing without clicking", Command("STATUS").find("MODE=OVERLAY TARGET=OVERLAY") != std::string::npos);
    check("Alt+M preserves foreground and edit focus", GetForegroundWindow() == window && GetFocus() == edit);
    SetWindowTextW(edit,L"");
    recordExternalKeys = true; externalKeyEvents = 0;
    Key('A',true); Key('A',false); Key(VK_LSHIFT,true); Key('B',true); Key('B',false); Key(VK_LSHIFT,false);
    Key(VK_LCONTROL,true); Key('A',true); Key('A',false); Key(VK_LCONTROL,false);
    recordExternalKeys = false;
    check("overlay typing delivers no key events to external edit",externalKeyEvents==0);
    GetWindowTextW(edit,content,64);
    check("overlay letters and editing chords never reach external edit", std::wstring(content).empty());
    check("typing leaves foreground unchanged", GetForegroundWindow()==window && GetFocus()==edit);
    Key(VK_MENU,true); Key('M',true); Key('M',false); Key(VK_MENU,false);
    check("second Alt+M returns to normal typing", Command("STATUS").find("MODE=NORMAL TARGET=NONE") != std::string::npos);
    Key('Z',true); Key('Z',false); GetWindowTextW(edit,content,64);
    check("typing resumes in original external edit", std::wstring(content)==L"z");
    // Each physical press toggles once, even when presses are close together.
    INPUT taps[8]{};
    const WORD chordKeys[8]={VK_MENU,'M','M',VK_MENU,VK_MENU,'M','M',VK_MENU};
    for(int n=0;n<8;++n){taps[n].type=INPUT_KEYBOARD;taps[n].ki.wVk=chordKeys[n];taps[n].ki.dwFlags=(n%4>=2)?KEYEVENTF_KEYUP:0;}
    SendInput(8,taps,sizeof(INPUT));Pump(300);
    check("two rapid Alt+M presses both toggle",Command("STATUS").find("MODE=NORMAL TARGET=NONE")!=std::string::npos);
    Key(VK_MENU,true);Key('M',true);Pump(250);Key('M',true);
    check("held Alt+M does not toggle repeatedly",Command("STATUS").find("MODE=OVERLAY TARGET=OVERLAY")!=std::string::npos);
    Key('M',false);Key(VK_MENU,false);
    Command("MODE overlay");
    // An outside click is an explicit return to ordinary typing.
    SetCursorPos(rc.right+30,rc.top+50); SendInput(2,mouse,sizeof(INPUT)); Pump(100);
    check("outside click releases overlay typing", Command("STATUS").find("TARGET=NONE")!=std::string::npos);
    Command("MODE overlay"); Command("HIDE");
    check("hide preserves overlay typing", Command("STATUS").find("MODE=OVERLAY TARGET=OVERLAY")!=std::string::npos);
    // Hidden clicks must release routing even within the former overlay rectangle.
    SetCursorPos(rc.left+30,rc.top+50); SendInput(2,mouse,sizeof(INPUT)); Pump(150);
    check("hidden click releases typing", Command("STATUS").find("TARGET=NONE")!=std::string::npos);
    Command("MODE overlay");
    check("hidden overlay cannot start a new capture", Command("STATUS").find("TARGET=NONE")!=std::string::npos);
    Command("SHOW");
    Command("SET_OPACITY 0.80");Command("GHOST on");
    check("ghost starts at 50 percent",Command("GET_OPACITY")=="0.50\n");
    Command("SET_OPACITY 0.60");Command("GHOST on");
    check("enabled ghost preserves manual opacity",Command("GET_OPACITY")=="0.60\n");
    Command("GHOST off");
    check("leaving ghost restores 100 percent",Command("GET_OPACITY")=="1.00\n");
    Command("GHOST on");
    check("reentering ghost sets 50 percent",Command("GET_OPACITY")=="0.50\n");
    Command("GHOST off");
    Pump(300);
    for(int prompt=0;prompt<3;++prompt) {
        if(prompt==2) Key(VK_ESCAPE,true);
        Key(VK_MENU,true);Key(VK_SHIFT,true);Key('K',true);
        for(int wait=0;wait<50&&!FindWindowW(L"CronqKillPrompt",nullptr);++wait)Pump(20);
        check("kill shortcut opens confirmation",FindWindowW(L"CronqKillPrompt",nullptr)!=nullptr);
        if(!prompt){Key('K',false);Key(VK_SHIFT,false);Key(VK_MENU,false);}
        Key(VK_ESCAPE,true);Key(VK_ESCAPE,false);Pump(100);
        check("Escape removes kill confirmation",FindWindowW(L"CronqKillPrompt",nullptr)==nullptr);
        if(prompt){Key('K',false);Key(VK_SHIFT,false);Key(VK_MENU,false);}
        Pump(350);
    }
    Command("RESET");
    Command("SHOW");
    DestroyWindow(window);
    std::printf("runtime_test: %d failure(s)\n", failed);
    return failed ? 1 : 0;
}
