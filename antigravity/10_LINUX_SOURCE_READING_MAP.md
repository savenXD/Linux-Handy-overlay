# Linux source reading map

The Linux C code is the specification. For every feature you port, **read the listed Linux code first** (read-only!) and copy its
behaviour, constants, strings and edge cases. Line numbers are approximate (the file may have shifted a few lines); search by function name.

All paths are relative to the repo root. Do **not** modify any of these files.

## Window and UI (`src/window.c`, 2.6k lines)

| Topic | Function / region | Port to |
| --- | --- | --- |
| Layout constants | `src/window.h` (`OVERLAY_*` defines) | `overlay.h` (done) |
| SVG icons | top of file, `s_svg_*` (~lines 27–145) | `windows/ui/index.html` inline `<svg>` (verbatim) |
| CSS theme | `s_phone_css` (~line 249 onward) | `windows/ui/cronq.css` |
| Window creation, status bar, move handle, 2 top buttons, central stack, toast box, nav bar | `window_init` (~1740–2110) | HTML structure |
| Home page | `create_home_page` (~1530) | HTML Home page |
| App icon buttons | `create_app_icon_widget` (~1501), `on_app_icon_clicked` | HTML |
| Pills (mode / ghost / grab) | `create_nav_pill` (~1426), `overlay_update_status_ui` | HTML pills + `state` message |
| Toast | `overlay_show_toast` (2200 ms) | UI toast + GDI fallback (done) |
| Move / resize / double-click | `on_header_button_press`, `on_header_motion_notify`, `on_window_motion_notify` (~726–880) | `overlay.cpp` (done for GDI; Phase 2 adds `BeginExternalDrag`) |
| Fullscreen / default size / compact mode / set_size | `overlay_set_fullscreen`, `overlay_set_default_size`, `overlay_set_compact_mode`, `overlay_set_size` (~520–720) | `overlay.cpp` (done) |
| Show / hide / reposition / apply_config / toggle | `overlay_show`, `overlay_hide`, … (~2160–2290) | done |
| App switching | `overlay_switch_app` (~996), `overlay_go_home`, `overlay_back` (~1029–1050), `overlay_universal_reset` (~1098) | `overlay::SwitchApp` + Phase 2 |
| Popup modal | `overlay_show_popup_modal` (~1595), `overlay_close_popup_modal`, `on_browser_popup_created`, `on_browser_divert` (~1480) | HTML modal layer |
| URL entry, reload/stop button, progress bar | `on_url_entry_activate`, `on_btn_reload_clicked`, `on_browser_uri_changed`, `on_browser_progress_changed`, `on_browser_state_changed` | HTML + `state` message |
| Ghost page | `create_ghost_page` (~1234), `on_test_entry_changed` (counter text), `ghost_settings_save`, `on_ghost_start`, `ghost_countdown_tick`, `ghost_eta_tick`, `overlay_set_ghost_button_state` (~1363), `overlay_ghost_progress` | HTML Ghost page (Phase 2 visuals, Phase 3 behaviour) |
| Snap | `overlay_snap`, `on_snap_timeout`, `save_and_prune_snap` (~2391–2640), snap icon stack | `snap.cpp` (Phase 3) |
| Click-through | `overlay_set_ghost` (~2283) | `overlay::SetGhost` (done) |
| X11 state properties (test hooks) | `update_x11_state_properties` (~350) | not needed on Windows |

## Browser (`src/browser.c`, `src/browser.h`)

| Topic | Function | Port to |
| --- | --- | --- |
| URL normalisation | `load_uri_internal` (~557) | `browser_logic::NormalizeInput` |
| Allowlist | `browser_is_domain_allowed_for_chatgpt` (~257) | `browser_logic::IsDomainAllowedForChatGpt` |
| Navigation diversion + auth detection | `on_decide_policy` (~331) | `NavigationStarting` handler |
| Popups / new windows | `on_web_view_create` (~222), `on_popup_close`, `on_popup_destroy` | `NewWindowRequested` + popup controller |
| ChatGPT Enter-to-send script | `s_chatgpt_enter_script` (~41–124) | `windows/ui/chatgpt_enter.js` + `ChatGptUserScript()` |
| WebView settings | `configure_webview` (~404) | `ConfigureView` |
| Shared persistent store | `browser_init` (~443) | one environment + user-data folder |
| Reset | `browser_universal_reset` (~638) | `browser::UniversalReset` |
| Mute on hide | `browser_pause` / `browser_resume` (~709) | `put_IsMuted` |
| Image paste | `browser_paste` (~729) | Phase 3 task 3.4 |
| Crash recovery | `on_web_process_terminated` (~380) | `ProcessFailed` handler |

## Input, hotkeys (`src/input.c`, `src/hotkey.c`)

