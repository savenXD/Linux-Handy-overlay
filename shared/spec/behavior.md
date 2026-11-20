# Cronq shared behavior — 1.4.1

The Linux implementation (`src/`, GTK/WebKitGTK/X11) and Windows implementation
(`windows/`, Win32/WebView2) share this contract. Linux exclusions requested by the
owner are listed explicitly below. Both VERSION files must match.

## Apps and typing

Home is a “This page isn’t available” error screen, without ChatGPT branding. Only a primary-button double-click on the word “recovery” opens ChatGPT; a single click does nothing. The former Browser/search
and Ghost Typer controls, modules, commands and configuration are removed. The
bottom bar shows four tiny low-contrast dots with invisible full-sized click zones and 2px markers near the bottom edge without hover effects, retaining the Back, Home, Reset, and Snap routes in that order. Accessible names remain Previous, Overview, Reload, and Report. The header shows the keyboard and ghost dots plus an invisible drag area; appearance, fullscreen and hide remain keyboard actions. Alt+W does nothing.
ChatGPT retains its existing per-user profile, cookies and related authentication
popups. Back/reload apply to ChatGPT; Reset returns Home, normal typing and solid
mouse interaction while retaining the login.

Alt+M switches between exactly two destinations:

- **Normal** (startup): keys go to the current desktop app; the status ball is gray.
- **Overlay**: keys go only to the overlay's current editable control or ChatGPT
  composer; the status ball glows green, including in both Camouflage modes.

The overlay never activates itself or calls the platform OS focus setter. Enabling
routing prepares the DOM composer locally. Hovering an editable field enables routing.
Hiding with Alt+X preserves the selected field and exclusive typing, with no idle
timeout; showing it again retains the green indicator. An outside click (including
a click through the hidden overlay footprint), Alt+M, locking, click-through,
suspend/session lock or reset releases routing. Ghost mode starts at 50% opacity
on each off-to-on transition and restores 100% on exit; subsequent manual opacity changes remain available. If the
Linux keyboard grab fails, routing stays normal and the ball never turns green.
The old Mirror/Exclusive enum, default setting and duplicate text path do not exist.
Clipboard select/copy/cut/paste inside ChatGPT remain available. Image paste uses
only the current clipboard, never an old screenshot file. Live ChatGPT/OAuth remain
subject to the site's behavior and the installed web engine.

## Shortcuts

Unless stated otherwise, Alt shortcuts require neither Ctrl nor Shift. Layout keys
are active only while the overlay is visible. Lock blocks all except its unlock
chord and kill switch. Global keys retain their effect while hidden.

| Shortcut | Action | Availability |
| --- | --- | --- |
| Alt+X | Show/hide | Global; configurable `toggle_key` |
| Alt+M | Normal / overlay typing | Visible; configurable `mode_key` |
| Alt+G | Mouse click-through | Visible; configurable `ghost_key` |
| Alt+A / Alt+S | Opacity −/+10%, 20–100% | Visible |
| Alt+F | Full monitor / previous rectangle | Visible |
| Alt+D | Fitted default size | Visible |
| Alt+P | Screenshot without overlay | Global; configurable `snap_key` |
| Alt+C | Toggle solid / Camouflage, exchanging contrast tone | Visible |
| Alt+T | Exchange light/dark or black/white text | Visible |
| Alt+Left / Alt+Right | Dock bottom-left / bottom-right and save corner | Visible |
| Alt+K | Hide and lock | Global |
| Alt+K+X | Unlock and show, either key order with Alt held | Locked |
| Alt+Shift+K | Destructive uninstall prompt | Global, including locked |

Kill confirmation: Enter confirms, Escape or another nonmodifier cancels. Other
nonmodifier keys still reach the normal app. The prompt cancels after 15 seconds
when visible or 5 seconds when hidden (no prompt window shown). Confirmation removes
Cronq, settings, saved logins, cache, screenshots and startup entries. Lock is not
persisted. Tests must never confirm this prompt against the user's profile.

## Appearance and geometry

Appearance is saved as `[Appearance] mode`: 0 Dark, 1 Light, 2 Camo black text
(default), 3 Camo white text. The native UI and ChatGPT change together without a
page reload. Camouflage removes surrounding page chrome and backgrounds but keeps
conversation/composer text and the typing indicator. Linux needs a compositor for
transparency; Windows uses a color key.

