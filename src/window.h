#ifndef WINDOW_H
#define WINDOW_H

#include <gtk/gtk.h>
#include <X11/Xlib.h>
#include <stdbool.h>

#define OVERLAY_WIDTH            360
#define OVERLAY_HEIGHT           640
#define OVERLAY_MIN_WIDTH        260
#define OVERLAY_MIN_HEIGHT       180
#define OVERLAY_COMPACT_HEIGHT   280
#define OVERLAY_EXPANDED_HEIGHT  640
#define OVERLAY_MARGIN           12
#define PHONE_CORNER_RADIUS      24.0

typedef enum {
    CLIPBOARD_ACTION_SELECT_ALL,
    CLIPBOARD_ACTION_COPY,
    CLIPBOARD_ACTION_PASTE,
    CLIPBOARD_ACTION_CUT
} ClipboardAction;

gboolean    window_init(gboolean start_visible);
void        window_cleanup(void);

void        overlay_show(void);
void        overlay_hide(void);
void        overlay_toggle(void);
void        overlay_reposition(void);
Window      overlay_get_xid(void);
gboolean    overlay_is_visible(void);

void        overlay_get_geometry(int *x, int *y, int *w, int *h);
void        overlay_queue_draw(void);
void        overlay_set_ghost(gboolean ghost);
void        overlay_set_active_focus(gboolean active);
void        overlay_apply_config(void);
void        overlay_apply_theme(void);
void        overlay_set_capture_hidden(gboolean hidden);   /* capture guard: unmap/map without changing visibility intent */
void        overlay_dock_corner(const char *corner);
void        overlay_set_opacity(double opacity);
double      overlay_get_opacity(void);
void        overlay_set_size(int width, int height);
void        overlay_set_compact_mode(gboolean compact);
void        overlay_toggle_ghost_input(void);
void        overlay_set_fullscreen(void);
void        overlay_set_default_size(void);

#include "ghost.h"

void        overlay_switch_app(const char *app_name);
const char *overlay_get_current_app(void);
void        overlay_go_home(void);
void        overlay_back(void);
void        overlay_reload(void);
void        overlay_universal_reset(void);
void        overlay_snap(void);
void        overlay_capture_begin(void);
void        overlay_capture_end(void);
void        overlay_set_ghost_button_state(GhostButtonState state);
void        overlay_ghost_progress(size_t typed, size_t total);
void        overlay_show_toast(const char *msg);

GtkWidget  *overlay_get_window_widget(void);
GtkWidget  *overlay_get_entry_widget(void);
GtkWidget  *overlay_get_test_entry(void);
GtkWidget  *overlay_get_url_entry(void);

const char *overlay_get_test_entry_text(void);
const char *overlay_get_entry_text(void);
void        overlay_clear_entry_text(void);
void        overlay_set_entry_text(const char *text);

void        overlay_execute_clipboard_action(ClipboardAction action);
void        overlay_show_popup_modal(GtkWidget *popup_view);
void        overlay_close_popup_modal(void);
gboolean    overlay_has_popup_modal(void);
GtkWidget  *overlay_get_popup_child_view(void);

void        overlay_update_status_ui(void);

#endif /* WINDOW_H */
