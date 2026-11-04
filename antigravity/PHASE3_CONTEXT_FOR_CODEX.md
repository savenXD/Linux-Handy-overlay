# Phase 3 Context for Codex

> Historical phase handoff. The combined fixes and current verification are in [REPORT_INTEGRATION.md](REPORT_INTEGRATION.md). Its results supersede the claims below.

## Overview
This document was prepared by **Gemini 3.8 Flash** for **Codex** (and any collaborating agents) to explain everything implemented in **Phase 3** of the Windows port of Cronq.

Phase 3 implements the three "focus-free" core subsystems:
1. **Focus-Free Overlay Typing** (Mirror Mode & Exclusive Mode routing into WebView2 without taking OS focus).
2. **Snap Screenshot Engine** (Native Win32 display affinity `WDA_EXCLUDEFROMCAPTURE`, WIC PNG encoding, clipboard, and file retention).
3. **Ghost Writer Engine** (Human-like automated typing into external applications via SendInput, timing models, typo generation, and UIPI safety guards).

All code is written in C++17 Win32, completely modular, compiles with zero warnings under MinGW, and keeps Linux files 100% untouched.

---

## 1. Architecture & Threading Model (Task 3.0 & 3.2)

### Hook Worker Thread (`windows/src/hotkey.cpp`, `hotkey.h`)
- Windows removes low-level hooks (`WH_KEYBOARD_LL`, `WH_MOUSE_LL`) if they take longer than ~300ms. Because WebView2 runs on the UI thread, the hooks run on a **dedicated background hook thread** with its own `GetMessage` message pump and a 15-second hook watchdog timer.
- Shared thread flags are synchronized atomically via `InterlockedExchange` / `InterlockedCompareExchange`:
  - `g_typingTarget`: `0` = None, `1` = Overlay.
  - `g_keyMode`: `0` = Mirror, `1` = Exclusive.
  - `g_shieldEnabled`: boolean (swallows Ctrl+A/C/V/X to protect host clipboard).
  - `g_guardActive`: boolean (Ghost Writer self-echo suppression).
  - Overlay bounding rectangle (`g_rectLeft`, `g_rectTop`, `g_rectRight`, `g_rectBottom`).
- The hook thread **never blocks** and only classifies events and posts asynchronous Win32 messages to the overlay window:
  - `WM_CRONQ_CLICK (WM_APP + 5)`: Mouse button down (`wParam`: `1` if inside overlay rect, `0` if outside).
  - `WM_CRONQ_KEY (WM_APP + 6)`: Keystroke forwarded for overlay typing (`wParam` = VK, `lParam` = scan code & flags).
  - `WM_CRONQ_CLIPBOARD (WM_APP + 7)`: Swallowed clipboard shortcut (`wParam` = 'A'|'C'|'V'|'X', `lParam` = 1 down / 0 up).
  - `WM_CRONQ_HOTKEY (WM_APP + 2)`: Global Alt+<key> triggers.
  - `WM_CRONQ_ALTFIX (WM_APP + 4)`: Injects dummy key `0xE8` to suppress Windows Alt-menu opening in the foreground app.

### Input Routing & State Machine (`windows/src/input.cpp`, `input.h`)
- Manages `TypingTarget` (None / Overlay), `KeyMode` (Mirror / Exclusive), `GhostMode` (Solid / Click-through), and `GrabStatus` (None / Active / Failed).
- **Mouse Click Routing:**
  - Click inside overlay -> Sets target to `Overlay`, engages Exclusive swallow if enabled, and resets the 60-second idle timer. (Ignored if Ghost click-through mode is active).
  - Click outside overlay -> Releases target back to `None`.
- **Auto-Release Triggers (Safety Nets against keyboard swallow traps):**
  - Overlay hidden (`overlay::Hide()` -> `input::NotifyOverlayHidden()`).
  - Ghost click-through toggled on.
  - Ghost Writer started (`ghost::Run()`).
  - Hotkey toggle (`Alt+X`).
  - 60-second idle timeout (`input::OnIdleTimerExpired()`).
  - Daemon exit.

---

## 2. Key Injection into WebView2 without OS Focus (Task 3.3)

