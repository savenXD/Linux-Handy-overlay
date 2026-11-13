# Windows screenshot reliability

The current investigation targets Windows 11 x64. See the
[adversarial Windows 11 review](CAPTURE_REVIEW_WINDOWS11.md) for the blocking
mechanisms, capture responses and limits of a hardware-level capture claim.

## Temporary Alt+O experiment

Alt+Shift+O additionally captures the external foreground **window** directly with
`CreateForWindow`. Its dimensions come from the WGC capture item rather than a
monitor or Win32 window rectangle. The window is selected when the shortcut is
pressed. Its files live in `snaps-wgc-window` with latest index
`latest_snap_wgc_window.png`; CLI equivalent: `cronq snap-wgc-window`.

Both experimental modes select a D3D device on the adapter owning the target display
where DXGI exposes that association. Indirect displays without a matching output
retain the default-GPU path. Device removed/reset/hung/access-lost errors permit
one fresh device/item/pool/session attempt within the existing time budget.
Access denial, protection, black pixels and unsupported APIs do not trigger that
retry. Logs include GPU vendor/device/LUID, capture-item dimensions, affinity
values or explicit query failure, and device-removed reason. No window titles or
application contents are logged.

Alt+O (or `cronq snap-wgc`) uses **Windows Graphics Capture only**. It never falls
back to another backend, so an experimental success cannot be mistaken for a
successful GDI/DXGI fallback. Alt+P and the camera button keep their existing path.
Alt+O works while hidden; Cronq's Alt+K lock disables this shortcut. It shares
the single-capture debounce, worker, encoder and clipboard publication pipeline.

Windows' capture border is left enabled. There is no picker: the monitor(s) are
selected by the same configured screenshot scope as Alt+P. The free-threaded frame
pool runs off the UI thread; session, pool and frame are explicitly closed on
every exit path. Capture and GPU readback have finite waits and cancellation.
The cursor is excluded so a cursor over an otherwise black frame does not make
that frame appear successful. Output is 8-bit SDR, without custom HDR tone mapping.

Experimental captures go to `%LOCALAPPDATA%\cronq\snaps-wgc`, with the same retention
limit as ordinary snaps. `%LOCALAPPDATA%\cronq\latest_snap_wgc.png` is the latest
successful experimental image. Normal snap files and `latest_snap.png` are not
replaced by an Alt+O capture. Successful results are also copied to the clipboard.
Failures preserve existing files/clipboard and are prefixed `WGC test:` in feedback.
Known foreground protection and inaccessible desktops are reported before capture;
this mode does not remove another application's capture protection.

To compare on the focus app: take Alt+P and Alt+O in the same focus session, inspect
both PNGs, and check `%LOCALAPPDATA%\cronq\cronq.log` for `WGC` diagnostics if Alt+O
fails. A temporary colored display border is expected. The experiment is isolated
in `wgc_capture.cpp`, `Backend::Wgc`, `snap::RunExperimental` and `Id::SnapWgc` for
straightforward removal after the test.

Alt+P, the camera button and `cronq snap` share the same capture pipeline.
It captures the configured scope using DXGI Desktop Duplication first, creating a
D3D11 device on the adapter that owns each display. Frames are read back with
their GPU row pitch and display rotation, then composed in desktop coordinates.
Devices and duplication interfaces are released after every snap so a later snap
does not reuse a lost session after a fullscreen, GPU or desktop transition.

GDI (`BitBlt` into an initialized top-down DIB section) is a compatibility fallback
when duplication is unsupported, unavailable or returns a completely black output.
There is one duplication recreation attempt after access loss, finite frame and
readback waits, and cancellation during shutdown. Capture, encoding and file writes
run on a worker. PNG files and `latest_snap.png` are published through temporary
files. The UI retries a busy clipboard up to 20 times at 25 ms intervals and reports
disk and clipboard failures separately.

An entirely black composed image is treated as an ambiguous failure. It is neither
saved nor put on the clipboard, including when the actual desktop is entirely
black. A dark image containing any nonzero RGB pixel is preserved. Black pixels
alone cannot establish whether an application is intentionally blocking capture.
Failed captures preserve the previous clipboard and saved screenshots; they do not
replace them with a black image. The failure icon, toast and log explain the result.

Explicit foreground display affinity, DXGI protected-content masking and inaccessible
or different input desktops produce a restriction error. There is no fallback after
an explicit protection/access error. Administrator mode and UIAccess are not general
solutions to capture restrictions. Supported Windows capture APIs do not guarantee
access to protected surfaces or secure desktops. This implementation does not promise
to capture through an application's intentional protection. The focus app reported
by the user has not been reproduced or inspected.

