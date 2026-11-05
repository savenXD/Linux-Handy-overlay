#include "hotkey.h"
#include "config.h"
#include "window.h"
#include "input.h"
#include "ghost.h"
#include "xutil.h"
#include "theme.h"
#include "killswitch.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XInput2.h>
#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static KeySym char_to_keysym_lower(char c) {
    if (c >= 'A' && c <= 'Z') c = c + ('a' - 'A');
    if (c >= 'a' && c <= 'z') return XK_a + (c - 'a');
    if (c >= '0' && c <= '9') return XK_0 + (c - '0');
    return XK_x;
}

static KeySym char_to_keysym_upper(char c) {
    if (c >= 'a' && c <= 'z') c = c - ('a' - 'A');
    if (c >= 'A' && c <= 'Z') return XK_A + (c - 'A');
    return NoSymbol;
}

static Display   *s_hotkey_dpy = NULL;
static Window     s_root = None;
static int        s_xi_opcode = 0;
static GSource   *s_hotkey_source = NULL;
static gint64     s_last_toggle_time = 0;
static gint64     s_last_mode_cycle_time = 0;
static gint64     s_last_ghost_toggle_time = 0;
static gint64     s_last_opacity_dec_time = 0;
static gint64     s_last_opacity_inc_time = 0;
static gint64     s_last_fullscreen_time = 0;
static gint64     s_last_default_size_time = 0;
static gint64     s_last_snap_time = 0;
static gint64     s_last_ghost_write_time = 0;

static bool       s_x_key_down = false;
static bool       s_g_key_down = false;
static bool       s_a_key_down = false;
static bool       s_s_key_down = false;
static bool       s_f_key_down = false;
static bool       s_d_key_down = false;
static bool       s_p_key_down = false;
static bool       s_m_key_down = false;
static bool       s_w_key_down = false;

/* Lock / kill switch / theme / corner hotkeys */
static bool       s_locked = false;
static gint64     s_last_lock_time = 0;
static gint64     s_last_camo_time = 0;
static gint64     s_last_text_time = 0;
static gint64     s_last_dock_time = 0;
static gint64     s_last_kill_time = 0;
static bool       s_prompt_grabs_on = false;

/* Raw event modifier tracking for resilience against external grabs */
static bool       s_raw_ctrl_down = false;
static bool       s_raw_shift_down = false;
static bool       s_raw_alt_down = false;

/* Active clipboard-key shield state */
static bool       s_clipboard_shield_active = false;
static gint64     s_last_shield_time = 0;
static KeyCode    s_last_shield_keycode = 0;

/* Cinnamon locate-pointer grab management (releases Muffin Control_L grab during overlay typing) */
static GSettings *s_cinnamon_mouse_settings = NULL;
static gboolean   s_orig_locate_pointer = FALSE;
static gboolean   s_managed_locate_pointer = FALSE;

static const unsigned int s_lock_combos[] = {
    0,
    Mod2Mask,                /* NumLock */
    LockMask,                /* CapsLock */
    Mod2Mask | LockMask      /* NumLock + CapsLock */
};
#define NUM_LOCK_COMBOS (sizeof(s_lock_combos) / sizeof(s_lock_combos[0]))

static inline bool is_keycode_down(const char keys[32], KeyCode kc) {
    if (kc == 0) return false;
    return (((unsigned char)keys[kc / 8]) & (1 << (kc % 8))) != 0;
}

#define MAX_CACHED_MOD_KEYCODES 16
static KeyCode s_cached_ctrl_keycodes[MAX_CACHED_MOD_KEYCODES];
static int     s_cached_ctrl_count = 0;
static KeyCode s_cached_shift_keycodes[MAX_CACHED_MOD_KEYCODES];
static int     s_cached_shift_count = 0;
static KeyCode s_cached_alt_keycodes[MAX_CACHED_MOD_KEYCODES];
static int     s_cached_alt_count = 0;

/* Cached application hotkey keycodes */
static KeyCode s_hk_x_l = 0, s_hk_x_u = 0;
static KeyCode s_hk_g_l = 0, s_hk_g_u = 0;
static KeyCode s_hk_a_l = 0, s_hk_a_u = 0;
static KeyCode s_hk_s_l = 0, s_hk_s_u = 0;
static KeyCode s_hk_f_l = 0, s_hk_f_u = 0;
static KeyCode s_hk_d_l = 0, s_hk_d_u = 0;
static KeyCode s_hk_p_l = 0, s_hk_p_u = 0;
static KeyCode s_hk_m_l = 0, s_hk_m_u = 0;
static KeyCode s_hk_w_l = 0, s_hk_w_u = 0;
static KeyCode s_hk_c_l = 0, s_hk_c_u = 0;
static KeyCode s_hk_t_l = 0, s_hk_t_u = 0;
static KeyCode s_hk_k_l = 0, s_hk_k_u = 0;
static KeyCode s_hk_left = 0, s_hk_right = 0;
static KeyCode s_hk_ux_l = 0, s_hk_ux_u = 0;      /* literal X: the unlock chord is always Alt+K+X */
static KeyCode s_kc_return = 0, s_kc_kp_enter = 0, s_kc_escape = 0;

