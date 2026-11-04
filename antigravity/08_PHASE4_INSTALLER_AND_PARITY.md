# Phase 4 — Installer, hardening, documentation, final parity

**Goal:** turn the working app into something a normal person can install, and prove (as far as possible) that the Windows
version behaves exactly like Linux. Phase 3 must be finished and reported first.

**Verification reality:** the installer and autostart cannot be run here (Inno Setup is Windows-only). Everything you write must be
syntactically checked as far as possible (PowerShell parse with `pwsh -NoProfile -Command "[scriptblock]::Create((Get-Content -Raw f.ps1))"`
if `pwsh` exists; otherwise careful review) and every step goes into the owner's manual checklist.

---

## Task 4.1 — Crash safety and logging polish (small, do first)

1. In `RunDaemon`, call `SetUnhandledExceptionFilter(...)`: on a crash write `crash-<timestamp>.log` in `DataDir()` with exception
   code, faulting address, module + offset, and then return `EXCEPTION_EXECUTE_HANDLER` (process exits non-zero → supervisor restarts it).
   Keep it tiny and allocation-free in the handler (static buffers, `WriteFile`).
2. Log rotation already exists (2 MB → `.old`). Verify it works by a unit test of the rotate decision (pure function).
3. Make sure **every** `LogErr` carries enough context to diagnose a remote user's problem (HRESULT in hex, file path, API name).
4. Run the daemon for 30 minutes under Wine with the smoke script's actions in a loop (add `windows/scripts/wine-soak.sh`): handles/memory must not grow
   (use `wine tasklist` memory column before/after; fail if growth > 50%).

## Task 4.2 — Per-user installer (Inno Setup)

Create `windows/installer/cronq.iss`. Requirements:

- **No admin rights:** `PrivilegesRequired=lowest`, `DefaultDirName={localappdata}\Programs\Cronq`, `DisableProgramGroupPage=yes`.
- Files: `cronq.exe`, `cronqw.exe`, `WebView2Loader.dll`, the whole `ui\` folder, `config` sample not needed (app creates it).
  Source paths relative to a `dist\` folder produced by the CI/PowerShell script (Task 4.3).
- Start Menu shortcut "Cronq" → `cronqw.exe` (no console window; no arguments = show-or-start).
- Task (checked by default) **"Start Cronq automatically when I sign in"**: writes
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` value `Cronq` = `"{app}\cronqw.exe" --supervise` (the supervisor restarts it after crashes). Removed on uninstall.
- Optional task: add `{app}` to the user `PATH` so `cronq` works in any terminal (`[Registry]` on `HKCU\Environment` with the `NeedsAddPath` check pattern; broadcast the change — Inno does
  this automatically with `ChangesEnvironment=yes`).
- **WebView2 runtime check** in `[Code]`: look for the Evergreen runtime in `HKLM\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}`
  and `HKCU\Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}` (value `pv`, must not be `0.0.0.0`). If missing: download the Microsoft
  bootstrapper (`https://go.microsoft.com/fwlink/p/?LinkId=2124703`) with Inno's `DownloadTemporaryFile` and run it with `/silent /install`; if that fails show a message with the
  manual link and continue (the app degrades gracefully).
