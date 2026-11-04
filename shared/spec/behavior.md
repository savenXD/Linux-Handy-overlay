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
