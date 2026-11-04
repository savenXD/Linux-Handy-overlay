# Linux features → Windows equivalents

This is the master table. For each Linux feature: what Linux uses, where the Linux code is, what Windows uses, and
which phase builds it. "DONE" means it already exists in `windows/` and is tested.

## A. Window and system

| Feature | Linux (tool → file) | Windows replacement | Phase |
| --- | --- | --- | --- |
| Overlay window that WMs cannot manage | GTK popup = override-redirect X11 window → `src/window.c` | Win32 `WS_POPUP` with `WS_EX_TOPMOST \| WS_EX_TOOLWINDOW \| WS_EX_NOACTIVATE \| WS_EX_LAYERED` → `windows/src/overlay.cpp` | 1 DONE |
| Never takes focus | never touches `_NET_ACTIVE_WINDOW`; `focus_on_map=FALSE` | `WM_MOUSEACTIVATE → MA_NOACTIVATE`, `WS_EX_NOACTIVATE`, never call `SetForegroundWindow`/`SetFocus` | 1 DONE (+ safety net in 2) |
| Rounded corners | Cairo + RGBA visual | `SetWindowRgn(CreateRoundRectRgn)` | 1 DONE |
| Opacity (Alt+A/S, `--opacity`) | `gtk_widget_set_opacity` | `SetLayeredWindowAttributes(LWA_ALPHA)` | 1 DONE |
| Click-through "Ghost mode" (Alt+G) | X Shape extension, empty input region → `overlay_set_ghost` | toggle `WS_EX_TRANSPARENT` | 1 DONE |
| Stay above fullscreen / always-on-top apps | restack watcher + token bucket → `src/raise.c` | `SetWinEventHook` + 100 ms watchdog, `SetWindowPos(HWND_TOPMOST, SWP_NOACTIVATE)`, same token bucket → `windows/src/raise.cpp` | 1 DONE |
| Move / resize / double-click maximise height | GTK event handlers in `window.c` | manual drag with mouse capture in `overlay.cpp` (GDI UI). **In Phase 2 the header becomes HTML**, so dragging switches to message-driven polling drag | 1 DONE / 2 |
| Compact home height (280) vs expanded (640) | `overlay_set_compact_mode` | `SetCompactMode` in `overlay.cpp` | 1 DONE |
| Multi-monitor + corner + margin | `calculate_monitor_position` | `CalcPosition` (uses work area, DPI scaled) | 1 DONE |
| DPI scaling | not needed on X11 | per-monitor v2 awareness + `S()` scaling | 1 DONE |

## B. Hotkeys, CLI, process

| Feature | Linux | Windows | Phase |
| --- | --- | --- | --- |
| Global Alt+key hotkeys, swallowed | XInput2 raw events + passive grabs + dedupe → `src/hotkey.c` | `WH_KEYBOARD_LL` hook, `GetAsyncKeyState` for Ctrl/Shift/Alt, swallow key + its release, dummy-key trick so Alt release does not open menus → `windows/src/hotkey.cpp` | 1 DONE |
| Dedupe windows (150/120/200 ms) | `trigger_*` in hotkey.c | `DedupeUs()` in hotkey.cpp | 1 DONE |
| CLI to running instance | Unix socket `~/.local/share/cronq/ipc.sock`, line protocol → `src/main.c` | Named pipe `\\.\pipe\cronq-<user>`, same line protocol → `ipc.cpp`, `app.cpp`, `main.cpp` | 1 DONE |
| Single instance | socket probe | `FILE_FLAG_FIRST_PIPE_INSTANCE` | 1 DONE |
| Supervisor (restart on crash) | `scripts/cronq-supervisor` bash loop | `cronqw.exe --supervise` | 1 DONE |
| Config file | `~/.config/cronq/config.ini` (GKeyFile) | `%APPDATA%\cronq\config.ini`, same keys/ranges → `config.cpp` | 1 DONE |
| Logs | stdout / `cronq.log` | `%LOCALAPPDATA%\cronq\cronq.log` | 1 DONE |
| Autostart | `.desktop` in `~/.config/autostart` | `HKCU\...\Run` entry | 4 |
| Installer / uninstaller (`--purge`) | `install.sh`, `uninstall.sh` | Inno Setup installer + PowerShell dev scripts | 4 |

## C. Browser and apps (Phase 2)

| Feature | Linux | Windows | Phase |
| --- | --- | --- | --- |
| Embedded browser engine | WebKitGTK 4.1 → `src/browser.c` | **WebView2** (Edge/Chromium) | 2 |
| Two web views (Chrome, ChatGPT) sharing one login store | one `WebKitWebContext`, persistent SQLite cookies | one `ICoreWebView2Environment` + one user-data folder `%LOCALAPPDATA%\cronq\webview` | 2 |
| Mobile look | user agent string from config | `put_UserAgent`; DPI-scaled physical bounds (no device emulation, per Phase 2 step 2.3-5) | 2 |
| URL normalisation / search | `load_uri_internal` | port exactly (pure function + unit test) | 2 |
| ChatGPT "Enter sends" user-script | `s_chatgpt_enter_script` | `AddScriptToExecuteOnDocumentCreated` (wrapped, see Phase 2 doc) | 2 |
| ChatGPT domain allowlist + divert to browser | `browser_is_domain_allowed_for_chatgpt`, `on_decide_policy` | port exactly + `NavigationStarting` | 2 |
| Login popups (Google/Microsoft/Apple) shown in a modal inside the overlay | `on_web_view_create`, `overlay_show_popup_modal` | `NewWindowRequested` + a third WebView2 in a modal layer | 2 |
| Universal reset (keeps logins) | `browser_universal_reset` | recreate browser view, reload ChatGPT home | 2 |
| Mute when hidden | `browser_pause/resume` | `ICoreWebView2_8::put_IsMuted` | 2 |
| UI (header pills, URL bar, home screen, bottom nav, toasts, ghost page) | GTK widgets + CSS + SVG in `window.c` | **HTML/CSS/JS** in a UI WebView2 (same colours, same SVG icons) | 2 |

