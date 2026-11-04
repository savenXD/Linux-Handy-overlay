# Windows integration review — 2026-10-22

This report supersedes verification claims in REPORT_PHASE_2/3/4. The owner authorized a combined build,
test and senior code review after the parallel phase agents finished. Linux implementation files were read
as the reference and left untouched.

## Changes and reasons

- Extracted a deterministic input router. Mirror now forwards releases as well as presses; Exclusive
  forwards repeats and consumes the matching release even if the mode/target changes. Clipboard shield
  retains the matching release. Ghost's self-echo guard no longer disables physical Alt+W cancellation.
- Hook callbacks capture modifier, lock, scan, extended-key and repeat information before the UI handoff.
  They track physical modifiers instead of querying stale asynchronous state on the UI thread. Hook
  initialization/shutdown no longer destroys synchronization objects while a worker could still use them.
- Restored saved default key mode after input initialization; made grab status reflect actual hook state.
  Activity refreshes the idle deadline, stale timer messages cannot release a fresh target, and hidden
  overlays cannot become a typing target. Mode switches, hide and Reset release routing.
- Browser typing uses the live popup controller after creation completes. Clicking content clears the
  URL-input route. CDP dispatch failures are logged. Normal text paste no longer substitutes an old Snap;
  the latest-image fallback is restricted to advertised image clipboard formats.
- Added bounded DIB decoding for 24/32-bit top-down/bottom-up clipboard images, checked dimensions,
  strides, masks and buffer lengths, and centralized WIC PNG encoding with pixel-format negotiation.
  Saved-image fallback reads are bounded. Bitmap allocation/capture failures restore the overlay safely.
- Snapshot naming/pruning now sorts same-second collision suffixes numerically and keeps unrelated
  `snap-*.png` files out of retention deletion. Clipboard ownership failures free untransferred handles;
  clipboard contention does not block the UI in retry sleeps. Filesystem failures are logged.
- Ghost cancellation releases an injected held letter and modifiers. Instant mode sends batches of 16
  with Unicode injection instead of running the slow dwell loop. Modifier settling includes Shift and
  cancels rather than forcing a physically held modifier up. Foreground events cancel typing even during
  a long pause; returning focus does not resume it. Progress/ETA updates have a 500 ms timer.
- Timer creation failures unwind typing/guard/capture state. Lock/suspend cancel writing and release
  routing; resume/unlock explicitly reinstall hooks. Caps Lock state is captured by the hook. The optional
  UIAccess token is considered when deciding whether an elevated writer target is permitted.
- The UI flushes the latest editor value before Start and hydrates saved timing switches without echoing
  settings back. JSON bridge capacity accommodates normal editor contents. Invalid Unicode sequences
  are rejected by the writer decoder. INI timing updates preserve BOM/comments/spacing and save via a
  checked temporary file plus replacement.
- Crash reporting now uses bounded fixed buffers and Win32 writes rather than CRT stderr/file locks on
  the faulting thread. Reports include exception, module base/offset and x64 registers. Log-size checks
  use all 64 bits and open logs with delete sharing for rotation.
- IPC retries transient pipe connection gaps with bounded waits, rather than exhausting retries in a busy spin and falsely reporting that the daemon is absent. A 5,000-request native regression check covers repeated connections. It rejects truncated/overlong requests and publishes its target atomically. Startup event failures and
  shutdown waits preserve worker synchronization lifetimes. Synchronous pipe draining is cancelled on
  shutdown before joining the worker.
- Generic COM ownership was separated from WebView2 callback helpers, so the OFF build no longer needs
  WebView2 headers through the screenshot module. Optional UIAccess builds embed their manifest in MinGW
  too, copy matching assets/loader and retain the same executable when supervised. MSVC link arguments
  preserve manifest paths containing spaces.
- Setup uses VERSION, enforces supported architecture/minimum Windows version, requires all payloads,
  checks runtime installation failures, offers launch, removes deselected autostart on upgrades, and
  tracks PATH ownership. Purge is an actual unchecked choice shown before uninstall proceeds; silent
  uninstall retains data. Setup waits for the overlay to stop instead of assuming a fixed sleep suffices.
- Packaging checks native CMake exit codes, validates complete output before replacing distribution files,
  and copies the UI belonging to that build. Developer scripts validate the full payload before installation, wait for processes from their own install directory to exit, handle absent user PATH/Run keys, and create the Run key only when absent, preserving unrelated startup values. Generated
  distributions are ignored by Git. CI's Linux-file guard also covers codex branches and quotes base refs.
