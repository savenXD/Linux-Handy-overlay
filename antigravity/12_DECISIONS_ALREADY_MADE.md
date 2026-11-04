# Decisions already made (do not re-decide)

Each entry: the decision, the reason, and what would justify revisiting it (only the owner can approve a change).

## Architecture

| # | Decision | Reason |
| --- | --- | --- |
| 1 | The Windows app is a **separate implementation** in `windows/` (C++17, Win32, WebView2). It shares no `.c` files with Linux. Shared: the written spec (`shared/spec`), sample config, visual design (CSS/SVG copied). | Linux code is X11/GTK/WebKit all the way down; sharing source would risk Linux regressions. The owner demanded isolation. |
| 2 | **C++**, not C. | WebView2's COM interfaces are painful in C. |
| 3 | **WebView2** (Edge/Chromium) is the browser engine. | WebKitGTK does not exist on Windows; WebView2 is Microsoft's supported embedding of the engine every Windows 10/11 machine can have. |
| 4 | The whole overlay UI is **HTML/CSS/JS** in a dedicated "UI" WebView2, served from `<exe dir>\ui` via a virtual host. The existing GDI drawing stays only as a fallback when WebView2 is unavailable. | Matches the GTK-CSS look easily, reuses the SVG icons, far less native drawing code. |
| 5 | **Windowed hosting** of WebView2 (child HWNDs), not composition hosting. | Simplest and most likely to work first time. Composition hosting is the documented fallback (Phase 2 playbook) if the owner reports blank content or focus problems. |
| 6 | One WebView2 **environment** and one user-data folder `%LOCALAPPDATA%\cronq\webview` for Browser, ChatGPT, popup. | Same as Linux's shared WebKit context; logins persist automatically. |
| 7 | Single-threaded GUI. Keyboard/mouse hooks run on **their own thread** (Phase 3 task 3.0), post messages to the UI thread. | Hook timeout safety. |
| 8 | IPC = **named pipe** `\\.\pipe\cronq-<user>`, line protocol identical to Linux. Single instance via `FILE_FLAG_FIRST_PIPE_INSTANCE`. | Natural Windows equivalent of the Unix socket. |
| 9 | Two executables: `cronq.exe` (console CLI) and `cronqw.exe` (no console, the overlay). Same source (`main.cpp`) built twice. | Console programs print; GUI programs must not flash a console window at login. |
| 10 | All native→UI and UI→native messages are **flat JSON**; parsed by our own `jsonlite` (no third-party JSON library). | No new dependency; flat messages are enough. |

## Behaviour

| # | Decision | Reason |
| --- | --- | --- |
| 11 | Hotkeys are `Alt+key`, swallowed by a **low-level keyboard hook**, with `GetAsyncKeyState` for Ctrl/Shift/Alt and the VK_E8 "menu suppressor" so the app below does not open its menu. | Tested in Phase 1. |
| 12 | Default corner and maximise-height use the monitor **work area**; fullscreen uses the full monitor. | Keep the taskbar Start button reachable. |
| 13 | Config pixel sizes are scaled by monitor DPI; the process is per-monitor-v2 DPI aware. | Otherwise the "phone" is tiny on 4K laptops. |
| 14 | The home screen is **compact (280 px tall)**; other apps expand to 640 — exactly Linux. | Parity. |
| 15 | Window buttons: **fullscreen** and **close (hides, does not quit)** — Linux has exactly these two. (The Phase 1 GDI placeholder drew three; fix it in Phase 2.) | Parity with `window_init`. |
| 16 | New-window handling in WebView2 uses a heuristic: auth-looking URI or non-user-initiated → popup modal (keeps `window.opener` for OAuth); plain user link click → same view. | WebView2 does not expose Linux's `nav_type`. |
| 17 | Typing into the overlay is delivered with **DevTools Protocol** `Input.dispatchKeyEvent` + `Emulation.setFocusEmulationEnabled`. | The overlay never has OS focus. |
| 18 | Mirror = keys pass through **and** are forwarded; Exclusive = keys are swallowed and forwarded. Exclusive auto-releases on idle (60 s), hide, ghost mode, Alt+M, Alt+X, exit. | Mirrors Linux; protects against a dead keyboard. |
| 19 | Snap uses `WDA_EXCLUDEFROMCAPTURE` during the capture only; fallback = hide/show with no activation. | No flicker and no focus change on modern Windows. |
| 20 | Ghost Writer uses `SendInput` with `VkKeyScanExW`; characters not on the layout use `KEYEVENTF_UNICODE`. Every injected event carries `kInjectTag`. Aborts with a toast if the target is elevated and we are not. | Windows equivalent of XTest + keycode remapping; UIPI makes elevated targets impossible. |
| 21 | Config key set and ranges are identical to Linux; the Ghost page stores `[GhostTiming] human/speed/safe` in the same `config.ini`. | Parity. |
| 22 | `--mode m|e` accepted (Linux forwards them literally and fails). | Obvious Linux bug; harmless superset. |

## Process

| # | Decision | Reason |
| --- | --- | --- |
| 23 | Cross-compile with llvm-mingw on Linux; verify with Wine; CI builds MSVC on Windows. Keep CMake valid for both. | We can test a lot without Windows. |
| 24 | No new third-party libraries. Allowed: Win32, COM, WIC (`windowscodecs`), WebView2 SDK. | Fewer unverifiable moving parts. |
| 25 | Static linking for MinGW builds; static CRT for MSVC. | No redistributables to install. |
| 26 | Installer: Inno Setup, per-user, no admin. UIAccess is optional and off by default. | Simplicity; UIAccess needs signing and Program Files. |
| 27 | Phases are done in order; each ends with a report; the owner decides when to start the next. | Control and reviewability. |

## If you need a decision that is not here

Choose the simplest, most defensive option, mark it with `// DECISION:` in the code, list it in the report. For anything that changes architecture, user-visible behaviour or the build, stop and ask.