- **Before install/upgrade/uninstall:** stop the running app: run `{app}\cronq.exe kill` (ignore exit code) and wait 1.5 s, so files are not locked.
- **Uninstall:** page with a checkbox **"Also delete my settings, logins and saved screenshots' index (purge)"** unchecked by default. Normal uninstall keeps `%APPDATA%\cronq` and
  `%LOCALAPPDATA%\cronq`. Purge deletes both folders (the equivalent of Linux `uninstall.sh --purge`). Never delete `Pictures\cronq` (user's files).
- Output name `cronq-setup-<version>.exe`, version from `windows/VERSION` (create it: `0.1.0`), also used for `kVersion` in `main.cpp` (single source of truth: generate a header at CMake time with
  `configure_file`).

Also provide `windows/installer/install.ps1` and `uninstall.ps1` for developers (mirror Linux `install.sh`/`uninstall.sh`): copy files to `%LOCALAPPDATA%\Programs\Cronq`, create the Run key,
Start Menu shortcut (`WScript.Shell`), launch; `-Purge` switch on uninstall. `Set-StrictMode -Version Latest`, `$ErrorActionPreference='Stop'`.

## Task 4.3 — Packaging script and CI

1. `windows/installer/make-dist.ps1`: builds Release with CMake (`cmake -S windows -B windows/build -A x64`, `cmake --build ... --config Release`), creates `windows/dist/` with the files above.
2. Update `.github/workflows/windows.yml`: after build + ctest, run `make-dist.ps1`, `choco install innosetup -y`, `iscc windows\installer\cronq.iss`, upload the setup exe as an artifact.
   Keep the Linux-untouched guard job. Pin action versions (`actions/checkout@v4`, `actions/upload-artifact@v4`).
3. The MSVC build must compile with **zero warnings at `/W3`** (the workflow cannot be run here; mentally compile your new code against MSVC rules: no `__attribute__`, no GNU extensions,
   `#include <cstdint>`, avoid VLAs, use `_wfopen`/`snprintf` consistently, no designated initializers).

## Task 4.4 — Optional: UIAccess build (overlay over elevated windows)

Mark everything here **OPTIONAL — do it only if Tasks 4.1–4.3 and 4.5–4.7 are fully done**, and keep it off by default.
Windows lets a process with `uiAccess="true"` in its manifest, **signed** and installed in a **secure location** (`%ProgramFiles%`), interact with elevated windows and sit above them (this is how the on-screen keyboard works).
Deliver: a second manifest `cronqw-uia.manifest` (`requestedExecutionLevel level="asInvoker" uiAccess="true"`), a CMake target `cronqw_uia` (not built by default), and
`windows/UIACCESS.md` explaining certificate creation (self-signed, imported into Trusted Root), signing with `signtool`, and installing to `Program Files`. Do not enable in the installer.

## Task 4.5 — Multi-monitor, DPI, and robustness audit (read your own code)

Review `overlay.cpp`, `browser.cpp`, `snap.cpp`, `ghost.cpp`, `hotkey.cpp` for each item and fix what you find:

- Monitor removed while the overlay is on it → `WM_DISPLAYCHANGE` repositions (already) and clamps inside a visible monitor.
- Monitor with different DPI: after moving the overlay between monitors, sizes stay in physical pixels (documented); `WM_DPICHANGED` handler either ignores or rescales consistently (decide, document in `shared/spec/behavior.md`).
- Taskbar on any edge (top/left/right): `CalcPosition` uses the work area → verify the maths for each corner/edge combination with a pure function test (`CalcCorner(workRect, w, h, margin, corner)`: extract it from `overlay.cpp` into a pure function and test all four corners).
- `config monitor = N` beyond the number of monitors → falls back to primary (already; add the test).
- Sleep/resume and lock/unlock: after `WM_POWERBROADCAST (PBT_APMRESUMEAUTOMATIC)` and `WM_WTSSESSION_CHANGE` (register with `WTSRegisterSessionNotification`) re-install the keyboard/mouse hooks and re-assert topmost.
- Explorer restart (taskbar crash): overlay must survive; re-assert topmost on `TaskbarCreated` (`RegisterWindowMessageW(L"TaskbarCreated")`).
- Memory: no `ComPtr` cycles (handlers holding `ComPtr`s to the controller that owns them → capture raw pointers or weak flags, break in `Cleanup`).
- Handles: every `CreateEvent/CreateThread/CreateFile/OpenProcess/GetDC` has a close/release on all paths (grep and check).
- Thread safety: list every global touched by the hook thread and confirm each is volatile/atomic/locked.

## Task 4.6 — Documentation

1. `windows/README.md` (user-facing, English, simple words, same tone as the Linux README): what it is, requirements (Windows 10 2004+ / 11, WebView2 runtime), install, quick start, hotkeys table,
   CLI commands, config file location and keys, uninstall/purge, troubleshooting (log paths, `cronq --daemon` for live logs, WebView2 missing, antivirus false positives for unsigned exe), and an honest
   **"What it can and cannot do on Windows"** section: can — stay above borderless-fullscreen games/video, stay above always-on-top windows, never steal focus, click-through, clean snaps via display-affinity exclusion;
   cannot — exclusive-fullscreen DirectX games, elevated (admin) windows without the UIAccess build, UAC / Ctrl+Alt+Del screen, kernel anti-cheat, apps that detect injected keys.
2. `windows/PARITY.md` (Task 4.7).
3. Update every phase checklist in `windows/README.md` into one final **manual acceptance checklist** (numbered, grouped by feature).
4. Update `antigravity/00_START_HERE.md` status table (mark phases done) at the very end.

## Task 4.7 — Final parity audit (this is the "error-free, same as Linux" gate)

Create `windows/PARITY.md` — a table with one row per item and a status of **VERIFIED (Wine)**, **UNIT TESTED**, **UNVERIFIED (Windows)** or **DIFFERENT (intentional, link)**. Walk, in order:

1. Every row of `shared/spec/behavior.md` (hotkeys + dedupe, sizes, toast texts, loop guard, IPC commands, STATUS/GET_GEOMETRY formats).
2. Every Linux README feature bullet (`README.md`, sections "What It Does", "Keyboard Shortcuts", "Mouse Controls", "Configuration").
3. Every CLI command in Linux `--help`.
4. Every config key in `src/config.c` (name, default, range) → compare with `windows/src/config.cpp` by script: write a small test that parses `shared/fixtures/config_defaults.ini` **and** greps `src/config.c` key names (read-only!) to confirm none is missing on Windows.
5. Every toast text and log line listed in the phase documents.
6. Look for **remaining stubs**: `grep -rn "Phase 3\|arrives in\|not implemented\|TODO\|FIXME" windows/` must return nothing.
7. Compile warnings: zero. `wine-smoke.sh`: all green. All unit tests: green.
8. `git diff --stat -- src Makefile scripts install.sh uninstall.sh tests cronq.desktop README.md assets` is empty.

Any row that is not VERIFIED/UNIT TESTED must have an exact manual test step in the final checklist.

## Deliverables

`windows/installer/` (iss, ps1 scripts, make-dist), `windows/VERSION`, CI workflow update, `windows/README.md` (final), `windows/PARITY.md`, optional `windows/UIACCESS.md`, crash handler, soak script, and a final report
per `11_HOW_TO_REPORT_BACK.md` including a **"Top 10 things most likely to go wrong on first Windows run"** list with how to diagnose each (log lines to look for).

## Final manual acceptance checklist skeleton (expand it in `windows/README.md`)

1. Fresh Windows 10 22H2 VM / Windows 11 machine, standard user. Run `cronq-setup-x.y.z.exe` (no UAC prompt). Overlay appears on first launch (Start Menu).
2. Reboot / sign out and in: Cronq starts automatically (hidden); Alt+X shows it.
3. Phase 1, 2, 3 checklists pass.
4. Kill `cronqw.exe` in Task Manager: it returns within ~2–3 s (supervisor).
5. Uninstall (without purge): app gone, `%APPDATA%\cronq\config.ini` and logins remain. Reinstall: logins still valid. Uninstall with purge: folders removed.
6. Play a borderless-fullscreen video and a game in borderless mode: overlay stays visible; Alt+G makes it click-through; Alt+P snap excludes it.