| Topic | Function | Port to |
| --- | --- | --- |
| Modes, targets, idle timeout (60 s), exclusive grab | `input_set_key_mode`, `input_set_typing_target`, `restart_idle_timer`, `acquire_exclusive_grab`, `release_exclusive_grab` | `input.cpp` (Phase 3) |
| Click inside/outside overlay | `input_handle_raw_button` (~336), `input_notify_native_click`, `input_notify_overlay_hidden` | mouse hook + `input.cpp` |
| Raw key forwarding | `input_handle_raw_key` (~386) | Phase 3.3 (CDP key events). Note: Linux skips modifier-only keys |
| Hotkey matching, Alt-only rule, dedupe | `handle_raw_key_press`, `is_alt_physically_down`, `trigger_*` | `hotkey.cpp` (done) |
| Clipboard shield | `hotkey_install_clipboard_shield` (~521) / `remove` | Phase 3.2 |
| Linux-only (ignore) | pointer fallback `on_pointer_fallback_check`, Cinnamon locate-pointer, XI2 plumbing | not needed |

## Ghost Writer (`src/ghost.c`, 1042 lines)

| Topic | Function / constant | Port to |
| --- | --- | --- |
| Speeds | `k_mode_delay_ms` {300,115,85}, `ghost_speed_delay_ms`, `ghost_speed_wpm`, `validate_timing` | `ghost_logic` |
| Persisted settings | `ghost_get_timing`, `ghost_set_timing` (`[GhostTiming] human speed safe`) | `ghost.cpp` + `ini_writer` |
| Interval model | `rand_lognormal`, `next_interval_ms` | `ghost_logic::Typist` |
| Typo model | `typo_neighbor`, `take_next`, `s_typo_pct = 3` | `Typist::Next` |
| ETA | `model_ms`, `ghost_eta_remaining_ms` | `ghost_logic::EtaMs` |
| Text normalisation | `normalize_text_for_typing` | `NormalizeText` |
| Char → key | `char_to_keysym`, `resolve_keysym_on_layout`, spare keycode logic | `VkKeyScanExW` + Unicode fallback |
| Step machine | `on_ghost_typing_step` (phases CHAR/PRESS/RELEASE), `instant_step` (batch 16) | `ghost.cpp` timers |
| Target + app name | `find_target_window` (~527) | `GetForegroundWindow` + process name |
| Start / settle / finish / cancel | `ghost_writer_run` (~898), `on_ghost_typing_settle`, `finish_typing`, `ghost_writer_cancel`, `disengage_guard_delayed` | `ghost.cpp` |
| CapsLock handling | `XkbGetState`/`XkbLockModifiers` in run/finish/cancel | `GetKeyState(VK_CAPITAL)` + tagged VK_CAPITAL |
| Guard (ignore own keys) | `s_guard_active`, `ghost_is_guard_active` | `g_guardActive` + `kInjectTag` |

## Config, CLI, process (`src/config.c`, `src/main.c`, `scripts/`)

| Topic | Function | Windows (done in Phase 1) |
| --- | --- | --- |
| Config keys/ranges | `load_from_key_file`, `set_defaults`, `create_default_config_file` | `config.cpp` |
| IPC commands | `on_ipc_client_connected` (~79) | `app::HandleCommand` |
| CLI | `main` (~326) | `main.cpp` |
| Launcher/supervisor | `scripts/cronq`, `scripts/cronq-supervisor` | `main.cpp` |
| Install/uninstall | `install.sh`, `uninstall.sh`, `cronq.desktop` | Phase 4 installer |

## Tests worth copying the *intent* of (read-only)

`tests/test_phase6_snap.sh` (snap), `tests/test_phase7_ghost.sh` and `tests/test_ghost_page.sh` / `test_ghost_page.c` (ghost page behaviour and exact keystrokes),
`tests/run_phase3_tests.sh` (input modes), `tests/acceptance_phase1.sh` (basic overlay), `tests/adversary/*` (stacking attacks). They describe *what must hold true*; use them
to choose Wine/unit checks and manual steps.

## How to read C quickly if you are not fluent

- `g_strlcpy(dst, src, n)` = bounded copy. `g_ascii_strcasecmp(a,b)==0` = case-insensitive equal. `g_str_has_prefix/suffix` = starts/ends with.
- `g_random_int_range(a, b)` returns an integer in **[a, b)** (b excluded). `g_random_double_range(a,b)` is in [a,b).
- `g_timeout_add(ms, fn, data)` = run `fn` after `ms` ms (repeat while it returns TRUE) → on Windows `SetTimer` + a flag.
- `gboolean` = bool. `gunichar` = a Unicode code point.
- `X*` calls (XTest, XQuery..) are X11; they have no direct Windows meaning — use the equivalents in `02_LINUX_FEATURES_AND_WINDOWS_EQUIVALENTS.md`.