## D. Focus-free input, Snap, Ghost Writer (Phase 3)

| Feature | Linux | Windows | Phase |
| --- | --- | --- | --- |
| Detect clicks inside/outside overlay | XInput2 raw buttons → `input_handle_raw_button` | `WH_MOUSE_LL` hook | 3 |
| Typing target state (NONE / OVERLAY), idle 60 s timeout | `input.c` | port the state machine | 3 |
| Mirror mode (key goes to both) | raw key → synthesised GTK event | hook passes key on + forwards copy to overlay | 3 |
| Exclusive mode (key only to overlay) | `XGrabKeyboard` | hook returns 1 (swallow) + forwards to overlay | 3 |
| Deliver keys into overlay without OS focus | synthetic GDK key event | DevTools protocol `Input.dispatchKeyEvent` + `Emulation.setFocusEmulationEnabled(true)` | 3 |
| Clipboard shield (Ctrl+A/C/V/X act on overlay while typing there) | passive grabs → `hotkey_install_clipboard_shield` | hook swallows + CDP editing `commands` | 3 |
| Image paste into web views | `browser_paste` JS synthetic paste event | same JS, clipboard image read via Win32 + WIC PNG encode | 3 |
| Snap without overlay in it, no focus change | unmap overlay, `gdk_pixbuf_get_from_window` → `overlay_snap` | `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)` + `BitBlt` (fallback: hide) | 3 |
| Snap → clipboard + PNG + prune (keep 20) | `save_and_prune_snap` | CF_DIB + "PNG" clipboard formats, `Pictures\cronq`, prune | 3 |
| Ghost Writer typing engine | XTest + spare keycode remap → `src/ghost.c` | `SendInput` + `VkKeyScanExW`, Unicode fallback | 3 |
| Human timing model, Extra safe typos, pauses, ETA | `ghost.c` constants and functions | port line for line (pure code + unit tests) | 3 |
| Stops if focus changes | `XGetInputFocus` compare | `GetForegroundWindow` compare | 3 |
| Ghost page UI + settings saved to config | `create_ghost_page`, `ghost_set_timing` | HTML page + config writer | 3 |

## E. Packaging (Phase 4)

| Feature | Windows |
| --- | --- |
| Installer | Inno Setup, per-user, no admin |
| WebView2 runtime | detect; install the Evergreen bootstrapper if missing |
| Optional UIAccess build (overlay above elevated apps) | optional, documented, off by default |
| Windows readme with "what it can/cannot bypass" | `windows/README.md` |

## CLI commands (all already work in Phase 1; behaviour of some grows later)

`cronq` (show / launch), `kill|stop|quit`, `show`, `hide`, `toggle`, `status`, `snap`, `--mode [m|e|mirror|exclusive]`,
`--ghost [on|off]`, `--app <home|browser|chatgpt|ghost>`, `--load <url>`, `--back`, `--home`, `--reload`,
`--reset`, `--opacity <v>`, `--size <w> <h>`, `--fullscreen`, `--default-size`, `--get-geometry`, `--get-entry`,
`--set-entry <text>`, `--clear-entry`, `--has-modal`, `--close-modal`, `--ghost-write`, `--paste`, `--reload-config`,
`--help`. Windows extras: `--daemon`, `--supervise`, `--version`.

Commands that are stubs today and must become real: `--snap` (3), `--ghost-write` (3), `--paste` (3), `--load`/`--app`/
`--back`/`--home`/`--reload`/`--reset` (2), `--has-modal`/`--close-modal` (2), `--get-entry`/`--set-entry`/`--clear-entry` (3).

## Config keys (same names and ranges as Linux; `windows/src/config.cpp` already parses all of them)

`[General]` width height margin corner monitor opacity start_visible default_key_mode ·
`[Browser]` home_url search_url_template user_agent chatgpt_allowlist ·
`[Hotkeys]` toggle_key mode_key ghost_key snap_key ghost_writer_key shield_clipboard_keys ·
`[Snap]` snap_scope snap_delay_ms snap_save snap_keep_count ·
`[GhostWriter]` ghost_delay_ms ghost_newline ghost_tab ghost_max_chars ·
`[GhostTiming]` human speed safe (written by the Ghost page; Phase 3 must add reading/writing it).
Alt+A, Alt+S, Alt+F, Alt+D letters are fixed (not configurable) on Linux, so they are fixed on Windows too.
