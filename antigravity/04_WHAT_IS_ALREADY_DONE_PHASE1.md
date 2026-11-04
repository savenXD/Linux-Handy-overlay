# What is already done: Phase 1 (complete and tested)

Phase 1 delivered the **Windows shell**: everything except the browser, the UI content, focus-free typing, snap and
Ghost Writer. It was written against the Linux behaviour, compiled with llvm-mingw, and exercised under Wine
(22/22 automated checks pass; see `05_BUILD_AND_TEST_GUIDE.md`). It has **not** been run on real Windows yet.

Do not rewrite it. Extend it.

## Files in `windows/`

| File | Purpose | Notes for you |
| --- | --- | --- |
| `CMakeLists.txt` | Builds `cronq.exe` (console CLI), `cronqw.exe` (windowless overlay), `sendkeys.exe`, `config_test.exe`; opt-in `CRONQ_WEBVIEW2` | Everything links statically (no DLLs to ship except `WebView2Loader.dll`) |
| `cmake/llvm-mingw.cmake` | Toolchain file for cross-compiling on Linux | |
| `cmake/FetchWebView2.cmake` | Downloads the pinned WebView2 SDK (1.0.3967.48) when `-DCRONQ_WEBVIEW2=ON` | Already works and is verified to compile |
| `compat/EventToken.h` | One-line shim so MinGW finds the SDK's `EventToken.h` | Only used for non-MSVC builds |
| `tools/gen_webview2_uuids.py` | Generates `src/webview2_mingw_uuids.h` from `WebView2.h` | Re-run only if the SDK version changes |
| `src/common.h/.cpp` | Includes, message ids (`WM_CRONQ_*`), `Widen/Narrow`, `Trim`, `EqNoCase`, `DataDir/ConfigDir`, `Log/LogErr`, `NowUs` | Log goes to `%LOCALAPPDATA%\cronq\cronq.log` and stdout |
| `src/config.h/.cpp` | INI parser + `AppConfig` with Linux's exact keys, defaults, ranges; creates default `config.ini` | `config::ParseInto` is unit-tested. Phase 3 must add `[GhostTiming]` read/write |
| `src/input.h/.cpp` | Key mode (Mirror/Exclusive) and Ghost (click-through) state | `GetTypingTarget()`/`GetGrabStatus()` are stubs returning None; Phase 3 implements them |
| `src/overlay.h/.cpp` | The window: styles, rounded region, DPI, positioning, opacity, resize, fullscreen, default size, compact mode, toasts, click-through, GDI-drawn placeholder UI, mouse drag/resize | **Phase 2 replaces the GDI body/header with HTML**, but keeps all window logic |
| `src/raise.h/.cpp` | Stay-on-top watcher with the Linux loop guard | Works; do not change the constants |
| `src/hotkey.h/.cpp` | Low-level keyboard hook, 9 hotkeys, dedupe, Alt-menu suppressor, hook re-install every 15 s | Phase 3 extends the hook (typing capture); read the comments first |
| `src/ipc.h/.cpp` | Named-pipe server (worker thread) + client; safe request hand-off to the UI thread | Reuse `ipc::Request` pattern for any cross-thread hand-off |
| `src/app.h/.cpp` | `HandleCommand()` — every IPC command; `OnAppMessage()` | Many commands are stubs (listed below) |
| `src/main.cpp` | CLI, `--daemon`, `--supervise`, daemon startup/shutdown order | `RunDaemon()` is the init order; add new modules there |
| `src/com_support.h`, `src/webview2_include.h`, `src/webview2_mingw_uuids.h` | `ComPtr<T>` and `MakeHandler<>` lambda→COM-callback helper; correct WebView2 include | **Use these. Do not write your own COM helpers.** Compile-verified against the real SDK |
| `tests/config_test.cpp` | Unit test for the config parser | Pattern to copy for your own unit tests |
| `tests/sendkeys.cpp` | Dev tool: sends `alt+x` etc. with `SendInput` | Used by the smoke test |
| `tests/webview2_compile_check.cpp` | Compile/link check of WebView2 + helpers | |
| `scripts/build-mingw.sh`, `scripts/wine-smoke.sh` | Build and automated check scripts | |
| `README.md` | User-facing readme and the manual Windows checklist | You maintain the checklists per phase |

Shared: `shared/spec/behavior.md` (the rules both platforms obey) and `shared/fixtures/config_defaults.ini`.
CI: `.github/workflows/windows.yml`.

## Runtime behaviour today

