# Phase 3 — Typing without focus, Snap, Ghost Writer

**Goal:** the three "focus-free" core features: (a) type into the overlay while the app underneath keeps focus
(Mirror / Exclusive modes), (b) Snap, (c) Ghost Writer (types into other apps like a human).

Before starting: Phase 2 must be finished and reported. Re-read `03_NON_NEGOTIABLE_RULES.md` and `09_WIN32_AND_COM_PITFALLS.md`.
Linux sources for this phase: see the Phase 3 rows in `10_LINUX_SOURCE_READING_MAP.md` (`src/input.c`, `src/ghost.c`,
`src/window.c` snap + ghost page, `src/hotkey.c` shield).

**Verification reality:** the logic (timing model, text normalisation, key tables, INI writing, file pruning) is **pure and
fully unit-testable**: do that. `SendInput`, hooks and file creation partly run under Wine; real focus/foreground behaviour,
real clipboard exchange with other apps, `WDA_EXCLUDEFROMCAPTURE` and elevated windows are **UNVERIFIED** until the owner tests.

---

## Task 3.0 — Move the keyboard hook to its own thread (robustness refactor of Phase 1 code)

**Why:** a low-level hook that does not return within ~300 ms is silently removed by Windows. With WebView2 on the UI thread a
slow moment would kill hotkeys. A dedicated hook thread has nothing else to do, so it always answers instantly.

**Do:**

1. In `hotkey.cpp` add a hook thread: `CreateThread` → thread function installs `WH_KEYBOARD_LL` (and, in Task 3.2, `WH_MOUSE_LL`),
   runs `GetMessage` loop, owns the 15-second reinstall timer (`SetTimer(nullptr,...)` on that thread), unhooks on `WM_QUIT`.
   `Init()` starts it and waits (event, 2 s) until the hook is installed, returning false on failure. `Cleanup()` posts `WM_QUIT`
   to the thread (`PostThreadMessageW`) and joins (wait ≤ 2 s).
2. State shared between threads must be read inside the hook without blocking: use `volatile LONG`/`InterlockedExchange` for
   flags (typing target, key mode, ghost mode, shield enabled, guard active), and for the hotkey VK table use a small struct
   protected by a `CRITICAL_SECTION` that is held only for a table copy (uncontended = microseconds) or two alternating
   tables with an atomic index. Overlay rectangle: four `volatile LONG`.
3. The hook still only **classifies and posts** (`PostMessageW(overlayHwnd, ...)`); all actions run on the UI thread.
4. `InjectAltSuppressor` (the dummy key that stops the Alt menu) keeps running on the UI thread via `WM_CRONQ_ALTFIX`.
5. **Acceptance:** all 22 smoke checks still pass (they use `sendkeys.exe`, so they exercise the new thread).

If this task turns out to destabilise anything, revert it and report; the rest of Phase 3 can still be done on the UI thread.

## Task 3.1 — Pure modules first (unit-tested)

Write, with tests (`*_test.cpp`, run in `wine-smoke.sh`):

**`src/ghost_logic.h/.cpp`** (namespace `cronq::ghost_logic`) — exact ports of `src/ghost.c`:

- `struct Timing { bool human; int speed; bool safe; int letterMinMs, letterMaxMs, linesMin, linesMax, pauseMinMs, pauseMaxMs; bool preserveLayout; }`
- `Timing Validate(Timing t)`:
  `speed` outside 0..2 → 0; `m = SpeedDelayMs(speed, human)`; `letterMin = max(10, m/2)`, `letterMax = max(10, m*2)`;
  `linesMin=3, linesMax=6, pauseMin=2000, pauseMax=7000, preserveLayout=true`; `safe` normalised to bool.
- `int SpeedDelayMs(int speed, bool human)` = `human ? {300,115,85}[clamp(speed,0,2)] : 0` (`k_mode_delay_ms`).
  `int SpeedWpm(speed, human)` = `d ? (int)(12000.0/d + 0.5) : 0`.
