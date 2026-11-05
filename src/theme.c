#include "theme.h"
#include "config.h"
#include "window.h"
#include "browser.h"
#include "xutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>

static ThemeMode s_mode = THEME_CAMO_BLACK;
static bool      s_auto = true;          /* no saved mode: camo text colour follows the system */
static bool      s_system_dark = false;
static ThemeMode s_prev_solid = THEME_LIGHT;   /* restored when Alt+C leaves camouflage */

/* Is the desktop's GTK theme dark? Judged by the theme's own background colour. */
static bool detect_system_dark(void) {
    GtkWidgetPath *path = gtk_widget_path_new();
    gtk_widget_path_append_type(path, GTK_TYPE_WINDOW);
    GtkStyleContext *ctx = gtk_style_context_new();
    gtk_style_context_set_path(ctx, path);
    gtk_widget_path_free(path);
    GdkRGBA c;
    bool dark = false;
    if (gtk_style_context_lookup_color(ctx, "theme_bg_color", &c)) {
        double lum = 0.2126 * c.red + 0.7152 * c.green + 0.0722 * c.blue;
        dark = lum < 0.5;
    }
    g_object_unref(ctx);
    return dark;
}

static void theme_apply_everywhere(bool toast);

static void on_system_theme_changed(GObject *obj, GParamSpec *pspec, gpointer data) {
    (void)obj; (void)pspec; (void)data;
    bool dark = detect_system_dark();
    if (dark == s_system_dark) return;
    s_system_dark = dark;
    xutil_log("THEME", "System theme changed: %s", dark ? "dark" : "light");
    if (s_auto) {
        s_mode = dark ? THEME_CAMO_WHITE : THEME_CAMO_BLACK;
        theme_apply_everywhere(false);
    } else if (theme_is_camo()) {
        browser_apply_theme((int)s_mode);
    }
}

void theme_init(int mode_from_config) {
    static bool watching = false;
    s_system_dark = detect_system_dark();
    if (!watching) {
        GtkSettings *gs = gtk_settings_get_default();
        if (gs) g_signal_connect(gs, "notify::gtk-theme-name", G_CALLBACK(on_system_theme_changed), NULL);
        watching = true;
    }
    if (mode_from_config < 0 || mode_from_config > 3) {
        s_auto = true;
        s_mode = s_system_dark ? THEME_CAMO_WHITE : THEME_CAMO_BLACK;
    } else {
        s_auto = false;
        s_mode = (ThemeMode)mode_from_config;
    }
    if (s_mode == THEME_DARK || s_mode == THEME_LIGHT) s_prev_solid = s_mode;
}

ThemeMode theme_get_mode(void) { return s_mode; }

bool theme_is_camo(void) { return s_mode == THEME_CAMO_BLACK || s_mode == THEME_CAMO_WHITE; }

bool theme_is_light_text_on_dark(void) { return s_mode == THEME_DARK || s_mode == THEME_CAMO_WHITE; }

const char *theme_mode_label(void) {
    switch (s_mode) {
        case THEME_DARK:  return "Dark";
        case THEME_LIGHT: return "Light";
        default:          return "Camo";
    }
}

const char *theme_fg_hex(void) {
    return theme_is_light_text_on_dark() ? "#ffffff" : "#000000";
}

static void theme_apply_everywhere(bool toast) {
    overlay_apply_theme();
    browser_apply_theme((int)s_mode);
    if (toast) {
        static const char *names[] = { "Dark mode", "Light mode", "Camouflage: black text", "Camouflage: white text" };
        overlay_show_toast(names[s_mode]);
    }
}

void theme_set_mode(ThemeMode mode) {
    if (mode < 0 || mode > 3) return;
    s_mode = mode;
    s_auto = false;
    if (mode == THEME_DARK || mode == THEME_LIGHT) s_prev_solid = mode;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", (int)mode);
    config_save_value("Appearance", "mode", buf);
    xutil_log("THEME", "Appearance mode -> %d", (int)mode);
    theme_apply_everywhere(true);
}

void theme_cycle(void) {
    switch (s_mode) {
        case THEME_DARK:  theme_set_mode(THEME_LIGHT); break;
        case THEME_LIGHT: theme_set_mode(THEME_CAMO_BLACK); break;
        default:          theme_set_mode(THEME_DARK); break;
    }
}

void theme_toggle_camo(void) {
    if (theme_is_camo()) theme_set_mode(s_prev_solid);
    else theme_set_mode(THEME_CAMO_BLACK);
}

void theme_toggle_text(void) {
    switch (s_mode) {
        case THEME_DARK:        theme_set_mode(THEME_LIGHT); break;
        case THEME_LIGHT:       theme_set_mode(THEME_DARK); break;
        case THEME_CAMO_BLACK:  theme_set_mode(THEME_CAMO_WHITE); break;
        case THEME_CAMO_WHITE:  theme_set_mode(THEME_CAMO_BLACK); break;
    }
}

void theme_reload_from_config(void) {
    int m = config_get()->appearance_mode;
    if (m < 0 || m > 3 || (ThemeMode)m == s_mode) return;
    s_mode = (ThemeMode)m;
    if (s_mode == THEME_DARK || s_mode == THEME_LIGHT) s_prev_solid = s_mode;
    theme_apply_everywhere(false);
}

/* ---------- CSS / SVG recolouring ---------- */

