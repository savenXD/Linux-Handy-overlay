# Windows 11 screenshot investigation: Deepak's focus app

Last updated: 2026-11-13. Status: **OPEN — the user reports that capture still fails.**

For the move from Linux to native Windows, start with the root
[Windows agent handoff](../WINDOWS_AGENT_HANDOFF.md). A later, target-specific
[CodeTantra SEA 5.2.0 investigation](docs/codetantra-sea/README.md) now supplies
recovered capture-policy code and a Windows `0x11` log. The historical unnamed
Deepak-app context below is retained separately; identity with CodeTantra was not
established. Statements below about a missing target concern that earlier stage.

This is the continuation record for the screenshot work. Read it before changing
capture code or claiming that a new backend fixes the reported app. It records
implementation, tests, user feedback, research and unresolved questions separately.
The documentation update itself does not change capture behavior.

## Problem and user constraints

- Target platform: Windows 11 x64. Windows 10 was discussed initially, then removed
  from this investigation's scope. Older installer/platform requirements are not a
  Windows 10 screenshot compatibility certification.
- The target is a custom focus/Pomodoro app written by the user's mentor, Deepak.
  Its actual product name, version, implementation language and binary identity
  have not been supplied. "Deepak's app" is a description, not an executable name.
- The original symptom was a fully black screenshot while the app's fullscreen
  content remained visible. The user initially said the PNG was black, with some
  uncertainty; no original image or clipboard artifact was supplied for inspection.
- After v1.3.0, the user reported that none of the proposed solutions worked and
  screenshots were still unavailable. This is negative user acceptance evidence,
  not a captured per-backend trace or a controlled native test report.
- The user explicitly ruled out capture timing and the wrong monitor as the
  explanation. Do not repeatedly propose delay tuning or monitor selection as the
  main solution without new contradictory evidence.
- The app is described as strict, fullscreen and screenshot-protected. Those are
  user observations; its exact permissions and protection mechanism are unknown.
- The user reports that Deepak offered a bounty for a bypass. The bounty scope
  has not been independently inspected. No communication with Deepak occurred.
- Use the user's existing Windows installation in dual boot. The VM proposal was
  rejected; do not make a VM a prerequisite for the next experiment.
- Requested outcome: capture the visible app reliably. A universal, downside-free
  "whatever the user sees" guarantee has not been established.

No copy of this app, native trace, source inspection or decompilation has yet been
performed. No consultation with Linus Torvalds occurred; the earlier request for
that perspective was not an actual external consultation.

## Current release and delivery checkpoint

