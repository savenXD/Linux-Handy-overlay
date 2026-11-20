#include "window.h"
#include "hotkey.h"
#include "chat_view.h"
#include "config.h"
#include "input.h"
#include "raise.h"
#include "theme.h"
#include "xutil.h"

#include <gdk/gdkx.h>
#include <gio/gio.h>
#include <cairo.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <math.h>
#include <X11/extensions/shape.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif





static const char *s_svg_check_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#22c55e\" stroke-width=\"2.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<polyline points=\"20 6 9 17 4 12\"/>"
    "</svg>";

static const char *s_svg_failed_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#ef4444\" stroke-width=\"2.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<line x1=\"18\" y1=\"6\" x2=\"6\" y2=\"18\"/>"
    "<line x1=\"6\" y1=\"6\" x2=\"18\" y2=\"18\"/>"
    "</svg>";

static const char *s_svg_report_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"12\" height=\"12\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#e4e4e7\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<path d=\"M14 3H6v18h12V7l-4-4Z\"/><path d=\"M14 3v5h4M9 12h6M9 16h4\"/></svg>";

static const char *s_svg_snap_running =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#38bdf8\" stroke-width=\"2.5\" stroke-linecap=\"round\">"
    "<circle cx=\"12\" cy=\"12\" r=\"9\" stroke-dasharray=\"28\" stroke-dashoffset=\"10\"/>"
    "</svg>";

static const char *s_svg_close_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#ef4444\" stroke-width=\"2.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<line x1=\"18\" y1=\"6\" x2=\"6\" y2=\"18\"/>"
    "<line x1=\"6\" y1=\"6\" x2=\"18\" y2=\"18\"/>"
    "</svg>";



static const char *s_svg_error_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"32\" height=\"36\" viewBox=\"0 0 32 36\" fill=\"none\" stroke=\"#a1a1aa\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<path d=\"M19 2H6a2 2 0 0 0-2 2v28a2 2 0 0 0 2 2h20a2 2 0 0 0 2-2V11L19 2Z\"/>"
    "<path d=\"M19 2v9h9M11 19h.01M21 19h.01M11 27c3-3 7-3 10 0\"/></svg>";

typedef struct {
    GtkWidget  *img;
    const char *svg;
    int         w, h;
} SvgEntry;

static GPtrArray *s_svg_entries = NULL;

static GdkPixbuf *render_svg_pixbuf(const char *svg_str, int width, int height, gboolean themed) {
    char *tmp = themed ? theme_recolor_svg(svg_str) : NULL;
    const char *src = tmp ? tmp : svg_str;
    GInputStream *stream = g_memory_input_stream_new_from_data(src, -1, NULL);
    GdkPixbuf *pb = NULL;
    if (stream) {
        pb = gdk_pixbuf_new_from_stream_at_scale(stream, width, height, TRUE, NULL, NULL);
        g_object_unref(stream);
    }
    g_free(tmp);
    return pb;
}

static void on_svg_image_finalized(gpointer data, GObject *where_the_object_was) {
    SvgEntry *e = data;
    if (s_svg_entries) g_ptr_array_remove(s_svg_entries, e);
    (void)where_the_object_was;
    g_free(e);
}

/* App-tile icons keep their own colours in every theme; UI-chrome icons follow the theme. */
static GtkWidget *create_svg_image(const char *svg_str, int width, int height) {
    if (!svg_str) return gtk_label_new("");
    gboolean themed = TRUE;
    GdkPixbuf *pb = render_svg_pixbuf(svg_str, width, height, themed);
    if (!pb) return gtk_label_new("");
    GtkWidget *img = gtk_image_new_from_pixbuf(pb);
    g_object_unref(pb);
    if (themed) {
        if (!s_svg_entries) s_svg_entries = g_ptr_array_new();
        SvgEntry *e = g_new0(SvgEntry, 1);
        e->img = img; e->svg = svg_str; e->w = width; e->h = height;
        g_ptr_array_add(s_svg_entries, e);
        g_object_weak_ref(G_OBJECT(img), on_svg_image_finalized, e);
    }
    return img;
}

static void refresh_themed_svgs(void) {
    if (!s_svg_entries) return;
    for (guint i = 0; i < s_svg_entries->len; i++) {
        SvgEntry *e = g_ptr_array_index(s_svg_entries, i);
        GdkPixbuf *pb = render_svg_pixbuf(e->svg, e->w, e->h, TRUE);
        if (pb) {
            gtk_image_set_from_pixbuf(GTK_IMAGE(e->img), pb);
            g_object_unref(pb);
        }
    }
}

static GtkCssProvider *s_css_provider = NULL;
static GtkWidget *s_error_icon = NULL;
static GtkWidget *s_error_code = NULL;
static GtkWidget *s_status_bar = NULL;
static GtkWidget *s_status_pills = NULL;
static GtkWidget *s_bottom_nav = NULL;

static gboolean s_capture_hidden = FALSE;
static gboolean s_is_snapping = FALSE;
static guint    s_snap_revert_timer = 0;
static gboolean s_was_visible_during_snap = FALSE;

static GtkWidget *s_window = NULL;
static GtkWidget *s_root_box = NULL;
static GtkWidget *s_overlay = NULL;
static GtkWidget *s_stack = NULL;

static GtkWidget *s_mode_pill_box = NULL;
static GtkWidget *s_mode_dot = NULL;
static GtkWidget *s_mode_pill = NULL;

static GtkWidget *s_ghost_pill_box = NULL;
static GtkWidget *s_ghost_dot = NULL;
static GtkWidget *s_ghost_pill_label = NULL;

static GtkWidget *s_grab_pill = NULL;
static GtkWidget *s_grab_box = NULL;
static GtkWidget *s_grab_pill_label = NULL;

static GtkWidget *s_move_handle_eb = NULL;

static GtkWidget *s_btn_back = NULL;
static GtkWidget *s_btn_home = NULL;
static GtkWidget *s_btn_universal_refresh = NULL;
static GtkWidget *s_btn_snap = NULL;
static GtkWidget *s_snap_stack = NULL;


static GtkWidget *s_popup_container = NULL;
static GtkWidget *s_popup_child_view = NULL;

static Window     s_xid = None;
static gboolean   s_visible = FALSE;

static int        s_pos_x = 0;
static int        s_pos_y = 0;
static int        s_win_width = OVERLAY_WIDTH;
static int        s_win_height = OVERLAY_COMPACT_HEIGHT;
static double     s_current_opacity = 1.0;
static GtkWidget *s_opacity_label = NULL;
static char       s_current_app[32] = "home";

typedef enum {
    RESIZE_NONE = 0,
    RESIZE_TOP = 1 << 0,
    RESIZE_BOTTOM = 1 << 1,
    RESIZE_LEFT = 1 << 2,
    RESIZE_RIGHT = 1 << 3,
    MOVE_DRAG = 1 << 4
} DragOperation;

static DragOperation s_active_drag = RESIZE_NONE;
static int s_drag_start_root_x = 0;
static int s_drag_start_root_y = 0;
static int s_drag_start_win_x = 0;
static int s_drag_start_win_y = 0;
static int s_drag_start_win_w = 0;
static int s_drag_start_win_h = 0;
static int s_prev_saved_w = OVERLAY_WIDTH;
static int s_prev_saved_h = OVERLAY_HEIGHT;
static int s_prev_saved_x = 0;
static int s_prev_saved_y = 0;
static gboolean s_is_maximized = FALSE;
static gboolean s_user_moved = FALSE;

