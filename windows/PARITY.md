# Windows parity and verification

## v1.1.1 capture protection — PENDING native verification

| Area | Evidence | Open |
| :--- | :--- | :--- |
| Overlay excluded from capture (`WDA_EXCLUDEFROMCAPTURE`) | Cross-compiles; config default unit-tested | Verify on real Windows with Zoom, Meet, OBS and `getDisplayMedia`: overlay absent, still visible locally |
| WebView2 popups/menus | Cannot be excluded (other process). Disabled instead: context menus, DevTools, accelerator keys, autofill/password bubbles, script dialogs, permission prompts, download bubble, `title` tooltips (page script + UI). The old 150 ms process-tree sweep was removed: cross-process `SetWindowDisplayAffinity` is refused | Confirm none of these appear; `<select>` lists and the file picker remain visible by design |
| Fail-closed on unsupported Windows | Code path only | Test on Windows 10 older than 2004 |

## v1.0.0 native Windows update — 2026-11-05

The following checks now have native Windows evidence in addition to the historical audit below:

- `theme_runtime_test`: WebView2 Light/Dark and transparent palettes, page-wrapper background removal,
  page reloads, configuration persistence, and visible black/white text over two changing desktop underlay colors.
- `lock_hotkey_runtime_test.ps1`: Alt+K hides and disables normal shortcuts, Alt+X cannot bypass the lock,
  and both key orders of Alt+K+X restore the overlay.
- `camouflage_hotkey_runtime_test.ps1`: Alt+C toggles camouflage; Alt+T changes text color without
  changing transparency; the temporary lock blocks both.
- `corner_hotkey_runtime_test.ps1`: Alt+Left/Right preserve size and bottom alignment, retain the selected
  corner through hide/show and configuration reload, and respect temporary locking. A separate installed-app
  restart check verified that the saved corner is restored.

These checks use an isolated local ChatGPT-style fixture for browser styling. They do not certify live
ChatGPT OAuth, every third-party page, games, IME/layout combinations, or all multi-monitor configurations.
The older per-feature statuses below remain the evidence baseline for features outside these checks.

This is the integration audit from 2026-10-22. It supersedes broad verification claims in the individual
phase reports. Windows uses the Linux implementation as its behavior reference; native Windows acceptance
is still required before calling the port complete.

- **VERIFIED (Wine)** means the specific behavior was exercised under Wine. WebView2 was unavailable, so
  this does not establish real browser rendering, OAuth, clipboard permissions or browser focus behavior.
