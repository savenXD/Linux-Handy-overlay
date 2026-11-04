# Non-negotiable rules

Read this twice. These rules exist because the owner cannot test on Windows and cannot afford regressions.

## 1. Never touch the Linux implementation

Forbidden to edit, rename, delete or reformat (even whitespace):

```
src/            Makefile        scripts/        install.sh      uninstall.sh
tests/          cronq.desktop   README.md       assets/         DEVLOG.md
```

You may edit only: `windows/**`, `shared/**`, `.github/workflows/windows.yml`, `antigravity/**` (status updates).
If you believe a Linux file must change, **stop and ask** in your report. Before every report, run:

```bash
git diff --stat -- src Makefile scripts install.sh uninstall.sh tests cronq.desktop README.md assets
```

It must print nothing.

## 2. The "cannot test on Windows" rule (the big one)

Nobody — including you — can run this on real Windows during your work. The owner will test later and expects it to
work first time. Therefore:

1. **Compile everything** with `windows/scripts/build-mingw.sh`. **Zero warnings.** Warnings are bugs.
2. **Run `windows/scripts/wine-smoke.sh`.** The 22 Phase 1 checks must stay green after every change. Wine can
   exercise the window, IPC, hotkey hook (via `SendInput`), geometry and supervisor, so use it for regressions.
3. **Put logic in pure functions** (URL normalisation, allowlist matching, typing timing, INI writing, key mapping
   tables, message parsing) in their own `.cpp/.h` that do not call Win32. **Write a unit test for each** (copy the
   pattern in `windows/tests/config_test.cpp`) and run it under Wine. Pure logic you can fully verify; do that to
   shrink the unverified surface.
4. **Everything that needs real Windows** (WebView2, real focus behaviour, DPI, UAC, games) must be:
   - written defensively (check every HRESULT / return value; log failures with `LogErr`; never crash),
   - listed in `windows/README.md` under the phase's **manual test checklist** with exact steps the owner can follow,
   - labelled **UNVERIFIED** in your report.
5. **Graceful degradation is mandatory.** If WebView2 is missing or fails (this is always the case under Wine),
   the daemon must still start, keep IPC and hotkeys working, show the placeholder UI and log one clear error.
   This keeps the Wine smoke test usable as a regression net for all later phases.
6. **Re-read your diff** before declaring a step done, hunting for the mistakes in `09_WIN32_AND_COM_PITFALLS.md`.
7. **Do not "improve" working Phase 1 code** unless a task says so or you found a real bug (then say so in the report).

## 3. Honesty rules

- Never write "works", "verified" or "tested" unless you executed it. Allowed labels: **COMPILED**, **PASSED UNDER WINE**,
  **UNIT TEST PASSED**, **UNVERIFIED (needs Windows)**.
- If a step is skipped or partly done, say so plainly.
- If something fails, paste the real error output. Do not paraphrase it into something friendlier.
- Do not invent API names or behaviour. If unsure whether a Win32/WebView2 function exists or what its signature is,
  open the header (`windows/build-mingw*/webview2-*/build/native/include/WebView2.h` or
  `~/.local/llvm-mingw/x86_64-w64-mingw32/include/`) and read it. Headers do not lie; your memory does.

## 4. Scope rules

- Work on **one phase at a time**, in order (2 → 3 → 4). Finish, report, stop.
- Do only what the phase document lists. No extra features, no refactors "while you are there".
- No new dependencies beyond the WebView2 SDK (already wired into CMake). Forbidden: Electron, .NET, Qt, WIL, WRL,
  Boost, any package manager libraries, any runtime download at app start. If you think you need one, ask.
- Do not run `git commit`, `git push`, `git reset`, `git checkout` on files, or anything that rewrites history.
  Leave your work in the working tree; the owner reviews and commits.
- Do not delete or move files you did not create.

## 5. Code style (match the existing `windows/src/`)

- C++17, `namespace cronq { namespace <module> { ... } }`, one module = one `.h` + one `.cpp`.
- Strings: **UTF-8 `std::string` internally**; convert with `Widen()`/`Narrow()` only at the Win32 boundary; call the
  wide (`W`) Win32 functions only. Never use `A` functions or `TCHAR`.
- Logging: `Log("TAG", "fmt", ...)` / `LogErr(...)`. Tags are upper-case module names (`WINDOW`, `HOTKEY`, `BROWSER` ...).
  Use the same message wording as the Linux logs when a Linux equivalent exists.
- No exceptions thrown or caught across Win32/COM callbacks (wrap callback bodies in `try { ... } catch (...) {}` if you
  use anything that can throw, e.g. `std::string` allocation is fine but `std::stoi` is not).
- No `std::thread`/`std::mutex` (we use Win32 `CreateThread`, `CRITICAL_SECTION`, events). No global constructors that
  do real work. No `new`-without-owner (use `ComPtr`, `std::unique_ptr`, or a clear owner).
- Comments: explain *why*, briefly, like the existing files. No banner comments, no commented-out code.
- Magic numbers copied from Linux must keep a comment pointing at the Linux origin, e.g. `// src/ghost.c k_mode_delay_ms`.
- Naming follows the existing files: `PascalCase` functions in namespaces, `g_` prefix for file-scope state, `k` prefix
  for constants.

## 6. Forbidden techniques (each one breaks a core feature)

| Do NOT do this | Because |
| --- | --- |
| Call `SetForegroundWindow`, `SetFocus`, `SetActiveWindow`, `BringWindowToTop`, `AttachThreadInput` on the overlay | steals focus — the #1 product rule |
| `ShowWindow(hwnd, SW_SHOW)` / `SW_RESTORE` / `SW_SHOWNORMAL` on the overlay | activates it. Only `SetWindowPos(... SWP_NOACTIVATE ...)` / `SW_SHOWNOACTIVATE` / `SW_HIDE` |
| Remove `WS_EX_NOACTIVATE` or `WS_EX_TOOLWINDOW` | focus theft / taskbar button appears |
| Call `SendInput`, COM, `Sleep`, file I/O or anything slow **inside the keyboard/mouse hook callback** | Windows silently removes slow hooks (timeout ≈ 300 ms) — hotkeys die |
| Block the UI thread (waits, `Sleep`, sync network, `WaitForSingleObject` on slow things) | freezes hotkeys and the window |
| Use WebView2 from any thread other than the UI thread | WebView2 is single-threaded apartment only |
| Put raw pointers into `PostMessage` without a lifetime plan | use-after-free (see `ipc::Request` for the safe pattern) |
| Modify the overlay's z-order with anything but `HWND_TOPMOST` + `SWP_NOACTIVATE` | breaks the stay-on-top guarantee |
| Hard-code `C:\Users\...` or a username | use `DataDir()`, `ConfigDir()` |
| Store secrets/cookies yourself | WebView2 profile folder handles it |
| Add telemetry, network calls, auto-updaters | not requested |
| Change a hotkey letter, default size, toast text or config key | parity with Linux is the product |

## 7. When you are unsure

1. Look for the answer in `12_DECISIONS_ALREADY_MADE.md`.
2. Read the Linux code (`10_LINUX_SOURCE_READING_MAP.md`) and copy its behaviour.
3. If still unsure and the choice is reversible and local: pick the **simplest and most defensive** option, mark it with
   `// DECISION:` and list it in your report.
4. If the choice affects architecture, user-visible behaviour or the build: **stop and ask** in your report. Do not guess.