static void hotkey_refresh_modifier_cache(Display *dpy) {
    if (!dpy) return;

    s_cached_ctrl_count = 0;
    s_cached_shift_count = 0;
    s_cached_alt_count = 0;

    KeyCode ctrl_l = XKeysymToKeycode(dpy, XK_Control_L);
    KeyCode ctrl_r = XKeysymToKeycode(dpy, XK_Control_R);
    if (ctrl_l && s_cached_ctrl_count < MAX_CACHED_MOD_KEYCODES) s_cached_ctrl_keycodes[s_cached_ctrl_count++] = ctrl_l;
    if (ctrl_r && ctrl_r != ctrl_l && s_cached_ctrl_count < MAX_CACHED_MOD_KEYCODES) s_cached_ctrl_keycodes[s_cached_ctrl_count++] = ctrl_r;

    KeyCode shift_l = XKeysymToKeycode(dpy, XK_Shift_L);
    KeyCode shift_r = XKeysymToKeycode(dpy, XK_Shift_R);
    if (shift_l && s_cached_shift_count < MAX_CACHED_MOD_KEYCODES) s_cached_shift_keycodes[s_cached_shift_count++] = shift_l;
    if (shift_r && shift_r != shift_l && s_cached_shift_count < MAX_CACHED_MOD_KEYCODES) s_cached_shift_keycodes[s_cached_shift_count++] = shift_r;

    KeyCode alt_l = XKeysymToKeycode(dpy, XK_Alt_L);
    KeyCode alt_r = XKeysymToKeycode(dpy, XK_Alt_R);

    if (alt_l && s_cached_alt_count < MAX_CACHED_MOD_KEYCODES) s_cached_alt_keycodes[s_cached_alt_count++] = alt_l;
    if (alt_r && alt_r != alt_l && s_cached_alt_count < MAX_CACHED_MOD_KEYCODES) s_cached_alt_keycodes[s_cached_alt_count++] = alt_r;

    XModifierKeymap *modmap = XGetModifierMapping(dpy);
    if (modmap) {
        for (int i = 0; i < modmap->max_keypermod; i++) {
            KeyCode kc_ctrl = modmap->modifiermap[ControlMapIndex * modmap->max_keypermod + i];
            if (kc_ctrl && s_cached_ctrl_count < MAX_CACHED_MOD_KEYCODES) {
                bool found = false;
                for (int j = 0; j < s_cached_ctrl_count; j++) {
                    if (s_cached_ctrl_keycodes[j] == kc_ctrl) { found = true; break; }
                }
                if (!found) s_cached_ctrl_keycodes[s_cached_ctrl_count++] = kc_ctrl;
            }

            KeyCode kc_shift = modmap->modifiermap[ShiftMapIndex * modmap->max_keypermod + i];
            if (kc_shift && s_cached_shift_count < MAX_CACHED_MOD_KEYCODES) {
                bool found = false;
                for (int j = 0; j < s_cached_shift_count; j++) {
                    if (s_cached_shift_keycodes[j] == kc_shift) { found = true; break; }
                }
                if (!found) s_cached_shift_keycodes[s_cached_shift_count++] = kc_shift;
            }

            KeyCode kc_alt = modmap->modifiermap[Mod1MapIndex * modmap->max_keypermod + i];
            if (kc_alt && s_cached_alt_count < MAX_CACHED_MOD_KEYCODES) {
                bool found = false;
                for (int j = 0; j < s_cached_alt_count; j++) {
                    if (s_cached_alt_keycodes[j] == kc_alt) { found = true; break; }
                }
                if (!found) s_cached_alt_keycodes[s_cached_alt_count++] = kc_alt;
            }
        }
        XFreeModifiermap(modmap);
    }

    /* Refresh registered hotkey keycodes */
    const AppConfig *cfg = config_get();
    char x_c = (cfg && cfg->toggle_key) ? cfg->toggle_key : 'x';
    char g_c = (cfg && cfg->ghost_key) ? cfg->ghost_key : 'g';
    char p_c = (cfg && cfg->snap_key) ? cfg->snap_key : 'p';
    char m_c = (cfg && cfg->mode_key) ? cfg->mode_key : 'm';
    char w_c = (cfg && cfg->ghost_writer_key) ? cfg->ghost_writer_key : 'w';

    s_hk_x_l = XKeysymToKeycode(dpy, char_to_keysym_lower(x_c));
    s_hk_x_u = XKeysymToKeycode(dpy, char_to_keysym_upper(x_c));
    s_hk_g_l = XKeysymToKeycode(dpy, char_to_keysym_lower(g_c));
    s_hk_g_u = XKeysymToKeycode(dpy, char_to_keysym_upper(g_c));
    s_hk_a_l = XKeysymToKeycode(dpy, XK_a);
    s_hk_a_u = XKeysymToKeycode(dpy, XK_A);
    s_hk_s_l = XKeysymToKeycode(dpy, XK_s);
    s_hk_s_u = XKeysymToKeycode(dpy, XK_S);
    s_hk_f_l = XKeysymToKeycode(dpy, XK_f);
    s_hk_f_u = XKeysymToKeycode(dpy, XK_F);
    s_hk_d_l = XKeysymToKeycode(dpy, XK_d);
    s_hk_d_u = XKeysymToKeycode(dpy, XK_D);
    s_hk_p_l = XKeysymToKeycode(dpy, char_to_keysym_lower(p_c));
    s_hk_p_u = XKeysymToKeycode(dpy, char_to_keysym_upper(p_c));
    s_hk_m_l = XKeysymToKeycode(dpy, char_to_keysym_lower(m_c));
    s_hk_m_u = XKeysymToKeycode(dpy, char_to_keysym_upper(m_c));
    s_hk_w_l = XKeysymToKeycode(dpy, char_to_keysym_lower(w_c));
    s_hk_w_u = XKeysymToKeycode(dpy, char_to_keysym_upper(w_c));

    s_hk_c_l = XKeysymToKeycode(dpy, XK_c);
    s_hk_c_u = XKeysymToKeycode(dpy, XK_C);
    s_hk_t_l = XKeysymToKeycode(dpy, XK_t);
    s_hk_t_u = XKeysymToKeycode(dpy, XK_T);
    s_hk_k_l = XKeysymToKeycode(dpy, XK_k);
    s_hk_k_u = XKeysymToKeycode(dpy, XK_K);
    s_hk_left = XKeysymToKeycode(dpy, XK_Left);
    s_hk_right = XKeysymToKeycode(dpy, XK_Right);
    s_hk_ux_l = XKeysymToKeycode(dpy, XK_x);
    s_hk_ux_u = XKeysymToKeycode(dpy, XK_X);
    s_kc_return = XKeysymToKeycode(dpy, XK_Return);
    s_kc_kp_enter = XKeysymToKeycode(dpy, XK_KP_Enter);
    s_kc_escape = XKeysymToKeycode(dpy, XK_Escape);
}

static inline bool is_ctrl_keycode(KeyCode kc) {
    if (kc == 0) return false;
    for (int i = 0; i < s_cached_ctrl_count; i++) {
        if (s_cached_ctrl_keycodes[i] == kc) return true;
    }
    return false;
}

static inline bool is_shift_keycode(KeyCode kc) {
    if (kc == 0) return false;
    for (int i = 0; i < s_cached_shift_count; i++) {
        if (s_cached_shift_keycodes[i] == kc) return true;
    }
    return false;
}

static inline bool is_alt_keycode(KeyCode kc) {
    if (kc == 0) return false;
    for (int i = 0; i < s_cached_alt_count; i++) {
        if (s_cached_alt_keycodes[i] == kc) return true;
    }
    return false;
}

static inline bool is_hotkey_candidate(KeyCode kc) {
    return (kc == s_hk_x_l || (s_hk_x_u && kc == s_hk_x_u) ||
            kc == s_hk_g_l || (s_hk_g_u && kc == s_hk_g_u) ||
            kc == s_hk_a_l || (s_hk_a_u && kc == s_hk_a_u) ||
            kc == s_hk_s_l || (s_hk_s_u && kc == s_hk_s_u) ||
            kc == s_hk_f_l || (s_hk_f_u && kc == s_hk_f_u) ||
            kc == s_hk_d_l || (s_hk_d_u && kc == s_hk_d_u) ||
            kc == s_hk_p_l || (s_hk_p_u && kc == s_hk_p_u) ||
            kc == s_hk_m_l || (s_hk_m_u && kc == s_hk_m_u) ||
            kc == s_hk_w_l || (s_hk_w_u && kc == s_hk_w_u) ||
            kc == s_hk_c_l || (s_hk_c_u && kc == s_hk_c_u) ||
            kc == s_hk_t_l || (s_hk_t_u && kc == s_hk_t_u) ||
            kc == s_hk_k_l || (s_hk_k_u && kc == s_hk_k_u) ||
            kc == s_hk_ux_l || (s_hk_ux_u && kc == s_hk_ux_u) ||
            (s_hk_left && kc == s_hk_left) || (s_hk_right && kc == s_hk_right));
}

static bool is_alt_physically_down(Display *dpy) {
    char keys[32];
    XQueryKeymap(dpy, keys);

    bool alt_down = false;
    for (int i = 0; i < s_cached_alt_count; i++) {
        if (is_keycode_down(keys, s_cached_alt_keycodes[i])) {
            alt_down = true;
            break;
        }
    }

    bool ctrl_down = false;
    for (int i = 0; i < s_cached_ctrl_count; i++) {
        if (is_keycode_down(keys, s_cached_ctrl_keycodes[i])) {
            ctrl_down = true;
            break;
        }
    }

    bool shift_down = false;
    for (int i = 0; i < s_cached_shift_count; i++) {
        if (is_keycode_down(keys, s_cached_shift_keycodes[i])) {
            shift_down = true;
            break;
        }
    }

    s_raw_alt_down = alt_down;
    if (s_raw_ctrl_down) ctrl_down = true;
    if (s_raw_shift_down) shift_down = true;

    return (alt_down && !ctrl_down && !shift_down);
}