- **UNIT TESTED** means deterministic logic or a native API test passed. It does not establish all desktop integration.
- **UNVERIFIED (Windows)** maps to the numbered steps in [README.md](README.md#windows-acceptance-checklist).
- **DIFFERENT (intentional)** follows [the shared behavior specification](../shared/spec/behavior.md).

## Features and remaining acceptance

| Feature | Evidence | Status / Windows checklist |
| --- | --- | --- |
| Overlay creation, show/hide, compact/expanded geometry | Smoke: creation, visibility, clamping, app expansion | VERIFIED (Wine); native styling/focus: 3, 5 |
| Alt+X/G/F/D/M | SendInput through the actual low-level hook; state and geometry assertions | VERIFIED (Wine); external Alt menus and held chords: 4 |
| Alt+A/S opacity bounds and hotkey dedupe | Implemented; CLI opacity changes exercised | UNVERIFIED (Windows): 4 |
| Alt+P capture | CLI/native capture produces valid PNG; WIC pixel round-trip passes | PNG path VERIFIED (Wine); display exclusion/clipboard: 11 |
| Alt+W start/cancel | External EDIT receives exact text; cancellation while guard is active stops typing | VERIFIED (Wine); application-specific input: 12–14 |
| Mirror / Exclusive routing | Press, release, repeat, shield, target change and guard routing unit tests; mouse activation and hide-release in native receiver | UNIT TESTED + partial VERIFIED (Wine); WebView2: 8–10 |
| Modifier snapshots, layout translation | Pure key table tests; captures modifier/scan/lock/repeat state at hook time | UNIT TESTED; native layouts, AltGr, IME: 8 |
| Clipboard shield and paste | Dispatch implemented; DIB24/32 conversion bounds and PNG codec tested | UNIT TESTED; real text/image paste and stale-image regression: 10 |
| Human writer timing and Extra safe repair | Seeded timing/typo simulation and exact reconstructed output tests; external human typing | UNIT TESTED + VERIFIED (Wine); long text/settings/ETA: 12–14 |
| Instant Unicode writer | 512-character batch case plus accents, CJK and emoji in external EDIT | VERIFIED (Wine); target compatibility: 13 |
| Writer cleanup / focus change | Alt+W cancel, Reset cancel, letter release, focus-away-and-back regression | VERIFIED (Wine); lock/sleep and Caps Lock: 14 |
| Native WebView2 views and persistent profile | SDK compile check, lifecycle/source/ownership review; graceful missing-runtime fallback | UNVERIFIED (Windows): 6–9 |
| URLs, allowlist, history policy, ChatGPT script | Browser pure tests; app switching/Back/Reset fallback checks | UNIT TESTED; actual page navigation/OAuth: 6, 7 |
| Popups | Uses the live popup controller as key target; closes stale requests/deferrals safely | UNVERIFIED (Windows): 7, 8 |
| Reset behavior | Home + Mirror + Solid; releases routing and cancels writer | VERIFIED (Wine), matching owner's Linux choice |
| Toast layering | Windowed WebView content reserves room during the 2.2-second toast | UNVERIFIED (Windows): 3, 6 |
| Drag/resize and height maximize | Implemented; corner math covers taskbars and offset monitors | UNIT TESTED math; native interaction: 3, 5 |
| Invalid monitor index | Runtime falls back to actual primary monitor, not enumeration index zero | Source reviewed; UNVERIFIED (Windows): 5 |
| DPI movement | Preserves physical rectangle, updates control scale; Default Size uses current DPI | DIFFERENT (intentional); UNVERIFIED (Windows): 5 |
| Stay-on-top loop guard | Token bucket and watcher implemented; basic topmost operation in Wine | Games, competing topmost windows and rate limiting UNVERIFIED (Windows): 3, 17 |
| Resume/unlock / Explorer restart | Explicit hook reinstall and topmost recovery; lock/suspend cancel writer and routing | UNVERIFIED (Windows): 16 |
| CLI and IPC | Smoke exercises status, geometry, visibility, size, opacity, modes, app state, Back, modal query, Reset and kill | VERIFIED (Wine) for those paths; browser actions require 6, 7 |
| Supervisor | Force-killed child restarts; clean kill stops supervision and restart history is written | VERIFIED (Wine); real sign-in behavior: 2 |
| Crash diagnostics | Deliberate child exception produces exception/module-offset/x64-register report and nonzero exit | VERIFIED (Wine) |
| Log rotation | Pure 64-bit size decision tests; startup rotation implementation reviewed | UNIT TESTED; native file behavior: 17 |
| Per-user installer | Inno 6.7.3 compile; isolated Wine install, running supervised upgrade, binary/version, Run/PATH, normal retention and interactive purge checks | VERIFIED (Wine); actual runtime download/sign-in/upgrade: 1, 2, 18 |
| Runtime bootstrap failure | Checks launch, exit code and post-install detection; gives fallback instructions | UNVERIFIED (Windows): 1 |
| Optional UIAccess | Both cross-build and embedded uiAccess manifest checked; retains UIAccess exe when supervised | Compiled; signing, secure installation and elevated targets UNVERIFIED (Windows): 15 |
| MSVC build | Windows CI configured for native build, tests and installer staging | UNVERIFIED locally; run the Windows workflow |
| Stability | Full fixed-binary memory soak and smoke logs recorded in integration report | Wine has no usable handle counter; native WebView2/handle/GDI soak: 17 |

## Configuration keys Parity (`config.ini`)

Verified by automated parity test: `windows/tests/parity_config_test.cpp`.

| Section | Key Name | Linux Default | Windows Default | Permitted Range | Parity Status |
| --- | --- | --- | --- | --- | --- |
| `[General]` | `width` | 360 | 360 | 200..3840 | **UNIT TESTED** |
| `[General]` | `height` | 640 | 640 | 300..2160 | **UNIT TESTED** |
| `[General]` | `margin` | 12 | 12 | 0..500 | **UNIT TESTED** |
| `[General]` | `corner` | `bottom-left` | `bottom-left` | 4 corners | **UNIT TESTED** |
| `[General]` | `monitor` | `primary` | `primary` | `primary`, index | **UNIT TESTED** |
| `[General]` | `opacity` | 1.0 | 1.0 | 0.1..1.0 | **UNIT TESTED** |
| `[General]` | `start_visible` | false | false | boolean | **UNIT TESTED** |
| `[General]` | `default_key_mode` | `mirror` | `mirror` | `mirror`, `exclusive` | **UNIT TESTED** |
| `[Browser]` | `home_url` | `https://html5test.com` | `https://html5test.com` | URL string | **UNIT TESTED** |
| `[Browser]` | `search_url_template` | `https://duckduckgo.com/?q=%s` | `https://duckduckgo.com/?q=%s` | Template | **UNIT TESTED** |
| `[Browser]` | `user_agent` | Pixel 8 UA | Pixel 8 UA | String | **UNIT TESTED** |
| `[Browser]` | `chatgpt_allowlist` | 18 domains | 18 domains | Comma-separated | **UNIT TESTED** |
| `[Hotkeys]` | `toggle_key` | `x` | `x` | char | **UNIT TESTED** |
| `[Hotkeys]` | `mode_key` | `m` | `m` | char | **UNIT TESTED** |
| `[Hotkeys]` | `ghost_key` | `g` | `g` | char | **UNIT TESTED** |
| `[Hotkeys]` | `snap_key` | `p` | `p` | char | **UNIT TESTED** |
| `[Hotkeys]` | `ghost_writer_key` | `w` | `w` | char | **UNIT TESTED** |
| `[Hotkeys]` | `shield_clipboard_keys` | true | true | boolean | **UNIT TESTED** |
| `[Snap]` | `snap_scope` | `monitor` | `monitor` | `monitor`, `all` | **UNIT TESTED** |
| `[Snap]` | `snap_delay_ms` | 80 | 80 | 60..5000 | **UNIT TESTED** |
| `[Snap]` | `snap_save` | true | true | boolean | **UNIT TESTED** |
| `[Snap]` | `snap_keep_count` | 20 | 20 | 1..500 | **UNIT TESTED** |
| `[GhostWriter]` | `ghost_delay_ms` | 0 | 0 | 0..5000 | **UNIT TESTED** |
| `[GhostWriter]` | `ghost_newline` | `space` | `space` | `space`, `return`, `skip` | **UNIT TESTED** |
| `[GhostWriter]` | `ghost_tab` | `space` | `space` | `space`, `tab`, `skip` | **UNIT TESTED** |
| `[GhostWriter]` | `ghost_max_chars` | 100000 | 100000 | 0..5000000 | **UNIT TESTED** |

---

## Toasts and diagnostics

The writer uses `Typing cancelled`, `Stopped: focus changed`, `Stopped: unsupported character`,
`Nothing to type`, `No focused app`, and `Target runs as administrator` as appropriate. Completion is
`Typed N chars in <app>`. Formatting helpers are unit tested; final rendering and visibility belong to
Windows steps 3 and 12–14. Snap uses the camera/snapping/checkmark state, not a claimed “saved” toast.

## Automated results

See [REPORT_INTEGRATION.md](../antigravity/REPORT_INTEGRATION.md) for exact build/test counts, soak measurements,
installer checks and limitations. Historical phase reports remain as implementation history, not current proof.
Linux source, build, tests, scripts and assets were not edited during this integration review.
