> This report was written before the user returned the Windows log. Read [03-windows-runtime.md](03-windows-runtime.md) for confirmed runtime affinity and updated probe status. References to untested Windows behavior below describe the earlier analysis stage.

# Detailed static report (historical pre-runtime snapshot)

The shipped application contains an exact configuration-driven capture restriction and a relaunch path consistent with the reported Resume → brief shutdown → protected window transition. This is static evidence from the supplied installer, not an observation of the user's live ADS session. No working student-side screenshot bypass has been established.

## The relevant implementation

Readable reconstruction of the recovered operations (not original source):

```js
const config = store.get('config') || {};
mainWindow.setContentProtection(!config.allowScreenCapture);
```

Function `Ff`, recovered pseudocode line 125064, loads `config`, reads `allowScreenCapture`, negates its truthiness, and passes it to Electron's `setContentProtection`. False or missing means capture protection is enabled. This is an actual call with its controlling condition, stronger evidence than finding an API name in strings or imports.

Electron documents that its Windows implementation uses `SetWindowDisplayAffinity` with `WDA_EXCLUDEFROMCAPTURE`. Windows can omit the protected window from capture; older supported behavior produces black content. The precise appearance depends on Windows and the capture path. No administrator prompt is required for an application to apply capture protection to its own window. Sources: [Electron BrowserWindow](https://www.electronjs.org/docs/latest/api/browser-window#winsetcontentprotectionenable-macos-windows), [Microsoft SetWindowDisplayAffinity](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity).

## Why Resume can close and reopen the application

Function `Lt` (line 21406) logs `handler.configureSea`, compares the incoming configuration with the stored configuration, and on a change:

1. Saves the new `config` and the current window URL as `relaunch`.
2. Shows a notification titled `Reconfiguring CodeTantra SEA`, with `Preparing your test with latest configuration...`.
3. Calls Electron `app.relaunch()` and `app.quit()`.

The handler registration maps `sea_reconfigure` to `Lt`. This closely matches the user's description. A live page/server trace was not collected, so the ADS-specific triggering request and exact configuration values remain unverified.

## Why the window keeps returning to fullscreen

Function `Zf` (line 127668) configures handlers for minimize, blur, hide, resize, unmaximize, restore, and display-metric changes. When access to other applications is disallowed, its recovery callback restores and shows the window, sets fullscreen, calls `setAlwaysOnTop(true, 'screen-saver', 1)`, focuses it, and reapplies capture protection from `allowScreenCapture`. The callback uses a 100 ms timeout and a throttled wrapper. The main BrowserWindow creation also enables kiosk mode on the Windows/non-macOS branch and always-on-top behavior.

`allowAccessToOtherApps` and `allowScreenCapture` are distinct policy values. Capture could therefore be permitted in this code while other-app access remains restricted. This does not establish that a student-facing setting exists. The user has a student account only; no instructor/admin UI was inspected.

The store is initialized with an encryption key. A plain-text config edit is not a verified solution. Startup version-reset defaults include `allowScreenCapture: false` and `allowAccessToOtherApps: true`; screenshot blocking should not be equated with fullscreen enforcement in every configuration.

## Why repacking or adding debug flags is not an established fix

REA identified enabled `EnableEmbeddedAsarIntegrityValidation` and `OnlyLoadAppFromAsar` fuses. `RunAsNode`, `EnableNodeOptionsEnvironmentVariable`, and `EnableNodeCliInspectArguments` are disabled. Consequently, a simple archive edit or adding Node debugging flags cannot be presented as a working workaround. The installer and application were not patched or executed during this analysis.

## Evidence and recovery quality

- Installer SHA-256: `a92b68ebbdb2cd163dd6e32fb22c7e0f11f15e516155f1627f1ae8d09e8e7ec1`.
- Main executable SHA-256: `26e13fc1f9258c33912c387358366b2999e89949f7ffd0ced603de8cb5ac4ae4`.
- Main cached bytecode SHA-256: `0cd680a356735566551cc2ab2baa27f8354b7532045a49e67b79e3a3ab42634e`.
- REA inspected packaging, imports, helper code, and Electron fuses. Supplemental offline recovery used [v8asm](https://github.com/aynakeya/v8asm), commit `0d1e8a9d05618f459c96b1e1eddb2bf6b95bc8f5`, adapted to V8 14.2.231.22 using matching official V8 headers and the application's snapshot. [Electron 39.8.10 release metadata](https://releases.electronjs.org/release/v39.8.10) confirms this V8 version.
- Snapshot version, magic, and checksum matched the code cache. Across 1,175 bytecode arrays and 80,514 instructions, raw instruction ranges and decoded jump boundaries passed structural validation. This does not prove complete semantic correctness of every recovered function.
- `main-recovered-pseudocode.js` is analysis pseudocode, not executable recovered source. Unsupported operations remain. Synthetic `0xf000...` identifiers are serialized-object identifiers, not live Windows addresses.
- `secure-mode-bytecode.json` contains selected functions and their original decoded instructions. `recovery-validation.json`, `v8-14.2.231.22-profile.json`, `v8-profile-source-hashes.json`, and `v8asm-version-support.patch` preserve recovery provenance.
- `secure-mode-rea-evidence.json` contains this pass's REA records; `rea-evidence.json` contains the earlier pass. The earlier `findings.md` predates full bytecode recovery; this report supersedes its uncertainty about the capture flag's call site.
- Twelve unpacked companion files differed from ASAR-recorded hashes during initial inspection. Their cause was not established. Analysis continued with that limitation recorded.

## Read-only Windows diagnostic

`CaptureStateProbe.ps1` and `Run-CaptureStateProbe.cmd` collect window metadata for `CodeTantra-SEA` processes for 120 seconds. They do not take screenshots, change the app, inject code, or bypass restrictions. They record process/window IDs, visibility, minimization, topmost/foreground state, and the result of reading display affinity. No page contents, passwords, window titles, or network traffic are collected.

Copy both files to the same writable folder on Windows. Start the CMD launcher before entering the protected course, reproduce Resume, then exit the course normally. The script creates `capture-state-YYYYMMDD-HHMMSS.jsonl` in that folder. Share that log for interpretation. Administrator rights are not requested. The launcher uses a process-scoped PowerShell execution policy override to run this local script; it does not change the saved machine policy.

A successful affinity read of `0x11` means exclude-from-capture, `0x1` means monitor-only, and `0x0` means no display-affinity restriction on that particular window. A failed read is **inconclusive**, not proof that protection is absent. Microsoft documents conditions on successful reads; see [GetWindowDisplayAffinity](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowdisplayaffinity). Other protection mechanisms may coexist.

The protected app may prevent this probe from running or may use a different process name; an empty/partial log is not proof of no protection. This Linux environment has no PowerShell/Windows runtime, so the probe has been reviewed but not execution-tested. Actual screenshot behavior and any proposed workaround still require Windows validation.
