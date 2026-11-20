# Linux / Windows parity — 1.5.0

Linux now follows the current Windows feature set, with the owner's explicit
exclusions: no God-copy/God-paste, Alt+O or Alt+Shift+O. Windows native capture
experiments remain Windows-only. Root `VERSION` and `windows/VERSION` both read
`1.5.0`; generated defaults are identical.

## Implemented changes

| Area | Linux implementation | Verification |
| --- | --- | --- |
| Removed features | Standalone Browser, Ghost Typer/Writer, their pages/settings/commands and Alt+W removed. ChatGPT renderer retained as `chat_view.c`. Home matches the Windows 404 shell, with double-click recovery entry and four low-contrast footer dots with full-sized click areas. | Native widget tree and rejection of retired CLI/IPC/apps |
| Typing | Alt+M normal / overlay-only, gray / green glowing indicator; dedicated X11 keyboard grab, no mirror fallback, local DOM focus. | Real XTest typing into WebKitGTK: exact overlay text, empty underlying entry, unchanged XGetInputFocus, then normal app typing restored |
| Failure/release behavior | Failed grab stays normal; lock, outside click, session lock, reset and click-through release routing. Hide preserves exclusive typing without an idle timeout. | Rival-client grab conflict, hide, lock/unlock, outside click, freedesktop lock signal and reset tested. Hidden text delivery, ghost opacity and kill-prompt Escape cancellation tested; suspend release source-reviewed |
| Appearance | Same four palettes, Alt+C/T, live ChatGPT styling, indicator above content in all modes. | Real WebKit DOM computed colors and visible composer/indicator for all four modes; dark screenshot inspected |
| ChatGPT / login | Same allowed navigation, related auth popups, persistent Linux cookies/data/cache directories. Normal context menus, DevTools and permission/dialog surfaces suppressed. | Local HTML/composer and clipboard tests; popup/session retention source-reviewed. Live OAuth and account persistence need user desktop acceptance |
| Clipboard | Text/select/copy/cut and current image paste in ChatGPT. No stale screenshot fallback. | Text replaces composer selection, image paste event has image/png, no external-app paste, unchanged OS focus |
| Window | Compact Home, fitted expanded phone size, work-area docking, opacity, fullscreen/restore, drag/resize. Bounded topmost loop. | Home 280px, expanded360×640 on1280×900, fullscreen exact monitor, opacity, saved dock, nonactivating restack. Multi-monitor/DPI/drag need desktop acceptance |
| Lock / kill switch | Existing Alt+K / Alt+K+X retained; kill prompt releases routing, unrelated key cancels, 15s visible /5s hidden. | Actual shortcut locking/unlocking, safe Escape cancel, hidden5s expiry. Destructive confirmation deliberately never run against a user profile |
| Screenshots | Active-app monitor or all, clipboard plus XDG data PNG, timestamped names, retention, no overlay pixels, black-frame rejection, preserve hide intent during capture. | Exact PNG dimensions, pixel equality between clipboard and disk, retention2 after3 captures, all-black leaves clipboard/files unchanged, hide during capture stays hidden |
| Recovery | Native signal report retains3, safe signal handler; supervisor restart hidden with2–60s backoff; WebKit reload with1–30s backoff and saved last committed URL. | Four disposable native crashes produce3 reports; fake supervisor failure restarts hidden and clean/intentional exits stop; actual WebKit process termination recovers local fixture |
| Update/version | Manual official stable-release updater, HTTPS, SHA256, strict archive whitelist, rollback and profile retention; shared version. | Six Python tests cover versions/origin/architecture, bad hash, traversal/links, profile preservation and rollback; actual release archive validated. No network update installed |
| Packaging / CI | Linux release archive+SHA alongside Windows installer; both-platform CI replaces old Linux-untouched guard. | Fresh Linux compile, tar validation, shell checks. CI configuration prepared locally; hosted CI/release not executed |

## Evidence from this workspace

- Fresh `make -B -j8 all tests/linux_runtime_test`: no compiler warnings or errors.
- Isolated Xephyr/XDG/DBus suite: **98 PASS checks**, plus **6 Python updater tests**.
  It uses real GTK3, WebKitGTK4.1 and X11 input with local page fixtures, not a mock browser.
- Windows cross-build succeeded; CTest under an isolated Wine prefix: **21/21 passed**.
- Actual Linux distribution passed the production updater's SHA256, archive-member,
  ELF and version validation. Frontend download-gate regression passed its trigger
  and wrong-code checks; the optional correct-secret check was not configured.
- `git diff --check` and shell syntax checks passed.

Reproduce with `make test` in an X11 session, or `xvfb-run -a make test` in CI.
Each run prints its temporary artifact directory, including logs, saved screenshots
and `data/typing-overlay.png`. Tests leave the installed application and real profile
alone. No live updater installation, package publication or account login was performed.

## Platform limits

Linux requires X11; native Wayland apps cannot be controlled through XWayland.
Camouflage transparency needs a compositor; the nested test display had none, so
its alpha appearance was not certified visually. X11 capture guarding is best
effort with a possible first-frame gap, not Windows display affinity. Keyboard
grabs preserve OS focus but can be observed by X11 clients through grab-mode focus
notifications. Already-held keys crossing a toggle, complex IMEs, raw-input games,
protected/elevated desktop interaction and real multi-monitor/DPI behavior still
need environment-specific acceptance. Windows Wine tests do not establish native
WebView2, OAuth or protected GPU capture behavior.

Linux release CI targets Ubuntu24.04/glibc2.39, x86_64; build from source on older
compatible X11 distributions. The packager/updater also recognize aarch64 when a
native archive is built; no aarch64 CI release is configured.

The detailed feature/shortcut contract is in `shared/spec/behavior.md`.
