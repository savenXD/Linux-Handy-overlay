#include "god_copy.h"
#include "god_copy_protocol.h"
#include "ghost.h"
#include "hotkey.h"
#include "overlay.h"
#include <vector>

namespace cronq::god_copy {
namespace {
constexpr UINT_PTR kTimer = 1021;
HANDLE g_process = nullptr, g_mapping = nullptr, g_job = nullptr;
Packet* g_packet = nullptr;
HWND g_target = nullptr, g_focus = nullptr;
DWORD g_pid = 0;
ULONGLONG g_started = 0, g_clipboardDeadline = 0;
std::wstring g_retained;
size_t g_position = 0;
bool g_pasting = false, g_startedPaste = false;

void Erase(std::wstring& text) {
    if (!text.empty()) SecureZeroMemory(text.data(), text.size() * sizeof(wchar_t));
    text.clear();
}
bool ModifiersHeld() {
    for (int vk : {VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN, VK_RWIN})
        if (GetAsyncKeyState(vk) & 0x8000) return true;
    return false;
}
bool Target() {
    g_target = GetForegroundWindow();
    if (!g_target || g_target == overlay::Hwnd()) return false;
    DWORD thread = GetWindowThreadProcessId(g_target, &g_pid);
    if (!thread || g_pid == GetCurrentProcessId()) return false;
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    if (!GetGUIThreadInfo(thread, &info) || !info.hwndFocus) return false;
    g_focus = info.hwndFocus;
    return true;
}
bool TargetUnchanged() {
    DWORD pid = 0;
    if (GetForegroundWindow() != g_target || !IsWindow(g_target)) return false;
    DWORD thread = GetWindowThreadProcessId(g_target, &pid);
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    return pid == g_pid && GetGUIThreadInfo(thread, &info) && info.hwndFocus == g_focus;
}
void CloseWorker() {
    if (g_process) {
        if (WaitForSingleObject(g_process, 0) == WAIT_TIMEOUT) TerminateProcess(g_process, 3);
        CloseHandle(g_process);
        g_process = nullptr;
    }
    if (g_job) { CloseHandle(g_job); g_job = nullptr; }
    if (g_packet) {
        SecureZeroMemory(g_packet, sizeof(Packet));
        UnmapViewOfFile(g_packet);
        g_packet = nullptr;
    }
    if (g_mapping) { CloseHandle(g_mapping); g_mapping = nullptr; }
}
void Stop() {
    KillTimer(overlay::Hwnd(), kTimer);
    CloseWorker();
    g_pasting = false;
    g_clipboardDeadline = 0;
}
void Fail(const char* message) {
    Stop();
    Log("GODCOPY", "%s", message); // Never log copied text.
    overlay::ShowToast(message);
}
bool PublishClipboard() {
    const SIZE_T bytes = (g_retained.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) return false;
    void* data = GlobalLock(memory);
    if (!data) { GlobalFree(memory); return false; }
    memcpy(data, g_retained.c_str(), bytes);
    GlobalUnlock(memory);
    bool success = false;
    if (OpenClipboard(overlay::Hwnd())) {
        if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory)) success = true;
        CloseClipboard();
    }
    if (!success) GlobalFree(memory);
    return success;
}
const char* Failure(Result r) {
    switch (r) {
        case Result::NoSelection: return "God-copy: no text selected; previous copy unchanged";
        case Result::Password: return "God-copy: password field excluded; previous copy unchanged";
        case Result::Changed: return "God-copy: focus changed; previous copy unchanged";
        case Result::TooLarge: return "God-copy: selection too large; previous copy unchanged";
        case Result::Unsupported: return "God-copy: app does not expose its selection; previous copy unchanged";
        default: return "God-copy: selection could not be read; previous copy unchanged";
    }
}
void CALLBACK Tick(HWND, UINT, UINT_PTR, DWORD) {
    if (hotkey::IsLocked()) { Cleanup(); return; }
    if (g_process) {
        DWORD wait = WaitForSingleObject(g_process, 0);
        if (wait == WAIT_TIMEOUT) {
            if (!TargetUnchanged()) Fail(Failure(Result::Changed));
            else if (GetTickCount64() - g_started > 4000)
                Fail("God-copy: app timed out; previous copy unchanged");
            return;
        }
        DWORD code = 1;
        GetExitCodeProcess(g_process, &code);
        if (wait != WAIT_OBJECT_0 || code != 0 || !TargetUnchanged()) {
            Fail(Failure(Result::Changed)); return;
        }
        if (!Valid(*g_packet)) { Fail(Failure(g_packet->result)); return; }
        Log("GODCOPY", "Selection read via %s (%lu UTF-16 units)",
            g_packet->backend == 1 ? "native EDIT" : "UI Automation", static_cast<unsigned long>(g_packet->length));
        Erase(g_retained);
        g_retained.assign(g_packet->text, g_packet->length);
        CloseWorker();
        g_clipboardDeadline = GetTickCount64() + 1000;
    }
    if (g_clipboardDeadline) {
        if (PublishClipboard()) {
            Stop();
            overlay::ShowToast("God-copy: saved internally and sent to clipboard");
        } else if (GetTickCount64() >= g_clipboardDeadline) {
            Stop();
            overlay::ShowToast("God-copy: saved internally; clipboard busy. Use Ctrl+Alt+V");
        }
        return;
    }
    if (!g_pasting) return;
    if (!TargetUnchanged() || ghost::IsTyping()) {
        Fail("God-paste stopped: focus changed or another writer started"); return;
    }
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { Fail("God-paste cancelled"); return; }
    if (ModifiersHeld()) {
        if (g_startedPaste || GetTickCount64() - g_started > 2000)
            Fail("God-paste stopped: release modifier keys");
        return;
    }
    g_startedPaste = true;
    // One UTF-16 scalar per tick: focus is rechecked before each scalar, never Tab/Backspace commands.
    // CRLF becomes one Return. Tab is a Unicode text packet to avoid moving to the next control.
    wchar_t c = g_retained[g_position];
    if (c == L'\r' && g_position + 1 < g_retained.size() && g_retained[g_position + 1] == L'\n')
        ++g_position;
    INPUT events[4]{};
    UINT count = 2;
    events[0].type = INPUT_KEYBOARD;
    events[0].ki.dwExtraInfo = kInjectTag;
    events[0].ki.wScan = c == L'\n' ? L'\r' : c;
    events[0].ki.dwFlags = KEYEVENTF_UNICODE;
    events[1] = events[0];
    events[1].ki.dwFlags |= KEYEVENTF_KEYUP;
    if (c >= 0xD800 && c <= 0xDBFF && g_position + 1 < g_retained.size()) {
        events[2] = events[0]; events[2].ki.wScan = g_retained[g_position + 1];
        events[3] = events[2]; events[3].ki.dwFlags |= KEYEVENTF_KEYUP;
        count = 4;
    }
    UINT sent = SendInput(count, events, sizeof(INPUT));
    if (sent != count) {
        // A partial insertion can leave a key down. Release only that injected key; do not retry text.
        if (sent & 1) SendInput(1, &events[sent], sizeof(INPUT));
        Fail("God-paste: input blocked or partial; text retained. Target acceptance unverified"); return;
    }
    g_position += count / 2;
    if (g_position == g_retained.size()) {
        Stop();
        overlay::ShowToast("God-paste: input sent; verify the text in the destination");
    }
}
bool StartTimer() { return SetTimer(overlay::Hwnd(), kTimer, 15, Tick) != 0; }
bool Busy() {
    if (g_process || g_pasting || g_clipboardDeadline || ghost::IsTyping() || ghost::IsGuardActive()) {
        overlay::ShowToast("God-copy: wait for the current operation to finish");
        return true;
    }
    return false;
}
}