The implemented work was committed as
`2e70af8e894c09d5772305de2a75dd3ded0b5748` and published as
[v1.3.0](https://github.com/savenXD/Linux-Handy-overlay/releases/tag/v1.3.0).
The repository's canonical name is `savenXD/Linux-Handy-overlay`; the older
`Linux-SEA-Cheat` remote redirects there.

- [Main Windows build](https://github.com/savenXD/Linux-Handy-overlay/actions/runs/43388015224)
  and [tag/release build](https://github.com/savenXD/Linux-Handy-overlay/actions/runs/43388333352)
  completed successfully, including MSVC compilation, CTest, installer-script and
  purge regressions, and Inno Setup packaging.
- The release publishes `cronq-setup.exe` and `SHA256SUMS.txt`. The downloaded
  v1.3.0 installer SHA-256 was
  `0f0cdc6f7807f1627b14f51ab6f7f78a1ea86e54d015dfe469533c7c7098917f`.
- The live [download frontend](https://ctcode.vercel.app) was compared with
  `frontend/index.html`. Its existing double-click error-label/secret-code flow
  was verified to resolve the stable
  [latest installer URL](https://github.com/savenXD/Linux-Handy-overlay/releases/latest/download/cronq-setup.exe).
  The installer downloaded through that URL matched the published checksum.
- A source push alone does not replace the installer. Future releases must
  publish the correctly named asset and be marked Latest. See
  [the frontend instructions](../frontend/README.md).

These are release-time observations, not a claim that the URL will always serve
v1.3.0. The exact installed version used in the user's unsuccessful retest was
not independently verified.

## Implemented approaches and their outcomes

| Approach | What changed or was tried | Actual evidence / outcome |
| --- | --- | --- |
| Original GDI screenshot | `BitBlt` capture was the original path | User reported a black screenshot with the focus app. No original artifact was inspected. |
| DXGI Desktop Duplication | Alt+P/camera/`cronq snap` now use GPU desktop duplication first; owning-adapter selection, row pitch, rotation and output composition are handled | Implemented and built. Relevant pixel/publication paths passed under Wine. Native DXGI compatibility with Deepak's app is not demonstrated; overall user retest failed. |
| GDI compatibility fallback | Automatic mode tries GDI for unavailable duplication or an ambiguous black DXGI output | Implemented. GDI fixture pixels and normal capture worked under Wine. It does not run after an explicit protection/access status. Not a demonstrated focus-app fix. |
| WGC monitor capture | Temporary Alt+O / `cronq snap-wgc`, using `CreateForMonitor`, with no DXGI/GDI fallback | Implemented and released. Routing and unavailable-WGC behavior passed under Wine. User reports no successful solution; no native frame/error trace identifies whether this backend ran. |
| WGC foreground-window capture | Temporary Alt+Shift+O / `cronq snap-wgc-window`, using `CreateForWindow` and the actual item size | Implemented and released. Shortcut routing/failure isolation checked under Wine. No successful Deepak-app capture or controlled native WGC-window result is available. |
| WGC adapter/device recovery | Target-display adapter where available; one fresh session/device retry for device removed/reset/hung/access lost | Implemented. Cross-build/CI passed. Real hybrid-GPU and device-loss recovery remain unverified; not evidence of defeating protection. |
| Worker, encoding and publication reliability | Background capture/encoding, cancellation, atomic file publication, clipboard retries, separate save/clipboard errors | Fixture/daemon tests passed, including saved/clipboard PNG equality and busy clipboard retry. These address reliability around capture, not recovery of withheld pixels. |
| Black-frame handling | Entirely black output is rejected and old successful files/clipboard are preserved | Implemented and tested with fixtures. This prevents publishing a new black image; it does not make the focus app capturable. A genuinely black desktop is also rejected. |
| Protection/desktop checks | Foreground display-affinity check, input-desktop check, DXGI masking status, explanatory errors | Implemented. The affinity early exit also confounds the experiment, as detailed below. |

The broad user failure report applies to the attempted solution as a whole.
Do not rewrite it as "native DXGI, WGC monitor and WGC window each returned black"
unless logs or new measurements establish those individual results.

## Confirmed implementation issue: capture can stop before the API runs

Source inspection after the unsuccessful retest found an overaggressive precheck
in [screen_capture.cpp](src/screen_capture.cpp):

1. `WindowProtectsCapture` calls `GetWindowDisplayAffinity` and returns true for
   any nonzero affinity. Query failures are logged as unknown and do not establish
   protection.
2. `screen_capture::Capture` returns `Status::ProtectedContent` before monitor
   enumeration or DXGI/GDI/WGC capture when that check is true.
3. `screen_capture::CaptureWindow` has the same early exit before its WGC call.
4. Separately, `ReadOutput` returns `ProtectedContent` when DXGI reports
   `ProtectedContentMaskedOut`, without copying that frame's pixels.

Therefore a failed shortcut can mean **Cronq refused to attempt capture**, rather
than **the selected capture API actually returned black**. The focus app's
affinity has not been measured here, so the precheck is a possible contributor,
not a confirmed explanation of the user's machine.

This issue was explained in the conversation, but **the precheck has not been
removed or changed**. The release and current source still contain it. Likewise,
the proposed raw diagnostic mode has not been implemented. Existing protection
fixtures intentionally expect a restriction response; their success does not
validate a bypass attempt and they would need review if experiment semantics change.

## Existing outputs and how to interpret failure

All paths below are under `%LOCALAPPDATA%\cronq`:

| Action | History directory | Latest successful PNG |
| --- | --- | --- |
| Alt+P / camera / `cronq snap` | `snaps` | `latest_snap.png` |
| Alt+O / `cronq snap-wgc` | `snaps-wgc` | `latest_snap_wgc.png` |
| Alt+Shift+O / `cronq snap-wgc-window` | `snaps-wgc-window` | `latest_snap_wgc_window.png` |

`snap_save` controls disk saving. These are successful-output paths, not raw
diagnostic dumps. On a failure, an existing latest PNG or clipboard image can
belong to an earlier attempt. Check its timestamp and the corresponding log; its
mere presence is not evidence that the latest shortcut succeeded.

Existing diagnostics means `%LOCALAPPDATA%\cronq\cronq.log`: target PID/affinity or
query error, capture operation/backend, GPU/item geometry where reached, HRESULTs
and publication outcomes. It is not a separate installed tool. These logs do not
intentionally record window titles, app contents or screenshots. The failure text
distinguishes blank output, protection, unavailable desktop, capture denial,
unavailable backend and cancellation. Inspect the actual event instead of treating
all of them as a black-image result.

### Source and test reading map

| Location | Responsibility |
| --- | --- |
| [screen_capture.cpp](src/screen_capture.cpp) / [header](src/screen_capture.h) | Backend/status selection, affinity/desktop checks, DXGI/GDI readback, composition and black rejection |
| [wgc_capture.cpp](src/wgc_capture.cpp) | WinRT monitor/window items, D3D device selection, frame/readback waits, teardown and device-loss retry |
| [snap.cpp](src/snap.cpp) | Shortcut-time window selection, worker ownership, encoding, history/latest files, clipboard publication and UI completion |
| [hotkey.cpp](src/hotkey.cpp) / [main.cpp](src/main.cpp) | Experimental shortcut and CLI routing |
| [capture_logic.cpp](src/capture_logic.cpp) | Surface copying/rotation and black-pixel logic |
| [capture_logic_test.cpp](tests/capture_logic_test.cpp) | Deterministic surface tests |
| [capture_runtime_test.cpp](tests/capture_runtime_test.cpp) | Pixel/protection fixtures, backend-required flags and daemon publication checks |
| [wgc_hotkey_runtime_test.ps1](tests/wgc_hotkey_runtime_test.ps1) | Native experimental-hotkey acceptance script |
| [capture-wine-test.sh](scripts/capture-wine-test.sh) | Repeatable Wine capture/routing regressions; not native GPU validation |
| [Windows workflow](../.github/workflows/windows.yml) | Build, CI checks, installer and tagged release publication |

## Tests completed versus acceptance still open

Completed during the implementation/release work:

- Full Windows/WebView2 cross-build; 21 CTest checks under Wine.
- Surface logic: padded rows, rotations, opaque alpha, invalid buffers, black
  versus nonzero dark pixels; sanitizer checks were also reported for this logic.
- GDI fixture pixels, daemon screenshot publication, saved/clipboard PNG equality,
  clipboard contention, cancellation and recovery after unavailable experiments.
- Both experimental hotkeys, modifier filtering, Alt+K lock and hidden-state
  handling in the repeatable Wine capture script.
- Native MSVC build and CI tests/installer packaging on GitHub's Windows runner.

Wine skipped native DXGI/protection behavior and could not supply actual WGC
frames. The interactive PowerShell hotkey test was not run locally. A successful
Windows CI build is not a test of the user's unlocked Windows 11 desktop, GPU or
focus application.

Still open: controlled native DXGI/WGC pixels, actual capture-border lifetime,
mixed/portrait displays, HDR treatment, hybrid GPUs, real device loss, secure-desktop
transitions and target-specific capture. The user has supplied negative acceptance
feedback for the focus app; do not leave that result described only as "pending".
See [SCREENSHOTS.md](SCREENSHOTS.md#verification) for executable test commands.

## Research findings: candidates, not implemented fixes

Searches covered Reddit, YouTube, X, official API/OBS documentation, published
research and source repositories. No third-party bypass tool was installed or run.

| Candidate | Evidence and limitation | Project status |
| --- | --- | --- |
| In-process display-affinity reset | IOActive reports a successful Signal Desktop demonstration by changing affinity from within the owning process; external/admin calls failed. This is relevant if Deepak uses the same API, which remains unknown. | Proposed target-specific experiment. No helper, injection or target modification implemented. |
| Direct rendering capture | OBS documents direct DirectX/OpenGL Game Capture, while warning that its specific-window mode is not for ordinary non-game windows. Target rendering technology matters. | Researched only; no rendering hook added or tested against this app. |
| DWM capture-path modification | Hawkeye's author reports bypass results with a temporary compositor capture-path modification and build/configuration limits. This was not independently reproduced here. | Researched only; no DWM patch or driver implemented. Not established as a durable Windows 11 feature. |
| VM-host console capture | Capturing a guest console from its host was suggested as a different capture boundary; efficacy for this app was unverified. | Rejected by the user; no VM setup attempted. |
| External HDMI capture | Suggested as a separate signal acquisition route, subject to hardware/output/protection constraints. | No hardware integration or real test. Not a guaranteed solution. |
| Elevation / UIAccess | Addresses some input/overlay privilege issues, not a general ownership or capture-protection override. | Not a demonstrated screenshot fix; no target-specific success. |
| Generic GPU-settings fixes | A YouTube tutorial inspected through its transcript addresses OBS GPU selection, not intentional exclusion. | Does not establish a bypass; not adopted as a solution for this reported app. |

Reddit contained conflicting explanations, including overly broad claims that
in-process changes cannot work. Prefer the API ownership contract and the
published target-specific demonstration over anonymous generalizations. X searches
did not yield a relevant technical demonstration that could be verified. No claim
is based on having watched or reproduced a video demonstration.

Ordinary window display affinity and DRM/protected video are different mechanisms.
Microsoft describes display affinity as a restriction on a specific set of public
capture APIs, without guaranteeing strict protection. That neither proves a bypass
for this app nor establishes that a Netflix/video capture technique transfers to it.

### Sources consulted

- [Microsoft: SetWindowDisplayAffinity](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity) — ownership requirement and flag semantics.
- [Microsoft: Desktop duplication](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/desktop-duplication-api) — GPU capture and protected-video limits.
- [Microsoft: DXGI protected-content masking](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/ns-dxgi1_2-dxgi_outdupl_frame_info).
- [IOActive: Signal Windows Desktop contentProtection bypass](https://www.ioactive.com/signal-windows-desktop-contentprotection-bypass/) — author-reported demonstration; not a Deepak-app result.
- [OBS: Game Capture Source](https://obsproject.com/kb/game-capture-source) — supported rendering route and non-game caveat.
- [Hawkeye: BitBlt readback-mode research](https://hawkeye-leo.github.io/hawkeye/research/capture/03-readback-mode/) — author-reported compositor modification; untested here.
- [Londopy/capture-bypass](https://github.com/Londopy/capture-bypass) — inspected project description; not audited, installed or endorsed as universally effective.
- [Reddit: discussion of Electron capture exclusion](https://www.reddit.com/r/Hacking_Tutorials/comments/1qibsli/anybody_here_who_has_good_experience_with_windows/) — conflicting community claims, not proof.
- [YouTube: OBS black-screen GPU-settings tutorial](https://www.youtube.com/watch?v=vm2rJl_DslY) — transcript examined; not evidence of bypassing deliberate exclusion.

## Reverse-engineering tooling checkpoint

The user requested REA from [morluto/rea](https://github.com/morluto/rea). Setup was
completed on the operator's Linux workstation, separate from Cronq's dependencies:

- `reverse-engineer-anything` skill installed, version 62.
- REA MCP/CLI 6.9.0, with 255 advertised MCP tools and 6 prompts.
- Isolated Node.js 22.23.4, Ghidra 12.2 and Temurin JDK 25.0.5+8. Ordinary project
  Node/Java selections were not globally replaced.
- Codex's MCP registration was added with a backup, preserving unrelated settings.
  After restart, a **live MCP call in this chat** confirmed connection and Ghidra
  availability, with no target open and no cleanup pending.
- A harmless generated ELF fixture was decompiled and its expected arithmetic
  recovered. This tests the installation; it is not analysis of Deepak's app or
  a Windows runtime capture test.

Tool advertisement does not make every provider available: optional IDA, Hopper,
Android and other specialized engines are not all installed. The available Ghidra
and built-in artifact routes are the starting point for this Windows-app question.
Linux can statically inspect a Windows artifact; observing its real Windows runtime
requires the appropriate host/environment. No target has been supplied or selected.

## Next work, in order

1. Obtain the exact target executable/install directory and version. Record its
   hash and artifact type before choosing native, managed, Electron or packaged
   analysis. Do not assume Electron because the research example used it.
2. Trace the actual capture-protection mechanism in that supplied artifact with
   REA. Keep static findings, inferred behavior and runtime observations distinct.
3. Implement the previously proposed experimental capture measurement mode if
   needed: log affinity without prematurely refusing, record which backend really
   ran, and retain raw results including black frames in a separate diagnostic
   directory. No such mode or command exists today. Keep actual OS access checks,
   cancellation and bounded waits; this is not permission to change a secure desktop.
4. Repeat a controlled native Windows 11 run: baseline without the focus app,
   target active, then baseline again. For each shortcut record installed Cronq
   version, target version/hash, Windows build, GPU/driver, timestamp, exact error,
   new-file identity and relevant log excerpt. Respect the user's timing/monitor
   exclusions; collect these facts to identify the capture path, not to restart
   those guesses.
5. If standard display affinity is confirmed, evaluate the researched in-process
   reset as a separate, target-specific experiment. Do not describe it as already
   integrated, safe for every process, a hardware capture path or a DRM solution.
6. Keep the issue open until a fresh PNG actually contains the target's visible
   content and the clipboard matches in repeatable native tests. A generic fixture
   pass, successful build, source push or suppression of a black output is not that
   acceptance result.

For every new attempt append: date, build/target identity, hypothesis, code change,
actual backend reached, test environment, outcome, evidence location and remaining
unknowns. If a measurement contradicts this record, update the current status and
preserve the earlier result as history rather than silently reclassifying it.
