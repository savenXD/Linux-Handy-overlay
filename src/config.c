#include "config.h"
#include "xutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>

static AppConfig s_config;
static char      s_config_path[1024] = {0};

static void set_defaults(AppConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->width = 360;
    cfg->height = 640;
    cfg->margin = 12;
    g_strlcpy(cfg->corner, "bottom-left", sizeof(cfg->corner));
    g_strlcpy(cfg->monitor, "primary", sizeof(cfg->monitor));
    cfg->opacity = 1.0;
    cfg->start_visible = false;
    cfg->default_key_mode = KEY_MODE_MIRROR;

    g_strlcpy(cfg->home_url, "https://html5test.com", sizeof(cfg->home_url));
    g_strlcpy(cfg->search_url_template, "https://duckduckgo.com/?q=%s", sizeof(cfg->search_url_template));
    g_strlcpy(cfg->user_agent,
              "Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Mobile Safari/537.36",
              sizeof(cfg->user_agent));
    g_strlcpy(cfg->chatgpt_allowlist,
              "chatgpt.com,openai.com,oaistatic.com,oaiusercontent.com,auth0.com,google.com,google.co.in,gstatic.com,googleusercontent.com,googleapis.com,recaptcha.net,cloudflare.com,challenges.cloudflare.com,microsoft.com,microsoftonline.com,live.com,msftauth.net,apple.com",
              sizeof(cfg->chatgpt_allowlist));

    cfg->toggle_key = 'x';
    cfg->mode_key = 'm';
    cfg->ghost_key = 'g';
    cfg->snap_key = 'p';
    cfg->shield_clipboard_keys = true;

    g_strlcpy(cfg->snap_scope, "monitor", sizeof(cfg->snap_scope));
    cfg->snap_delay_ms = 80;
    cfg->snap_save = true;
    cfg->snap_keep_count = 20;

    cfg->ghost_writer_key = 'w';
    cfg->ghost_delay_ms = 0;
    g_strlcpy(cfg->ghost_newline, "space", sizeof(cfg->ghost_newline));
    g_strlcpy(cfg->ghost_tab, "space", sizeof(cfg->ghost_tab));
    cfg->ghost_max_chars = 100000;

    cfg->hide_from_capture = true;
    cfg->appearance_mode = -1;   /* auto: camouflage, text colour follows the system theme */
}

static void create_default_config_file(const char *path) {
    const char *default_ini =
        "# Cronq Configuration\n"
        "\n"
        "[General]\n"
        "width = 360\n"
        "height = 640\n"
        "margin = 12\n"
        "# Options: bottom-left, bottom-right, top-left, top-right\n"
        "corner = bottom-left\n"
        "# Options: primary or monitor index (0, 1, ...)\n"
        "monitor = primary\n"
        "opacity = 1.0\n"
        "start_visible = false\n"
        "# Options: mirror, exclusive\n"
        "default_key_mode = mirror\n"
        "\n"
        "[Browser]\n"
        "home_url = https://html5test.com\n"
        "search_url_template = https://duckduckgo.com/?q=%s\n"
        "user_agent = Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Mobile Safari/537.36\n"
        "\n"
        "[Hotkeys]\n"
        "# Shortcuts combine Alt + <key> (Alt+X=Toggle, Alt+G=Ghost, Alt+A/S=Opacity, Alt+F=Fullscreen, Alt+D=DefaultSize, Alt+P=Snap)\n"
        "toggle_key = x\n"
        "mode_key = m\n"
        "ghost_key = g\n"
        "snap_key = p\n"
        "ghost_writer_key = w\n"
        "\n"
        "[Snap]\n"
        "# Options: monitor (primary), all (all screens)\n"
        "snap_scope = monitor\n"
        "snap_delay_ms = 80\n"
        "snap_save = true\n"
        "snap_keep_count = 20\n"
        "\n"
        "[GhostWriter]\n"
        "ghost_delay_ms = 0\n"
        "# Options: space, return, skip\n"
        "ghost_newline = space\n"
        "# Options: space, tab, skip\n"
        "ghost_tab = space\n"
        "ghost_max_chars = 100000\n";

    char *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);

    GError *error = NULL;
    if (!g_file_set_contents(path, default_ini, -1, &error)) {
        xutil_log_err("CONFIG", "Failed to create default config at '%s': %s", path, error->message);
        g_error_free(error);
    } else {
        xutil_log("CONFIG", "Generated default configuration at '%s'.", path);
    }
}