void Copy() {
    if (hotkey::IsLocked() || Busy()) return;
    if (!Target()) { overlay::ShowToast("God-copy: focus a text selection in another app"); return; }
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    g_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &security, PAGE_READWRITE, 0, sizeof(Packet), nullptr);
    if (!g_mapping) { Fail(Failure(Result::Failed)); return; }
    g_packet = static_cast<Packet*>(MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Packet)));
    if (!g_packet) { Fail(Failure(Result::Failed)); return; }
    ZeroMemory(g_packet, sizeof(Packet));
    g_packet->magic = kMagic;
    g_packet->window = reinterpret_cast<uintptr_t>(g_target);
    g_packet->focus = reinterpret_cast<uintptr_t>(g_focus);
    g_packet->process = g_pid;
    wchar_t exe[32768];
    DWORD n = GetModuleFileNameW(nullptr, exe, 32768);
    if (!n || n >= 32768) { Fail(Failure(Result::Failed)); return; }
    std::wstring command = L"\"" + std::wstring(exe, n) + L"\" --god-copy-worker " +
                           std::to_wstring(reinterpret_cast<uintptr_t>(g_mapping));
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> attributes(bytes);
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &bytes)) {
        Fail(Failure(Result::Failed)); return;
    }
    BOOL updated = UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                             &g_mapping, sizeof(g_mapping), nullptr, nullptr);
    PROCESS_INFORMATION pi{};
    BOOL created = updated && CreateProcessW(exe, command.data(), nullptr, nullptr, TRUE,
                         CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &si.StartupInfo, &pi);
    DeleteProcThreadAttributeList(si.lpAttributeList);
    SetHandleInformation(g_mapping, HANDLE_FLAG_INHERIT, 0);
    if (!created) { Fail(Failure(Result::Failed)); return; }
    g_process = pi.hProcess;
    g_job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!g_job || !SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
        !AssignProcessToJobObject(g_job, g_process) || ResumeThread(pi.hThread) == static_cast<DWORD>(-1)) {
        CloseHandle(pi.hThread);
        Fail("God-copy: could not isolate selection helper"); return;
    }
    CloseHandle(pi.hThread);
    g_started = GetTickCount64();
    if (!StartTimer()) { Fail("God-copy: could not start completion timer"); return; }
    overlay::ShowToast("God-copy: reading selected text...");
}
void Paste() {
    if (hotkey::IsLocked() || Busy()) return;
    if (g_retained.empty()) { overlay::ShowToast("God-paste: nothing retained; copy a selection first"); return; }
    if (!Target()) { overlay::ShowToast("God-paste: focus a destination in another app"); return; }
    g_position = 0;
    g_pasting = true;
    g_startedPaste = false;
    g_started = GetTickCount64();
    if (!StartTimer()) Fail("God-paste: could not start input timer");
}
bool IsBusy() { return g_process || g_pasting || g_clipboardDeadline; }
void Cleanup() { Stop(); Erase(g_retained); }
}