static const char *s_phone_css =
    "* {"
    "  -GtkScrollbar-has-backward-stepper: 0;"
    "  -GtkScrollbar-has-forward-stepper: 0;"
    "  font-family: \"Segoe UI\", Roboto, \"Noto Sans\", sans-serif;"
    "}"
    /* ---- shell ---- */
    ".phone-root { background-color: #000000; border: 1px solid #2a2a2a; border-radius: 22px; padding: 0; }"
    ".status-bar { padding: 7px 12px; background-color: #000000; border-bottom: 1px solid #1a1a1a; border-radius: 21px 21px 0 0; }"
    /* ---- status balls: colour only, meaning is in the tooltip ---- */
    ".nav-pill { background: transparent; border: none; border-radius: 999px; padding: 6px 5px; }"
    ".nav-pill-dot { min-width: 9px; min-height: 9px; border-radius: 999px; background-color: #737373; }"
    ".nav-pill.pill-normal .nav-pill-dot, .nav-pill.pill-solid .nav-pill-dot { background-color: #737373; }"
    ".nav-pill.pill-overlay .nav-pill-dot { background-color: #22c55e; box-shadow: 0 0 5px #22c55e, 0 0 12px rgba(34,197,94,0.65); }"
    ".nav-pill.pill-ghost .nav-pill-dot { background-color: #a855f7; }"
    ".nav-pill.pill-grab-alert .nav-pill-dot { background-color: #ef4444; }"
    /* ---- window controls ---- */
    ".move-capsule { padding: 4px 8px; border-radius: 6px; background: transparent; }"
    ".move-capsule:hover { background-color: #161616; }"
    ".resize-grip { opacity: 0.4; padding: 2px; }"
    ".resize-grip:hover { opacity: 1.0; }"
    /* ---- retained shell ---- */
    /* ---- home ---- */
    ".home-page { background-color: #000000; }"
    /* ---- bottom bar ---- */
    ".bottom-nav { background-color: #000000; padding: 6px 12px 8px; border-top: 1px solid #1a1a1a; border-radius: 0 0 21px 21px; }"
    ".nav-button { background: transparent; background-image: none; box-shadow: none; border: none; padding: 6px 4px; min-height: 34px; min-width: 0; border-radius: 10px; }"
    ".nav-button label { font-size: 10px; color: #a0a0a0; }"
    ".nav-button:hover { background-color: #161616; }"
    ".nav-button:hover label { color: #d4d4d4; }"
    ".nav-button:active { background-color: #222222; }"
    ".nav-button:disabled { opacity: 0.45; }"
    ".nav-button.nav-selected { background-color: #1c1c1c; }"
    ".nav-button.nav-selected label { color: #ffffff; font-weight: 600; }"
    ".nav-refresh-btn { background: transparent; }"
    /* ---- toast / popup ---- */
    ".popup-modal-layer { background-color: #000000; border-radius: 18px; }"
    ".popup-modal-bar { padding: 7px 12px; background-color: #0a0a0a; border-bottom: 1px solid #1a1a1a; }"
    ".popup-modal-title { color: #ffffff; font-size: 12px; font-weight: 600; }"
    ".popup-close-btn { background: transparent; border: none; padding: 4px 8px; border-radius: 6px; }"
    ".popup-close-btn:hover { background-color: #ef4444; }"
    ".phone-root { border-radius: 10px; border-color: #2a2a2a; }"
    ".status-bar { padding: 2px 12px; border-radius: 9px 9px 0 0; border-color: #1a1a1a; }"
    ".nav-pill { padding: 9px 6px; }"
    ".nav-pill-dot { min-width: 7px; min-height: 7px; }"
    ".move-capsule { padding: 0; } .move-capsule:hover { background: transparent; }"
    ".home-page { padding: 12px; }"
    ".error-symbol { margin-bottom: 12px; opacity: 0.8; }"
    ".error-code { font-size: 9px; letter-spacing: 1.5px; color: #a0a0a0; margin-bottom: 8px; }"
    ".error-title { font-size: 20px; font-weight: 600; letter-spacing: -0.5px; color: #ffffff; margin-bottom: 9px; }"
    ".error-description { font-size: 12px; color: #a0a0a0; }"
    ".bottom-nav { padding: 7px 12px; border-radius: 0 0 9px 9px; border-color: #1a1a1a; }"
    ".nav-button { min-height: 18px; padding: 5px 3px; border-radius: 5px; }"
    ".nav-button label { font-size: 10px; }"
    ".nav-button.nav-selected { background: transparent; }"
    ".nav-button.nav-selected label { font-weight: 400; color: #a0a0a0; }"
    ".nav-button:disabled { opacity: 0.35; }"
    ".nav-refresh-btn { border: 1px solid #333333; } .nav-refresh-btn label { color: #ffffff; }"
    ".nav-refresh-btn { border-color: transparent; }"
    ".bottom-nav { padding: 8px 12px 3px; border-top: none; }"
    ".nav-button { min-height: 26px; padding: 5px 3px 1px; }"
    ".nav-button:hover, .nav-button:active { background: transparent; }"
    ".nav-target-dot { min-width: 2px; min-height: 2px; border-radius: 99px; background-color: #474747; }"
    ".small-error .home-page { padding: 8px 12px; }"
    ".small-error .error-title { font-size: 17px; margin-bottom: 6px; }"
    ".small-error .error-code { font-size: 8px; margin-bottom: 5px; }"
    ".small-error .error-description { font-size: 11px; }";

static void update_x11_state_properties(void) {
    if (s_xid == None) return;
    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    Atom prop_state = XInternAtom(xdpy, "_CRONQ_STATE", False);
    Atom prop_app = XInternAtom(xdpy, "_CRONQ_APP", False);
    Atom utf8_string = XInternAtom(xdpy, "UTF8_STRING", False);

    char state_str[128];
    snprintf(state_str, sizeof(state_str), "MODE=%s TARGET=%s GHOST=%s GRAB=%s APP=%s",
             input_get_typing_target() == TYPING_TARGET_OVERLAY ? "OVERLAY" : "NORMAL",
             input_get_typing_target() == TYPING_TARGET_OVERLAY ? "OVERLAY" : "NONE",
             input_get_ghost_mode() == GHOST_MODE_ON ? "GHOST" : "SOLID",
             input_get_grab_status() == GRAB_STATUS_ACTIVE ? "ACTIVE" :
             (input_get_grab_status() == GRAB_STATUS_FAILED ? "FAILED" : "NONE"),
             s_current_app);

    XChangeProperty(xdpy, s_xid, prop_state, utf8_string, 8, PropModeReplace,
                    (unsigned char *)state_str, strlen(state_str));

    XChangeProperty(xdpy, s_xid, prop_app, utf8_string, 8, PropModeReplace,
                    (unsigned char *)s_current_app, strlen(s_current_app));
}

void overlay_update_status_ui(void) {
    if (!s_window) return;

    /* 1. Mode Pill */
    if (s_mode_pill_box && s_mode_pill) {
        gboolean typing = input_get_typing_target()==TYPING_TARGET_OVERLAY && input_get_grab_status()==GRAB_STATUS_ACTIVE;
        GtkStyleContext *sc = gtk_widget_get_style_context(s_mode_pill_box);
        gtk_style_context_remove_class(sc,"pill-normal");
        gtk_style_context_remove_class(sc,"pill-overlay");
        gtk_style_context_add_class(sc,typing?"pill-overlay":"pill-normal");
        gtk_label_set_text(GTK_LABEL(s_mode_pill),typing?"Overlay typing":"Normal typing");
        atk_object_set_name(gtk_widget_get_accessible(gtk_widget_get_parent(s_mode_pill_box)),
                            typing?"Typing: overlay":"Typing: normal app");
    }

    /* 2. Ghost Mode Pill */
    if (s_ghost_pill_box && s_ghost_pill_label) {
        GhostMode ghost = input_get_ghost_mode();
        GtkStyleContext *sc = gtk_widget_get_style_context(s_ghost_pill_box);
        gtk_style_context_remove_class(sc, "pill-solid");
        gtk_style_context_remove_class(sc, "pill-ghost");

        if (ghost == GHOST_MODE_ON) {
            gtk_label_set_text(GTK_LABEL(s_ghost_pill_label), "Ghost");
            gtk_style_context_add_class(sc, "pill-ghost");
        } else {
            gtk_label_set_text(GTK_LABEL(s_ghost_pill_label), "Solid");
            gtk_style_context_add_class(sc, "pill-solid");
        }
    }

    /* 3. Grab Alert Pill */
    if (s_grab_pill) {
        GrabStatus grab = input_get_grab_status();
        if (grab == GRAB_STATUS_FAILED) {
            gtk_widget_show(s_grab_pill);
        } else {
            gtk_widget_hide(s_grab_pill);
        }
    }

    if (s_btn_home) {
        GtkStyleContext *sc=gtk_widget_get_style_context(s_btn_home);
        if (!strcmp(s_current_app,"home")) gtk_style_context_add_class(sc,"nav-selected");
        else gtk_style_context_remove_class(sc,"nav-selected");
    }
    /* Update Back button sensitivity */
    if (s_btn_back) {
        if (strcmp(s_current_app, "home") == 0) {
            gtk_widget_set_sensitive(s_btn_back, FALSE);
        } else {
            gtk_widget_set_sensitive(s_btn_back, TRUE);
        }
    }

    update_x11_state_properties();
}

static GdkMonitor *current_monitor(void) {
    GdkDisplay *display = gdk_display_get_default();
    if (!display) return NULL;
    GdkMonitor *mon = s_user_moved ? gdk_display_get_monitor_at_point(display,
        s_pos_x + s_win_width/2, s_pos_y + s_win_height/2) : NULL;
    const AppConfig *cfg = config_get();
    if (!mon && g_ascii_strcasecmp(cfg->monitor,"primary")) {
        int n=atoi(cfg->monitor);
        if(n>=0 && n<gdk_display_get_n_monitors(display)) mon=gdk_display_get_monitor(display,n);
    }
    if(!mon) mon=gdk_display_get_primary_monitor(display);
    if(!mon && gdk_display_get_n_monitors(display)) mon=gdk_display_get_monitor(display,0);
    return mon;
}

static void fitted_size(int *width, int *height) {
    const AppConfig *cfg=config_get();
    GdkRectangle work={0,0,1920,1080};
    GdkMonitor *mon=current_monitor();
    if(mon) gdk_monitor_get_workarea(mon,&work);
    int aw=MAX(1,work.width-2*cfg->margin), ah=MAX(1,work.height-2*cfg->margin);
    double scale=MIN(1.0,MIN((double)aw/cfg->width,(double)(ah*3/4)/cfg->height));
    *width=MAX(OVERLAY_MIN_WIDTH,(int)(cfg->width*scale));
    *height=MAX(OVERLAY_MIN_HEIGHT,(int)(cfg->height*scale));
}

static void calculate_monitor_position(int *out_x, int *out_y) {
    const AppConfig *cfg = config_get();
    GdkMonitor *mon = current_monitor();
    GdkRectangle geom = {0, 0, 1920, 1080};
    if (mon) {
        gdk_monitor_get_workarea(mon, &geom);
    }

    int w = (s_win_width > 0) ? s_win_width : cfg->width;
    int h = (s_win_height > 0) ? s_win_height : cfg->height;
    int m = cfg->margin;

    int x = geom.x + m;
    int y = geom.y + geom.height - h - m; /* Default bottom-left */

    if (g_ascii_strcasecmp(cfg->corner, "bottom-right") == 0) {
        x = geom.x + geom.width - w - m;
        y = geom.y + geom.height - h - m;
    } else if (g_ascii_strcasecmp(cfg->corner, "top-left") == 0) {
        x = geom.x + m;
        y = geom.y + m;
    } else if (g_ascii_strcasecmp(cfg->corner, "top-right") == 0) {
        x = geom.x + geom.width - w - m;
        y = geom.y + m;
    }

    if (out_x) *out_x = x;
    if (out_y) *out_y = y;

    xutil_log("WINDOW", "Calculated position: (%d, %d) on monitor geom %dx%d+%d+%d (corner=%s, margin=%d)",
              x, y, geom.width, geom.height, geom.x, geom.y, cfg->corner, m);
}