- `std::string NormalizeText(const std::string& in)`: `\r\n` → `\n`; lone `\r` → `\n`; then **strip all trailing `\n`**
  (so a final Return never submits a form). Internal line breaks preserved.
- `Newline/Tab handling` (`char_to_keysym` logic): `\b` = Backspace. `\r` → `\n`. `\n`: cfg `ghost_newline` `skip` → skip char,
  `space` → type a space, else Return. `\t`: `ghost_tab` `skip` / `space` / else Tab. **When `preserveLayout` is true (it always is
  after `Validate`) the config values are overridden to `return` and `tab`** (Linux copies the config and forces these).
- The random engine: write a small class `Rng` with `int Range(int lo, int hi)` (inclusive), `double Unit()`, seedable, so tests are deterministic.
  Runtime seed from `QueryPerformanceCounter`.
- `double RandLogNormal(double median, double sigma)` = `median * exp(sigma * sqrt(-2*ln(u1)) * cos(2*pi*u2))` with
  `u1 ∈ [1e-9,1)`, `u2 ∈ [0,1)`.
- `class Typist` holding the context (text cursor in **code points**, drift, burst_left, last_c, lines_typed, next_pause_lines,
  since_typo=100 initially, plan queue of `{codepoint, count}`) with:
  - `Next()` ≙ `take_next`: queued correction items first; else the next code point; `since_typo++`; **extra safe typo rule**:
    if `human && safe && c in 'a'..'z' && since_typo > 6 && Range(0,99) < 3` then queue: `want = Range(0,2)` following lowercase
    letters `extras` (stop at the first non-lowercase) + `want+1` backspaces + the correct `c` (counted) + the `extras` again
    (counted); `since_typo = 0`; this call returns the **neighbour typo letter** from the table below and is **not counted**.
    Typo neighbours (index = letter − 'a'): `qsz vgn xdv sefc wrd drgv fthb gyjn uok hukm jil kop nj bhm ipl ol wa etf awdx ryg yij cfb qes zsc tuh asx`;
    pick a random character of that string.
  - `int NextIntervalMs(c)` ≙ `next_interval_ms` — port line by line. Constants: `m = SpeedDelayMs`; if not human return `m`; if safe `m *= 1.1`;
    on `' '` `drift = clamp(drift + Range(-0.06, 0.06), 0.85, 1.2)`; `m *= drift`; `d = RandLogNormal(m, 0.32)`; `' '` ×1.2, `'\n'` ×2.2,
    one of `,.;:!?` ×1.5, uppercase ×1.15; same char as last ×0.85; for `speed ≥ 1`: if `burst_left ≤ 0 && c==' ' && Range(0,99) < 10` then
    `burst_left = Range(15,45)`; while `burst_left > 0` ×0.72 and decrement; `d = clamp(d, max(35, m*0.45), m*4.0)`;
    with probability (safe ? 6 : 3)% add `Range(250,800)`; if safe: `r = Range(0,99)`; `' '` and `r<7` add `clamp(RandLogNormal(1000,0.4),500,2600)`;
    `.!?` and `r<25` add `Range(600,1800)`; `, ; :` and `r<12` add `Range(300,900)`; `'\n'` and `r<30` add `Range(800,2500)`.
  - Per key: `lead` = (needs modifier ? `Range(18,45)` human, `5` instant : 0) (plus `max(lead,35)` when the key needs the Unicode path);
    `dwell = clamp(RandLogNormal(min(80, total*0.7), 0.25), 35, 170)` (human) or `8` (instant); `dwell = max(2, min(dwell, total*7/10))`;
    `rest = max(1, total - lead - dwell)`. After a release: if the next planned item is a backspace and the last typed char was not a backspace add `Range(200,599)` ms.
    If human and `lines_typed >= next_pause_lines` and text remains: add `Range(pauseMin, pauseMax)` ms, reset `lines_typed = 0`,
    `next_pause_lines = Range(linesMin, linesMax)`. `lines_typed++` when a counted `'\n'` is typed.
  - Instant mode (`human == false`): batches of **16** characters per step with `next_ms = 2`; press+release back to back.
