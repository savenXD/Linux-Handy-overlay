# Recovered implementation

References below point into [main-recovered-pseudocode.js](evidence/main-recovered-pseudocode.js). Line numbers refer to this preserved copy. Pseudocode is not original executable source; corroborate important semantics using [selected bytecode](evidence/secure-mode-bytecode.json).

## Screenshot policy: Ff, line 125064

The function checks that the main window exists and is not destroyed, loads the persisted `config` or `{}`, and calls:

```js
mainWindow.setContentProtection(!config.allowScreenCapture);
```

False or absent `allowScreenCapture` enables protection; true disables it for that window. Its catch path logs `enforceContentProtection`. This is a decoded call and controlling branch, not merely an API string found in the binary.

Electron documents its Windows implementation as `SetWindowDisplayAffinity(..., WDA_EXCLUDEFROMCAPTURE)`. Windows capture exclusion is independent of fullscreen and ordinary key blocking. The later runtime log confirms `0x11` on the visible main window. Setting affinity requires a top-level window owned by the calling process; it is not an arbitrary cross-process setter.

Sources: [Electron API](https://www.electronjs.org/docs/latest/api/browser-window#winsetcontentprotectionenable-macos-windows), [Microsoft API](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity).

## Configuration transition: Lt, line 21406

`Lt(arg0)` logs `handler.configureSea`. If incoming configuration differs from the persisted value (including a JSON-stringified comparison), it saves the new `config`, saves the current window URL as `relaunch`, creates a notification titled `Reconfiguring CodeTantra SEA` with body `Preparing your test with latest configuration...`, then calls Electron `app.relaunch()` and `app.quit()` and returns false. Otherwise it returns true.

The `sea_reconfigure` handler registration near line 137818 is mapped to `Lt` through its initialization. This is a real client relaunch path consistent with the reported brief close/loader transition. The supplied Windows log did **not** capture a main-process PID replacement; do not claim it proves this handler ran when ADS opened.

## Window enforcement: Zf, line 127668

This function removes/reconfigures window-event listeners. Its enforced mode calls `Ff()` and installs handlers for blur, minimize, hide, resize, unmaximize, restore, and display-metric changes. If `allowAccessToOtherApps` is false, its recovery callback uses a 100 ms timeout and a throttled wrapper (1000 ms), then:

```js
window.restore();
window.show();
window.setFullScreen(true);
window.setAlwaysOnTop(true, 'screen-saver', 1);
window.focus();
window.setContentProtection(!config.allowScreenCapture);
```

The blur path also invokes page notification `window.ipcW.blurSea()`. `allowAccessToOtherApps` and `allowScreenCapture` are separate policy fields. Their independence is an implementation fact; an available student-facing control is not established.

Main-window construction in `Bf` begins near line 125724, with the constructor body around 126875. It sets always-on-top and enables kiosk on the Windows/non-macOS branch. Do not attribute macOS-only option spreads to Windows. Web preferences include disabled devtools, enabled context isolation, and disabled node integration.

## Configuration persistence and defaults

The store is `electron-store` initialized with an encryption key. No plain-text config edit was tested. Do not reproduce the embedded key unnecessarily. Startup compares stored application version; the version-change default near line 138935 includes:

```js
{ allowScreenCapture: false, allowAccessToOtherApps: true, isWrittenExam: false }
```

Capture exclusion can therefore already be enabled before a course opens. This fits the log, where the main window was protected from its first visible sample.

## Other relevant observations

- `sea_capture_integrity` exists and has an allowed-capture early return `{hooked:false, skipped:'allowed'}`. The full mechanism and its relation to other checks were not established.
- `WinKeyServer.exe` imports keyboard-hook APIs. That establishes capability, not which keys were blocked or whether it ran in this session.
- `fileManagerHost.exe` has separate OS-shell/session recovery-related logic (`--mark`, `--check`, `--force`, registry session state). Do not equate that helper with the ordinary `sea_reconfigure` transition or infer an OS kiosk logon from the user's symptoms.
- Main executable imports include `SetWindowDisplayAffinity` and `GetWindowDisplayAffinity`. Imports alone were initially only capability evidence; the later recovered call and runtime log supply stronger attribution.

## Electron fuses

REA found `EnableEmbeddedAsarIntegrityValidation` and `OnlyLoadAppFromAsar` enabled. `RunAsNode`, `EnableNodeOptionsEnvironmentVariable`, and `EnableNodeCliInspectArguments` are disabled. Cookie encryption is enabled; browser-process-specific V8 snapshot loading is disabled; extra file-protocol privileges are enabled.

Simple archive edits, `NODE_OPTIONS`, or adding inspector flags must not be presented as verified solutions. No patched artifact was made or tested.
