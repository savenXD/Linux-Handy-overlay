# Cronq behaviour spec (shared by Linux and Windows)

The Linux build in `src/` is the reference. The Windows build in `windows/` must match
every row below. Change a row here only when both platforms change together.

## Hotkeys

All hotkeys are `Alt` + key, with Ctrl and Shift NOT held. They work globally and the key
press is swallowed, so the focused application never sees it. The letter is matched on the
physical key for that letter in the current layout.

| Key | Action | Configurable key | Dedupe window |
| --- | --- | --- | --- |
| X | Toggle overlay | `toggle_key` | 150 ms |
| G | Toggle ghost (click-through) | `ghost_key` | 150 ms |
| A | Opacity -10% (min 20%) | fixed | 120 ms |
| S | Opacity +10% (max 100%) | fixed | 120 ms |
| F | Toggle fullscreen / restore | fixed | 200 ms |
| D | Default size, default corner | fixed | 200 ms |
| P | Snap (screenshot) | `snap_key` | 200 ms |
| M | Cycle Mirror / Exclusive | `mode_key` | 150 ms |
| W | Ghost Writer | `ghost_writer_key` | 200 ms |

### Lock, kill switch, appearance and docking (Windows and Linux)

| Keys | Action |
| --- | --- |
| Alt+K | Lock: hide the overlay and disable every other hotkey. No confirmation. Not persisted. |
| Alt+K+X (either press order, Alt held) | Unlock: hotkeys work again and the overlay is shown. |
| Alt+Shift+K | Kill switch prompt (works while locked). Enter confirms, Esc cancels, auto-cancel after 15 s. Windows also cancels on any other non-modifier key (which still reaches the app) and uses 5 s while the overlay is hidden, where the prompt is not drawn. |

| Alt+C | Toggle camouflage (restores the previous Dark/Light mode) |
| Alt+T | Camouflage: black/white text. Otherwise Dark/Light |
| Alt+Left / Alt+Right | Dock bottom-left / bottom-right and save `[General] corner` |

Appearance is saved as `[Appearance] mode`: 0 Dark, 1 Light, 2 camouflage with black text (default), 3 camouflage with white
text. Lock disables Alt+C/T/arrows too. Camouflage hides chrome (header, footer, controls) and makes backgrounds transparent.

Confirming quits Cronq and removes the program, autostart/startup entries, shortcuts, PATH entry and every
data directory (settings, logins, logs, screenshots) from the machine.

## Window

- Default size 360x640, minimum 260x180, margin 12, corner radius 24.
- Size is clamped to the primary monitor. Fullscreen = exact monitor rectangle.
- Double-click on the centre handle: maximise height (monitor height - 24, y = 12) / restore.
- Toasts last 2200 ms. Opacity toast text is `Opacity: N%`.
- Toast texts: `Restored size`, `Fullscreen mode`, `Default size (360x640)`, `Maximized height`.

## Stay-on-top loop guard

Token bucket: 300 raises/s, burst 60, 50 ms cooldown when empty, 100 ms watchdog poll.

## IPC commands (line based, case-insensitive unless noted)

TOGGLE SHOW HIDE KILL|QUIT|EXIT, `MODE [mirror|exclusive]`, `GHOST [on|off]`, `LOAD <url>` (case-sensitive
prefix), `APP <name>`, BACK HOME RELOAD RESET|UNIVERSAL_RESET, GET_TEST_ENTRY CLEAR_TEST_ENTRY
`SET_TEST_ENTRY <text>`, HAS_MODAL CLOSE_MODAL SNAP GHOST_WRITE PASTE, `SET_OPACITY <v>` GET_OPACITY,
`SET_SIZE <w> <h>`, FULLSCREEN DEFAULT_SIZE GET_GEOMETRY RELOAD_CONFIG STATUS.

STATUS reply: `MODE=<MIRROR|EXCLUSIVE> TARGET=<OVERLAY|NONE> GHOST=<GHOST|SOLID> GRAB=<ACTIVE|FAILED|NONE> VISIBLE=<TRUE|FALSE> APP=<name>`
GET_GEOMETRY reply: `X=<n> Y=<n> W=<n> H=<n>`

## Known intentional Windows differences

- The default corner is placed against the monitor work area (so the taskbar Start button
  stays reachable). Linux places it against the full monitor rectangle.
- Pixel sizes from config are scaled by the monitor DPI (150% display => 540x960 pixels).
- Moving the overlay between DPI settings preserves its current physical rectangle and rescales controls. Default Size uses the current DPI for configured dimensions.
- Hotkeys are swallowed by a low-level keyboard hook, not an X11 passive grab.
- Hiding the overlay from screen capture (`[Privacy] hide_from_capture`) is Windows-only; X11 has no equivalent.
- While the overlay is hidden, Windows passes the layout shortcuts (Alt+G/A/S/F/D/M/C/T, Alt+Left/Right) to the focused
  application; only Alt+X/P/W/K and Alt+Shift+K stay global. Linux keeps all grabs.
- Windows clears modifier state the hook missed (releases on the secure desktop or in elevated windows), so a
  plain letter is never mistaken for an Alt shortcut.
- Stay-on-top guard on Windows: 60 raises/s, burst 20, and a window that stays above after a raise (Start
  menu, higher shell bands) or wins 10 raises in 1 s is left alone for 3-5 s instead of being fought at full rate.
- Camouflage transparency uses a Windows colour key; on Linux it uses an ARGB window (needs a compositor), so empty areas do not click through.
