#ifndef CONFIG_H
#define CONFIG_H

#include <glib.h>
#include <stdbool.h>
#include "input.h"

typedef struct {
    int         width;
    int         height;
    int         margin;
    char        corner[32];      /* "bottom-left", "bottom-right", "top-left", "top-right" */
    char        monitor[32];     /* "primary" or "0", "1", ... */
    double      opacity;         /* 0.1 to 1.0 */
    bool        start_visible;
    KeyMode     default_key_mode;

    char        home_url[512];
    char        search_url_template[512];
    char        user_agent[512];
    char        chatgpt_allowlist[512];

    char        toggle_key;      /* default 'x' */
    char        mode_key;        /* default 'm' */
    char        ghost_key;       /* default 'g' */
    char        snap_key;        /* default 's' */

    bool        shield_clipboard_keys; /* default true */

    char        snap_scope[32];  /* "monitor" (default) or "all" */
    int         snap_delay_ms;   /* default 80 (min 60) */
    bool        snap_save;       /* default true */
    int         snap_keep_count; /* default 20 */

    char        ghost_writer_key; /* default 'w' */
    int         ghost_delay_ms;   /* default 0 */
    char        ghost_newline[32];/* "space" (default), "return", "skip" */
    char        ghost_tab[32];    /* "space" (default), "tab", "skip" */
    int         ghost_max_chars;  /* default 100000 (0 = unlimited) */

    /* [Appearance] mode: 0 Dark, 1 Light, 2 Camouflage black text, 3 Camouflage white text; -1 (unset) = camouflage with text colour following the system theme */
    int         appearance_mode;

    /* [Privacy] hide_from_capture: hide the overlay while another program captures the screen (default true) */
    bool        hide_from_capture;
} AppConfig;

bool              config_init(void);
void              config_cleanup(void);
const AppConfig  *config_get(void);
bool              config_reload(void);
const char       *config_get_path(void);
/* Persist a single key (keeps comments), then update the in-memory config. */
bool              config_save_value(const char *group, const char *key, const char *value);

#endif /* CONFIG_H */