/* Alt+Shift (no Ctrl) physically held: used for the Alt+Shift+K kill switch. */
static bool is_alt_shift_physically_down(Display *dpy) {
    char keys[32];
    XQueryKeymap(dpy, keys);
    bool alt_down = false, ctrl_down = false, shift_down = false;
    for (int i = 0; i < s_cached_alt_count; i++)   if (is_keycode_down(keys, s_cached_alt_keycodes[i]))   { alt_down = true;   break; }
    for (int i = 0; i < s_cached_ctrl_count; i++)  if (is_keycode_down(keys, s_cached_ctrl_keycodes[i]))  { ctrl_down = true;  break; }
    for (int i = 0; i < s_cached_shift_count; i++) if (is_keycode_down(keys, s_cached_shift_keycodes[i])) { shift_down = true; break; }
    if (s_raw_ctrl_down) ctrl_down = true;
    return alt_down && shift_down && !ctrl_down;
}

static bool is_key_physically_down(Display *dpy, KeyCode a, KeyCode b) {
    char keys[32];
    XQueryKeymap(dpy, keys);
    return is_keycode_down(keys, a) || is_keycode_down(keys, b);
}

GdkModifierType hotkey_get_active_modifiers(void) {
    GdkModifierType state = 0;
    if (s_raw_shift_down) state |= GDK_SHIFT_MASK;
    if (s_raw_ctrl_down)  state |= GDK_CONTROL_MASK;
    if (s_raw_alt_down)   state |= GDK_MOD1_MASK;
    return state;
}

static void get_hotkey_keycodes(Display *dpy,
                                KeyCode *x_l, KeyCode *x_u,
                                KeyCode *g_l, KeyCode *g_u,
                                KeyCode *a_l, KeyCode *a_u,
                                KeyCode *s_l, KeyCode *s_u,
                                KeyCode *f_l, KeyCode *f_u,
                                KeyCode *d_l, KeyCode *d_u,
                                KeyCode *p_l, KeyCode *p_u,
                                KeyCode *m_l, KeyCode *m_u,
                                KeyCode *w_l, KeyCode *w_u) {
    (void)dpy;
    if (x_l) *x_l = s_hk_x_l;
    if (x_u) *x_u = s_hk_x_u;
    if (g_l) *g_l = s_hk_g_l;
    if (g_u) *g_u = s_hk_g_u;
    if (a_l) *a_l = s_hk_a_l;
    if (a_u) *a_u = s_hk_a_u;
    if (s_l) *s_l = s_hk_s_l;
    if (s_u) *s_u = s_hk_s_u;
    if (f_l) *f_l = s_hk_f_l;
    if (f_u) *f_u = s_hk_f_u;
    if (d_l) *d_l = s_hk_d_l;
    if (d_u) *d_u = s_hk_d_u;
    if (p_l) *p_l = s_hk_p_l;
    if (p_u) *p_u = s_hk_p_u;
    if (m_l) *m_l = s_hk_m_l;
    if (m_u) *m_u = s_hk_m_u;
    if (w_l) *w_l = s_hk_w_l;
    if (w_u) *w_u = s_hk_w_u;
}

static void trigger_snap(const char *source_desc) {
    gint64 now = g_get_monotonic_time();
    if (now - s_last_snap_time < 200000) {
        xutil_log("HOTKEY", "Deduped snap from %s (<200ms since last trigger)", source_desc);
        return;
    }
    s_last_snap_time = now;
    xutil_log("HOTKEY", "Alt+P triggered via %s -> Taking snap", source_desc);
    overlay_snap();
}

static void trigger_hotkey_toggle(const char *source_desc) {
    gint64 now = g_get_monotonic_time();
    /* Dedupe within 150 ms (150,000 microseconds) */
    if (now - s_last_toggle_time < 150000) {
        xutil_log("HOTKEY", "Deduped toggle from %s (<150ms since last trigger)", source_desc);
        return;
    }

    s_last_toggle_time = now;
    xutil_log("HOTKEY", "Alt+X triggered via %s -> Toggling overlay", source_desc);
    input_release_grab();
    overlay_toggle();
}

static void trigger_ghost_toggle(const char *source_desc) {
    gint64 now = g_get_monotonic_time();
    if (now - s_last_ghost_toggle_time < 150000) {
        xutil_log("HOTKEY", "Deduped ghost toggle from %s (<150ms since last trigger)", source_desc);
        return;
    }
    s_last_ghost_toggle_time = now;
    xutil_log("HOTKEY", "Alt+G triggered via %s -> Toggling ghost mode", source_desc);
    input_toggle_ghost_mode();
}

static void trigger_opacity_dec(const char *source_desc) {
    gint64 now = g_get_monotonic_time();
    if (now - s_last_opacity_dec_time < 120000) {
        xutil_log("HOTKEY", "Deduped opacity dec from %s (<120ms since last trigger)", source_desc);
        return;
    }
    s_last_opacity_dec_time = now;
    xutil_log("HOTKEY", "Alt+A triggered via %s -> Decreasing opacity", source_desc);
    overlay_set_opacity(overlay_get_opacity() - 0.10);
}

static void trigger_opacity_inc(const char *source_desc) {
    gint64 now = g_get_monotonic_time();
    if (now - s_last_opacity_inc_time < 120000) {
        xutil_log("HOTKEY", "Deduped opacity inc from %s (<120ms since last trigger)", source_desc);
        return;
    }
    s_last_opacity_inc_time = now;
    xutil_log("HOTKEY", "Alt+S triggered via %s -> Increasing opacity", source_desc);
    overlay_set_opacity(overlay_get_opacity() + 0.10);
}

static void trigger_fullscreen(const char *source_desc) {
    gint64 now = g_get_monotonic_time();
    if (now - s_last_fullscreen_time < 200000) {
        xutil_log("HOTKEY", "Deduped fullscreen from %s (<200ms since last trigger)", source_desc);
        return;
    }
    s_last_fullscreen_time = now;
    xutil_log("HOTKEY", "Alt+F triggered via %s -> Fullscreen mode", source_desc);
    overlay_set_fullscreen();
}

static void trigger_default_size(const char *source_desc) {
    gint64 now = g_get_monotonic_time();
    if (now - s_last_default_size_time < 200000) {
        xutil_log("HOTKEY", "Deduped default size from %s (<200ms since last trigger)", source_desc);
        return;
    }
    s_last_default_size_time = now;
    xutil_log("HOTKEY", "Alt+D triggered via %s -> Default size", source_desc);
    overlay_set_default_size();
}

static void trigger_mode_cycle(const char *source_desc) {
    gint64 now = g_get_monotonic_time();
    if (now - s_last_mode_cycle_time < 150000) {
        xutil_log("HOTKEY", "Deduped mode cycle from %s (<150ms since last trigger)", source_desc);
        return;
    }
    s_last_mode_cycle_time = now;
    xutil_log("HOTKEY", "Alt+M triggered via %s -> Cycling key mode", source_desc);
    input_cycle_key_mode();
}