static void load_from_key_file(GKeyFile *kf, AppConfig *cfg) {
    GError *err = NULL;

    /* [General] */
    int w = g_key_file_get_integer(kf, "General", "width", &err);
    if (!err && w >= 200 && w <= 3840) cfg->width = w;
    g_clear_error(&err);

    int h = g_key_file_get_integer(kf, "General", "height", &err);
    if (!err && h >= 300 && h <= 2160) cfg->height = h;
    g_clear_error(&err);

    int m = g_key_file_get_integer(kf, "General", "margin", &err);
    if (!err && m >= 0 && m <= 500) cfg->margin = m;
    g_clear_error(&err);

    char *corner = g_key_file_get_string(kf, "General", "corner", &err);
    if (!err && corner && strlen(corner) > 0) {
        g_strlcpy(cfg->corner, corner, sizeof(cfg->corner));
        g_free(corner);
    }
    g_clear_error(&err);

    char *mon = g_key_file_get_string(kf, "General", "monitor", &err);
    if (!err && mon && strlen(mon) > 0) {
        g_strlcpy(cfg->monitor, mon, sizeof(cfg->monitor));
        g_free(mon);
    }
    g_clear_error(&err);

    double op = g_key_file_get_double(kf, "General", "opacity", &err);
    if (!err && op >= 0.1 && op <= 1.0) cfg->opacity = op;
    g_clear_error(&err);

    gboolean sv = g_key_file_get_boolean(kf, "General", "start_visible", &err);
    if (!err) cfg->start_visible = sv ? true : false;
    g_clear_error(&err);

    char *km = g_key_file_get_string(kf, "General", "default_key_mode", &err);
    if (!err && km) {
        if (g_ascii_strcasecmp(km, "exclusive") == 0) {
            cfg->default_key_mode = KEY_MODE_EXCLUSIVE;
        } else {
            cfg->default_key_mode = KEY_MODE_MIRROR;
        }
        g_free(km);
    }
    g_clear_error(&err);

    /* [Browser] */
    char *home = g_key_file_get_string(kf, "Browser", "home_url", &err);
    if (!err && home && strlen(home) > 0) {
        g_strlcpy(cfg->home_url, home, sizeof(cfg->home_url));
        g_free(home);
    }
    g_clear_error(&err);

    char *search = g_key_file_get_string(kf, "Browser", "search_url_template", &err);
    if (!err && search && strlen(search) > 0) {
        g_strlcpy(cfg->search_url_template, search, sizeof(cfg->search_url_template));
        g_free(search);
    }
    g_clear_error(&err);

    char *ua = g_key_file_get_string(kf, "Browser", "user_agent", &err);
    if (!err && ua && strlen(ua) > 0) {
        g_strlcpy(cfg->user_agent, ua, sizeof(cfg->user_agent));
        g_free(ua);
    }
    g_clear_error(&err);

    char *al = g_key_file_get_string(kf, "Browser", "chatgpt_allowlist", &err);
    if (!err && al && strlen(al) > 0) {
        g_strlcpy(cfg->chatgpt_allowlist, al, sizeof(cfg->chatgpt_allowlist));
        g_free(al);
    }
    g_clear_error(&err);

    /* [Hotkeys] */
    char *tk = g_key_file_get_string(kf, "Hotkeys", "toggle_key", &err);
    if (!err && tk && strlen(tk) > 0) {
        cfg->toggle_key = tk[0];
        g_free(tk);
    }
    g_clear_error(&err);

    char *mk = g_key_file_get_string(kf, "Hotkeys", "mode_key", &err);
    if (!err && mk && strlen(mk) > 0) {
        cfg->mode_key = mk[0];
        g_free(mk);
    }
    g_clear_error(&err);

    char *gk = g_key_file_get_string(kf, "Hotkeys", "ghost_key", &err);
    if (!err && gk && strlen(gk) > 0) {
        cfg->ghost_key = gk[0];
        g_free(gk);
    }
    g_clear_error(&err);

    gboolean sc = g_key_file_get_boolean(kf, "Hotkeys", "shield_clipboard_keys", &err);
    if (!err) {
        cfg->shield_clipboard_keys = sc ? true : false;
    }
    g_clear_error(&err);

    char *sk = g_key_file_get_string(kf, "Hotkeys", "snap_key", &err);
    if (err) {
        g_clear_error(&err);
        sk = g_key_file_get_string(kf, "Snap", "snap_key", &err);
    }
    if (!err && sk && strlen(sk) > 0) {
        cfg->snap_key = sk[0];
        g_free(sk);
    }
    g_clear_error(&err);

    /* [Snap] */
    char *scope = g_key_file_get_string(kf, "Snap", "snap_scope", &err);
    if (!err && scope && strlen(scope) > 0) {
        g_strlcpy(cfg->snap_scope, scope, sizeof(cfg->snap_scope));
        g_free(scope);
    }
    g_clear_error(&err);

    int delay = g_key_file_get_integer(kf, "Snap", "snap_delay_ms", &err);
    if (!err && delay >= 60 && delay <= 5000) {
        cfg->snap_delay_ms = delay;
    }
    g_clear_error(&err);

    gboolean ssave = g_key_file_get_boolean(kf, "Snap", "snap_save", &err);
    if (!err) {
        cfg->snap_save = ssave ? true : false;
    }
    g_clear_error(&err);

    int keep = g_key_file_get_integer(kf, "Snap", "snap_keep_count", &err);
    if (!err && keep >= 1 && keep <= 500) {
        cfg->snap_keep_count = keep;
    }
    g_clear_error(&err);

    /* [GhostWriter] */
    char *gwk = g_key_file_get_string(kf, "Hotkeys", "ghost_writer_key", &err);
    if (err) {
        g_clear_error(&err);
        gwk = g_key_file_get_string(kf, "GhostWriter", "ghost_writer_key", &err);
    }
    if (!err && gwk && strlen(gwk) > 0) {
        cfg->ghost_writer_key = gwk[0];
        g_free(gwk);
    }
    g_clear_error(&err);

    int gdelay = g_key_file_get_integer(kf, "GhostWriter", "ghost_delay_ms", &err);
    if (!err && gdelay >= 0 && gdelay <= 5000) {
        cfg->ghost_delay_ms = gdelay;
    }
    g_clear_error(&err);

    char *gnewline = g_key_file_get_string(kf, "GhostWriter", "ghost_newline", &err);
    if (!err && gnewline && strlen(gnewline) > 0) {
        g_strlcpy(cfg->ghost_newline, gnewline, sizeof(cfg->ghost_newline));
        g_free(gnewline);
    }
    g_clear_error(&err);

    char *gtab = g_key_file_get_string(kf, "GhostWriter", "ghost_tab", &err);
    if (!err && gtab && strlen(gtab) > 0) {
        g_strlcpy(cfg->ghost_tab, gtab, sizeof(cfg->ghost_tab));
        g_free(gtab);
    }
    g_clear_error(&err);

    int amode = g_key_file_get_integer(kf, "Appearance", "mode", &err);
    if (!err && amode >= 0 && amode <= 3) cfg->appearance_mode = amode;
    g_clear_error(&err);

    gboolean hfc = g_key_file_get_boolean(kf, "Privacy", "hide_from_capture", &err);
    if (!err) cfg->hide_from_capture = hfc;
    g_clear_error(&err);

    int gmax = g_key_file_get_integer(kf, "GhostWriter", "ghost_max_chars", &err);
    if (!err && gmax >= 0 && gmax <= 5000000) {
        cfg->ghost_max_chars = gmax;
    }
    g_clear_error(&err);
}