static gboolean on_window_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    (void)user_data;
    GdkScreen *screen = gtk_widget_get_screen(widget);
    if (gdk_screen_is_composited(screen)) {
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    }
    return FALSE; /* Let child widgets render */
}

static void set_window_cursor(GdkCursorType cursor_type) {
    if (!s_window) return;
    GdkWindow *gdk_win = gtk_widget_get_window(s_window);
    if (!gdk_win) return;
    if (cursor_type == (GdkCursorType)-1) {
        gdk_window_set_cursor(gdk_win, NULL);
    } else {
        GdkDisplay *display = gtk_widget_get_display(s_window);
        GdkCursor *cursor = gdk_cursor_new_for_display(display, cursor_type);
        gdk_window_set_cursor(gdk_win, cursor);
        g_object_unref(cursor);
    }
}

void overlay_set_opacity(double opacity) {
    if (opacity < 0.20) opacity = 0.20;
    if (opacity > 1.00) opacity = 1.00;
    s_current_opacity = opacity;
    if (s_window) {
        gtk_widget_set_opacity(s_window, s_current_opacity);
    }
    if (s_opacity_label) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d%%", (int)(s_current_opacity * 100.0 + 0.5));
        gtk_label_set_text(GTK_LABEL(s_opacity_label), buf);
    }
    char toast[64];
    snprintf(toast, sizeof(toast), "Opacity: %d%%", (int)(s_current_opacity * 100.0 + 0.5));
    overlay_show_toast(toast);
}

static void get_monitor_bounds(int *out_w, int *out_h) {
    GdkMonitor *mon = current_monitor();
    GdkRectangle geom = {0, 0, 1920, 1080};
    if (mon) {
        gdk_monitor_get_geometry(mon, &geom);
    }
    if (out_w) *out_w = geom.width > 0 ? geom.width : 1920;
    if (out_h) *out_h = geom.height > 0 ? geom.height : 1080;
}

double overlay_get_opacity(void) {
    return s_current_opacity;
}

static void update_error_density(int width, int height) {
    if (!s_root_box || !s_error_icon) return;
    GtkStyleContext *style = gtk_widget_get_style_context(s_root_box);
    if (width < 290 || height <= 240) gtk_style_context_add_class(style, "small-error");
    else gtk_style_context_remove_class(style, "small-error");
    gtk_widget_set_no_show_all(s_error_icon, height <= 240);
    if (height <= 240) gtk_widget_hide(s_error_icon);
    else gtk_widget_show(s_error_icon);
}

void overlay_set_size(int width, int height) {
    if (!s_window) return;

    int screen_w = 1920, screen_h = 1080;
    get_monitor_bounds(&screen_w, &screen_h);

    if (width < OVERLAY_MIN_WIDTH) width = OVERLAY_MIN_WIDTH;
    if (width > screen_w) width = screen_w;
    if (height < OVERLAY_MIN_HEIGHT) height = OVERLAY_MIN_HEIGHT;
    if (height > screen_h) height = screen_h;

    update_error_density(width, height);
    if (width == s_win_width && height == s_win_height) {
        if (s_xid != None) {
            Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
            XMoveWindow(xdpy, s_xid, s_pos_x, s_pos_y);
            XFlush(xdpy);
        }
        return;
    }

    s_win_width = width;
    s_win_height = height;

    gtk_window_resize(GTK_WINDOW(s_window), s_win_width, s_win_height);
    if (s_root_box) {
        gtk_widget_set_size_request(s_root_box, s_win_width, s_win_height);
    }

    if (s_xid != None) {
        Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
        XMoveResizeWindow(xdpy, s_xid, s_pos_x, s_pos_y, s_win_width, s_win_height);
        XFlush(xdpy);
    }
    overlay_queue_draw();
}

void overlay_set_fullscreen(void) {
    if (!s_window) return;
    int mon_x = 0, mon_y = 0, mon_w = 1920, mon_h = 1080;
    GdkMonitor *mon = current_monitor();
    GdkRectangle geom = {0, 0, 1920, 1080};
    if (mon) {
        gdk_monitor_get_geometry(mon, &geom);
    }
    mon_x = geom.x;
    mon_y = geom.y;
    mon_w = geom.width > 0 ? geom.width : 1920;
    mon_h = geom.height > 0 ? geom.height : 1080;

    if (s_win_width == mon_w && s_win_height == mon_h) {
        /* Already fullscreen -> toggle back to previous size */
        int rest_w = (s_prev_saved_w > 0) ? s_prev_saved_w : OVERLAY_WIDTH;
        int rest_h = (s_prev_saved_h > 0) ? s_prev_saved_h : OVERLAY_HEIGHT;
        s_pos_x = s_prev_saved_x;
        s_pos_y = s_prev_saved_y;
        gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);
        overlay_set_size(rest_w, rest_h);
        s_is_maximized = FALSE;
        overlay_show_toast("Restored size");
    } else {
        s_prev_saved_w = s_win_width;
        s_prev_saved_h = s_win_height;
        s_prev_saved_x = s_pos_x;
        s_prev_saved_y = s_pos_y;

        s_pos_x = mon_x;
        s_pos_y = mon_y;
        gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);
        overlay_set_size(mon_w, mon_h);
        s_is_maximized = TRUE;
        overlay_show_toast("Fullscreen mode");
    }
}

void overlay_set_default_size(void) {
    if (!s_window) return;
    int def_w,def_h;
    fitted_size(&def_w,&def_h);
    if(!strcmp(s_current_app,"home")) def_h=MIN(OVERLAY_COMPACT_HEIGHT,def_h);
    s_user_moved = FALSE;
    overlay_set_size(def_w, def_h);
    calculate_monitor_position(&s_pos_x, &s_pos_y);
    gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);
    s_is_maximized = FALSE;
    overlay_show_toast("Default size");
}

void overlay_set_compact_mode(gboolean compact) {
    if (!s_window) return;
    if (s_is_maximized) return;

    int fitted_w, fitted_h;
    fitted_size(&fitted_w,&fitted_h);
    int target_h = compact ? MIN(OVERLAY_COMPACT_HEIGHT,fitted_h) : fitted_h;
    if (s_win_height == target_h) return;

    int old_h = s_win_height;
    s_win_height = target_h;

    if (s_user_moved) {
        int diff = target_h - old_h;
        s_pos_y -= diff;
        if (s_pos_y < 0) s_pos_y = 0;
    } else {
        calculate_monitor_position(&s_pos_x, &s_pos_y);
    }

    gtk_window_resize(GTK_WINDOW(s_window), s_win_width, s_win_height);
    if (s_root_box) {
        gtk_widget_set_size_request(s_root_box, s_win_width, s_win_height);
    }
    gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);

    if (s_xid != None) {
        Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
        XMoveResizeWindow(xdpy, s_xid, s_pos_x, s_pos_y, s_win_width, s_win_height);
        XFlush(xdpy);
    }
    overlay_queue_draw();
    xutil_log("WINDOW", "Set compact mode: %s (height=%d, pos_y=%d)",
              compact ? "COMPACT" : "EXPANDED", s_win_height, s_pos_y);
}

static void on_window_size_allocate(GtkWidget *widget, GdkRectangle *allocation, gpointer user_data) {
    (void)user_data;
    (void)widget;
    update_error_density(allocation->width, allocation->height);
    if (allocation->width != s_win_width || allocation->height != s_win_height) {
        int old_h = s_win_height;
        s_win_width = allocation->width;
        s_win_height = allocation->height;

        if (!s_is_maximized) {
            if (!s_user_moved) {
                calculate_monitor_position(&s_pos_x, &s_pos_y);
            } else {
                s_pos_y -= (s_win_height - old_h);
                if (s_pos_y < 0) s_pos_y = 0;
            }
            gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);
        }

        if (s_xid != None) {
            Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
            XMoveResizeWindow(xdpy, s_xid, s_pos_x, s_pos_y, s_win_width, s_win_height);
            XFlush(xdpy);
        }
    }
}




static gboolean on_header_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    (void)user_data;
    if (event->button == 1) {
        if (event->type == GDK_2BUTTON_PRESS) {
            int screen_w = 1920, screen_h = 1080;
            get_monitor_bounds(&screen_w, &screen_h);

            if (!s_is_maximized) {
                s_prev_saved_w = s_win_width;
                s_prev_saved_h = s_win_height;
                s_prev_saved_x = s_pos_x;
                s_prev_saved_y = s_pos_y;

                int max_h = screen_h - 24;
                s_pos_y = 12;
                gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);
                overlay_set_size(s_win_width, max_h);
                s_is_maximized = TRUE;
                overlay_show_toast("Maximized height");
            } else {
                s_pos_x = s_prev_saved_x;
                s_pos_y = s_prev_saved_y;
                gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);
                overlay_set_size(s_prev_saved_w, s_prev_saved_h);
                s_is_maximized = FALSE;
                overlay_show_toast("Restored size");
            }
            return TRUE;
        }

        s_active_drag = MOVE_DRAG;
        s_drag_start_root_x = (int)event->x_root;
        s_drag_start_root_y = (int)event->y_root;
        s_drag_start_win_x = s_pos_x;
        s_drag_start_win_y = s_pos_y;
        s_drag_start_win_w = s_win_width;
        s_drag_start_win_h = s_win_height;
        set_window_cursor(GDK_FLEUR);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_header_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
    (void)widget;
    (void)user_data;
    if (s_active_drag == MOVE_DRAG) {
        int dx = (int)event->x_root - s_drag_start_root_x;
        int dy = (int)event->y_root - s_drag_start_root_y;
        s_pos_x = s_drag_start_win_x + dx;
        s_pos_y = s_drag_start_win_y + dy;
        s_user_moved = TRUE;
        gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);
        if (s_xid != None) {
            Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
            XMoveWindow(xdpy, s_xid, s_pos_x, s_pos_y);
            XFlush(xdpy);
        }
        return TRUE;
    }
    return FALSE;
}