Logs in `%LOCALAPPDATA%\cronq\cronq.log` record backend, output geometry, operation
and HRESULT on failure, and publication outcome. They do not record window titles,
application contents or screenshots. Capture protection for Cronq's own overlay is
restored on success, failure, cancellation and shutdown.

## Verification

`capture_logic_test` runs in CTest and checks padded GPU rows, all four rotations,
opaque alpha, malformed input, and black versus genuinely dark pixels.

On an unlocked native Windows machine, build and run:

```powershell
cmake -S windows -B windows/build -A x64
cmake --build windows/build --config Release
ctest --test-dir windows/build -C Release --output-on-failure
& windows/build/Release/capture_runtime_test.exe --require-dxgi --require-protection
& windows/build/Release/capture_runtime_test.exe --require-wgc
& windows/build/Release/capture_runtime_test.exe --require-wgc-window
```

The interactive test paints known red/blue pixels and verifies GDI, DXGI and the
automatic pipeline against them. It checks blank rejection, explicit window
protection and cancellation. Without `--require-dxgi`, an unsupported DXGI runtime
is reported as a skip, useful under Wine. Wine does not verify native GPU capture.

`--wgc` exercises WGC and reports unavailable support as a skip. `--require-wgc`
requires it to capture the known fixture correctly and checks a black fixture.
These options can briefly display the Windows capture border. They are opt-in;
the default test does not start a WGC session.

`tests/wgc_hotkey_runtime_test.ps1` checks the temporary shortcut, modifier filtering,
Alt+K lock and hidden-state preservation against a running test daemon. Capture
success is deliberately checked separately using `--require-wgc`.

The Alt+O build cross-compiles with WebView2 enabled. Under Wine, all 21 CTest checks
and existing pixel/daemon tests pass. Injected Alt+O dispatches WGC without fallback,
Ctrl+Alt+O and locked Alt+O do not dispatch, an unavailable WGC backend reports failure
without replacing either existing latest PNG, and normal captures still work afterward.
Actual WGC frames, its border lifetime and the custom focus app remain unverified on
native Windows 11. The test build is intended for that experiment.

v1.3.0 adds the window path and monitor-associated GPU/device-loss handling. The
repeatable `scripts/capture-wine-test.sh` runs the CTest suite, pixel/clipboard tests,
both experimental hotkeys with modifiers/lock checks, image preservation on failure,
and normal capture after experimental failures. These checks pass under Wine.
Actual WGC window pixels, hybrid-GPU behavior and recovery from real device loss
still require the native Windows 11 checks above.

With a test daemon running and screenshot saving enabled, also run
`capture_runtime_test.exe --daemon-snap`. This verifies the actual capture worker,
PNG file publication, CF_DIB pixel content, equality of saved and clipboard PNGs,
and successful retry while another thread briefly holds the clipboard. Run this
on a test account: it takes two screenshots and replaces the clipboard.

Local validation on 2026-11-13: full Windows/WebView2 cross-build passed; all 21
CTest checks passed under Wine; interactive pixel capture and daemon publication
tests passed, including an observed busy-clipboard retry. The portable surface
tests also passed with AddressSanitizer/UndefinedBehaviorSanitizer (leak checking
disabled because the sandbox uses ptrace). Native DXGI and display-affinity checks
were skipped under Wine and remain pending. JavaScript syntax validation passed.

The adversarial fixture additionally exercises actual blanking, actual exclusion
against a known green backing window, and recovery after those protections are
removed. These new native Windows 11 checks remain pending until run on that OS.

Native manual checks still required:

| Scenario | Expected result |
| --- | --- |
| Focus app, windowed and fullscreen | Correct saved PNG and pasted image, or a restriction/blank error with diagnostics |
| Ordinary GPU-rendered fullscreen app | Correct image through DXGI; no UI freeze |
| Mixed GPU adapters, negative monitor origins, portrait display | Every requested display present and correctly oriented |
| HDR | Usable 8-bit SDR screenshot; advanced HDR tone mapping is not implemented |
| UAC, lock screen or separate input desktop | Restriction error; no black file published |
| Clipboard held briefly by another process | Capture saved, clipboard publication retried without blocking the message loop |
| Clipboard unavailable for the retry interval | Saved image retained and clipboard failure reported |
| Disk unwritable / disk full | Clipboard result retained and save failure reported |
| Rapid shortcuts / exit during capture | Single in-flight snap, clean worker teardown and restored overlay state |

API references: [Desktop Duplication](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api),
[DuplicateOutput errors](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutput1-duplicateoutput),
[protected-content masking](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/ns-dxgi1_2-dxgi_outdupl_frame_info),
[display affinity](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity).