bool config_init(void) {
    set_defaults(&s_config);

    const char *config_dir = g_get_user_config_dir();
    snprintf(s_config_path, sizeof(s_config_path), "%s/cronq/config.ini", config_dir);

    if (!g_file_test(s_config_path, G_FILE_TEST_EXISTS)) {
        create_default_config_file(s_config_path);
    }

    GKeyFile *kf = g_key_file_new();
    GError *err = NULL;
    if (g_key_file_load_from_file(kf, s_config_path, G_KEY_FILE_NONE, &err)) {
        load_from_key_file(kf, &s_config);
        xutil_log("CONFIG", "Loaded configuration from '%s' (%dx%d, corner=%s, monitor=%s, home='%s').",
                  s_config_path, s_config.width, s_config.height, s_config.corner, s_config.monitor, s_config.home_url);
    } else {
        xutil_log_err("CONFIG", "Failed to load config '%s': %s. Using defaults.", s_config_path, err->message);
        g_error_free(err);
    }
    g_key_file_free(kf);
    return true;
}

bool config_reload(void) {
    if (s_config_path[0] == '\0') return false;

    GKeyFile *kf = g_key_file_new();
    GError *err = NULL;
    if (g_key_file_load_from_file(kf, s_config_path, G_KEY_FILE_NONE, &err)) {
        load_from_key_file(kf, &s_config);
        xutil_log("CONFIG", "Reloaded configuration from '%s' (SIGHUP).", s_config_path);
        g_key_file_free(kf);
        return true;
    } else {
        xutil_log_err("CONFIG", "Failed to reload config '%s': %s", s_config_path, err->message);
        g_error_free(err);
        g_key_file_free(kf);
        return false;
    }
}

bool config_save_value(const char *group, const char *key, const char *value) {
    if (s_config_path[0] == '\0') return false;
    GKeyFile *kf = g_key_file_new();
    GError *err = NULL;
    if (!g_key_file_load_from_file(kf, s_config_path,
                                   G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, &err)) {
        g_clear_error(&err);
    }
    g_key_file_set_string(kf, group, key, value);
    bool ok = g_key_file_save_to_file(kf, s_config_path, &err);
    if (!ok) {
        xutil_log_err("CONFIG", "Failed to save %s.%s: %s", group, key, err->message);
        g_error_free(err);
    }
    g_key_file_free(kf);
    if (ok) config_reload();
    return ok;
}

const AppConfig *config_get(void) {
    return &s_config;
}

const char *config_get_path(void) {
    return s_config_path;
}

void config_cleanup(void) {
    /* No dynamically allocated structures inside s_config */
}