static gboolean on_header_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    (void)user_data;
    if (event->button == 1 && s_active_drag == MOVE_DRAG) {
        s_active_drag = RESIZE_NONE;
        set_window_cursor((GdkCursorType)-1);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_window_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
    (void)widget;
    (void)user_data;
    if (s_active_drag != RESIZE_NONE) {
        int dx = (int)event->x_root - s_drag_start_root_x;
        int dy = (int)event->y_root - s_drag_start_root_y;

        if (s_active_drag == MOVE_DRAG) {
            s_pos_x = s_drag_start_win_x + dx;
            s_pos_y = s_drag_start_win_y + dy;
            s_user_moved = TRUE;
            gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);
            if (s_xid != None) {
                Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
                XMoveWindow(xdpy, s_xid, s_pos_x, s_pos_y);
                XFlush(xdpy);
            }
            return TRUE;
        }

        int new_w = s_drag_start_win_w;
        int new_h = s_drag_start_win_h;
        int new_x = s_drag_start_win_x;
        int new_y = s_drag_start_win_y;

        if (s_active_drag & RESIZE_RIGHT) {
            new_w = s_drag_start_win_w + dx;
        } else if (s_active_drag & RESIZE_LEFT) {
            new_w = s_drag_start_win_w - dx;
            if (new_w < OVERLAY_MIN_WIDTH) new_w = OVERLAY_MIN_WIDTH;
            new_x = s_drag_start_win_x + (s_drag_start_win_w - new_w);
        }

        if (s_active_drag & RESIZE_BOTTOM) {
            new_h = s_drag_start_win_h + dy;
        } else if (s_active_drag & RESIZE_TOP) {
            new_h = s_drag_start_win_h - dy;
            if (new_h < OVERLAY_MIN_HEIGHT) new_h = OVERLAY_MIN_HEIGHT;
            new_y = s_drag_start_win_y + (s_drag_start_win_h - new_h);
        }

        if (new_x != s_pos_x || new_y != s_pos_y) {
            s_pos_x = new_x;
            s_pos_y = new_y;
            gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);
        }
        overlay_set_size(new_w, new_h);
        return TRUE;
    }

    int border_px = 8;
    int x = (int)event->x;
    int y = (int)event->y;
    int w = s_win_width;
    int h = s_win_height;

    DragOperation edge = RESIZE_NONE;
    if (x <= border_px) edge |= RESIZE_LEFT;
    else if (x >= w - border_px) edge |= RESIZE_RIGHT;

    if (y <= border_px) edge |= RESIZE_TOP;
    else if (y >= h - border_px) edge |= RESIZE_BOTTOM;

    GdkCursorType cursor_type = (GdkCursorType)-1;
    if ((edge & RESIZE_TOP) && (edge & RESIZE_LEFT)) cursor_type = GDK_TOP_LEFT_CORNER;
    else if ((edge & RESIZE_TOP) && (edge & RESIZE_RIGHT)) cursor_type = GDK_TOP_RIGHT_CORNER;
    else if ((edge & RESIZE_BOTTOM) && (edge & RESIZE_LEFT)) cursor_type = GDK_BOTTOM_LEFT_CORNER;
    else if ((edge & RESIZE_BOTTOM) && (edge & RESIZE_RIGHT)) cursor_type = GDK_BOTTOM_RIGHT_CORNER;
    else if (edge & RESIZE_TOP) cursor_type = GDK_TOP_SIDE;
    else if (edge & RESIZE_BOTTOM) cursor_type = GDK_BOTTOM_SIDE;
    else if (edge & RESIZE_LEFT) cursor_type = GDK_LEFT_SIDE;
    else if (edge & RESIZE_RIGHT) cursor_type = GDK_RIGHT_SIDE;

    set_window_cursor(cursor_type);
    return FALSE;
}

static gboolean on_window_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    (void)user_data;
    input_notify_native_click(event);

    if (event->button == 1) {
        int border_px = 8;
        int x = (int)event->x;
        int y = (int)event->y;
        int w = s_win_width;
        int h = s_win_height;

        DragOperation edge = RESIZE_NONE;
        if (x <= border_px) edge |= RESIZE_LEFT;
        else if (x >= w - border_px) edge |= RESIZE_RIGHT;

        if (y <= border_px) edge |= RESIZE_TOP;
        else if (y >= h - border_px) edge |= RESIZE_BOTTOM;

        if (edge != RESIZE_NONE) {
            s_active_drag = edge;
            s_drag_start_root_x = (int)event->x_root;
            s_drag_start_root_y = (int)event->y_root;
            s_drag_start_win_x = s_pos_x;
            s_drag_start_win_y = s_pos_y;
            s_drag_start_win_w = s_win_width;
            s_drag_start_win_h = s_win_height;
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean on_window_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    (void)user_data;
    if (event->button == 1 && s_active_drag != RESIZE_NONE) {
        s_active_drag = RESIZE_NONE;
        set_window_cursor((GdkCursorType)-1);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_window_leave_notify(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data) {
    (void)widget;
    (void)event;
    (void)user_data;
    if (s_active_drag == RESIZE_NONE) {
        set_window_cursor((GdkCursorType)-1);
    }
    return FALSE;
}

static gboolean on_widget_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    (void)user_data;
    input_notify_native_click(event);
    return FALSE;
}

void overlay_switch_app(const char *app_name) {
    if (!s_stack || !app_name) return;

    if (g_ascii_strcasecmp(app_name, "chatgpt") == 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(s_stack), "chatgpt");
        g_strlcpy(s_current_app, "chatgpt", sizeof(s_current_app));
        chat_view_set_active_view("chatgpt");
        overlay_set_compact_mode(FALSE);
        xutil_log("WINDOW", "Switched view to ChatGPT app.");
    } else {
        gtk_stack_set_visible_child_name(GTK_STACK(s_stack), "home");
        g_strlcpy(s_current_app, "home", sizeof(s_current_app));
        chat_view_set_active_view("home");
        overlay_set_compact_mode(TRUE);
        xutil_log("WINDOW", "Switched view to Home screen.");
    }
    overlay_update_status_ui();
}

const char *overlay_get_current_app(void) {
    return s_current_app;
}

void overlay_go_home(void) {
    overlay_switch_app("home");
}

void overlay_back(void) {
    if (!strcmp(s_current_app,"chatgpt") && chat_view_can_go_back()) chat_view_go_back();
    else overlay_go_home();
}
void overlay_reload(void) { if (!strcmp(s_current_app,"chatgpt")) chat_view_reload(); }

void overlay_universal_reset(void) {
    xutil_log("WINDOW", "Triggering Universal Reset (returning to factory-fresh state)...");

    /* 1. Close any open popup modals */
    overlay_close_popup_modal();

    s_user_moved = FALSE;

    /* 3. Reset normal application typing and click-through state */
    input_release_grab();
    input_set_typing_target(TYPING_TARGET_NONE);
    input_set_ghost_mode(GHOST_MODE_OFF);
    overlay_set_ghost(FALSE);

    /* 4. Reset ChatGPT navigation, preserving login data */
    chat_view_universal_reset();

    /* 5. Switch overlay view to home screen */
    overlay_go_home();

    /* 6. Update UI and X11 state properties */
    overlay_update_status_ui();
    update_x11_state_properties();

    /* 7. Display confirmation toast */
    overlay_show_toast("Cronq reset to new state");
    xutil_log("WINDOW", "Universal Reset completed successfully.");
}

static void on_btn_back_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    overlay_back();
}

static void on_btn_home_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    overlay_go_home();
}

static void on_btn_universal_refresh_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    overlay_universal_reset();
}

static void on_btn_snap_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    overlay_snap();
}

void overlay_show_toast(const char *msg) {
    /* Status stays in the log; the overlay never opens a notification bubble. */
    if (msg) xutil_log("STATUS", "%s", msg);
}

static GtkWidget *create_nav_pill(GtkWidget **out_box, GtkWidget **out_dot, GtkWidget **out_label, const char *initial_text) {
    GtkWidget *eb = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(eb), FALSE);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "nav-pill");

    GtkWidget *dot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_valign(dot, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(dot), "nav-pill-dot");
    gtk_box_pack_start(GTK_BOX(box), dot, FALSE, FALSE, 0);

    GtkWidget *lbl = gtk_label_new(initial_text);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "nav-pill-text");
    gtk_widget_set_no_show_all(lbl, TRUE);   /* text kept for state/tooltip; only the ball is shown */
    gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(eb), box);

    if (out_box) *out_box = box;
    if (out_dot) *out_dot = dot;
    if (out_label) *out_label = lbl;

    return eb;
}

static gboolean on_mode_pill_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget; (void)event; (void)user_data;
    input_toggle_typing_target();
    overlay_update_status_ui();
    return TRUE;
}

static gboolean on_ghost_pill_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget; (void)event; (void)user_data;
    if (input_get_ghost_mode() == GHOST_MODE_ON) {
        input_set_ghost_mode(GHOST_MODE_OFF);
        overlay_set_ghost(FALSE);
    } else {
        input_set_ghost_mode(GHOST_MODE_ON);
        overlay_set_ghost(TRUE);
    }
    overlay_update_status_ui();
    return TRUE;
}

