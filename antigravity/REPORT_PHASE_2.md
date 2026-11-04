# Phase 2 report

> Historical phase handoff. The combined fixes and current verification are in [REPORT_INTEGRATION.md](REPORT_INTEGRATION.md). Its results supersede the claims below.

## Summary

Phase 2 code is implemented: WebView2 hosting, Browser/ChatGPT with one persistent profile, sign-in popup deferrals,
HTML/CSS/JS UI, validated native bridge, focus guard, browser IPC, and native fallback.
Reset follows the owner's confirmed Linux behavior: Home + Mirror/Solid.

**Integration testing is deferred at the owner's request.** Three other agents are editing later phases in this same
workspace. The earlier Wine run passed 31 checks, but it predates subsequent code and concurrent changes;
it must not be treated as acceptance of the final combined workspace. No real Windows testing was possible.

## Status of every task

| Task | Result | Evidence |
| --- | --- | --- |
| 2.1 WebView2 default/build/UI assets | COMPILED | `phase2-build.log`; ON and OFF cross-builds completed without warnings at the recorded checkpoint |
| 2.2 URL/auth/allowlist/JSON logic | UNIT TEST PASSED; COMPILED | Earlier Wine `browser_logic_test` and `jsonlite_test` passed; later bounded-integer parser/tests compiled but have not been rerun |
| 2.3 COM/environment/controllers/popups/reset/mute/recovery | COMPILED; UNVERIFIED (needs Windows) | Real SDK interfaces compiled; Wine exercises missing-runtime path only |
| 2.4 Native/UI bridge | COMPILED; UNVERIFIED (needs Windows) | Source validation, flat JSON, bounded queue, app/popup rectangle checks; browser-only mock exercised UI messages |
| 2.5 HTML UI and external drag | COMPILED; UNVERIFIED (needs Windows) | Browser-only visual/control review at 360x280 and 360x640; 19 exact Linux SVGs and original ChatGPT script checked by source comparison |
| 2.6 Browser IPC | PASSED UNDER WINE; UNVERIFIED (needs Windows) | Earlier fallback checks for app switching, hidden LOAD, Back, modal query, and Linux Reset; actual page navigation requires Windows |
| 2.7 Focus guard | COMPILED; UNVERIFIED (needs Windows) | External-only focus restoration with 10/sec rate limit; real Chromium child activation needs Windows |
| 2.8 Graceful fallback | PASSED UNDER WINE | Earlier daemon log: WebView2 missing runtime `0x80070002`; original hotkey/IPC/supervisor tests remained green |
| 2.9 Docs/manual checklist | UNVERIFIED (needs Windows) | Checklist and failure playbook written in `windows/README.md`; owner executes after all phases integrate |

## Files added or changed

Phase 2 additions:

- `windows/src/browser.h/.cpp` — shared asynchronous WebView2 environment and four controller slots, lifecycle generations, navigation policy, popup deferrals, muting, reset and recovery.
- `windows/src/browser_logic.h/.cpp`, `chatgpt_script.inc` — pure URL/auth/host/allowlist logic and verbatim Linux script inside DOM-ready wrapper.
- `windows/src/jsonlite.h/.cpp` — flat JSON, Unicode/surrogates, escaping and strict bounded integer parsing.
- `windows/src/ui_bridge.h/.cpp` — state/command dispatch, trusted origin boundary and content-slot sizing.
- `windows/src/focus_guard.h/.cpp` — external foreground-window safety net.
- `windows/ui/index.html`, `cronq.css`, `cronq.js`, `chatgpt_enter.js` — offline UI, exact Linux SVGs, translated GTK styles and original ChatGPT behavior.
- `windows/tests/browser_logic_test.cpp`, `jsonlite_test.cpp` — pure logic tests.
- `windows/tests/ui_preview_host.js`, `prepare_ui_preview.py` — optional browser-only mock preview; never shipped in `ui/`.

Phase 1 files extended for Phase 2:

- `windows/CMakeLists.txt` — WebView2 ON by default, new modules/tests, portable UTF-8 test compilation and one UI-copy target.
- `windows/scripts/build-mingw.sh` — WebView2 ON by default.
- `windows/scripts/wine-smoke.sh` — Phase 2 checks/unit tests, unit stdout, cleanup restricted to its own Xephyr process.
- `windows/src/main.cpp` — STA lifetime, browser/focus-guard startup and teardown before window destruction; phase label updated.
- `windows/src/overlay.h/.cpp` — UI state/toasts, HTML drag polling, controller bounds/position notifications, browser app switching and Linux Reset.
- `windows/src/app.cpp` — real browser IPC handlers. Phase 3 commands remain Phase 3 placeholders.
- `windows/src/com_support.h` — common callback exception boundary; no exceptions cross COM.
- `windows/src/webview2_include.h` — scoped suppression of the SDK's MSVC-only pragma warning under Clang.
- `windows/README.md` — current status, file layout, manual checklist, failure playbook and preview instructions.
- `antigravity/00_START_HERE.md`, `02_LINUX_FEATURES_AND_WINDOWS_EQUIVALENTS.md`, `06_PHASE2_BROWSER_AND_UI.md` — status and resolved documentation conflicts.

Concurrent work is preserved and is not claimed as Phase 2 work. In particular, another agent changed
`common.cpp` to call `log_rotate::ShouldRotate`; I added its existing `src/log_rotate.cpp` to CMake to resolve
the resulting undefined symbol. I did not implement that module or the other agents' Ghost/installer files.
The existing `.gitignore` change was present at task start; `.vscode/` appeared concurrently and was left alone.

## Test results (pasted, not paraphrased)

Latest completed cross-build checkpoint (before final whitespace-only cleanup and before later concurrent edits):

```text
[ 75%] Building CXX object CMakeFiles/cronq.dir/src/main.cpp.obj
[ 75%] Building CXX object CMakeFiles/cronqw.dir/src/main.cpp.obj
[ 79%] Linking CXX executable webview2_compile_check.exe
[ 82%] Built target webview2_compile_check
[ 86%] Linking CXX executable browser_logic_test.exe
[ 86%] Built target browser_logic_test
[ 89%] Linking CXX executable jsonlite_test.exe
[ 89%] Built target jsonlite_test
[ 93%] Linking CXX executable config_test.exe
[ 96%] Linking CXX executable cronq.exe
[ 96%] Built target config_test
[100%] Linking CXX executable cronqw.exe
[100%] Built target cronq
[100%] Built target cronqw

-rwxrwxr-x 1 saven saven  761856 Oct 22 11:29 /home/saven/Codding Projects/god-window/windows/build-mingw/browser_logic_test.exe
-rwxrwxr-x 1 saven saven 1473024 Oct 22 11:29 /home/saven/Codding Projects/god-window/windows/build-mingw/config_test.exe
-rwxrwxr-x 1 saven saven 1772544 Oct 22 11:29 /home/saven/Codding Projects/god-window/windows/build-mingw/cronq.exe
-rwxrwxr-x 1 saven saven 1772032 Oct 22 11:29 /home/saven/Codding Projects/god-window/windows/build-mingw/cronqw.exe
-rwxrwxr-x 1 saven saven  751616 Oct 22 11:29 /home/saven/Codding Projects/god-window/windows/build-mingw/jsonlite_test.exe
-rwxrwxr-x 1 saven saven  737280 Oct 22 10:49 /home/saven/Codding Projects/god-window/windows/build-mingw/sendkeys.exe
-rwxrwxr-x 1 saven saven  781312 Oct 22 11:29 /home/saven/Codding Projects/god-window/windows/build-mingw/webview2_compile_check.exe
```

Warning/error scan for both ON/OFF checkpoint logs returned no lines.
MSVC CI was not run. The newly added `ParseInt` unit cases are COMPILED but execution is deferred.

