# Windows fixes and verification

Date: 4 November 2026

## Reported problems and result

ChatGPT and the Chrome view opened as blank/black screens. The default overlay size was too large, and its positioning needed correction.

The Windows source was fixed, built with native Visual Studio/MSVC and WebView2, and installed over the existing Windows application. ChatGPT was reopened after installation. The user subsequently confirmed that the problem is fixed.

## What fixed the blank screens

### Separate native host windows for each WebView2 view

The decisive finding was that ChatGPT could render correctly inside WebView2 while the full overlay still appeared black. A WebView2 preview showed the real ChatGPT page, but a screenshot of the complete window showed only the surrounding controls and a black content area.

The UI and remote pages previously shared the same parent window. Their internal native windows could stack with the full-window black UI surface above the remote page.

`windows/src/browser.cpp` now creates a separate child host window for the UI, browser, ChatGPT, and sign-in popup. Layout explicitly places the UI host below the content hosts, positions each host in its assigned rectangle, and gives its controller bounds relative to that host. Closing a view also destroys its host. This prevents the UI surface from covering the loaded page during switching and resizing.

### Recovery after repeated process failures

The previous implementation attempted recovery only once per view and called `Reload()` even after the entire WebView2 browser process had exited.

The updated implementation:

- Recreates an affected view when its renderer exits or becomes unresponsive.
- Recreates the environment and controllers after a browser-process crash, using the same saved browser profile.
- Posts recovery work to the window message loop instead of closing controllers inside a COM event callback.
- Uses environment and view generations to reject stale callbacks and duplicate recovery messages.
- Retains a host-owned navigation URL for recovery.
- Attempts view recreation when a manual reload cannot run.
- Refreshes layout when the overlay is shown again.
- Reuses the loaded WebView2 loader instead of loading it again on every environment recovery.

The recovery message is declared in `windows/src/common.h`, handled in `windows/src/overlay.cpp`, and exposed through `windows/src/browser.h`.

### ChatGPT startup compatibility

ChatGPT and sign-in popups now use the installed WebView2 runtime's current browser identity, instead of the fixed Android Chrome 124 identity. The configured mobile user agent still applies to the Chrome view.

Failure to register the optional ChatGPT Enter-key helper is logged but no longer prevents the page from opening or shuts down the browser UI.

## Size and position fixes

Changes in `windows/src/geometry.cpp`, `windows/src/geometry.h`, and `windows/src/overlay.cpp`:

- Scale the configured phone dimensions for DPI, then proportionally fit them within 75% of the usable screen height after margins, also respecting available width.
- Keep Home compact and use the configured height for expanded views instead of a hard-coded expanded height.
- Clamp corner placement to the monitor work area, accounting for the taskbar and negative monitor coordinates.
- Preserve the appropriate top/bottom anchoring when changing height after moving the window.
- Use the current monitor for manual size limits and fullscreen.
- Make Default Size restore the fitted dimensions and the appropriate Home/expanded height.

On the tested display (1920×1020 usable area, 125% scaling), expanded dimensions changed from 450×800 physical pixels to 417×742. Bottom-left placement became `(15, 263)`, retaining a 15-pixel bottom margin. Home used 417×350.

## Tests that passed

### Native Release build

The Windows Release build completed successfully with Visual Studio Build Tools 2026/MSVC, including the WebView2 compile check and the new browser runtime test executable.

### CTest: 15 of 15 passed

The final run reported **100% tests passed, 0 tests failed out of 15**:

| Test | Result |
| --- | --- |
| `config_key_parity` | PASS |
| `config_parser` | PASS |
| `browser_logic_test` | PASS |
| `jsonlite_test` | PASS |
| `log_rotate_test` | PASS |
| `geometry_test` | PASS |
| `ghost_logic_test` | PASS |
| `snap_logic_test` | PASS |
| `ini_writer_test` | PASS |
| `key_translate_test` | PASS |
| `input_logic_test` | PASS |
| `image_logic_test` | PASS |
| `image_codec_test` | PASS |
| `crash_report_test` | PASS |
| `parity_config` | PASS |

The geometry test was extended to cover fitted dimensions on a scaled laptop display, expected corner placement, negative monitor coordinates, and oversized-window placement.

### Native WebView2 regression test

Added `windows/tests/browser_runtime_test.cpp` and its CMake build target. This is a separate, explicitly run acceptance test; it is not one of the 15 CTest tests. It ran with an isolated profile under `windows/build/acceptance-profile`, leaving the normal saved profile untouched.

The final live run passed:

- All controllers and the UI became ready.
- Six cycles through ChatGPT, browser, and Home, with 12 checks that web content had nonempty bounds, a visible native host, and stacking above the UI host.
- Hide/show operations during those cycles.
- Renderer readiness before each deliberate crash.
- Two separate ChatGPT renderer crashes, with successful view recreation after each.
- A deliberate whole-browser-process crash, followed by recreation of every controller and restoration of the UI.
- Loading the live ChatGPT document after recovery. Its reported title was `ChatGPT: Chat, Work, Create & Code with AI`.
- Capturing a rendered ChatGPT preview.
- Loading DuckDuckGo in the Chrome/browser view.

The final live run exited successfully. Its local output is in `windows/build/browser-live-test.log`; the rendered preview is in `windows/build/acceptance-profile/cronq/chatgpt-preview.png`. These are ignored build artifacts, not tracked source files.

### Full-window visual verification

Native window screenshots were inspected, separately from the WebView2 preview:

- ChatGPT was visible inside the complete overlay, including its prompt box and Log in button.
- Home displayed correctly after returning from ChatGPT.
- The Chrome view displayed the DuckDuckGo page, address bar, and surrounding overlay controls.

This distinction mattered: an early preview alone passed while the surrounding UI still covered the content. The full-window check caught that remaining defect before the final host-window fix.

### Installation and final checks

- Updated the installed binaries in `%LOCALAPPDATA%\Programs\Cronq`.
- Backed up the previous binaries in `windows/build/installed-backup`.
- Preserved the existing settings and WebView2 profile.
- Reopened the installed application in ChatGPT mode; status reported `VISIBLE=TRUE APP=chatgpt`.
- Verified that the installed `cronqw.exe` SHA-256 matched the tested Release binary.
- `git diff --check` passed.

## Repeating the checks

From the repository root, with CMake available:

```powershell
cmake --build windows/build --config Release --parallel 6
ctest --test-dir windows/build -C Release --output-on-failure
```

Run the browser acceptance test in a dedicated PowerShell session because the following commands change that session's AppData environment variables:

```powershell
$testRoot = Join-Path (Get-Location) 'windows\build\acceptance-profile'
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null
$env:LOCALAPPDATA = $testRoot
$env:APPDATA = $testRoot
& '.\windows\build\Release\browser_runtime_test.exe' --live
```

The test deliberately crashes its own isolated WebView2 processes. Live mode requires internet access and pauses for visual inspection. It does not sign in or submit prompts. `windows/README.md` also documents the changes and test procedure.

## Verification limits

ChatGPT sign-in/OAuth and authenticated conversation submission were not tested. The work did not establish long-duration stability, behavior on every Windows version, or native behavior across a physical mixed-DPI multi-monitor setup. Negative coordinates and fitting were covered by geometry tests. The successful checks address the observed rendering, repeated view opening, crash recovery, and default geometry problems; they do not guarantee that every unrelated Windows feature is defect-free.