Nominal phone dimensions are 360×640, minimum 260×180, margin 12. Default size fits
inside the selected monitor work area, keeping the expanded height within 75% of
available height. Home is at most 280 high; ChatGPT expands to fitted height.
Fullscreen uses the whole current monitor, not its work area. Dragging/resize and
handle double-click height maximize/restore retain their platform implementation.
Default/docking use work area so the desktop panel remains accessible. Windows
scales nominal dimensions by monitor DPI; GTK uses logical pixels and its display
scale. Linux native rendering and font metrics are not pixel-identical to WebView2.
Status messages are logged without visual toast bubbles. Stay-on-top guard is bounded to 60 raises/s, burst 20,
with a 3-second Linux cooldown (Windows also backs off persistent shell competition).

## Screenshot, persistence and recovery

Alt+P hides the overlay during capture, restores prior visibility, copies PNG-compatible
image data to the clipboard and optionally saves `snaps/snap-*.png` in Cronq's data
folder. Monitor scope selects the active desktop app's monitor, fallback primary;
`all` captures the desktop. Retention is configured with `snap_keep_count` (default20).
All-black/failed capture leaves the clipboard and previous files unchanged and shows
failure. Linux uses X11 root pixels; Windows prefers DXGI with GDI fallback.

Linux settings use `$XDG_CONFIG_HOME/cronq/config.ini` and data use
`$XDG_DATA_HOME/cronq` (standard ~/.config and ~/.local/share defaults). Existing
Linux cookie/data/cache locations are preserved. Windows uses its existing AppData
profile. Only ChatGPT's optional `allowlist` setting survives under `[ChatGPT]`;
legacy `[Browser] chatgpt_allowlist` is read for migration. Obsolete settings have no effect.

Both versions provide manual `cronq update [--yes]`, official GitHub stable release
selection, numeric version comparison and SHA256 verification before install.
Linux uses Python3/HTTPS and a bounded, whitelisted tar archive, preserving the
profile. CI publishes `cronq-linux-x86_64.tar.gz` plus `SHA256SUMS-linux.txt` beside
the Windows installer. Native Linux dependencies must already be installed; CI
release binaries target Ubuntu24.04/glibc2.39 or compatible newer systems. Older
systems should build from source. No update is installed by acceptance tests.

The supervisor restarts failures hidden with bounded exponential backoff (2–60s),
reset after a stable minute; clean exit stops it. Web-engine crashes release typing
and reload with bounded backoff. Native crash reports retain the newest three;
Linux logs signal, fault address and instruction address with signal-safe syscalls.
Suspend/session notifications release typing; resume re-registers Linux grabs.

## Commands

Shared line-based IPC: TOGGLE SHOW HIDE KILL|QUIT|EXIT, `MODE [normal|overlay]`,
`GHOST [on|off]`, `APP home|chatgpt`, BACK HOME RELOAD RESET|UNIVERSAL_RESET,
HAS_MODAL CLOSE_MODAL SNAP PASTE, `SET_OPACITY n` GET_OPACITY, `SET_SIZE w h`,
FULLSCREEN DEFAULT_SIZE GET_GEOMETRY RELOAD_CONFIG STATUS.
Simple commands are case-insensitive; parameterized prefixes are uppercase.
Removed commands return UNKNOWN_COMMAND, retired apps UNKNOWN_APP.

STATUS: `MODE=NORMAL|OVERLAY TARGET=NONE|OVERLAY GHOST=SOLID|GHOST GRAB=NONE|ACTIVE|FAILED VISIBLE=TRUE|FALSE APP=home|chatgpt LOCKED=TRUE|FALSE APPEARANCE=0..3`.
GET_GEOMETRY: `X=n Y=n W=n H=n`.
Linux retains its diagnostic IPC for theme, capture guard, locking and prompt cancellation.

## Explicit Linux exclusions and platform limits

- Do not implement God-copy/God-paste, Alt+O, Alt+Shift+O, SNAP_WGC or SNAP_WGC_WINDOW.
  Windows keeps these independent native experiments.
- Linux requires an X11 session. XWayland does not grant control over native Wayland
  apps; pure Wayland cannot fulfill this global-input contract.
- X11 has no secure per-window capture exclusion. Linux's XRecord/process guard is
  best effort: it hides after detecting capture activity and cannot guarantee the
  first captured frame. Windows display affinity has its own platform restrictions.
- X11 keyboard grabs preserve XGetInputFocus but clients can observe grab-mode focus
  notifications. Crossing the toggle with an already held key is subject to X server
  repeat/release behavior. Raw-input games, IMEs and elevated/secure desktops need
  platform-specific acceptance; do not claim that no other software can detect routing.
- Native WebKitGTK and WebView2 OAuth, upload/file-picker behavior, capture APIs and
  DPI behavior require verification on their actual desktop environments.