Earlier Wine smoke run, before the owner requested deferral and before subsequent edits:

```text
== preparing Wine prefix (first run takes a while)
== config parser unit test
PASS  config_test
== Phase 2 pure logic tests
PASS  browser_logic_test
PASS  jsonlite_test
== daemon
PASS  status reports running + visible
PASS  default geometry is compact 360 wide
== hotkeys through the low-level hook (SendInput)
PASS  Alt+X hides
PASS  Alt+X shows
PASS  Alt+G ghost on
PASS  Alt+G ghost off
PASS  Alt+M exclusive
PASS  Alt+M mirror
PASS  Alt+F fullscreen
PASS  Alt+F restores
PASS  Alt+D default size
PASS  Ctrl+Alt+X does NOT trigger
== CLI / IPC
PASS  opacity command accepted (no crash)
PASS  size clamps to minimum
PASS  size clamps to monitor
PASS  unknown command
== Phase 2 fallback / commands
PASS  WebView2 unavailable is logged without crashing
PASS  ChatGPT app expands
PASS  ChatGPT app state
PASS  Back without history returns Home
PASS  Load while hidden selects Browser
PASS  No modal in fallback
PASS  Reset restores Linux Home/Mirror/Solid
== clean kill
PASS  kill reply
PASS  not running afterwards
== supervisor restarts a crashed daemon
PASS  daemon up under supervisor
PASS  daemon restarted after being killed
PASS  crash.log written

Result: 31 passed, 0 failed
```

The agent-created disposable Wine prefix was removed after preserving its fallback log in `windows/build-mingw/phase2-fallback.log`.

The original baseline separately ended with:

```text
Result: 22 passed, 0 failed
```

Browser-only preview observations (mock host, not Windows/WebView2):

```text
19 chars · 2 lines
{ humanOptionsHidden: true }
{ toast: 'Ghost Writer arrives in Phase 3' }
stopTitle: 'Stop loading'
progressVisible: true
slot: '{"type":"slot","app":"browser","popup":false,"x":6,"y":87,"w":348,"h":491}'
console warnings/errors: []
Linux source parity: 19 SVGs copied verbatim; ChatGPT script unchanged inside DOM-ready wrapper; embedded script matches JS file.
```

JavaScript syntax checks and shell syntax checks completed without diagnostics before testing was deferred.

Failures encountered and resolved, with actual diagnostics:

```text
Xephyr did not start (is DISPLAY set?)
```

Desktop/X-server access outside the filesystem sandbox allowed the isolated Wine regression run.

```text
WebView2.h:19:9: warning: unknown pragma ignored [-Wunknown-pragmas]
```

Scoped suppression only around the vendor header's MSVC pragma fixed this; application warnings remain enabled.

```text
Error copying directory from "/home/saven/Codding Projects/god-window/windows/ui" to "/home/saven/Codding Projects/god-window/windows/build-mingw/ui": No such file or directory
```

Parallel EXE post-build copying raced. A single asset-copy target resolved the race and refreshes UI-only edits.

```text
ld.lld: error: undefined symbol: cronq::log_rotate::ShouldRotate(unsigned long long, unsigned long long)
```

This came from concurrent changes to `common.cpp`; registering the other agent's existing module resolved it at
the recorded build checkpoint. Shell-only builds also exposed two unused-global warnings, fixed with C++17
`[[maybe_unused]]` on WebView2-only state.

## Decisions I made (each marked `// DECISION:` in code)

1. Reset returns Home and restores Mirror/Solid, as explicitly confirmed by the owner. Browser reset remains separate from overlay reset; change the overlay wrapper to reverse it.
2. Toasts briefly reserve a row above the bottom navigation so windowed web content cannot cover the HTML feedback. The owner delegated this choice. Remove the toast rectangle adjustment in `cronq.js` to reverse it.
3. Reject duplicate JSON keys to avoid ambiguous native commands. Relax the `emplace` check to reverse it.
4. Bound the pre-ready message queue to 32; recompute state at ready. Change the queue bound in `ui_bridge.cpp` to reverse it.
5. Keep the dynamically loaded WebView2 loader mapped until process exit because outstanding callbacks may still use it. Add explicit release only after proving all callback lifetimes have ended.
6. WebView2 has no estimated progress event; show a midpoint loading indication until completion. Replace `Progress()` and loading UI together if better progress telemetry is introduced.
7. Use one UI copy target to avoid the observed parallel copy race and refresh changes without relinking. CMake uses `# DECISION:` because `//` is not its comment syntax.