### Focus Emulation
- On every controller created (`ui`, `browser`, `chatgpt`, `popup`), `Emulation.setFocusEmulationEnabled` is invoked with `{"enabled":true}` via CDP.
- This ensures web pages show active carets, `:focus` styling, and accept synthetic key events without Cronq needing to call `SetForegroundWindow()`.

### Key Translation (`windows/src/key_translate.cpp`, `key_translate.h`)
- Pure virtual-key translator converting Win32 VKs and scan codes to Chrome DevTools Protocol `Input.dispatchKeyEvent` payloads:
  - Printable keys -> `type: "keyDown"`, `text`, `unmodifiedText`, `key`, `code`, `windowsVirtualKeyCode`, `modifiers`.
  - Non-printable keys (Enter, Tab, Backspace, Esc, Arrows, Delete, Home/End, PageUp/Down) -> `type: "rawKeyDown"` with proper CDP key and code names.
  - Modifiers: Shift (`8`), Ctrl (`2`), Alt (`1`), Meta (`4`).
  - Auto-repeat detection.

### Browser Dispatch (`windows/src/browser.cpp`, `browser.h`)
- `browser::DispatchKeyEvent(vk, lParam)`: Resolves active controller slot (`popup` > `chatgpt` / `browser` / `ui`) and sends `Input.dispatchKeyEvent`.
- `browser::DispatchClipboardAction(vk, lParam)`: Sends `commands: ["selectAll" | "copy" | "paste" | "cut"]` to the active controller. If action is paste and clipboard holds an image, routes to `PasteImageOrText()`.
- `browser::SetUrlInputFocused(bool)`: Controlled by UI bridge `{"type":"focus","where":"url"|"page"}` so keystrokes land in the URL input bar rather than web content when typing in browser mode.

---

## 3. Synthetic Image & Text Paste (`windows/src/browser.cpp`, Task 3.4)

- `browser::PasteImageOrText()`:
  1. Checks Windows Clipboard for registered `"PNG"` or `CF_DIB` format.
  2. For DIB, uses Windows Imaging Component (WIC) to encode to PNG bytes in memory.
  3. Base64 encodes the PNG bytes (`snap_logic::Base64Encode`).
  4. If no image on clipboard, checks for `%LOCALAPPDATA%\cronq\latest_snap.png` as fallback.
  5. Injects the verbatim synthetic `ClipboardEvent('paste')` JavaScript script into the active WebView2 controller, dispatching a synthetic `File` via `DataTransfer`.
  6. If no image is available, falls back to standard text paste.

---

## 4. Snap Screenshot Subsystem (`windows/src/snap.cpp`, `snap.h`, `snap_logic.cpp`, `snap_logic.h`, Task 3.5)

- **Entry Point:** `snap::Run()` (triggered by `Alt+P`, CLI `cronq snap`, or UI Snap button).
- **Overlay Exclusion:**
  - Tries `SetWindowDisplayAffinity(overlayHwnd, WDA_EXCLUDEFROMCAPTURE)` (0x11, Windows 10 2004+).
  - If affinity returns `FALSE` (older Windows or Wine), falls back cleanly to Linux's method: `ShowWindow(SW_HIDE)`, captures after settling timer, and restores visibility with `SWP_NOACTIVATE | SWP_SHOWWINDOW`.
- **Capture:** `BitBlt` with `CAPTUREBLT | SRCCOPY` from virtual screen or primary monitor DC into 32-bit BGRA buffer. Sets alpha to 0xFF.
- **WIC PNG Encoding:** In-memory encoding using `IWICImagingFactory`, `IWICBitmapEncoder`, and `IWICStream`.
- **Clipboard:** Posts both standard `CF_DIB` (bottom-up DIB) and registered `"PNG"` format to Windows Clipboard.
- **Disk Storage & Retention:**
  - Saved to `%USERPROFILE%\Pictures\cronq\snap-YYYYMMDD-HHMMSS.png` (collision avoidance: `-1`, `-2` via `snap_logic::PickSnapName`).
  - Latest copy copied to `%LOCALAPPDATA%\cronq\latest_snap.png`.
  - Oldest files pruned to retain `snapKeepCount` (default 20) via `snap_logic::FilesToPrune`.
- **UI State Notification:** Posts `{"type":"snap-state","state":"snapping"}` -> captures -> `{"type":"snap-state","state":"done"}` (shows checkmark for 1500ms) -> `{"type":"snap-state","state":"camera"}`.