static void trigger_ghost_write(const char *source_desc) {
    gint64 now = g_get_monotonic_time();
    if (now - s_last_ghost_write_time < 200000) {
        xutil_log("HOTKEY", "Deduped ghost write from %s (<200ms)", source_desc);
        return;
    }
    s_last_ghost_write_time = now;
    xutil_log("HOTKEY", "Alt+W triggered via %s -> Ghost Write", source_desc);
    ghost_writer_run();
}

static inline bool is_kc(KeyCode kc, KeyCode l, KeyCode u) {
    return kc != 0 && (kc == l || (u != 0 && kc == u));
}

static void register_passive_grabs(void);
static void unregister_passive_grabs(void);

void hotkey_set_locked(bool locked) {
    if (s_locked == locked) return;
    s_locked = locked;
    if (locked) {
        xutil_log("HOTKEY", "LOCKED: hiding overlay and disabling all hotkeys except Alt+K+X / Alt+Shift+K.");
        ghost_writer_cancel();
        input_release_grab();
        overlay_hide();
        if (hotkey_is_clipboard_shield_active()) hotkey_remove_clipboard_shield();
    } else {
        xutil_log("HOTKEY", "UNLOCKED: hotkeys restored.");
    }
    if (s_hotkey_dpy) {
        unregister_passive_grabs();
        register_passive_grabs();
    }
    if (!locked) {
        overlay_show();
        overlay_show_toast("Unlocked");
    }
}

bool hotkey_is_locked(void) {
    return s_locked;
}

static bool dedupe_ok(gint64 *last, gint64 window_us, const char *what, const char *src) {
    gint64 now = g_get_monotonic_time();
    if (now - *last < window_us) {
        xutil_log("HOTKEY", "Deduped %s from %s", what, src);
        return false;
    }
    *last = now;
    return true;
}

/* Extended hotkeys: lock/unlock, kill switch, camouflage, text colour, corner docking.
 * Returns true when the key was consumed (including keys deliberately swallowed while locked). */
static bool dispatch_extended_hotkey(KeyCode kc, bool alt_shift, const char *src) {
    if (alt_shift) {
        if (is_kc(kc, s_hk_k_l, s_hk_k_u)) {
            if (dedupe_ok(&s_last_kill_time, 300000, "kill prompt", src)) {
                xutil_log("HOTKEY", "Alt+Shift+K triggered via %s -> Kill switch prompt", src);
                killswitch_prompt_show();
            }
            return true;
        }
        return false;
    }

    if (s_locked) {
        bool is_k = is_kc(kc, s_hk_k_l, s_hk_k_u);
        bool is_x = is_kc(kc, s_hk_ux_l, s_hk_ux_u);
        if (!is_k && !is_x) return false;
        bool partner = is_k ? is_key_physically_down(s_hotkey_dpy, s_hk_ux_l, s_hk_ux_u)
                            : is_key_physically_down(s_hotkey_dpy, s_hk_k_l, s_hk_k_u);
        if (partner && dedupe_ok(&s_last_lock_time, 250000, "unlock", src)) {
            xutil_log("HOTKEY", "Alt+K+X chord via %s -> Unlock", src);
            hotkey_set_locked(false);
        }
        return true;   /* Alt+K / Alt+X are swallowed while locked */
    }

    if (is_kc(kc, s_hk_k_l, s_hk_k_u)) {
        if (dedupe_ok(&s_last_lock_time, 250000, "lock", src)) {
            xutil_log("HOTKEY", "Alt+K triggered via %s -> Lock", src);
            hotkey_set_locked(true);
        }
        return true;
    }
    if (is_kc(kc, s_hk_c_l, s_hk_c_u)) {
        if (dedupe_ok(&s_last_camo_time, 250000, "camouflage", src)) {
            xutil_log("HOTKEY", "Alt+C triggered via %s -> Toggle camouflage", src);
            theme_toggle_camo();
        }
        return true;
    }
    if (is_kc(kc, s_hk_t_l, s_hk_t_u)) {
        if (dedupe_ok(&s_last_text_time, 250000, "text colour", src)) {
            xutil_log("HOTKEY", "Alt+T triggered via %s -> Toggle text colour", src);
            theme_toggle_text();
        }
        return true;
    }
    if (is_kc(kc, s_hk_left, 0) || is_kc(kc, s_hk_right, 0)) {
        if (dedupe_ok(&s_last_dock_time, 200000, "dock", src)) {
            bool left = is_kc(kc, s_hk_left, 0);
            xutil_log("HOTKEY", "Alt+%s triggered via %s -> Dock %s", left ? "Left" : "Right", src, left ? "bottom-left" : "bottom-right");
            overlay_dock_corner(left ? "bottom-left" : "bottom-right");
        }
        return true;
    }
    return false;
}

void hotkey_set_prompt_grabs(bool on) {
    if (!s_hotkey_dpy || s_root == None) return;
    KeyCode kcs[] = { s_kc_return, s_kc_kp_enter, s_kc_escape };
    for (size_t i = 0; i < sizeof(kcs) / sizeof(kcs[0]); i++) {
        if (!kcs[i]) continue;
        if (on) XGrabKey(s_hotkey_dpy, kcs[i], AnyModifier, s_root, False, GrabModeAsync, GrabModeAsync);
        else    XUngrabKey(s_hotkey_dpy, kcs[i], AnyModifier, s_root);
    }
    XSync(s_hotkey_dpy, False);
    s_prompt_grabs_on = on;
}

/* Returns true if the key was consumed by the kill prompt (Enter confirms, Esc cancels). */
static bool handle_prompt_key(KeyCode kc) {
    if (!killswitch_prompt_active()) return false;
    if (kc != 0 && (kc == s_kc_return || kc == s_kc_kp_enter)) { killswitch_prompt_confirm(); return true; }
    if (kc != 0 && kc == s_kc_escape) { killswitch_prompt_cancel(); return true; }
    return false;
}