static gboolean on_recovery_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget; (void)user_data;
    input_notify_native_click(event);
    if (event->button == 1 && event->type == GDK_2BUTTON_PRESS) {
        overlay_switch_app("chatgpt");
    }
    return TRUE;
}

static GtkWidget *error_label(const char *text, const char *css_class) {
    GtkWidget *label = gtk_label_new(text);
    gtk_style_context_add_class(gtk_widget_get_style_context(label), css_class);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
    return label;
}

static GtkWidget *create_home_page(void) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(page), "home-page");
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(content, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(content, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(content), "error-content");
    s_error_icon = create_svg_image(s_svg_error_icon, 32, 36);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_error_icon), "error-symbol");
    gtk_box_pack_start(GTK_BOX(content), s_error_icon, FALSE, FALSE, 0);
    s_error_code = error_label("HTTP ERROR 404", "error-code");
    gtk_box_pack_start(GTK_BOX(content), s_error_code, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), error_label("This page isn’t available", "error-title"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), error_label("The address may have changed.", "error-description"), FALSE, FALSE, 0);
    GtkWidget *line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(line, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(line), error_label("Try ", "error-description"), FALSE, FALSE, 0);
    GtkWidget *recovery = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(recovery), FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(recovery), "recovery-word");
    gtk_container_add(GTK_CONTAINER(recovery), error_label("recovery", "error-description"));
    atk_object_set_name(gtk_widget_get_accessible(recovery), "recovery");
    g_signal_connect(recovery, "button-press-event", G_CALLBACK(on_recovery_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(line), recovery, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(line), error_label(" or reload the page.", "error-description"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), line, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), content, TRUE, TRUE, 0);
    return page;
}

static void on_popup_close_btn_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    overlay_close_popup_modal();
}

void overlay_show_popup_modal(GtkWidget *popup_view) {
    if (!s_overlay || !popup_view) return;

    if (s_popup_container) {
        overlay_close_popup_modal();
    }

    s_popup_child_view = popup_view;

    s_popup_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_popup_container), "popup-modal-layer");

    /* Header bar with title and close button */
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(bar), "popup-modal-bar");

    GtkWidget *lbl = gtk_label_new("Popup / Sign In");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "popup-modal-title");
    gtk_box_pack_start(GTK_BOX(bar), lbl, FALSE, FALSE, 8);

    GtkWidget *spacer = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(bar), spacer, TRUE, TRUE, 0);

    GtkWidget *btn_close = gtk_button_new();
    gtk_widget_set_can_focus(btn_close, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_close), "popup-close-btn");
    GtkWidget *img_close = create_svg_image(s_svg_close_icon, 16, 16);
    gtk_container_add(GTK_CONTAINER(btn_close), img_close);
    g_signal_connect(btn_close, "clicked", G_CALLBACK(on_popup_close_btn_clicked), NULL);
    g_signal_connect(btn_close, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(bar), btn_close, FALSE, FALSE, 4);

    gtk_box_pack_start(GTK_BOX(s_popup_container), bar, FALSE, FALSE, 0);

    /* Scrolled window for popup view */
    const AppConfig *cfg = config_get();
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), FALSE);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroll), FALSE);
    gtk_widget_set_size_request(scroll, cfg->width, -1);
    gtk_container_add(GTK_CONTAINER(scroll), popup_view);
    gtk_box_pack_start(GTK_BOX(s_popup_container), scroll, TRUE, TRUE, 0);

    gtk_overlay_add_overlay(GTK_OVERLAY(s_overlay), s_popup_container);
    gtk_widget_show_all(s_popup_container);
    xutil_log("WINDOW", "Displayed popup modal layer inside cronq overlay.");
}

void overlay_close_popup_modal(void) {
    if (s_popup_container) {
        gtk_widget_destroy(s_popup_container);
        s_popup_container = NULL;
        s_popup_child_view = NULL;
        chat_view_clear_active_popup();
        xutil_log("WINDOW", "Closed popup modal layer.");
    }
}

gboolean overlay_has_popup_modal(void) {
    return (s_popup_container != NULL);
}

GtkWidget *overlay_get_popup_child_view(void) {
    return s_popup_child_view;
}

static void on_chat_view_popup_created(GtkWidget *popup_view, gpointer user_data) {
    (void)user_data;
    overlay_show_popup_modal(popup_view);
}

static void on_chat_view_popup_closed(GtkWidget *popup_view, gpointer user_data) {
    (void)user_data;
    (void)popup_view;
    overlay_close_popup_modal();
}

void overlay_execute_clipboard_action(ClipboardAction action) {
    if (!s_window) return;

    GtkWidget *focused = gtk_window_get_focus(GTK_WINDOW(s_window));
    xutil_log("WINDOW", "Executing clipboard action %d (focused widget=%s, current_app=%s)",
              action, focused ? G_OBJECT_TYPE_NAME(focused) : "NULL", s_current_app);

    if (focused && GTK_IS_EDITABLE(focused)) {
        switch (action) {
            case CLIPBOARD_ACTION_SELECT_ALL:
                gtk_editable_select_region(GTK_EDITABLE(focused), 0, -1);
                break;
            case CLIPBOARD_ACTION_COPY:
                gtk_editable_copy_clipboard(GTK_EDITABLE(focused));
                break;
            case CLIPBOARD_ACTION_PASTE:
                gtk_editable_paste_clipboard(GTK_EDITABLE(focused));
                break;
            case CLIPBOARD_ACTION_CUT:
                gtk_editable_cut_clipboard(GTK_EDITABLE(focused));
                break;
        }
    } else {
        if (action == CLIPBOARD_ACTION_PASTE) {
            chat_view_paste();
        } else {
            const char *cmd = NULL;
            switch (action) {
                case CLIPBOARD_ACTION_SELECT_ALL: cmd = WEBKIT_EDITING_COMMAND_SELECT_ALL; break;
                case CLIPBOARD_ACTION_COPY:       cmd = WEBKIT_EDITING_COMMAND_COPY; break;
                case CLIPBOARD_ACTION_CUT:        cmd = WEBKIT_EDITING_COMMAND_CUT; break;
                default: break;
            }
            if (cmd) {
                chat_view_execute_editing_command(cmd);
            }
        }
    }
    update_x11_state_properties();
}