- `int EtaMs(...)` ≙ `ghost_eta_remaining_ms` / `model_ms`: `per = m*1.15 + (safe ? m*0.1 + 35 : 0)`; `model = n*per + lines*1000`
  (human) or `n*1.0` (instant); scale the remainder by the actual pace once ≥ 25 chars typed: `ratio = clamp(elapsed/doneModel, 0.6, 1.8)`,
  `w = min(1, typed/100)`, `left *= 1 + (ratio-1)*w`.
- ETA text format: `< 60 s` → `est: %ds` (min 1); `< 3600` → `est: %dm %02ds`; else `est: %dh %02dm` (seconds rounded up from ms).
- Status text formats: `Typing %zu / %zu`, `Starting in %d… click the target` (the `…` is U+2026), `Ready`.
- Toast texts (exact): `Typed %zu chars in %s` (or `Typed %zu chars` when the app name is empty/`app`), `Stopped: focus changed`,
  `Stopped: unsupported character`, `Typing cancelled`, `Nothing to type`, `No focused app`, `Ghost init failed`,
  `Text too long (max %d)`, `Timing applied; could not save settings`.

**`src/ini_writer.h/.cpp`** — `bool SetIniInt(std::string& text, const char* section, const char* key, int value)`: update the key in
place if present, add it under the section if the section exists, or append a new section at the end; **keep every other line, comment
and blank line byte-for-byte**; preserve the file's line ending style. Tests: key exists / section exists / section missing / CRLF /
comments untouched. `ghost_set_timing`/`ghost_get_timing` equivalents then read/write `[GhostTiming] human speed safe` (ints) in `config.ini`.

**`src/key_translate.h/.cpp`** (pure tables) — mapping from virtual-key to DevTools protocol fields (§Task 3.3): `KeyInfo { std::string key; std::string code; int windowsVirtualKeyCode; bool printable; }` for letters, digits, punctuation, Backspace(8), Tab(9), Enter(13), Escape(27), Space(32), PageUp/Down, End, Home, arrows (37–40), Insert, Delete(46), F1–F12.

## Task 3.2 — Input routing (typing target, Mirror, Exclusive, shield, idle timer)

Port `src/input.c` into `input.cpp`, keeping the Phase 1 public API and filling the stubs:

- **State:** `TypingTarget {None, Overlay}`, `KeyMode`, `GhostMode`, `GrabStatus`. `STATUS` already prints them.
- **Mouse hook** (in the hook thread): on left/right/middle **button down** compute whether the cursor is inside the overlay rect.
  Do not swallow. Post `WM_CRONQ_CLICK(inside)` to the UI thread. UI thread logic = Linux `input_handle_raw_button`:
  inside + ghost mode on → ignore; inside + press → `SetTypingTarget(Overlay)` and restart the idle timer; outside + press → `SetTypingTarget(None)`.
- **`SetTypingTarget(Overlay)`:** if `cfg.shieldClipboardKeys` → enable the clipboard shield; if mode is Exclusive → engage the exclusive swallow; start the 60 s idle timer.
  **`SetTypingTarget(None)`:** disable shield, release exclusive, stop the idle timer. Idle timeout = **60 seconds** (`IDLE_TIMEOUT_SECONDS`) → target None
  (log `60-second idle timeout expired. Releasing typing target and grabs.`).
- **Triggers that force target None:** overlay hidden (`overlay::Hide` → `input::NotifyOverlayHidden`), ghost mode turned on, Ghost Writer starting,
  Alt+X toggle (`input::ReleaseGrab()` first, as `trigger_hotkey_toggle` does).