- Wine smoke now includes all relevant suites, native typing/cancel/focus/Instant Unicode integration and
  actual PNG output. The soak uses measured memory, reports Wine's zero handle count as unavailable, checks
  CLI failures, and snapshots binaries so concurrent clean builds cannot invalidate its run.

## Verification

Evidence is saved in `windows/build-review/` (ignored generated output).

| Check | Result | Evidence |
| --- | --- | --- |
| Clean x64 Release build, WebView2 ON + optional UIAccess | Passed, zero compiler warnings | `build.log`, `build-final.log` |
| Clean x64 Release build, WebView2 OFF | Passed, zero compiler warnings | `fallback-build.log`, `fallback-build-final.log` |
| Full CTest suite on each variant | 15/15 passed on each | `ctest.log`, `fallback-ctest.log` |
| Wine smoke/native integration | 46 passed, zero failed | `smoke.log` |
| Repeated native IPC connections | 5,000 requests passed; also included in smoke | `reconnect-after.log` |
| Host ASan/UBSan stress | 20,000 parser/DIB/INI cases and 50 seeded typo reconstructions passed | `parser-stress.log` |
| Fresh SDK configuration/download | Pinned WebView2 package downloaded and configured successfully | `fresh-sdk-configure.log` |
| Inno Setup 6.7.3 | Installer compiled successfully | `installer-build.log` |
| Installer running upgrade | Stops supervised daemon, keeps profile, removes deselected autostart | `installer-upgrade-test.log` |
| Normal uninstall | Keeps profile, removes program files and owned PATH entry, preserves unrelated PATH | `installer-upgrade-test.log`, `uninstall-verification.log` |
| Interactive purge uninstall | Choice starts unchecked; selecting it deletes roaming/local test profile and installed files | `installer-purge-test.log`, `purge-ui.log` |
| Packaging failure paths | Failed configure/build and incomplete payload preserve prior distribution; success copies matching built UI | `packaging-tests.log` |
| Script checks | Installer PowerShell scripts parse; JavaScript and shell syntax checks pass | `script-checks.log` |
| Developer installer regressions | Existing Run values survive; missing key is created; process exit is awaited; a stuck process blocks mutation | `install-script-test.log` |
| UIAccess artifact | Embedded `uiAccess="true"` manifest verified in cross-built executable | `cronqw_uia.exe` resource inspection |
| Linux isolation | No diff in Linux source, build, scripts, tests, README or assets | `git diff --stat` for the protected paths is empty |
| Full memory soak | In progress; final measurements will be recorded after the uninterrupted run | `soak.log` |

The host sanitizer run disables LeakSanitizer because this sandbox uses ptrace; AddressSanitizer and
UndefinedBehaviorSanitizer checks were active. Wine's `GetProcessHandleCount` reports zero, so no claim
of measured Windows handle stability is made. The soak uses actual working-set measurements, not
invented tasklist fallback values. Earlier interrupted/failed runs are not counted as passes.

Installer tests used a disposable Wine prefix and an Evergreen registry-detection fixture; they did not
install or exercise a real WebView2 runtime. The UI purge path was driven through actual checkbox/button
messages, with the initial unchecked state and resulting filesystem deletions asserted.

The review caught a short IPC reconnect failure during sustained CLI cycling. The client now waits
between connection retries and recognizes transient pipe states; it does not replay a successful or
partial command write. Both full test variants and the expanded smoke suite were rerun afterward.

The package is `windows/dist/cronq-setup-0.1.0.exe`. Keep it with this report when transferring to Windows.
The default `windows/build-mingw/` output is rebuilt from the final sources too.

## Remaining Windows acceptance

Use the single numbered checklist in `windows/README.md`; `windows/PARITY.md` maps each feature to it.
The critical remaining gate is a real Windows 10/11 run with WebView2: browser and ChatGPT login/OAuth,
focus preservation, Mirror/Exclusive CDP input, clipboard image attachment, display-affinity exclusion,
DPI/monitor changes, desktop recovery, actual Evergreen installation and sign-in autostart. Native MSVC
CI, signing/UIAccess installation, and sustained WebView2/handle/GDI measurements are still required.

Wine exercises the fallback shell because the WebView2 runtime is unavailable there. It cannot establish
that these native Windows paths work perfectly. The delivered installer is unsigned.