static void add_nav_content(GtkWidget *button, GtkWidget *icon, const char *text) {
    GtkWidget *column = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(column, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(column, GTK_ALIGN_END);
    /* Keep the screenshot state stack alive, without exposing its old icons. */
    if (icon) {
        gtk_widget_set_no_show_all(icon, TRUE);
        gtk_widget_hide(icon);
        gtk_box_pack_start(GTK_BOX(column), icon, FALSE, FALSE, 0);
    }
    GtkWidget *dot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(dot), "nav-target-dot");
    gtk_widget_set_halign(dot, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(dot, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(column), dot, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(button), column);
    atk_object_set_name(gtk_widget_get_accessible(button), text);
}

static void reload_theme_css(void) {
    if (!s_css_provider) return;
    char *css = theme_build_css(s_phone_css);
    gtk_css_provider_load_from_data(s_css_provider, css, -1, NULL);
    g_free(css);
}

static void set_camo_hidden(GtkWidget *w, gboolean hidden) {
    if (!w) return;
    gtk_widget_set_no_show_all(w, hidden);
    if (hidden) gtk_widget_hide(w);
    else gtk_widget_show_all(w);
}

/* Re-skin everything after a theme change (CSS, icons, camouflage chrome visibility). */
void overlay_apply_theme(void) {
    if (!s_window) return;
    gboolean camo = theme_is_camo();
    reload_theme_css();
    refresh_themed_svgs();
    set_camo_hidden(s_status_bar,FALSE);
    if (s_status_pills) {
        gtk_widget_set_hexpand(s_status_pills,camo);
        gtk_widget_set_halign(s_status_pills,camo?GTK_ALIGN_END:GTK_ALIGN_START);
    }
    if (s_status_bar) {
        GList *items=gtk_container_get_children(GTK_CONTAINER(s_status_bar));
        for(GList *it=items;it;it=it->next) {
            GtkWidget *group=it->data;
            if(GTK_IS_CONTAINER(group)) {
                GList *children=gtk_container_get_children(GTK_CONTAINER(group));
                for(GList *ch=children;ch;ch=ch->next) {
                    GtkWidget *item=ch->data;
                    if(item!=gtk_widget_get_parent(s_mode_pill_box) && item!=gtk_widget_get_parent(s_ghost_pill_box)) set_camo_hidden(item,camo);
                }
                g_list_free(children);
            }
        }
        g_list_free(items);
    }
    set_camo_hidden(s_bottom_nav, camo);
    /* Showing chrome after a theme change must keep the failure alert hidden
     * unless keyboard capture actually failed. */
    overlay_update_status_ui();
    gtk_widget_queue_draw(s_window);
    xutil_log("WINDOW", "Applied theme '%s' (camo=%d).", theme_mode_label(), camo);
}

gboolean window_init(gboolean start_visible) {
    const AppConfig *cfg = config_get();

    /* 1. Create GTK_WINDOW_POPUP -> Sets override_redirect at X11 level */
    s_window = gtk_window_new(GTK_WINDOW_POPUP);
    fitted_size(&s_win_width,&s_win_height);
    s_win_height = MIN(OVERLAY_COMPACT_HEIGHT,s_win_height);
    s_current_opacity = cfg->opacity;

    gtk_window_set_default_size(GTK_WINDOW(s_window), s_win_width, s_win_height);
    gtk_widget_set_size_request(s_window, OVERLAY_MIN_WIDTH, OVERLAY_MIN_HEIGHT);
    gtk_window_set_resizable(GTK_WINDOW(s_window), TRUE);
    gtk_widget_set_opacity(s_window, s_current_opacity);

    GdkScreen *screen = gtk_widget_get_screen(s_window);
    int screen_w = 1920, screen_h = 1080;
    get_monitor_bounds(&screen_w, &screen_h);

    /* Enforce size hints on GTK window (relaxed for full resize) */
    GdkGeometry hints;
    memset(&hints, 0, sizeof(hints));
    hints.min_width = OVERLAY_MIN_WIDTH;
    hints.max_width = screen_w;
    hints.min_height = OVERLAY_MIN_HEIGHT;
    hints.max_height = screen_h;
    gtk_window_set_geometry_hints(GTK_WINDOW(s_window), s_window, &hints,
                                  GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE);

    /* Connect size-allocate guard */
    g_signal_connect(s_window, "size-allocate", G_CALLBACK(on_window_size_allocate), NULL);

    /* 2. Configure RGBA visual for transparent rounded corners */
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(s_window, visual);
        xutil_log("WINDOW", "RGBA visual supported and compositor active. Transparency enabled.");
    } else {
        xutil_log("WINDOW", "RGBA visual or compositor not available. Falling back to opaque.");
    }
    gtk_widget_set_app_paintable(s_window, TRUE);
    g_signal_connect(s_window, "draw", G_CALLBACK(on_window_draw), NULL);

    /* 3. Focus policy & taskbar/pager hints */
    gtk_window_set_accept_focus(GTK_WINDOW(s_window), FALSE);
    gtk_window_set_focus_on_map(GTK_WINDOW(s_window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(s_window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(s_window), TRUE);

    /* Configure GtkSettings for perpetual cursor blinking */
    GtkSettings *settings = gtk_settings_get_default();
    if (settings) {
        g_object_set(settings,
                     "gtk-cursor-blink", TRUE,
                     "gtk-cursor-blink-time", 1000,
                     "gtk-cursor-blink-timeout", 2147483647,
                     NULL);
    }

    /* 4. Position window according to configuration */
    calculate_monitor_position(&s_pos_x, &s_pos_y);
    gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);

    /* Apply CSS styles (theme-aware) */
    theme_init(cfg->appearance_mode);
    s_css_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(s_css_provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    reload_theme_css();

    /* 5. Construct Phone UI Layout */
    s_root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(s_root_box, s_win_width, s_win_height);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_root_box), "phone-root");
    gtk_container_add(GTK_CONTAINER(s_window), s_root_box);

    /* --- Top Navigation / Status Bar --- */
    GtkWidget *status_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    s_status_bar = status_bar;
    gtk_style_context_add_class(gtk_widget_get_style_context(status_bar), "status-bar");
    gtk_box_pack_start(GTK_BOX(s_root_box), status_bar, FALSE, FALSE, 0);

    /* Left: Mode and Ghost Status Pills */
    GtkWidget *left_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    s_status_pills=left_box;
    gtk_widget_set_halign(left_box, GTK_ALIGN_START);

    GtkWidget *mode_pill_eb = create_nav_pill(&s_mode_pill_box, &s_mode_dot, &s_mode_pill, "Normal typing");
    g_signal_connect(mode_pill_eb, "button-press-event", G_CALLBACK(on_mode_pill_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(left_box), mode_pill_eb, FALSE, FALSE, 0);

    GtkWidget *ghost_pill_eb = create_nav_pill(&s_ghost_pill_box, &s_ghost_dot, &s_ghost_pill_label, "Solid");
    g_signal_connect(ghost_pill_eb, "button-press-event", G_CALLBACK(on_ghost_pill_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(left_box), ghost_pill_eb, FALSE, FALSE, 0);

    /* Grab Alert Pill (hidden unless grab fails) */
    s_grab_pill = create_nav_pill(&s_grab_box, NULL, &s_grab_pill_label, "Blocked");
    if (s_grab_box) {
        gtk_style_context_add_class(gtk_widget_get_style_context(s_grab_box), "pill-grab-alert");
    }
    gtk_box_pack_start(GTK_BOX(left_box), s_grab_pill, FALSE, FALSE, 0);
    gtk_widget_set_no_show_all(s_grab_pill, TRUE);
    gtk_widget_hide(s_grab_pill);

    gtk_box_pack_start(GTK_BOX(status_bar), left_box, FALSE, FALSE, 0);

    /* Center: Move Handle */
    s_move_handle_eb = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(s_move_handle_eb), FALSE);
    gtk_widget_add_events(s_move_handle_eb, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_BUTTON1_MOTION_MASK | GDK_POINTER_MOTION_MASK);
    gtk_widget_set_hexpand(s_move_handle_eb, TRUE);

    GtkWidget *move_capsule = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(move_capsule), "move-capsule");
    gtk_widget_set_size_request(move_capsule, 24, 30);
    gtk_container_add(GTK_CONTAINER(s_move_handle_eb), move_capsule);

    g_signal_connect(s_move_handle_eb, "button-press-event", G_CALLBACK(on_header_button_press), NULL);
    g_signal_connect(s_move_handle_eb, "motion-notify-event", G_CALLBACK(on_header_motion_notify), NULL);
    g_signal_connect(s_move_handle_eb, "button-release-event", G_CALLBACK(on_header_button_release), NULL);

    gtk_box_pack_start(GTK_BOX(status_bar), s_move_handle_eb, TRUE, TRUE, 0);

    /* --- Central Viewport: GtkOverlay wrapping GtkStack --- */
    s_overlay = gtk_overlay_new();
    gtk_box_pack_start(GTK_BOX(s_root_box), s_overlay, TRUE, TRUE, 0);

    s_stack = gtk_stack_new();
    gtk_stack_set_homogeneous(GTK_STACK(s_stack), FALSE);
    gtk_stack_set_transition_type(GTK_STACK(s_stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_set_transition_duration(GTK_STACK(s_stack), 180);
    gtk_container_add(GTK_CONTAINER(s_overlay), s_stack);

    /* 1. Page: Home Screen */
    GtkWidget *home_page = create_home_page();
    gtk_stack_add_named(GTK_STACK(s_stack), home_page, "home");

    /* 2. Page: ChatGPT App (Full-screen WebKit inside phone, no URL bar) */
    GtkWidget *cg_widget = chat_view_get_chatgpt_widget();
    if (cg_widget) {
        gtk_widget_add_events(cg_widget, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
        g_signal_connect(cg_widget, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
        gtk_stack_add_named(GTK_STACK(s_stack), cg_widget, "chatgpt");
    }

    /* Default view is HOME SCREEN */
    gtk_stack_set_visible_child_name(GTK_STACK(s_stack), "home");
    g_strlcpy(s_current_app, "home", sizeof(s_current_app));

    chat_view_set_popup_callbacks(on_chat_view_popup_created,on_chat_view_popup_closed,NULL);

    /* --- Bottom Navigation Bar --- */
    GtkWidget *bottom_nav = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    s_bottom_nav = bottom_nav;
    gtk_box_set_homogeneous(GTK_BOX(bottom_nav), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(bottom_nav), "bottom-nav");
    gtk_box_pack_start(GTK_BOX(s_root_box), bottom_nav, FALSE, FALSE, 0);

    s_btn_back = gtk_button_new();
    gtk_widget_set_can_focus(s_btn_back, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_btn_back), "nav-button");
    gtk_widget_set_sensitive(s_btn_back, FALSE);
    add_nav_content(s_btn_back, NULL, "Previous");
    g_signal_connect(s_btn_back, "clicked", G_CALLBACK(on_btn_back_clicked), NULL);
    g_signal_connect(s_btn_back, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(bottom_nav), s_btn_back, TRUE, TRUE, 0);

    s_btn_home = gtk_button_new();
    gtk_widget_set_can_focus(s_btn_home, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_btn_home), "nav-button");
    add_nav_content(s_btn_home, NULL, "Overview");
    g_signal_connect(s_btn_home, "clicked", G_CALLBACK(on_btn_home_clicked), NULL);
    g_signal_connect(s_btn_home, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(bottom_nav), s_btn_home, TRUE, TRUE, 0);

    /* Universal Refresh Button (New State) */
    s_btn_universal_refresh = gtk_button_new();
    gtk_widget_set_can_focus(s_btn_universal_refresh, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_btn_universal_refresh), "nav-button");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_btn_universal_refresh), "nav-refresh-btn");
    add_nav_content(s_btn_universal_refresh, NULL, "Reload");
    g_signal_connect(s_btn_universal_refresh, "clicked", G_CALLBACK(on_btn_universal_refresh_clicked), NULL);
    g_signal_connect(s_btn_universal_refresh, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(bottom_nav), s_btn_universal_refresh, TRUE, TRUE, 0);

    /* Snap Button (Phase 6) */
    s_btn_snap = gtk_button_new();
    gtk_widget_set_can_focus(s_btn_snap, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_btn_snap), "nav-button");
    gtk_widget_set_sensitive(s_btn_snap, TRUE);

    s_snap_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(s_snap_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(s_snap_stack), 100);

    GtkWidget *img_snap_cam = create_svg_image(s_svg_report_icon, 12, 12);
    GtkWidget *img_snap_running = create_svg_image(s_svg_snap_running, 12, 12);
    GtkWidget *img_snap_done = create_svg_image(s_svg_check_icon, 12, 12);

    gtk_stack_add_named(GTK_STACK(s_snap_stack), img_snap_cam, "camera");
    gtk_stack_add_named(GTK_STACK(s_snap_stack), img_snap_running, "snapping");
    gtk_stack_add_named(GTK_STACK(s_snap_stack), img_snap_done, "done");
    gtk_stack_add_named(GTK_STACK(s_snap_stack), create_svg_image(s_svg_failed_icon,12,12), "failed");

    add_nav_content(s_btn_snap, s_snap_stack, "Report");
    gtk_stack_set_visible_child_name(GTK_STACK(s_snap_stack), "camera");

    g_signal_connect(s_btn_snap, "clicked", G_CALLBACK(on_btn_snap_clicked), NULL);
    g_signal_connect(s_btn_snap, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(bottom_nav), s_btn_snap, TRUE, TRUE, 0);

    /* Click and motion listeners on main window for edge resizing */
    gtk_widget_add_events(s_window, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                    GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(s_window, "button-press-event", G_CALLBACK(on_window_button_press), NULL);
    g_signal_connect(s_window, "button-release-event", G_CALLBACK(on_window_button_release), NULL);
    g_signal_connect(s_window, "motion-notify-event", G_CALLBACK(on_window_motion_notify), NULL);
    g_signal_connect(s_window, "leave-notify-event", G_CALLBACK(on_window_leave_notify), NULL);

    /* Realize widget to obtain underlying XID */
    gtk_widget_realize(s_window);
    GdkWindow *gdk_win = gtk_widget_get_window(s_window);
    s_xid = gdk_x11_window_get_xid(gdk_win);

    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    XMoveResizeWindow(xdpy, s_xid, s_pos_x, s_pos_y, s_win_width, s_win_height);

    /* Enforce size hints at X11 level (relaxed for full resize) */
    XSizeHints *sh = XAllocSizeHints();
    if (sh) {
        sh->flags = PMinSize | PMaxSize;
        sh->min_width = OVERLAY_MIN_WIDTH;
        sh->max_width = screen_w;
        sh->min_height = OVERLAY_MIN_HEIGHT;
        sh->max_height = screen_h;
        XSetWMNormalHints(xdpy, s_xid, sh);
        XFree(sh);
    }

    /* Ensure X11 input hint is FALSE */
    XWMHints *wm_hints = XGetWMHints(xdpy, s_xid);
    if (!wm_hints) wm_hints = XAllocWMHints();
    if (wm_hints) {
        wm_hints->flags |= InputHint;
        wm_hints->input = False;
        XSetWMHints(xdpy, s_xid, wm_hints);
        XFree(wm_hints);
    }

    Atom net_wm_name = XInternAtom(xdpy, "_NET_WM_NAME", False);
    Atom utf8_string = XInternAtom(xdpy, "UTF8_STRING", False);
    XChangeProperty(xdpy, s_xid, net_wm_name, utf8_string, 8, PropModeReplace,
                    (unsigned char *)"Cronq", 5);
    XStoreName(xdpy, s_xid, "Cronq");
    XClassHint class_hint;
    class_hint.res_name = "cronq";
    class_hint.res_class = "Cronq";
    XSetClassHint(xdpy, s_xid, &class_hint);

    update_x11_state_properties();
    overlay_apply_theme();

    xutil_log("WINDOW", "Created override-redirect overlay XID 0x%lx (%lu)",
              (unsigned long)s_xid, (unsigned long)s_xid);




    if (start_visible) {
        overlay_show();
    } else {
        overlay_hide();
    }

    return TRUE;
}