- **Key mode change while target is Overlay:** → Exclusive engages the swallow; → Mirror releases it.
- **Hook behaviour for a key event that is NOT one of our Alt hotkeys:**
  - target None → pass through (unchanged).
  - target Overlay + **Mirror** → pass through **and** post a copy `WM_CRONQ_KEY` to the UI thread (vk, scan, flags, down/up).
  - target Overlay + **Exclusive** → **swallow** (return 1) and post the copy. Swallow modifier keys too. Alt hotkeys are matched first, so they keep working.
  - Clipboard shield: target Overlay + shield enabled + exactly Ctrl held (no Alt/Shift) + key in {A, C, V, X} → swallow and post `WM_CRONQ_CLIPBOARD(action)`
    (also for the key-up). Applies in both modes (that is what the Linux passive grabs do).
  - Ignore events tagged `kInjectTag` (already done) and, while `ghost_is_guard_active` (Ghost Writer typing), ignore everything and pass through.
- **Safety nets against a dead keyboard (mandatory):** exclusive swallow is engaged **only** while target is Overlay; it auto-releases on idle timeout, on hide, on
  ghost mode on, on `Alt+M`, on `Alt+X`, on daemon exit, and when the hook thread exits. If the UI thread has not answered a posted key within 2 s, do nothing special
  (the hook never waits). Log every engage/release. Ctrl+Alt+Del always works at OS level.
- `GRAB` in `STATUS`: `ACTIVE` while exclusive swallow is engaged, `FAILED` if the hook is not installed (shows the grab-alert pill), else `NONE`.

## Task 3.3 — Delivering keys into the overlay without OS focus

The overlay's web contents never have OS focus, so keys are injected with the **DevTools Protocol** through
`ICoreWebView2::CallDevToolsProtocolMethod(L"Input.dispatchKeyEvent", paramsJson, handler)` (use `MakeHandler<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler, HRESULT, LPCWSTR>`; ignore the result but log failures once).

1. On every controller after creation call `Emulation.setFocusEmulationEnabled` with `{"enabled":true}` so pages believe they have focus (caret shows, `:focus` works).
2. **Which controller receives the key** (`g_keyTarget`): popup if open; else by app: `chatgpt` → ChatGPT; `browser` → the browser content unless the URL input has DOM
   focus (the UI page reports `{"type":"focus","where":"url"|"page"}` on `focusin/focusout` of the URL input) → then the UI controller; `home` and `ghost` → the UI controller.
3. **Translating a key event** (UI thread, on `WM_CRONQ_KEY`): modifiers from `GetAsyncKeyState` (Alt=1, Ctrl=2, Shift=8 → CDP `modifiers`). Printable text with
   `ToUnicodeEx(vk, scan, keyState, buf, 8, 0x4, hkl)` (flag 0x4 = do not change dead-key state; `hkl = GetKeyboardLayout(GetWindowThreadProcessId(GetForegroundWindow()))`).
   - Key down, printable (≥ 1 UTF-16 unit, no Ctrl/Alt except AltGr): `{"type":"keyDown","text":T,"unmodifiedText":T,"key":T,"code":C,"windowsVirtualKeyCode":VK,"modifiers":M}`.
   - Key down, non-printable (Backspace, Delete, arrows, Home/End, Tab, Esc, Enter): `{"type":"rawKeyDown",...}`; Enter additionally `"text":"\r"`; Tab no text.
   - Key up: `{"type":"keyUp",...}`.
   - Auto-repeat down events: same as down with `"autoRepeat":true`.
   - Pure modifier presses (Shift, Ctrl, Alt, Win, CapsLock) are **not** forwarded (Linux skips them too).
   Escape JSON via `json::Escape`; never build JSON with unescaped user text.