static void handle_raw_key_press(int detail) {
    if (!s_hotkey_dpy) return;
    if (ghost_is_guard_active()) return;

    /* Deduplicate against active clipboard-shield grabs (<120ms) */
    gint64 now = g_get_monotonic_time();
    if (s_clipboard_shield_active && (now - s_last_shield_time < 120000) && ((KeyCode)detail == s_last_shield_keycode)) {
        xutil_log("HOTKEY", "Deduped raw keypress matching shielded keycode %d (<120ms)", detail);
        return;
    }

    KeyCode kc = (KeyCode)detail;

    if (handle_prompt_key(kc)) return;

    /* Fast modifier tracking */
    if (is_ctrl_keycode(kc))  s_raw_ctrl_down = true;
    if (is_shift_keycode(kc)) s_raw_shift_down = true;
    if (is_alt_keycode(kc))   s_raw_alt_down = true;

    /* If not a hotkey candidate, forward to overlay/background immediately with 0 X11 queries */
    if (!is_hotkey_candidate(kc)) {
        input_handle_raw_key(detail, true);
        return;
    }

    /* Alt+Shift+K kill switch (works even while locked) */
    if (is_kc(kc, s_hk_k_l, s_hk_k_u) && is_alt_shift_physically_down(s_hotkey_dpy)) {
        dispatch_extended_hotkey(kc, true, "XInput2 Raw Event");
        return;
    }

    /* Key matches a hotkey letter: strictly check if Alt is physically held down without Ctrl/Shift */
    if (!is_alt_physically_down(s_hotkey_dpy)) {
        s_x_key_down = false;
        s_g_key_down = false;
        s_a_key_down = false;
        s_s_key_down = false;
        s_f_key_down = false;
        s_d_key_down = false;
        s_p_key_down = false;
        s_m_key_down = false;
        s_w_key_down = false;
        input_handle_raw_key(detail, true);
        return;
    }

    if (dispatch_extended_hotkey(kc, false, "XInput2 Raw Event")) return;
    if (s_locked) {
        input_handle_raw_key(detail, true);
        return;
    }

    if (kc == s_hk_x_l || (s_hk_x_u != 0 && kc == s_hk_x_u)) {
        trigger_hotkey_toggle("XInput2 Raw Event");
        return;
    }
    if (kc == s_hk_g_l || (s_hk_g_u != 0 && kc == s_hk_g_u)) {
        trigger_ghost_toggle("XInput2 Raw Event");
        return;
    }
    if (kc == s_hk_a_l || (s_hk_a_u != 0 && kc == s_hk_a_u)) {
        trigger_opacity_dec("XInput2 Raw Event");
        return;
    }
    if (kc == s_hk_s_l || (s_hk_s_u != 0 && kc == s_hk_s_u)) {
        trigger_opacity_inc("XInput2 Raw Event");
        return;
    }
    if (kc == s_hk_f_l || (s_hk_f_u != 0 && kc == s_hk_f_u)) {
        trigger_fullscreen("XInput2 Raw Event");
        return;
    }
    if (kc == s_hk_d_l || (s_hk_d_u != 0 && kc == s_hk_d_u)) {
        trigger_default_size("XInput2 Raw Event");
        return;
    }
    if (kc == s_hk_p_l || (s_hk_p_u != 0 && kc == s_hk_p_u)) {
        trigger_snap("XInput2 Raw Event");
        return;
    }
    if (kc == s_hk_m_l || (s_hk_m_u != 0 && kc == s_hk_m_u)) {
        trigger_mode_cycle("XInput2 Raw Event");
        return;
    }
    if (kc == s_hk_w_l || (s_hk_w_u != 0 && kc == s_hk_w_u)) {
        trigger_ghost_write("XInput2 Raw Event");
        return;
    }

    input_handle_raw_key(detail, true);
}

static void handle_raw_key_release(int detail) {
    if (!s_hotkey_dpy) return;

    KeyCode kc = (KeyCode)detail;

    /* Fast modifier release tracking */
    if (is_ctrl_keycode(kc))  s_raw_ctrl_down = false;
    if (is_shift_keycode(kc)) s_raw_shift_down = false;
    if (is_alt_keycode(kc)) {
        s_raw_alt_down = false;
        s_x_key_down = false;
        s_g_key_down = false;
        s_a_key_down = false;
        s_s_key_down = false;
        s_f_key_down = false;
        s_d_key_down = false;
        s_p_key_down = false;
        s_m_key_down = false;
        s_w_key_down = false;
    }

    if (ghost_is_guard_active()) return;

    /* Deduplicate against active clipboard-shield grabs (<120ms) */
    gint64 now = g_get_monotonic_time();
    if (s_clipboard_shield_active && (now - s_last_shield_time < 120000) && (kc == s_last_shield_keycode)) {
        return;
    }

    if (kc == s_hk_x_l || (s_hk_x_u != 0 && kc == s_hk_x_u)) s_x_key_down = false;
    if (kc == s_hk_g_l || (s_hk_g_u != 0 && kc == s_hk_g_u)) s_g_key_down = false;
    if (kc == s_hk_a_l || (s_hk_a_u != 0 && kc == s_hk_a_u)) s_a_key_down = false;
    if (kc == s_hk_s_l || (s_hk_s_u != 0 && kc == s_hk_s_u)) s_s_key_down = false;
    if (kc == s_hk_f_l || (s_hk_f_u != 0 && kc == s_hk_f_u)) s_f_key_down = false;
    if (kc == s_hk_d_l || (s_hk_d_u != 0 && kc == s_hk_d_u)) s_d_key_down = false;
    if (kc == s_hk_p_l || (s_hk_p_u != 0 && kc == s_hk_p_u)) s_p_key_down = false;
    if (kc == s_hk_m_l || (s_hk_m_u != 0 && kc == s_hk_m_u)) s_m_key_down = false;
    if (kc == s_hk_w_l || (s_hk_w_u != 0 && kc == s_hk_w_u)) s_w_key_down = false;

    /* Forward key release to input subsystem */
    input_handle_raw_key(detail, false);
}

static void register_passive_grabs(void);
static void unregister_passive_grabs(void);

void hotkey_install_clipboard_shield(void) {
    if (!s_hotkey_dpy || s_root == None) return;
    if (s_clipboard_shield_active) return;

    /* If Cinnamon's locate-pointer is enabled, Muffin holds a passive grab on Control_L
     * that intercepts Control keys before any other client on root. Temporarily disable it. */
    if (s_cinnamon_mouse_settings) {
        s_orig_locate_pointer = g_settings_get_boolean(s_cinnamon_mouse_settings, "locate-pointer");
        if (s_orig_locate_pointer) {
            g_settings_set_boolean(s_cinnamon_mouse_settings, "locate-pointer", FALSE);
            s_managed_locate_pointer = TRUE;
            g_settings_sync();
            g_usleep(15000); /* 15 ms to allow Muffin to process ungrab */
            xutil_log("HOTKEY", "Temporarily disengaged Cinnamon locate-pointer (releases WM Control_L grab).");
        }
    }

    KeyCode a_l = XKeysymToKeycode(s_hotkey_dpy, XK_a);
    KeyCode a_u = XKeysymToKeycode(s_hotkey_dpy, XK_A);
    KeyCode c_l = XKeysymToKeycode(s_hotkey_dpy, XK_c);
    KeyCode c_u = XKeysymToKeycode(s_hotkey_dpy, XK_C);
    KeyCode v_l = XKeysymToKeycode(s_hotkey_dpy, XK_v);
    KeyCode v_u = XKeysymToKeycode(s_hotkey_dpy, XK_V);
    KeyCode x_l = XKeysymToKeycode(s_hotkey_dpy, XK_x);
    KeyCode x_u = XKeysymToKeycode(s_hotkey_dpy, XK_X);

    KeyCode kcs[] = { a_l, a_u, c_l, c_u, v_l, v_u, x_l, x_u };
    size_t num_kcs = sizeof(kcs) / sizeof(kcs[0]);

    for (size_t i = 0; i < NUM_LOCK_COMBOS; i++) {
        unsigned int mods = ControlMask | s_lock_combos[i];
        for (size_t k = 0; k < num_kcs; k++) {
            if (kcs[k] != 0) {
                XGrabKey(s_hotkey_dpy, kcs[k], mods, s_root, False, GrabModeAsync, GrabModeAsync);
            }
        }
    }
    XSync(s_hotkey_dpy, False);
    s_clipboard_shield_active = true;
    xutil_log("HOTKEY", "Installed active clipboard-key shield (Ctrl+A/C/V/X passive grabs engaged).");
}

