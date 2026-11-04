// Shared Win32 includes, message ids and small helpers.
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

#include <cstdint>
#include <string>

// Messages posted to the overlay window (all handled on the UI thread).
#define WM_CRONQ_IPC (WM_APP + 1)     // wParam = ipc::Request*
#define WM_CRONQ_HOTKEY (WM_APP + 2)  // wParam = hotkey::Id
#define WM_CRONQ_QUIT (WM_APP + 3)
#define WM_CRONQ_BROWSER_RECOVER (WM_APP + 8)
#define WM_CRONQ_ALTFIX (WM_APP + 4)  // inject the "menu bar suppressor" key
#define WM_CRONQ_CLICK (WM_APP + 5)   // wParam = isInside (1/0)
#define WM_CRONQ_KEY \
    (WM_APP + 6)  // wParam = vk; lParam: down bit 0, CDP modifiers bits 1-4, locks 6-7, scan 16-23, extended
                  // 24, repeat 25
#define WM_CRONQ_CLIPBOARD (WM_APP + 7)  // wParam = vk (A, C, V, X), lParam = isDown (1/0)

// Marks keyboard events that cronq injected itself, so our own hook ignores them.
constexpr ULONG_PTR kInjectTag = 0x43524F4E;  // 'CRON'

namespace cronq {

std::wstring Widen(const std::string& s);
std::string Narrow(const std::wstring& s);

bool EqNoCase(const std::string& a, const char* b);
std::string Trim(const std::string& s);

// %LOCALAPPDATA%\cronq (logs, webview data, snaps) and %APPDATA%\cronq (config).
// Both are created on first use.
std::wstring DataDir();
std::wstring ConfigDir();

// Timestamped "[HH:MM:SS] [TAG] message" lines in DataDir()\cronq.log, mirrored to stdout.
void Log(const char* tag, const char* fmt, ...);
void LogErr(const char* tag, const char* fmt, ...);

// Microseconds from a monotonic clock.
int64_t NowUs();

}  // namespace cronq