4. **Clipboard shield actions** (on `WM_CRONQ_CLIPBOARD`): send `Input.dispatchKeyEvent` `rawKeyDown` for the key with `"commands":["selectAll"|"copy"|"paste"|"cut"]`
   and `"modifiers":2` to `g_keyTarget`. If the clipboard holds an **image** and the action is paste, use the image-paste path (3.4) instead.
5. `--paste` / `PASTE` IPC command = same as the paste action on the current `g_keyTarget` (Linux `overlay_execute_clipboard_action(PASTE)`).
6. The UI controller needs the same handling, so URL input and the Ghost editor work: typing into them is just keys delivered to the UI controller.

## Task 3.4 — Clipboard image paste (port of `browser_paste`)

If the clipboard has an image (registered format `"PNG"` first; else `CF_DIBV5`/`CF_DIB`): get PNG bytes (for DIB use **WIC** to encode, see 3.5), base64 them
(write a pure `Base64Encode` + test), and run **exactly** the JavaScript from `browser_paste` in `src/browser.c` (the synthetic `ClipboardEvent('paste')` with a `DataTransfer`
file) via `ExecuteScript` on `g_keyTarget`. Fallback order if the clipboard has no usable image: the latest snap file `%LOCALAPPDATA%\cronq\latest_snap.png`.
Otherwise normal text paste (shield `commands:["paste"]`).

## Task 3.5 — Snap (Alt+P, `--snap`, bottom-nav Snap button)

Port `overlay_snap`, `on_snap_timeout`, `save_and_prune_snap`. New module `src/snap.h/.cpp` (`snap::Run()`), pure parts unit-tested.

1. **Debounce:** if a snap is in progress, log `overlay_snap: capture already in progress; debounced.` and return. Hotkey dedupe 200 ms already exists.
2. **UI state:** Snap button icon `camera` → `snapping` while capturing → `done` (check) for **1500 ms** → back to `camera`; on failure back to `camera` immediately.
   Push `{"type":"snap-state","state":"camera|snapping|done"}` to the UI (add to the protocol and to `index.html`).
3. **Exclude the overlay from the capture without changing focus:** `SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)` (0x11, Windows 10 2004+).
   If it returns FALSE (old Windows or Wine) **fall back to Linux's method**: hide the overlay with `ShowWindow(SW_HIDE)` (not activating), wait,
   capture, show again with `SetWindowPos(HWND_TOPMOST ... SWP_NOACTIVATE | SWP_SHOWWINDOW)`.
   Wait `max(60, cfg.snapDelayMs)` ms (use a timer, do not `Sleep` on the UI thread) between hiding/excluding and capturing so DWM has composed a frame. Restore affinity to `WDA_NONE` right after capture.
   If the overlay was not visible: capture immediately.
4. **Capture:** `snap_scope == "all"` → virtual screen (`SM_XVIRTUALSCREEN`, `SM_YVIRTUALSCREEN`, `SM_CXVIRTUALSCREEN`, `SM_CYVIRTUALSCREEN`); otherwise the **primary monitor**
   (`MonitorFromPoint({0,0}, MONITOR_DEFAULTTOPRIMARY)` rect). `GetDC(nullptr)`, `CreateCompatibleDC`, `CreateCompatibleBitmap`, `BitBlt(... SRCCOPY | CAPTUREBLT)`,
   then `GetDIBits` into a 32-bit top-down `BITMAPINFO` buffer. Release every GDI object. The process is per-monitor DPI aware so coordinates are physical pixels.
5. **Clipboard:** `OpenClipboard(overlayHwnd)` (retry up to 5 × 20 ms; the clipboard may be busy), `EmptyClipboard`, set `CF_DIB` (BITMAPINFOHEADER + pixels, `biHeight` positive bottom-up
   or negative top-down consistently) **and** the registered format `"PNG"` (`RegisterClipboardFormatW(L"PNG")`) with the PNG bytes; `CloseClipboard`. Always close the clipboard on every path.