void hotkey_remove_clipboard_shield(void) {
    if (!s_hotkey_dpy || s_root == None) return;
    if (!s_clipboard_shield_active) return;

    KeyCode a_l = XKeysymToKeycode(s_hotkey_dpy, XK_a);
    KeyCode a_u = XKeysymToKeycode(s_hotkey_dpy, XK_A);
    KeyCode c_l = XKeysymToKeycode(s_hotkey_dpy, XK_c);
    KeyCode c_u = XKeysymToKeycode(s_hotkey_dpy, XK_C);
    KeyCode v_l = XKeysymToKeycode(s_hotkey_dpy, XK_v);
    KeyCode v_u = XKeysymToKeycode(s_hotkey_dpy, XK_V);
    KeyCode x_l = XKeysymToKeycode(s_hotkey_dpy, XK_x);
    KeyCode x_u = XKeysymToKeycode(s_hotkey_dpy, XK_X);

    KeyCode kcs[] = { a_l, a_u, c_l, c_u, v_l, v_u, x_l, x_u };
    size_t num_kcs = sizeof(kcs) / sizeof(kcs[0]);

    for (size_t i = 0; i < NUM_LOCK_COMBOS; i++) {
        unsigned int mods = ControlMask | s_lock_combos[i];
        for (size_t k = 0; k < num_kcs; k++) {
            if (kcs[k] != 0) {
                XUngrabKey(s_hotkey_dpy, kcs[k], mods, s_root);
            }
        }
    }
    XFlush(s_hotkey_dpy);
    s_clipboard_shield_active = false;
    xutil_log("HOTKEY", "Removed active clipboard-key shield (passive grabs released).");

    /* Restore Cinnamon locate-pointer setting */
    if (s_managed_locate_pointer && s_cinnamon_mouse_settings) {
        g_settings_set_boolean(s_cinnamon_mouse_settings, "locate-pointer", s_orig_locate_pointer);
        s_managed_locate_pointer = FALSE;
        g_settings_sync();
        xutil_log("HOTKEY", "Restored Cinnamon locate-pointer setting to %d.", s_orig_locate_pointer);
    }
}

bool hotkey_is_clipboard_shield_active(void) {
    return s_clipboard_shield_active;
}

static void hotkey_process_events(void) {
    if (!s_hotkey_dpy) return;

    while (XPending(s_hotkey_dpy) > 0) {
        XEvent ev;
        XNextEvent(s_hotkey_dpy, &ev);

        if (ev.type == GenericEvent && ev.xcookie.extension == s_xi_opcode) {
            if (XGetEventData(s_hotkey_dpy, &ev.xcookie)) {
                if (ev.xcookie.evtype == XI_RawKeyPress) {
                    XIRawEvent *raw = (XIRawEvent *)ev.xcookie.data;
                    handle_raw_key_press(raw->detail);
                } else if (ev.xcookie.evtype == XI_RawKeyRelease) {
                    XIRawEvent *raw = (XIRawEvent *)ev.xcookie.data;
                    handle_raw_key_release(raw->detail);
                } else if (ev.xcookie.evtype == XI_RawButtonPress || ev.xcookie.evtype == XI_RawButtonRelease) {
                    XIRawEvent *raw = (XIRawEvent *)ev.xcookie.data;
                    Window r_root, r_child;
                    int rx = 0, ry = 0, wx = 0, wy = 0;
                    unsigned int mask = 0;
                    XQueryPointer(s_hotkey_dpy, s_root, &r_root, &r_child, &rx, &ry, &wx, &wy, &mask);
                    input_handle_raw_button(raw->detail, (ev.xcookie.evtype == XI_RawButtonPress), rx, ry);
                }
                XFreeEventData(s_hotkey_dpy, &ev.xcookie);
            }
        } else if (ev.type == KeyPress) {
            if (ghost_is_guard_active()) continue;

            /* 1. Check if event is caught by active clipboard shield */
            if (s_clipboard_shield_active) {
                unsigned int active_mods = ev.xkey.state & (ControlMask | ShiftMask | Mod1Mask);
                if (active_mods == ControlMask) {
                    KeyCode a_l = XKeysymToKeycode(s_hotkey_dpy, XK_a);
                    KeyCode a_u = XKeysymToKeycode(s_hotkey_dpy, XK_A);
                    KeyCode c_l = XKeysymToKeycode(s_hotkey_dpy, XK_c);
                    KeyCode c_u = XKeysymToKeycode(s_hotkey_dpy, XK_C);
                    KeyCode v_l = XKeysymToKeycode(s_hotkey_dpy, XK_v);
                    KeyCode v_u = XKeysymToKeycode(s_hotkey_dpy, XK_V);
                    KeyCode x_l = XKeysymToKeycode(s_hotkey_dpy, XK_x);
                    KeyCode x_u = XKeysymToKeycode(s_hotkey_dpy, XK_X);

                    KeyCode kc = ev.xkey.keycode;
                    if (kc == a_l || (a_u != 0 && kc == a_u)) {
                        s_last_shield_time = g_get_monotonic_time();
                        s_last_shield_keycode = kc;
                        xutil_log("HOTKEY", "Shield grab caught Ctrl+A -> dispatching SelectAll to overlay");
                        overlay_execute_clipboard_action(CLIPBOARD_ACTION_SELECT_ALL);
                        continue;
                    } else if (kc == c_l || (c_u != 0 && kc == c_u)) {
                        s_last_shield_time = g_get_monotonic_time();
                        s_last_shield_keycode = kc;
                        xutil_log("HOTKEY", "Shield grab caught Ctrl+C -> dispatching Copy to overlay");
                        overlay_execute_clipboard_action(CLIPBOARD_ACTION_COPY);
                        continue;
                    } else if (kc == v_l || (v_u != 0 && kc == v_u)) {
                        s_last_shield_time = g_get_monotonic_time();
                        s_last_shield_keycode = kc;
                        xutil_log("HOTKEY", "Shield grab caught Ctrl+V -> dispatching Paste to overlay");
                        overlay_execute_clipboard_action(CLIPBOARD_ACTION_PASTE);
                        continue;
                    } else if (kc == x_l || (x_u != 0 && kc == x_u)) {
                        s_last_shield_time = g_get_monotonic_time();
                        s_last_shield_keycode = kc;
                        xutil_log("HOTKEY", "Shield grab caught Ctrl+X -> dispatching Cut to overlay");
                        overlay_execute_clipboard_action(CLIPBOARD_ACTION_CUT);
                        continue;
                    }
                }
            }

            /* 1b. Kill prompt keys (Enter / Esc) */
            if (handle_prompt_key(ev.xkey.keycode)) continue;

            /* 1c. Alt+Shift+K kill switch */
            if ((ev.xkey.state & Mod1Mask) && (ev.xkey.state & ShiftMask) && !(ev.xkey.state & ControlMask)) {
                dispatch_extended_hotkey(ev.xkey.keycode, true, "Passive XGrabKey");
                continue;
            }

            /* 2. Global hotkeys (Alt+X / G / A / S / F / D / P / M / W, plus C / T / K / Left / Right) */
            if ((ev.xkey.state & Mod1Mask) == 0 || !is_alt_physically_down(s_hotkey_dpy)) {
                continue;
            }
            if (dispatch_extended_hotkey(ev.xkey.keycode, false, "Passive XGrabKey")) continue;
            if (s_locked) continue;

            KeyCode x_l, x_u, g_l, g_u, a_l, a_u, s_l, s_u, f_l, f_u, d_l, d_u, p_l, p_u, m_l, m_u, w_l, w_u;
            get_hotkey_keycodes(s_hotkey_dpy,
                                &x_l, &x_u, &g_l, &g_u, &a_l, &a_u, &s_l, &s_u,
                                &f_l, &f_u, &d_l, &d_u, &p_l, &p_u, &m_l, &m_u, &w_l, &w_u);

            KeyCode kc = ev.xkey.keycode;
            if (kc == x_l || (x_u != 0 && kc == x_u)) {
                s_x_key_down = true;
                trigger_hotkey_toggle("Passive XGrabKey");
            } else if (kc == g_l || (g_u != 0 && kc == g_u)) {
                s_g_key_down = true;
                trigger_ghost_toggle("Passive XGrabKey");
            } else if (kc == a_l || (a_u != 0 && kc == a_u)) {
                s_a_key_down = true;
                trigger_opacity_dec("Passive XGrabKey");
            } else if (kc == s_l || (s_u != 0 && kc == s_u)) {
                s_s_key_down = true;
                trigger_opacity_inc("Passive XGrabKey");
            } else if (kc == f_l || (f_u != 0 && kc == f_u)) {
                s_f_key_down = true;
                trigger_fullscreen("Passive XGrabKey");
            } else if (kc == d_l || (d_u != 0 && kc == d_u)) {
                s_d_key_down = true;
                trigger_default_size("Passive XGrabKey");
            } else if (kc == p_l || (p_u != 0 && kc == p_u)) {
                s_p_key_down = true;
                trigger_snap("Passive XGrabKey");
            } else if (kc == m_l || (m_u != 0 && kc == m_u)) {
                s_m_key_down = true;
                trigger_mode_cycle("Passive XGrabKey");
            } else if (kc == w_l || (w_u != 0 && kc == w_u)) {
                s_w_key_down = true;
                trigger_ghost_write("Passive XGrabKey");
            }
        } else if (ev.type == KeyRelease) {
            if (ghost_is_guard_active()) continue;

            KeyCode x_l, x_u, g_l, g_u, a_l, a_u, s_l, s_u, f_l, f_u, d_l, d_u, p_l, p_u, m_l, m_u, w_l, w_u;
            get_hotkey_keycodes(s_hotkey_dpy,
                                &x_l, &x_u, &g_l, &g_u, &a_l, &a_u, &s_l, &s_u,
                                &f_l, &f_u, &d_l, &d_u, &p_l, &p_u, &m_l, &m_u, &w_l, &w_u);

            KeyCode kc = ev.xkey.keycode;
            if (kc == x_l || (x_u != 0 && kc == x_u)) {
                s_x_key_down = false;
            } else if (kc == g_l || (g_u != 0 && kc == g_u)) {
                s_g_key_down = false;
            } else if (kc == a_l || (a_u != 0 && kc == a_u)) {
                s_a_key_down = false;
            } else if (kc == s_l || (s_u != 0 && kc == s_u)) {
                s_s_key_down = false;
            } else if (kc == f_l || (f_u != 0 && kc == f_u)) {
                s_f_key_down = false;
            } else if (kc == d_l || (d_u != 0 && kc == d_u)) {
                s_d_key_down = false;
            } else if (kc == p_l || (p_u != 0 && kc == p_u)) {
                s_p_key_down = false;
            } else if (kc == m_l || (m_u != 0 && kc == m_u)) {
                s_m_key_down = false;
            } else if (kc == w_l || (w_u != 0 && kc == w_u)) {
                s_w_key_down = false;
            }
        } else if (ev.type == MappingNotify) {
            XRefreshKeyboardMapping(&ev.xmapping);
            hotkey_refresh_modifier_cache(s_hotkey_dpy);
            xutil_log("HOTKEY", "Keyboard mapping updated by server. Re-registering grabs.");
            unregister_passive_grabs();
            register_passive_grabs();
            if (s_clipboard_shield_active) {
                hotkey_remove_clipboard_shield();
                hotkey_install_clipboard_shield();
            }
        }
    }
}