## Deviations from the instructions

- Further runtime/integration checks were deferred at the owner's explicit request after confirming three other agents are working on later phases in this same folder. No further Wine run was started after that instruction.
- Reset follows the owner-confirmed Linux overlay behavior rather than the Phase 2 guide's Browser landing page.
- Toast reservation is the small, owner-delegated windowed-hosting adaptation described above.
- Followed the detailed Phase 2 instruction to use the mobile user agent and DPI-scaled bounds without device emulation; corrected the conflicting summary table.
- The fallback header already had exactly two buttons at task start; no redundant change was made there.
- Browser-only mock preview supplements the requested checks but does not prove native focus, OAuth or compositing.
- Minimal CMake integration was necessary for another agent's new logging dependency; no later-phase implementation was undertaken by this agent.

## Things I could not verify (needs the owner on Windows)

Run the complete numbered **Phase 2 manual checklist** in `windows/README.md` after Phase 3/4 integration:

1. Start with Runtime, DLL and complete UI folder; inspect controller creation logs and real UI.
2. Keep Notepad active while clicking every overlay control; verify no focus change or menu activation.
3. Use CLI website/search navigation, Back/Home and repeated switching; check current URL and loading/stop state.
4. After Phase 3 input is present, test Google/Microsoft/Apple OAuth, self-closing modal and login persistence after restart/reset.
5. Test external ChatGPT link diversion and popup-close/reset/quit while popup creation is pending.
6. Move/resize every edge/corner, double-click height, fullscreen/default size and 150% DPI layout.
7. Check whole-window click-through, opacity and audio muting while hidden, including popup audio.
8. Reset from Exclusive/Ghost and verify Home/Mirror/Solid with cleared editors and retained login.
9. Verify toast row restores after 2200 ms and Ghost controls remain accessible at minimum height.
10. Test missing loader/runtime/UI assets; expect fallback plus functional CLI/hotkeys.
11. After Phase 3, test ChatGPT Enter once, Shift+Enter newline and IME/modifier exclusions.

## Risks / things most likely to break on the first Windows run

1. Layered-window child compositing: UI or content blank/black despite controller-created logs. Follow the documented staged playbook before considering composition hosting.
2. Chromium child activation: `FOCUS_GUARD` restore/denied logs or caret flicker in Notepad. The guard mitigates this but native focus is unverified.
3. OAuth provider behavior/opener handling: inspect `NewWindowRequested uri=... isUserInitiated=...`, popup controller creation and `put_NewWindow`/`Complete` HRESULTs.
4. Ghost click-through across child windows: clicks may still hit web content; manual test every view and popup.
5. Integration drift: later agents are editing shared source/build files. Rerun the complete build, unit and Wine suites only after their work settles, then perform Windows acceptance.
6. Runtime process crashes: one Reload attempt per controller; inspect `ProcessFailed view=... kind=... exit=...`; full browser-process loss may still require restarting Cronq.

## Linux files untouched

The following command printed nothing at the report checkpoint:

```text
$ git diff --stat -- src Makefile scripts install.sh uninstall.sh tests cronq.desktop README.md assets DEVLOG.md
```

Working tree snapshot (most Windows/docs/shared files were already untracked at task start):

```text
 M .gitignore
?? .github/
?? .vscode/
?? antigravity/
?? shared/
?? windows/
```

## Questions for the owner

None. Phase 2 code is ready for integration; comprehensive testing is deferred as requested.