6. **PNG encoding with WIC** (no extra dependency): `CoCreateInstance(CLSID_WICImagingFactory, ..., IID_PPV_ARGS(&factory))`, `CreateBitmapFromMemory(w,h,GUID_WICPixelFormat32bppBGRA,stride,size,pixels)`,
   `CreateEncoder(GUID_ContainerFormatPng)`, `IWICStream` (`InitializeFromMemory` for the clipboard copy, `InitializeFromFilename` for the file), `CreateNewFrame`, `Initialize`, `SetSize`,
   `SetPixelFormat` (`GUID_WICPixelFormat32bppBGRA`), `WritePixels`, `Commit` frame and encoder. Link `windowscodecs` (add to CMake). If `windowscodecs` is missing under Wine, log and skip PNG
   (clipboard `CF_DIB` still works). Alpha: set every pixel's alpha to 0xFF before encoding.
7. **Save (if `cfg.snapSave`):** directory `%USERPROFILE%\Pictures\cronq` (use `SHGetKnownFolderPath(FOLDERID_Pictures)` and append `\cronq`; fall back to `%USERPROFILE%\Pictures\cronq`),
   create it. File name `snap-YYYYMMDD-HHMMSS.png` (local time); if it exists use `snap-YYYYMMDD-HHMMSS-1.png`, `-2`, ... Copy (not symlink) the file to
   `%LOCALAPPDATA%\cronq\latest_snap.png`. **Prune:** list `snap-*.png` in the folder, sort by name ascending, delete the oldest until `snapKeepCount` (default 20, `<= 0` → 20) remain
   (log `Pruned old screenshot: <path>`). Put name generation and pruning decision logic in pure functions with unit tests (`PickSnapName(existingNames, stamp)`, `FilesToPrune(names, keep)`).
8. Log `Captured snap: %dx%d at (%d, %d)` and `Saved screenshot to <path>` like Linux.
9. **Wine smoke check to add:** `cli snap`, then poll for a `snap-*.png` under the prefix's `Pictures\cronq` and assert it exists and starts with the PNG signature `89 50 4E 47`. (If WIC is unavailable under Wine, make the smoke check optional and say so.)

## Task 3.6 — Ghost Writer (Alt+W, `--ghost-write`, Ghost page)

New module `src/ghost.h/.cpp` (namespace `cronq::ghost`) using `ghost_logic`. Port `src/ghost.c` behaviour.

### Page wiring (HTML ⟷ native)

New UI → native messages: `{"type":"ghost-text","value":"..."}` (send on every editor change, debounce 100 ms; native stores it as the test entry text used by
`GET/SET/CLEAR_TEST_ENTRY` and typing), `{"type":"ghost-settings","human":bool,"speed":0-2,"safe":bool}`, `{"type":"ghost-start"}`.
Native → UI: `{"type":"ghost-status","text":"...","eta":"...","button":"Start|Cancel","state":"idle|typing|done|failed"}`; `set-text` to fill the editor
(`--set-entry`). The Ghost button in the bottom nav uses state icons: idle / typing / done (check) / failed, reverting to idle after the same delay Linux uses
(read `on_ghost_revert_btn_label` and `overlay_set_ghost_button_state` in window.c).

Page behaviour (copy `on_ghost_start`, `ghost_countdown_tick`, `ghost_settings_save`):
- Settings (`human`, `speed`, `safe`) are saved to `[GhostTiming]` in `config.ini` **whenever they change** (and before starting). Load them on startup; defaults human=1, speed=0, safe=0.
- `Start`: if a countdown is running → cancel it; if typing → `ghost::Cancel()`; if the editor is empty → toast `Nothing to type`; else save settings, release exclusive grab and set target None,
  show `Starting in 3… click the target`, run a **1000 ms** timer: 3 → 2 → 1 (updating the status) then call `ghost::Run()`. The button reads `Cancel` during countdown and typing.