/* Custom GSource to integrate secondary Display socket FD into GLib */
typedef struct {
    GSource     source;
    Display    *dpy;
    GPollFD     poll_fd;
} HotkeyGSource;

static gboolean hotkey_source_prepare(GSource *source, gint *timeout) {
    HotkeyGSource *hsrc = (HotkeyGSource *)source;
    *timeout = -1;
    return (XPending(hsrc->dpy) > 0);
}

static gboolean hotkey_source_check(GSource *source) {
    HotkeyGSource *hsrc = (HotkeyGSource *)source;
    if (hsrc->poll_fd.revents & (G_IO_IN | G_IO_HUP | G_IO_ERR)) {
        return TRUE;
    }
    return (XPending(hsrc->dpy) > 0);
}

static gboolean hotkey_source_dispatch(GSource *source, GSourceFunc callback, gpointer user_data) {
    (void)source;
    (void)callback;
    (void)user_data;
    hotkey_process_events();
    return G_SOURCE_CONTINUE;
}

static GSourceFuncs s_hotkey_source_funcs = {
    hotkey_source_prepare,
    hotkey_source_check,
    hotkey_source_dispatch,
    NULL, NULL, NULL
};

/* Fills the keycodes to grab with Alt. Locked: only K and X (unlock chord). */
static size_t build_grab_list(KeyCode *out, size_t max, bool locked) {
    size_t n = 0;
    #define ADD(k) do { if ((k) != 0 && n < max) out[n++] = (k); } while (0)
    ADD(s_hk_k_l); ADD(s_hk_k_u);
    ADD(s_hk_ux_l); ADD(s_hk_ux_u);
    if (!locked) {
        ADD(s_hk_x_l); ADD(s_hk_x_u);
        ADD(s_hk_g_l); ADD(s_hk_g_u);
        ADD(s_hk_a_l); ADD(s_hk_a_u);
        ADD(s_hk_s_l); ADD(s_hk_s_u);
        ADD(s_hk_f_l); ADD(s_hk_f_u);
        ADD(s_hk_d_l); ADD(s_hk_d_u);
        ADD(s_hk_p_l); ADD(s_hk_p_u);
        ADD(s_hk_m_l); ADD(s_hk_m_u);
        ADD(s_hk_w_l); ADD(s_hk_w_u);
        ADD(s_hk_c_l); ADD(s_hk_c_u);
        ADD(s_hk_t_l); ADD(s_hk_t_u);
        ADD(s_hk_left); ADD(s_hk_right);
    }
    #undef ADD
    return n;
}

static void grab_or_ungrab(bool grab, bool locked) {
    KeyCode kcs[40];
    size_t n = build_grab_list(kcs, sizeof(kcs) / sizeof(kcs[0]), locked);
    for (size_t i = 0; i < NUM_LOCK_COMBOS; i++) {
        for (size_t k = 0; k < n; k++) {
            unsigned int mods = Mod1Mask | s_lock_combos[i];
            if (grab) XGrabKey(s_hotkey_dpy, kcs[k], mods, s_root, False, GrabModeAsync, GrabModeAsync);
            else      XUngrabKey(s_hotkey_dpy, kcs[k], mods, s_root);
        }
        /* Alt+Shift+K kill switch is always armed */
        KeyCode kk[2] = { s_hk_k_l, s_hk_k_u };
        for (int j = 0; j < 2; j++) {
            if (!kk[j]) continue;
            unsigned int mods = Mod1Mask | ShiftMask | s_lock_combos[i];
            if (grab) XGrabKey(s_hotkey_dpy, kk[j], mods, s_root, False, GrabModeAsync, GrabModeAsync);
            else      XUngrabKey(s_hotkey_dpy, kk[j], mods, s_root);
        }
    }
}

