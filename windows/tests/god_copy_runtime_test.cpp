// Interactive desktop fixture: run alongside a daemon. Never run against a real user document.
#include "common.h"
#include <cstdio>
#include <string>
namespace {
WNDPROC original;
LRESULT CALLBACK BlockCopy(HWND w, UINT m, WPARAM a, LPARAM b) {
    if (((m == WM_KEYDOWN || m == WM_SYSKEYDOWN) && a == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) ||
        (m == WM_CHAR && a == 3)) return 0;
    if (m == WM_COPY || m == WM_CUT || m == WM_PASTE) return 0;
    return CallWindowProcW(original, w, m, a, b);
}
LRESULT CALLBACK Receiver(HWND w, UINT m, WPARAM a, LPARAM b) { return DefWindowProcW(w, m, a, b); }
void Pump(DWORD ms) {
    auto until = GetTickCount64() + ms;
    do {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        Sleep(2);
    } while (GetTickCount64() < until);
}
void Key(WORD vk, bool down) {
    INPUT i{}; i.type = INPUT_KEYBOARD; i.ki.wVk = vk; i.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &i, sizeof(i)); Pump(25);
}
void Chord(WORD key, bool alt) {
    Key(VK_LCONTROL, true);
    if (alt) Key(VK_LMENU, true);
    Key(key, true); Key(key, false);
    if (alt) Key(VK_LMENU, false);
    Key(VK_LCONTROL, false);
}
std::wstring Clipboard() {
    std::wstring text;
    if (OpenClipboard(nullptr)) {
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h) { auto p = static_cast<const wchar_t*>(GlobalLock(h)); if (p) { text = p; GlobalUnlock(h); } }
        CloseClipboard();
    }
    return text;
}
void ClearClipboard(HWND w) { if (OpenClipboard(w)) { EmptyClipboard(); CloseClipboard(); } }
std::wstring Text(HWND w) { wchar_t text[512]{}; GetWindowTextW(w, text, 512); return text; }
}
int main() {
    if (!FindWindowW(L"CronqOverlayWindow", nullptr)) { std::puts("FAIL: start daemon first"); return 1; }
    WNDCLASSW wc{}; wc.lpfnWndProc = Receiver; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"GodCopyFixture";
    RegisterClassW(&wc);
    HWND window = CreateWindowW(wc.lpszClassName, L"God-copy test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                50, 50, 600, 400, nullptr, nullptr, wc.hInstance, nullptr);
    HWND edit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE,
                              10, 10, 550, 300, window, nullptr, wc.hInstance, nullptr);
    original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(BlockCopy)));
    SetForegroundWindow(window); SetFocus(edit); Pump(200);
    for (WORD vk : {VK_LMENU, VK_RMENU, VK_MENU, VK_LCONTROL, VK_RCONTROL, VK_CONTROL,
                    VK_LSHIFT, VK_RSHIFT, VK_SHIFT, VK_LWIN, VK_RWIN}) Key(vk, false);
    int failures = 0;
    auto check = [&](const char* name, bool ok) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", name); if (!ok) ++failures; };
    const std::wstring sample = L"selected \u03A9 \u4E2D \U0001F600\r\nnext line";
    SetWindowTextW(edit, (L"prefix " + sample + L" suffix").c_str());
    SendMessageW(edit, EM_SETSEL, 7, 7 + sample.size());
    ClearClipboard(window);
    SendMessageW(edit, WM_COPY, 0, 0);
    check("fixture blocks WM_COPY", Clipboard().empty());
    Chord('C', false); Pump(150);
    check("fixture blocks normal Ctrl+C", Clipboard().empty());
    Chord('C', true);
    for (int n = 0; n < 100 && Clipboard() != sample; ++n) Pump(50);
    check("Ctrl+Alt+C copies exact selection including Unicode and CRLF", Clipboard() == sample);
    ClearClipboard(window);
    SetWindowTextW(edit, L""); SetFocus(edit);
    Chord('V', true); Pump(2000);
    check("Ctrl+Alt+V uses retained text after clipboard clearing, bypassing WM_PASTE", Text(edit) == sample);
    check("retained paste does not republish clipboard", Clipboard().empty());
    // A failed copy must preserve the prior buffer, and must never copy the entire document.
    SetWindowTextW(edit, L"unselected document"); SendMessageW(edit, EM_SETSEL, 0, 0);
    Chord('C', true); Pump(1000);
    check("no-selection copy leaves clipboard empty", Clipboard().empty());
    SetWindowTextW(edit, L""); Chord('V', true); Pump(2000);
    check("no-selection failure preserves earlier retained text", Text(edit) == sample);
    // Password content is never read, and a failed request preserves the earlier retained copy.
    SetWindowLongPtrW(edit, GWL_STYLE, GetWindowLongPtrW(edit, GWL_STYLE) | ES_PASSWORD);
    SetWindowTextW(edit, L"password fixture"); SendMessageW(edit, EM_SETSEL, 0, -1);
    Chord('C', true); Pump(1000);
    check("password copy leaves clipboard empty", Clipboard().empty());
    SetWindowLongPtrW(edit, GWL_STYLE, GetWindowLongPtrW(edit, GWL_STYLE) & ~ES_PASSWORD);
    SetWindowTextW(edit, L""); Chord('V', true); Pump(2000);
    check("password copy preserves earlier retained text", Text(edit) == sample);
    // Keep the clipboard locked longer than the retry deadline. Selection must still be retained.
    const std::wstring busyText = L"clipboard locked new selection";
    SetWindowTextW(edit, busyText.c_str()); SendMessageW(edit, EM_SETSEL, 0, -1);
    bool held = OpenClipboard(window) != FALSE;
    check("fixture locks clipboard", held);
    Chord('C', true); Pump(1800);
    if (held) CloseClipboard();
    SetWindowTextW(edit, L""); Chord('V', true); Pump(2000);
    check("clipboard contention still retains the new selection", Text(edit) == busyText);
    // Restore original selected text for the focus-cancellation check.
    SetWindowTextW(edit, sample.c_str()); SendMessageW(edit, EM_SETSEL, 0, -1);
    Chord('C', true); Pump(1000);
    // Switching focus within the same top-level window must cancel a paste.
    HWND second = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE, 10, 320, 300, 25,
                                window, nullptr, wc.hInstance, nullptr);
    SetFocus(edit); SetWindowTextW(edit, L"");
    Chord('V', true); Pump(60); SetFocus(second); Pump(1000);
    check("paste stops when focused control changes", Text(second).empty() && Text(edit) != sample);
    // Wrong modifiers and AltGr must not activate the copy feature.
    SetFocus(edit); SetWindowTextW(edit, sample.c_str()); SendMessageW(edit, EM_SETSEL, 0, -1);
    ClearClipboard(window);
    Key(VK_LSHIFT, true); Chord('C', true); Key(VK_LSHIFT, false); Pump(700);
    check("Ctrl+Alt+Shift+C does not copy", Clipboard().empty());
    Key(VK_LCONTROL, true); Key(VK_RMENU, true); Key('C', true); Key('C', false);
    Key(VK_RMENU, false); Key(VK_LCONTROL, false); Pump(700);
    check("AltGr+C does not activate god-copy", Clipboard().empty());
    // Locking must erase the internal copy, not merely disable the hotkey.
    Key(VK_LMENU, true); Key('K', true); Key('K', false); Key(VK_LMENU, false); Pump(200);
    Chord('C', true); Pump(500);
    check("locked shortcut does not copy", Clipboard().empty());
    Key(VK_LMENU, true); Key('K', true); Key('X', true); Key('X', false); Key('K', false); Key(VK_LMENU, false);
    Pump(200); SetForegroundWindow(window); SetFocus(edit); SetWindowTextW(edit, L"");
    Chord('V', true); Pump(1000);
    check("lock erases retained text", Text(edit).empty());
    DestroyWindow(window);
    return failures ? 1 : 0;
}