- **Alt+W / `--ghost-write` call `ghost::Run()` immediately (no countdown)**; if already typing → cancel (Linux `ghost_writer_run`). Debounce 200 ms.
- Footer clicks and Enter inside the editor must never start typing (the Start button is the only trigger).
- ETA label updates every 500 ms while typing; hidden when not typing.

### `ghost::Run()` — exact flow (port of `ghost_writer_run`)

1. Debounce 200 ms. If typing → cancel and return.
2. Text = editor text; empty → toast `Nothing to type`, state failed. `NormalizeText`; empty after normalising → same.
3. `cfg.ghostMaxChars` (0 = 1,000,000): if the code-point count exceeds it → toast `Text too long (max N)`.
4. Release exclusive grab, remove clipboard shield, typing target None.
5. **Find the target window:** `HWND t = GetForegroundWindow()`. Invalid if null, equal to the overlay (or its child), the desktop (`GetDesktopWindow()`), or the shell (`GetShellWindow()`),
   or invisible → toast `No focused app`. App name = process image file name without `.exe` (`GetWindowThreadProcessId`, `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)`,
   `QueryFullProcessImageNameW`), else `app`.
6. **Elevation check (Windows-only addition):** if the target process is elevated and ours is not (`OpenProcessToken` + `GetTokenInformation(TokenElevation)`), abort with toast
   `Target runs as administrator` — `SendInput` into elevated windows silently does nothing (UIPI). Treat inability to query as "not elevated".
7. **CapsLock:** if `GetKeyState(VK_CAPITAL) & 1` → send a CapsLock press/release (tagged) to turn it off; remember to turn it back on in `Finish` and `Cancel`.
8. Engage the **self-echo guard** (`g_guardActive = true`; the hook ignores all events and passes them through while it is set). Disengage ~`Range(...)` after finishing as Linux
   `disengage_guard_delayed` does (read it; keep the delay).
9. **Settle phase (≈10 ms timer, repeat):** wait until the user physically released Ctrl and Alt (`GetAsyncKeyState(VK_CONTROL/VK_MENU/VK_LWIN/VK_RWIN)`), up to 24 retries × 10 ms,
   then send key-up for Ctrl/Alt/Win (tagged) to avoid hotkey bleed, then start stepping after `Range(120, 349)` ms.
10. **Stepping** is a timer-driven state machine, one phase per tick, exactly like Linux's three phases **CHAR → PRESS → RELEASE** (human) or the batch `instant_step` (human off). Use
    `SetTimer(hwnd, kGhostTimerId, ms, nullptr)` re-armed each step (min 1 ms). Never `Sleep`.
    - At the start of every CHAR phase: `if (GetForegroundWindow() != target)` → `Cancel()` + toast `Stopped: focus changed`.
    - Resolve each character to input: `VkKeyScanExW(ch, hkl)` (hkl = layout of the target thread). If result ≠ -1: low byte = VK, high byte flags: 1 = Shift, 2 = Ctrl, 4 = Alt
      (AltGr = Ctrl+Alt). Press the needed modifiers **before** the key (the `lead` delay) and release after. If -1 (character not on the layout, emoji, CJK...): send the character with
      `KEYEVENTF_UNICODE` (`wVk = 0`, `wScan = UTF-16 unit`, one INPUT per unit, down then up; surrogate pairs = two units). If that is impossible → `Stopped: unsupported character`.
      `\n` → `VK_RETURN`; `\t` → `VK_TAB`; backspace → `VK_BACK`. Use scan codes (`MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC, hkl)`) and set `KEYEVENTF_EXTENDEDKEY` for extended keys (arrows, Home/End/Insert/Delete, right Ctrl/Alt, numpad Enter).
    - **Every INPUT gets `dwExtraInfo = kInjectTag`**, one `SendInput` call per key event group (modifier down, key down; later key up, modifier up). Check the return value; on failure `Cancel` + toast.
    - Timing exactly per `Typist` (§3.1): dwell between key down and up; rest until the next char; chunk pauses; typo plan; burst; thinking pauses.
    - Progress: update the status text `Typing N / M` at most every 120 ms.
