# Windows tool removal review — 2026-11-19

The Windows overlay now exposes Home and ChatGPT. Two inert home slots and one inert navigation slot reserve room for future tools. Window sizing, compact/expanded behavior, resizing and docking are unchanged.

## Removed runtime functionality

- All Ghost Typer/Writer modules, timing simulations, synthetic typing engine, countdown/progress/focus-poll timers, input guard, editor buffer, settings, bridge messages, CLI/IPC commands and Alt+W registration.
- The standalone browsing controller, its lazy creation and recovery state, address/search normalization, home/search URLs, mobile user agent, address/search UI, browser-specific suspension/memory handling and arbitrary URL/app commands.
- Dedicated tests/build entries and unused editor/INI-reading helpers. Generic INI writing remains necessary for appearance and saved corner settings.

Shared rendering is now `src/chat_view.cpp` / `src/chat_view.h`; ChatGPT navigation/authentication/scripts live alongside `src/chat_logic.cpp` / `src/chat_logic.h`. The controller array contains only the local shell, ChatGPT and sign-in popup. ChatGPT's external-site diversion no longer creates a browsing view; unsupported navigation is cancelled with a toast.

## Preserved behavior

| Area | Review evidence |
| --- | --- |
| ChatGPT | Same persistent WebView2 profile path, preloading, sign-in popups, recovery, non-activating hosts, keyboard routing, clipboard text/image paste and appearance scripts. The ChatGPT script assets are unchanged. |
| Screenshot capture | Capture implementations and codecs remain; capture runtime fixtures check pixels, disk/clipboard equality, clipboard contention, cancellation and unavailable experimental backends. |
| God-copy/paste | Selection worker/protocol unchanged; only obsolete Writer interlocks and wording removed from the coordinator. All 16 selection/retained-paste fixture checks pass. |
| Other UI/input | Click-through Ghost mode, normal/overlay-only typing, show/hide, opacity, fullscreen, docking, themes, lock/unlock, reset and kill switch remain. Geometry, lock and kill-switch implementations are unchanged. Input routing subsequently changes to the two-target Alt+M behavior documented in README.md. |

The native receiver regression rejects retired IPC commands and app names, verifies that Alt+W reaches the external application, and checks typing-target activation and release. ChatGPT's allowlist moves to `[ChatGPT] allowlist`; reading the old `[Browser] chatgpt_allowlist` remains solely to preserve existing ChatGPT settings. Windows has its own default-configuration fixture; Linux files are unchanged.

## Folder and binary audit

Reviewed Windows source, UI, build definitions, installer, scripts, tests and current documentation. Searched implementation assets for retired module calls, identifiers, settings, commands and editor messages; no implementations or registrations remain. The built core archive has no `cronq::ghost::` or `cronq::browser::` symbols. Stale generated target files for the deleted modules were removed from existing build directories.

Remaining occurrences of “browser” belong to WebView2 SDK/engine APIs required by ChatGPT, external applications used in tests, the ChatGPT settings migration, regression cases that reject removed commands, historical release notes, or third-party evidence in the screenshot investigation. Historical release notes are explicitly labelled. Investigation evidence and SDK definitions are retained; they do not restore a standalone overlay browser.

## Validation and limits

- WebView2-enabled llvm-mingw cross-build and a fresh WebView2-disabled build pass without compiler warnings.
- All 21 CTest tests pass under Wine.
- All 16 God-copy runtime fixture checks pass under Wine.
- Screenshot runtime fixtures pass, including exact disk/clipboard PNG equality and publication after unsupported experimental captures.
- UI automation verifies reserved slots, dimensions, ChatGPT content bounds, popup controls, screenshot button states, input pills, Back, themes and minimum size, with no JavaScript errors. The rendered home screen was visually inspected.
- The Wine smoke test passes all 49 checks and covers global shortcuts, adaptive geometry, focus/routing, retired commands, 5,000 IPC requests, screenshots and supervisor recovery. Its old size expectations were corrected to the existing `FitPhone` behavior, including compact Home on Alt+D; product geometry code was not changed.

Wine does not provide the real WebView2 runtime or native DXGI/WGC support. The ChatGPT/OAuth and theme fixtures cross-compile, but a live Windows ChatGPT sign-in/session and native GPU capture were not exercised here. Existing protected-app screenshot limitations remain unchanged. No release was published or installed on a Windows machine.

## Subsequent typing-target update

Alt+M now toggles only normal-app and overlay-only typing. The mirroring/exclusive mode enum, configuration default and duplicate-delivery path have been removed. Typing starts in the normal app. The existing ball is gray normally and glows green only for active overlay routing, including in Camouflage; the header reserves space above native ChatGPT content so the indicator is not covered. No window dimension or docking changes were made.

Entering overlay typing prepares the current editable field, or ChatGPT composer, with DOM focus only; no HWND activation or controller `MoveFocus` is used. Outside clicks, hide, reset, lock, sleep and the idle timeout release routing. Hook availability changes refresh the indicator and release routing on failure. Key ownership lasts through release, preventing held keys from leaking repeats across a target change. A held Alt+M toggles once, and separate rapid presses are each honored.

The updated runtime fixture passes its focus, zero-external-key-event, normal typing, rapid-toggle and held-shortcut checks under Wine. All 49 smoke checks, 21 CTest tests, 16 God-copy checks, WebView2-enabled/disabled cross-builds and UI checks pass. The native `chat_runtime_test.exe` additionally contains a local composer fixture to verify actual CDP text delivery while another editor retains OS focus; this fixture compiles but still requires native Windows/WebView2 execution.
