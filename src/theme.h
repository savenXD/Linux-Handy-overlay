#ifndef THEME_H
#define THEME_H

#include <glib.h>
#include <stdbool.h>

/* Appearance modes (persisted as [Appearance] mode in config.ini) */
typedef enum {
    THEME_DARK = 0,
    THEME_LIGHT = 1,
    THEME_CAMO_BLACK = 2,   /* transparent, black text (default) */
    THEME_CAMO_WHITE = 3    /* transparent, white text */
} ThemeMode;

void        theme_init(int mode_from_config);
ThemeMode   theme_get_mode(void);
bool        theme_is_camo(void);
bool        theme_is_light_text_on_dark(void);  /* true when foreground is white-ish */
const char *theme_mode_label(void);             /* "Dark", "Light", "Camo" */

/* Mode changes: persist to config, restyle overlay + web views, and toast. */
void        theme_set_mode(ThemeMode mode);
void        theme_cycle(void);          /* UI button: Dark -> Light -> Camo -> Dark */
void        theme_toggle_camo(void);    /* Alt+C */
void        theme_toggle_text(void);    /* Alt+T */
void        theme_reload_from_config(void);  /* after config reload / SIGHUP */

/* Helpers used by window.c / browser.c */
char       *theme_build_css(const char *base_dark_css);   /* caller g_free()s */
char       *theme_recolor_svg(const char *svg);           /* caller g_free()s */
const char *theme_fg_hex(void);                           /* "#000000" / "#ffffff" etc. */

#endif /* THEME_H */