---

## 5. Ghost Writer Engine (`windows/src/ghost.cpp`, `ghost.h`, `ghost_logic.cpp`, `ghost_logic.h`, Task 3.6)

- **Entry Point:** `ghost::Run()` (triggered immediately by `Alt+W` or `--ghost-write`) or `ghost::OnGhostStartRequested()` (triggered by UI Start button with 3..2..1 countdown).
- **Pure Models (`ghost_logic`):**
  - Timing model: Slow (40 WPM / 300ms base), Moderate (104 WPM / 115ms base), Fast (141 WPM / 85ms base).
  - Log-normal distribution: Box-Muller transform for natural human variance.
  - State machine: `Typist` with drift factor (0.85 to 1.20), burst typing (15 to 45 chars at 0.72x delay), line pauses (2 to 7 seconds every 3 to 6 lines), and thinking pauses.
  - Extra Safe Mode: Natural typo generator and backspace correction queue.
  - Normalization: Strips trailing `\n` to prevent accidental form submission in external applications. Preserves internal line breaks.
- **Win32 Input Delivery:**
  - Foreground window target check (`GetForegroundWindow()`).
  - UIPI check (`OpenProcessToken` + `TokenElevation`): Aborts safely with `Target runs as administrator` toast if target is elevated and Cronq is not.
  - CapsLock check: Turns off CapsLock before typing and restores it afterwards.
  - Self-echo guard: Sets `g_guardActive = true` so the hotkey hook ignores injected keystrokes; disengages with a delayed timer.
  - Settle phase: Polls `GetAsyncKeyState` up to 24x10ms to ensure user released Ctrl/Alt/Win before typing starts.
  - Non-blocking state machine driven by `WM_TIMER`: CHAR -> PRESS -> RELEASE.
  - Character mapping: Uses `VkKeyScanExW` for layout-mapped keys, falls back to `KEYEVENTF_UNICODE` for emojis, CJK, and complex characters.
  - Focus change watchdog: If foreground focus changes away from the target window during typing, aborts instantly with toast `Stopped: focus changed`.
- **Settings Persistence (`windows/src/ini_writer.cpp`, `ini_writer.h`):**
  - In-place byte-for-byte INI reader/writer (`[GhostTiming] human speed safe`) in `%APPDATA%\cronq\config.ini` preserving comments, empty lines, and line endings.

---

## 6. Native ⟷ UI Bridge Protocol Extensions (`windows/src/ui_bridge.cpp`, `windows/ui/cronq.js`)

### Messages from UI to Native (`postMessage`)
- `{"type":"snap"}` -> Triggers `snap::Run()`.
- `{"type":"ghost-start"}` -> Triggers `ghost::OnGhostStartRequested()`.
- `{"type":"ghost-text","value":"..."}` -> Synced to `app::SetTestEntryText(value, false)`.
- `{"type":"ghost-settings","human":bool,"speed":0-2,"safe":bool}` -> Updates & saves `[GhostTiming]`.
- `{"type":"focus","where":"url"|"page"}` -> Routes keyboard input to URL bar or web content.

### Messages from Native to UI (`PostToUi`)
- `{"type":"snap-state","state":"camera"|"snapping"|"done"}` -> Updates Snap nav button icon.
- `{"type":"ghost-status","text":"...","eta":"...","button":"Start"|"Cancel","state":"idle"|"typing"|"done"|"failed"}` -> Updates status line, live ETA, Start/Cancel button, and Typer nav icon.
- `{"type":"set-text","value":"..."}` -> Fills Ghost Typer editor from CLI `--set-entry`.
- `{"type":"reset-editor"}` -> Clears editor on Universal Reset.

---

## 7. Zero Collision Guarantee with Phase 2
- Phase 2 webview lifecycle (`CreateCoreWebView2Controller`, environment creation, navigation policy, modal popups, muting) remains 100% intact.
- Phase 3 only added cleanly scoped helper methods to `browser.h` (`DispatchKeyEvent`, `DispatchClipboardAction`, `PasteImageOrText`, `SetUrlInputFocused`) and hooked into `ui_bridge::OnMessage` and `app::HandleCommand`.
- Zero files in `src/`, `Makefile`, `scripts/`, or Linux tests were modified.