void overlay_set_capture_hidden(gboolean hidden) {
    if(hidden)input_release_grab();
    if (!s_window || s_xid == None) return;
    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    if (hidden) {
        s_capture_hidden = TRUE;
        if (s_visible) {
            XUnmapWindow(xdpy, s_xid);
            XFlush(xdpy);
        }
    } else {
        s_capture_hidden = FALSE;
        if (s_visible) {
            XMapRaised(xdpy, s_xid);
            XFlush(xdpy);
        }
    }
}

void overlay_show(void) {
    if (!s_window) return;

    if (!s_is_maximized && !s_user_moved) {
        calculate_monitor_position(&s_pos_x, &s_pos_y);
    }

    gtk_widget_show_all(s_window);
    gtk_widget_realize(s_window);

    gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);
    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    XMoveResizeWindow(xdpy, s_xid, s_pos_x, s_pos_y, s_win_width, s_win_height);
    if (s_capture_hidden) {
        XUnmapWindow(xdpy, s_xid);   /* a capture is running: stay out of it until the guard restores us */
    } else {
        XMapRaised(xdpy, s_xid);
    }
    XFlush(xdpy);

    s_visible = TRUE;
    xutil_log("WINDOW", "Overlay shown (mapped raised at (%d, %d))", s_pos_x, s_pos_y);



    chat_view_resume();
    hotkey_reload_grabs();
    overlay_update_status_ui();
    raise_start();
}

void overlay_hide(void) {
    if (!s_window) return;

    raise_stop();
    input_notify_overlay_hidden();
    chat_view_pause();



    /* Keep GTK/WebKit mapped internally so native key dispatch still works.
     * Only the X11 surface is hidden; no OS focus is changed. */
    XUnmapWindow(gdk_x11_display_get_xdisplay(gdk_display_get_default()), s_xid);
    XFlush(gdk_x11_display_get_xdisplay(gdk_display_get_default()));
    s_visible = FALSE;
    hotkey_reload_grabs();
    xutil_log("WINDOW", "Overlay hidden");
}

void overlay_reposition(void) {
    if (!s_window || s_xid == None) return;

    s_user_moved = FALSE;
    calculate_monitor_position(&s_pos_x, &s_pos_y);

    gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);
    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    XMoveResizeWindow(xdpy, s_xid, s_pos_x, s_pos_y, s_win_width, s_win_height);
    XFlush(xdpy);

    overlay_update_status_ui();
    xutil_log("WINDOW", "Overlay repositioned to (%d, %d)", s_pos_x, s_pos_y);
}

void overlay_apply_config(void) {
    if (!s_window || s_xid == None) return;

    const AppConfig *cfg = config_get();
    s_user_moved = FALSE;
    s_is_maximized = FALSE;
    fitted_size(&s_win_width,&s_win_height);
    if(!strcmp(s_current_app,"home"))s_win_height=MIN(OVERLAY_COMPACT_HEIGHT,s_win_height);
    calculate_monitor_position(&s_pos_x, &s_pos_y);
    s_current_opacity = cfg->opacity;

    gtk_window_resize(GTK_WINDOW(s_window), s_win_width, s_win_height);
    if (s_root_box) {
        gtk_widget_set_size_request(s_root_box, s_win_width, s_win_height);
    }
    gtk_widget_set_opacity(s_window, s_current_opacity);
    gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);

    if (s_opacity_label) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d%%", (int)(s_current_opacity * 100.0 + 0.5));
        gtk_label_set_text(GTK_LABEL(s_opacity_label), buf);
    }

    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    XMoveResizeWindow(xdpy, s_xid, s_pos_x, s_pos_y, s_win_width, s_win_height);
    XFlush(xdpy);

    overlay_update_status_ui();
    xutil_log("WINDOW", "Applied config: %dx%d, opacity=%.2f, pos=(%d,%d)",
              s_win_width, s_win_height, s_current_opacity, s_pos_x, s_pos_y);
}

void overlay_dock_corner(const char *corner) {
    if (!s_window || !corner) return;
    config_save_value("General", "corner", corner);
    s_is_maximized = FALSE;
    overlay_reposition();
    overlay_show_toast(g_ascii_strcasecmp(corner, "bottom-right") == 0 ? "Docked bottom-right" : "Docked bottom-left");
}

void overlay_toggle(void) {
    if (s_visible) {
        overlay_hide();
    } else {
        overlay_show();
    }
}

Window overlay_get_xid(void) {
    return s_xid;
}

gboolean overlay_is_visible(void) {
    return s_visible;
}

void overlay_get_geometry(int *x, int *y, int *w, int *h) {
    if (x) *x = s_pos_x;
    if (y) *y = s_pos_y;
    if (w) *w = s_win_width;
    if (h) *h = s_win_height;
}

void overlay_queue_draw(void) {
    if (s_window) {
        gtk_widget_queue_draw(s_window);
    }
    overlay_update_status_ui();
}

void overlay_set_ghost(gboolean ghost) {
    if (s_xid == None) return;
    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    if (ghost) {
        XShapeCombineRectangles(xdpy, s_xid, ShapeInput, 0, 0, NULL, 0, ShapeSet, YXBanded);
        xutil_log("WINDOW", "Applied ShapeInput 0 rects: overlay is click-through (GHOST).");
    } else {
        XShapeCombineMask(xdpy, s_xid, ShapeInput, 0, 0, None, ShapeSet);
        xutil_log("WINDOW", "Restored default ShapeInput mask: overlay is SOLID.");
    }
    XFlush(xdpy);
    overlay_update_status_ui();
}

void overlay_set_active_focus(gboolean active) {
    if (!s_window) return;
    GdkWindow *gdk_win = gtk_widget_get_window(s_window);
    if (!gdk_win) return;

    GdkDisplay *disp = gdk_window_get_display(gdk_win);
    GdkSeat *seat = gdk_display_get_default_seat(disp);
    GdkDevice *keyboard = seat ? gdk_seat_get_keyboard(seat) : NULL;

    GdkEvent *event = gdk_event_new(GDK_FOCUS_CHANGE);
    event->focus_change.type = GDK_FOCUS_CHANGE;
    event->focus_change.window = g_object_ref(gdk_win);
    event->focus_change.send_event = FALSE;
    event->focus_change.in = active;
    if (keyboard) {
        gdk_event_set_device(event, keyboard);
    }
    gtk_widget_event(s_window,event);
    gdk_event_free(event);
}

GtkWidget *overlay_get_window_widget(void) {
    return s_window;
}

static char *get_user_runtime_snap_path(void) {
    const char *rdir = g_get_user_runtime_dir();
    if (!rdir || rdir[0] == '\0') {
        rdir = g_get_user_data_dir();
    }
    char *cronq_dir = g_build_filename(rdir, "cronq", NULL);
    g_mkdir_with_parents(cronq_dir, 0700);
    char *snap_path = g_build_filename(cronq_dir, "latest_snap.png", NULL);
    g_free(cronq_dir);
    return snap_path;
}