static void register_passive_grabs(void) {
    if (!s_hotkey_dpy || s_root == None) return;
    grab_or_ungrab(true, s_locked);
    XSync(s_hotkey_dpy, False);
    xutil_log("HOTKEY", "Registered passive XGrabKey for %s across lock variations.",
              s_locked ? "Alt+K / Alt+X (locked) and Alt+Shift+K"
                       : "Alt+x/g/a/s/f/d/p/m/w/c/t/k/Left/Right and Alt+Shift+K");
}

static void unregister_passive_grabs(void) {
    if (!s_hotkey_dpy || s_root == None) return;
    grab_or_ungrab(false, false);   /* release the superset so nothing lingers across lock changes */
    XFlush(s_hotkey_dpy);
    xutil_log("HOTKEY", "Released all passive XGrabKey grabs.");
}

void hotkey_reload_grabs(void) {
    if (!s_hotkey_dpy || s_root == None) return;
    hotkey_refresh_modifier_cache(s_hotkey_dpy);
    unregister_passive_grabs();
    register_passive_grabs();
    if (s_clipboard_shield_active) {
        hotkey_remove_clipboard_shield();
        hotkey_install_clipboard_shield();
    }
    xutil_log("HOTKEY", "Reloaded passive grabs based on updated configuration.");
}

gboolean hotkey_init(void) {
    /* Initialize Cinnamon mouse settings to manage locate-pointer grab during overlay typing */
    GSettingsSchemaSource *src = g_settings_schema_source_get_default();
    if (src) {
        GSettingsSchema *schema = g_settings_schema_source_lookup(src, "org.cinnamon.desktop.peripherals.mouse", TRUE);
        if (schema) {
            s_cinnamon_mouse_settings = g_settings_new("org.cinnamon.desktop.peripherals.mouse");
            g_settings_schema_unref(schema);
        }
    }

    /* 1. Open dedicated SECOND Display connection */
    s_hotkey_dpy = XOpenDisplay(NULL);
    if (!s_hotkey_dpy) {
        xutil_log_err("HOTKEY", "Failed to open dedicated X11 display connection for hotkeys!");
        return FALSE;
    }

    int screen = DefaultScreen(s_hotkey_dpy);
    s_root = RootWindow(s_hotkey_dpy, screen);

    /* Cache modifier and hotkey keycodes */
    hotkey_refresh_modifier_cache(s_hotkey_dpy);

    /* 2. Verify XInput 2.2+ extension support */
    int xi_event, xi_error;
    if (!XQueryExtension(s_hotkey_dpy, "XInputExtension", &s_xi_opcode, &xi_event, &xi_error)) {
        xutil_log_err("HOTKEY", "XInputExtension not supported on this X server!");
        XCloseDisplay(s_hotkey_dpy);
        s_hotkey_dpy = NULL;
        return FALSE;
    }

    int major = 2, minor = 2;
    if (XIQueryVersion(s_hotkey_dpy, &major, &minor) != Success) {
        xutil_log_err("HOTKEY", "XInput2 version 2.2+ required, but server reported %d.%d", major, minor);
        XCloseDisplay(s_hotkey_dpy);
        s_hotkey_dpy = NULL;
        return FALSE;
    }
    xutil_log("HOTKEY", "XInput2 initialized (version %d.%d, opcode %d).", major, minor, s_xi_opcode);

    /* 3. Select XI_RawKeyPress, XI_RawKeyRelease, XI_RawButtonPress, XI_RawButtonRelease on root window */
    XIEventMask mask;
    unsigned char mask_bits[XIMaskLen(XI_RawButtonRelease)] = {0};
    mask.deviceid = XIAllMasterDevices;
    mask.mask_len = sizeof(mask_bits);
    mask.mask = mask_bits;
    XISetMask(mask_bits, XI_RawKeyPress);
    XISetMask(mask_bits, XI_RawKeyRelease);
    XISetMask(mask_bits, XI_RawButtonPress);
    XISetMask(mask_bits, XI_RawButtonRelease);

    if (XISelectEvents(s_hotkey_dpy, s_root, &mask, 1) != Success) {
        xutil_log_err("HOTKEY", "Failed to select XI2 raw events on root window!");
        XCloseDisplay(s_hotkey_dpy);
        s_hotkey_dpy = NULL;
        return FALSE;
    }
    XSync(s_hotkey_dpy, False);
    xutil_log("HOTKEY", "Selected XI_RawKeyPress/Release and XI_RawButtonPress/Release on root window.");

    /* 4. Register passive XGrabKey on root window */
    register_passive_grabs();

    /* 5. Integrate secondary Display socket into GLib main loop */
    GSource *source = g_source_new(&s_hotkey_source_funcs, sizeof(HotkeyGSource));
    HotkeyGSource *hsrc = (HotkeyGSource *)source;
    hsrc->dpy = s_hotkey_dpy;
    hsrc->poll_fd.fd = ConnectionNumber(s_hotkey_dpy);
    hsrc->poll_fd.events = G_IO_IN | G_IO_HUP | G_IO_ERR;
    hsrc->poll_fd.revents = 0;

    g_source_add_poll(source, &hsrc->poll_fd);
    g_source_attach(source, NULL);
    s_hotkey_source = source;

    xutil_log("HOTKEY", "Integrated dedicated Display FD %d into GLib main loop.", hsrc->poll_fd.fd);
    return TRUE;
}

void hotkey_cleanup(void) {
    if (!s_hotkey_dpy) return;

    hotkey_remove_clipboard_shield();
    unregister_passive_grabs();

    if (s_hotkey_source) {
        g_source_destroy(s_hotkey_source);
        g_source_unref(s_hotkey_source);
        s_hotkey_source = NULL;
    }

    if (s_cinnamon_mouse_settings) {
        g_object_unref(s_cinnamon_mouse_settings);
        s_cinnamon_mouse_settings = NULL;
    }

    XCloseDisplay(s_hotkey_dpy);
    s_hotkey_dpy = NULL;
    s_root = None;
    xutil_log("HOTKEY", "Hotkey system cleaned up.");
}

void hotkey_sync_raw_modifiers(void) {
    if (!s_hotkey_dpy) return;
    char keys[32] = {0};
    XQueryKeymap(s_hotkey_dpy, keys);

    bool ctrl_down = false;
    for (int i = 0; i < s_cached_ctrl_count; i++) {
        if (is_keycode_down(keys, s_cached_ctrl_keycodes[i])) { ctrl_down = true; break; }
    }
    bool shift_down = false;
    for (int i = 0; i < s_cached_shift_count; i++) {
        if (is_keycode_down(keys, s_cached_shift_keycodes[i])) { shift_down = true; break; }
    }
    bool alt_down = false;
    for (int i = 0; i < s_cached_alt_count; i++) {
        if (is_keycode_down(keys, s_cached_alt_keycodes[i])) { alt_down = true; break; }
    }

    s_raw_ctrl_down = ctrl_down;
    s_raw_shift_down = shift_down;
    s_raw_alt_down = alt_down;

    s_x_key_down = false;
    s_g_key_down = false;
    s_a_key_down = false;
    s_s_key_down = false;
    s_f_key_down = false;
    s_d_key_down = false;
    s_p_key_down = false;
    s_m_key_down = false;
    s_w_key_down = false;
}

void hotkey_drain_pending_events(void) {
    if (!s_hotkey_dpy) return;
    XSync(s_hotkey_dpy, False);
    while (XPending(s_hotkey_dpy) > 0) {
        XEvent ev;
        XNextEvent(s_hotkey_dpy, &ev);
    }
    hotkey_sync_raw_modifiers();
}