11. **Finish:** release any held key/modifier, restore CapsLock, disengage guard (delayed), status `Typing`→ done state, button check icon, toast `Typed N chars in <app>`.
12. **Cancel:** kill the timer, release held key and modifiers (send key-ups), restore CapsLock, disengage guard, state failed, toast `Typing cancelled`.

### Persisting and reading timing
`ghost::GetTiming()` reads `[GhostTiming]` once and caches; `SetTiming()` validates, writes through `SetIniInt` and toasts `Timing applied; could not save settings` on write failure.

## Task 3.7 — Wiring checklist

- `app.cpp`: `SNAP` → `snap::Run()`; `GHOST_WRITE` → `ghost::Run()`; `PASTE` → paste action; `GET_TEST_ENTRY`/`SET_TEST_ENTRY`/`CLEAR_TEST_ENTRY` → the ghost editor text
  (and push `set-text` to the UI).
- `hotkey.cpp` `Dispatch`: `Id::Snap` → `snap::Run()`; `Id::GhostWrite` → `ghost::Run()`; `Id::Mode` → `input::CycleKeyMode()` (already); remove the temporary "arrives in Phase 3" toasts everywhere.
- `main.cpp`: init order after overlay: `input`, `ghost`, `snap`; cleanup in reverse; cancel typing on shutdown.
- STATUS reports real `TARGET`/`GRAB`.

## Deliverables

1. Zero warnings; 22 original smoke checks green **plus**: unit tests (`ghost_logic`, `ini_writer`, `key_translate`, `snap` naming/pruning, base64) all passing under Wine, and the snap smoke check (or an explicit note why optional).
2. `windows/README.md` → `## Phase 3 manual checklist` (below) and updated bypass/limits section.
3. Report per `11_HOW_TO_REPORT_BACK.md`, listing every item as COMPILED / UNIT TEST PASSED / PASSED UNDER WINE / UNVERIFIED.

## Phase 3 manual checklist (for the owner on Windows)

1. Notepad focused. Click inside the overlay's ChatGPT prompt or URL bar and type "hello". **Mirror:** "hello" appears in the overlay **and** in Notepad; Notepad keeps focus. Click on the Notepad window: typing goes only to Notepad.
2. Alt+M → **Exclusive**: click the overlay, type: text appears only in the overlay, Notepad gets nothing. Alt+M back. Wait 60 s idle: typing target returns to none (`cronq status` → `TARGET=NONE`).
3. While typing in the overlay press Ctrl+A / Ctrl+C / Ctrl+V: they act on the overlay's field, not on Notepad.
4. Copy an image (Win+Shift+S), click into ChatGPT's prompt, press Ctrl+V: the image attaches.
5. Alt+P: screenshot taken. The overlay is **not** in the image, the previous window keeps focus, the clipboard has the image (paste into Paint), a PNG exists in `Pictures\cronq`, the Snap icon shows a check for ~1.5 s. Take 25 snaps: only 20 files remain.
6. Open the Ghost page, paste multi-line text, pick Moderate, Start. Click Notepad within 3 s. Text is typed with human-like timing, line breaks preserved, status counts up, ETA shown; **no trailing Enter** at the end.
7. Press Alt+W while typing: stops with `Typing cancelled`. Alt-Tab to another window while typing: stops with `Stopped: focus changed`.
8. Switch Human-like off: text appears in fast batches. Extra safe on: occasional typos get corrected; final text exactly equals the input.
9. Type `héllo — 日本語 😀`: unsupported characters are typed via Unicode input or stop with the documented toast; never crash.
10. Run Notepad **as administrator** and Ghost Write into it: toast `Target runs as administrator`.