- `cronq.exe --daemon --show` starts the overlay. `cronqw.exe` is the same program without a console window.
- `cronq.exe` with no arguments shows the running overlay, or launches `cronqw.exe --daemon --show` in the background.
- Initial window: 360 wide, **280 tall (compact "home")**, bottom-left, margin 12 against the **work area**
  (so the taskbar is not covered). `Alt+D` / `--default-size` gives 360x640. Other apps (`--app browser|chatgpt|ghost`)
  switch to 640 tall. (This is exactly the Linux behaviour.)
- The window is dark, rounded, shows two pills (Mirror/Exclusive, Solid/Ghost), three window buttons, a centre drag
  handle, a title, a status line and a list of hotkeys, plus toasts.
- Window buttons: `—` and `✕` hide the overlay (they do NOT quit), `▢` toggles fullscreen. Pills are clickable.
  Double-click the handle = maximise height / restore.
- All 9 hotkeys work; the key press is swallowed; the focused app does not open its menu on Alt release.
- IPC commands implemented fully: `TOGGLE SHOW HIDE KILL/QUIT/EXIT MODE[…] GHOST[…] SET_OPACITY GET_OPACITY SET_SIZE
  FULLSCREEN DEFAULT_SIZE GET_GEOMETRY RELOAD_CONFIG STATUS APP`.
- Supervisor: `cronqw.exe --supervise` restarts a crashed daemon after 2 s and writes `crash.log`; a clean `kill`
  ends both.

## Stubs you must replace (search for these strings)

| Place | Today | Becomes |
| --- | --- | --- |
| `app.cpp` `LOAD`, `RELOAD`, `BACK`, `HOME`, `RESET`, `HAS_MODAL`, `CLOSE_MODAL` | switch app / no-op | real browser actions (Phase 2) |
| `app.cpp` `GET/SET/CLEAR_TEST_ENTRY` | in-memory string | the Ghost editor text (Phase 3) |
| `app.cpp` `SNAP`, `GHOST_WRITE`, `PASTE` and `hotkey.cpp` `Id::Snap`, `Id::GhostWrite` | toast "arrives in Phase 3" | real features (Phase 3) |
| `input.cpp` `GetTypingTarget`, `GetGrabStatus` | always None | real state (Phase 3) |
| `overlay.cpp` `Paint()` body | placeholder text | removed when the HTML UI exists (keep as fallback when WebView2 is missing) |

## Verified results (under Wine on a nested X display)

Daemon starts and renders; `status`, `--get-geometry`, `--opacity`, `--size` (with min/max clamping), `--ghost`,
`kill` all answer correctly; Alt+X / G / M / A / F / D work through the real low-level hook; **Ctrl+Alt+X correctly does
not trigger**; kill ends the process cleanly; the supervisor restarts a force-killed daemon and stops after `kill`.
The config parser passes its unit test (defaults, overrides, CRLF, BOM, invalid values, comments).

## Bugs already found and fixed during testing (so you do not reintroduce them)

1. `SetDefaultSize` computed the corner position with the *old* height → window left the screen. Fixed: set size first.
2. CMake linked only two executables statically; the test tools died with "DLL not found". Fixed: all four are static.
3. `pkill -f <pattern>` in a test script killed its own shell. Use `pkill -x Xephyr`.
4. First `wine` command creates a prefix and takes ~30–60 s; scripts must pre-run `wine wineboot --init`.

## Known deviations from Linux (intentional, already in `shared/spec/behavior.md`)

1. Default corner/maximise-height use the monitor **work area** (taskbar stays visible); fullscreen uses the full monitor.
2. Pixel sizes from config are multiplied by the monitor DPI scale (150% screen → 540x960 px).
3. Hotkeys are swallowed by a low-level hook instead of X11 grabs.
4. `--mode m|e` also work (Linux's CLI forwards `m`/`e` literally and then answers UNKNOWN_COMMAND — an obvious Linux bug).

## Things Phase 1 intentionally left for later (do not forget)

- The keyboard hook runs on the **UI thread**. Fine now. Once WebView2 is on the UI thread, a busy UI thread could miss
  the ~300 ms hook deadline and Windows would silently unhook it (we re-install every 15 s, but keys are lost in
  between). **Phase 3 task 3.0 moves the hook to its own thread.** In Phase 2 just never block the UI thread.
- No mouse hook yet (needed for "click outside the overlay").
- No focus-guard safety net yet (Phase 2 adds it).
- The window uses `WS_EX_LAYERED` + `LWA_ALPHA` for opacity. Hosting WebView2 inside a layered window is believed to
  work on Windows 8+ but could not be confirmed here. Phase 2 has a decision gate for it (see `06_PHASE2...`).