static gint compare_snap_names(gconstpointer a,gconstpointer b) { return g_strcmp0(*(char *const *)a,*(char *const *)b); }
static gboolean save_and_prune_snap(GdkPixbuf *pb, const AppConfig *cfg) {
    char *dir_buf=g_build_filename(g_get_user_data_dir(),"cronq","snaps",NULL);

    if (g_mkdir_with_parents(dir_buf, 0755) != 0) {
        xutil_log("WINDOW", "Failed to create directory %s: %s", dir_buf, g_strerror(errno));
        g_free(dir_buf);
        return FALSE;
    }

    GDateTime *now = g_date_time_new_now_local();
    char *dt_str = g_date_time_format(now, "%Y%m%d-%H%M%S-%f");
    g_date_time_unref(now);

    char *base_name = g_strdup_printf("snap-%s.png", dt_str ? dt_str : "unknown");
    char *filepath = g_build_filename(dir_buf, base_name, NULL);
    g_free(base_name);

    int suffix = 1;
    while (g_file_test(filepath, G_FILE_TEST_EXISTS)) {
        g_free(filepath);
        base_name = g_strdup_printf("snap-%s-%d.png", dt_str ? dt_str : "unknown", suffix++);
        filepath = g_build_filename(dir_buf, base_name, NULL);
        g_free(base_name);
    }
    g_free(dt_str);

    GError *err = NULL;
    gboolean saved=FALSE;
    if (gdk_pixbuf_save(pb, filepath, "png", &err, "compression", "9", NULL)) {
        saved=TRUE;
        xutil_log("WINDOW", "Saved screenshot to %s", filepath);

        /* 1. Secure primary link in user runtime directory */
        char *user_snap = get_user_runtime_snap_path();
        unlink(user_snap);
        if (symlink(filepath, user_snap) != 0) {
            gchar *data = NULL;
            gsize len = 0;
            if (g_file_get_contents(filepath, &data, &len, NULL)) {
                g_file_set_contents(user_snap, data, len, NULL);
                g_free(data);
            }
        }
        g_free(user_snap);

    } else {
        xutil_log("WINDOW", "Failed to save screenshot %s: %s", filepath, err ? err->message : "unknown");
        if (err) g_error_free(err);
    }
    g_free(filepath);

    /* Prune old files exceeding snap_keep_count */
    int keep_count = (cfg->snap_keep_count > 0) ? cfg->snap_keep_count : 20;
    GDir *dir = g_dir_open(dir_buf, 0, NULL);
    if (dir) {
        GPtrArray *files = g_ptr_array_new_with_free_func(g_free);
        const char *name = NULL;
        while ((name = g_dir_read_name(dir)) != NULL) {
            if (g_str_has_prefix(name, "snap-") && g_str_has_suffix(name, ".png")) {
                g_ptr_array_add(files, g_strdup(name));
            }
        }
        g_dir_close(dir);

        g_ptr_array_sort(files, compare_snap_names);

        if ((int)files->len > keep_count) {
            int to_delete = (int)files->len - keep_count;
            for (int i = 0; i < to_delete; i++) {
                char *old_path = g_build_filename(dir_buf, (const char *)g_ptr_array_index(files, i), NULL);
                g_unlink(old_path);
                xutil_log("WINDOW", "Pruned old screenshot: %s", old_path);
                g_free(old_path);
            }
        }
        g_ptr_array_unref(files);
    }
    g_free(dir_buf);
    return saved;
}

void overlay_capture_begin(void) {
    if (s_visible && s_xid != None) {
        raise_stop();
        Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
        XUnmapWindow(xdpy, s_xid);
        XSync(xdpy, False);
        xutil_log("WINDOW", "overlay_capture_begin: unmapped overlay XID 0x%lx and paused restack watcher", (unsigned long)s_xid);
    }
}

void overlay_capture_end(void) {
    if (s_was_visible_during_snap && s_visible && !s_capture_hidden && s_xid != None) {
        Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
        XMapRaised(xdpy, s_xid);
        XSync(xdpy, False);
        raise_start();
        xutil_log("WINDOW", "overlay_capture_end: remapped overlay XID 0x%lx and resumed restack watcher", (unsigned long)s_xid);
    }
}

static gboolean on_snap_revert_btn_label(gpointer user_data) {
    (void)user_data;
    if (s_snap_stack) {
        gtk_stack_set_visible_child_name(GTK_STACK(s_snap_stack), "camera");
    }
    s_snap_revert_timer = 0;
    return G_SOURCE_REMOVE;
}

static gboolean has_visible_pixels(GdkPixbuf *pb) {
    int width=gdk_pixbuf_get_width(pb),height=gdk_pixbuf_get_height(pb),channels=gdk_pixbuf_get_n_channels(pb),stride=gdk_pixbuf_get_rowstride(pb);
    const guchar *pixels=gdk_pixbuf_get_pixels(pb);
    for(int y=0;y<height;y++)for(int x=0;x<width;x++){const guchar *p=pixels+y*stride+x*channels;if(p[0]||p[1]||p[2])return TRUE;}
    return FALSE;
}
static GdkMonitor *active_monitor(GdkDisplay *display) {
    Display *xdpy=gdk_x11_display_get_xdisplay(display);Window focus,child;int revert,x,y;
    XGetInputFocus(xdpy,&focus,&revert);
    XWindowAttributes attr;
    if(focus!=None && focus!=PointerRoot && XGetWindowAttributes(xdpy,focus,&attr) &&
       XTranslateCoordinates(xdpy,focus,DefaultRootWindow(xdpy),attr.width/2,attr.height/2,&x,&y,&child))
        return gdk_display_get_monitor_at_point(display,x,y);
    return gdk_display_get_primary_monitor(display);
}
static gboolean on_snap_timeout(gpointer user_data) {
    (void)user_data;
    const AppConfig *cfg = config_get();

    int rx = 0, ry = 0, rw = 0, rh = 0;
    GdkWindow *root = gdk_get_default_root_window();

    if (g_ascii_strcasecmp(cfg->snap_scope, "all") == 0) {
        rx = 0;
        ry = 0;
        rw = gdk_window_get_width(root);
        rh = gdk_window_get_height(root);
    } else {
        GdkDisplay *display = gdk_display_get_default();
        GdkMonitor *mon = active_monitor(display);
        if (!mon && s_window && gtk_widget_get_window(s_window)) {
            mon = gdk_display_get_monitor_at_window(display, gtk_widget_get_window(s_window));
        }
        if (!mon) {
            mon = gdk_display_get_monitor(display, 0);
        }
        if (mon) {
            GdkRectangle geom;
            gdk_monitor_get_geometry(mon, &geom);
            rx = geom.x;
            ry = geom.y;
            rw = geom.width;
            rh = geom.height;
        } else {
            rx = 0;
            ry = 0;
            rw = gdk_window_get_width(root);
            rh = gdk_window_get_height(root);
        }
    }

    /* Capture screenshot from root window */
    GdkPixbuf *pb = gdk_pixbuf_get_from_window(root, rx, ry, rw, rh);

    /* Immediately restore overlay */
    overlay_capture_end();
    if(pb && !has_visible_pixels(pb)){g_object_unref(pb);pb=NULL;}

    if (pb) {
        xutil_log("WINDOW", "Captured snap: %dx%d at (%d, %d)", rw, rh, rx, ry);

        /* Set to X11 CLIPBOARD */
        GtkClipboard *clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        gtk_clipboard_set_image(clip, pb);
        gtk_clipboard_store(clip);

        /* Save to disk if enabled */
        gboolean saved=!cfg->snap_save || save_and_prune_snap(pb,cfg);
        overlay_show_toast(saved?"Screenshot copied":"Screenshot copied; could not save PNG");

        g_object_unref(pb);

        if (s_snap_stack) {
            gtk_stack_set_visible_child_name(GTK_STACK(s_snap_stack), "done");
            if (s_snap_revert_timer > 0) {
                g_source_remove(s_snap_revert_timer);
            }
            s_snap_revert_timer = g_timeout_add(1500, on_snap_revert_btn_label, NULL);
        }
    } else {
        xutil_log_err("WINDOW", "Capture unavailable or entirely black; clipboard unchanged");
        overlay_show_toast("Screenshot unavailable or entirely black");
        if (s_snap_stack) {
            gtk_stack_set_visible_child_name(GTK_STACK(s_snap_stack), "failed");
        }
    }

    s_is_snapping = FALSE;
    return G_SOURCE_REMOVE;
}

void overlay_snap(void) {
    if (s_is_snapping) {
        xutil_log("WINDOW", "overlay_snap: capture already in progress; debounced.");
        return;
    }

    s_is_snapping = TRUE;
    const AppConfig *cfg = config_get();

    if (s_snap_stack) {
        gtk_stack_set_visible_child_name(GTK_STACK(s_snap_stack), "snapping");
    }

    s_was_visible_during_snap = s_visible;

    if (s_was_visible_during_snap) {
        overlay_capture_begin();
        int delay = cfg->snap_delay_ms;
        if (delay < 60) delay = 60;
        g_timeout_add(delay, on_snap_timeout, NULL);
    } else {
        g_idle_add(on_snap_timeout, NULL);
    }
}

void window_cleanup(void) {

    if (s_snap_revert_timer > 0) {
        g_source_remove(s_snap_revert_timer);
        s_snap_revert_timer = 0;
    }
    overlay_close_popup_modal();
    if (s_window) {
        gtk_widget_destroy(s_window);
        s_window = NULL;
    }
    s_xid = None;
    s_visible = FALSE;
    xutil_log("WINDOW", "Window cleaned up.");
}