static bool parse_gray_hex(const char *p, int *out) {
    /* p points at '#'; accepts #rrggbb where r==g==b */
    if (p[0] != '#') return false;
    for (int i = 1; i <= 6; i++) if (!g_ascii_isxdigit(p[i])) return false;
    if (g_ascii_isxdigit(p[7])) return false;
    int r = (int)strtol((char[]){p[1], p[2], 0}, NULL, 16);
    int g = (int)strtol((char[]){p[3], p[4], 0}, NULL, 16);
    int b = (int)strtol((char[]){p[5], p[6], 0}, NULL, 16);
    if (r != g || g != b) return false;
    *out = r;
    return true;
}

typedef enum { PROP_BG, PROP_BORDER, PROP_TEXT } PropKind;

static PropKind classify_prop(const char *name, size_t len) {
    #define IS(s) (len == strlen(s) && strncmp(name, s, len) == 0)
    if (IS("background-color") || IS("background")) return PROP_BG;
    if (IS("color") || IS("caret-color")) return PROP_TEXT;
    return PROP_BORDER;   /* border, border-color, border-top, border-bottom */
    #undef IS
}

static void append_mapped_value(GString *out, const char *val, PropKind kind) {
    bool camo = theme_is_camo();
    bool light = (s_mode == THEME_LIGHT);
    const char *fg = theme_fg_hex();
    int fgv = theme_is_light_text_on_dark() ? 255 : 0;
    for (const char *p = val; *p; ) {
        int v;
        if (*p == '#' && parse_gray_hex(p, &v)) {
            if (light) {
                g_string_append_printf(out, "#%02x%02x%02x", 255 - v, 255 - v, 255 - v);
            } else if (camo) {
                if (kind == PROP_BG)          g_string_append(out, "transparent");
                else if (kind == PROP_BORDER) g_string_append_printf(out, "rgba(%d,%d,%d,0.45)", fgv, fgv, fgv);
                else                          g_string_append(out, fg);
            } else {
                g_string_append_len(out, p, 7);
            }
            p += 7;
        } else {
            g_string_append_c(out, *p++);
        }
    }
}

char *theme_build_css(const char *base_dark_css) {
    GString *out = g_string_new(NULL);
    const char *p = base_dark_css;
    static const char *props[] = { "background-color", "background", "caret-color", "border-bottom", "border-color",
                                   "border-top", "border", "color", NULL };
    while (*p) {
        /* Match a declaration property at a token boundary */
        bool matched = false;
        if (p == base_dark_css || p[-1] == ' ' || p[-1] == '{' || p[-1] == ';') {
            for (int i = 0; props[i]; i++) {
                size_t n = strlen(props[i]);
                if (strncmp(p, props[i], n) == 0 && p[n] == ':') {
                    PropKind kind = classify_prop(p, n);
                    const char *vstart = p + n + 1;
                    const char *vend = vstart;
                    while (*vend && *vend != ';' && *vend != '}') vend++;
                    char *val = g_strndup(vstart, (gsize)(vend - vstart));
                    g_string_append_len(out, p, (gssize)(n + 1));
                    append_mapped_value(out, val, kind);
                    g_free(val);
                    p = vend;
                    matched = true;
                    break;
                }
            }
        }
        if (!matched) g_string_append_c(out, *p++);
    }

    if (theme_is_camo()) {
        const char *fg = theme_fg_hex();
        g_string_append_printf(out,
            ".phone-root { border-color: transparent; background-color: transparent; }"
            "scrolledwindow, viewport, stack, overlay, .home-page, .ghost-page { background-color: transparent; background-image: none; }"
            ".toast-box { background-color: transparent; border: 1px solid %s; }"
            ".toast-label { color: %s; }"
            "entry, textview, textview text { background-color: transparent; color: %s; caret-color: %s; }"
            "scrollbar, scrollbar slider, scrollbar trough { background: transparent; }"
            ".ghost-page button, .ghost-page label, .home-page label { color: %s; }", fg, fg, fg, fg, fg);
    }
    return g_string_free(out, FALSE);
}

static void neutral_replacement(const char *hex, char *repl, size_t n) {
    if (theme_is_camo()) {
        g_strlcpy(repl, theme_fg_hex(), n);
        return;
    }
    int r = (int)strtol((char[]){hex[1], hex[2], 0}, NULL, 16);
    int g = (int)strtol((char[]){hex[3], hex[4], 0}, NULL, 16);
    int b = (int)strtol((char[]){hex[5], hex[6], 0}, NULL, 16);
    snprintf(repl, n, "#%02x%02x%02x", 255 - r, 255 - g, 255 - b);
}

char *theme_recolor_svg(const char *svg) {
    static const char *neutrals[] = { "#e4e4e7", "#a1a1aa", "#71717a", "#525252", "#ffffff", NULL };
    char *cur = g_strdup(svg);
    if (s_mode == THEME_DARK) return cur;
    /* Two passes through placeholders so an already-replaced colour is never re-matched. */
    for (int i = 0; neutrals[i]; i++) {
        char ph[16];
        snprintf(ph, sizeof(ph), "@@%d@@", i);
        char **parts = g_strsplit(cur, neutrals[i], -1);
        char *joined = g_strjoinv(ph, parts);
        g_strfreev(parts);
        g_free(cur);
        cur = joined;
    }
    for (int i = 0; neutrals[i]; i++) {
        char ph[16], repl[8];
        snprintf(ph, sizeof(ph), "@@%d@@", i);
        neutral_replacement(neutrals[i], repl, sizeof(repl));
        char **parts = g_strsplit(cur, ph, -1);
        char *joined = g_strjoinv(repl, parts);
        g_strfreev(parts);
        g_free(cur);
        cur = joined;
    }
    return cur;
}
