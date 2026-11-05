#include "window.h"
#include "hotkey.h"
#include "browser.h"
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

static const char *s_svg_back_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#e4e4e7\" stroke-width=\"2.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<line x1=\"19\" y1=\"12\" x2=\"5\" y2=\"12\"/>"
    "<polyline points=\"12 19 5 12 12 5\"/>"
    "</svg>";

static const char *s_svg_home_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#e4e4e7\" stroke-width=\"2.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<path d=\"M3 9.5L12 3l9 6.5V20a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V9.5z\"/>"
    "<polyline points=\"9 22 9 12 15 12 15 22\"/>"
    "</svg>";

static const char *s_svg_ghost_idle =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#e4e4e7\" stroke-width=\"2.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<path d=\"M9 10h.01M15 10h.01M12 2a8 8 0 0 0-8 8v10l3-2 3 2 2-2 2 2 3-2 3 2V10a8 8 0 0 0-8-8z\"/>"
    "</svg>";

static const char *s_svg_ghost_typing =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#38bdf8\" stroke-width=\"2.6\" stroke-linecap=\"round\">"
    "<circle cx=\"12\" cy=\"12\" r=\"9\" stroke-dasharray=\"28\" stroke-dashoffset=\"10\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"2.5\" fill=\"#38bdf8\"/>"
    "</svg>";

static const char *s_svg_check_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#22c55e\" stroke-width=\"2.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<polyline points=\"20 6 9 17 4 12\"/>"
    "</svg>";

static const char *s_svg_failed_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#ef4444\" stroke-width=\"2.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<line x1=\"18\" y1=\"6\" x2=\"6\" y2=\"18\"/>"
    "<line x1=\"6\" y1=\"6\" x2=\"18\" y2=\"18\"/>"
    "</svg>";

static const char *s_svg_camera_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#e4e4e7\" stroke-width=\"2.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<path d=\"M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z\"/>"
    "<circle cx=\"12\" cy=\"13\" r=\"4\"/>"
    "</svg>";

static const char *s_svg_snap_running =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#38bdf8\" stroke-width=\"2.5\" stroke-linecap=\"round\">"
    "<circle cx=\"12\" cy=\"12\" r=\"9\" stroke-dasharray=\"28\" stroke-dashoffset=\"10\"/>"
    "</svg>";

static const char *s_svg_reload_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#a1a1aa\" stroke-width=\"2.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<polyline points=\"23 4 23 10 17 10\"/>"
    "<path d=\"M20.49 15a9 9 0 1 1-2.12-9.36L23 10\"/>"
    "</svg>";

static const char *s_svg_stop_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#a1a1aa\" stroke-width=\"2.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<line x1=\"18\" y1=\"6\" x2=\"6\" y2=\"18\"/>"
    "<line x1=\"6\" y1=\"6\" x2=\"18\" y2=\"18\"/>"
    "</svg>";

static const char *s_svg_search_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#71717a\" stroke-width=\"2.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<circle cx=\"11\" cy=\"11\" r=\"8\"/>"
    "<line x1=\"21\" y1=\"21\" x2=\"16.65\" y2=\"16.65\"/>"
    "</svg>";

static const char *s_svg_close_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#ef4444\" stroke-width=\"2.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<line x1=\"18\" y1=\"6\" x2=\"6\" y2=\"18\"/>"
    "<line x1=\"6\" y1=\"6\" x2=\"18\" y2=\"18\"/>"
    "</svg>";

static const char *s_svg_universal_refresh_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#ffffff\" stroke-width=\"2.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<path d=\"M21.5 2v6h-6\"/>"
    "<path d=\"M21.34 15.57a10 10 0 1 1-.57-8.38l5.67-5.67\"/>"
    "</svg>";

static const char *s_svg_chatgpt_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 64 64\">"
    "<rect x=\"1\" y=\"1\" width=\"62\" height=\"62\" rx=\"16\" ry=\"16\" fill=\"#121214\" stroke=\"#2a2a2a\" stroke-width=\"1.2\"/>"
    "<g transform=\"translate(16, 16) scale(1.3333)\">"
    "<path fill=\"#ffffff\" d=\"M22.2819 9.8211a5.9847 5.9847 0 0 0-.5157-4.9108 6.0462 6.0462 0 0 0-6.5098-2.9A6.0651 6.0651 0 0 0 4.9807 4.1818a5.9847 5.9847 0 0 0-3.9977 2.9 6.0462 6.0462 0 0 0 .7427 7.0966 5.98 5.98 0 0 0 .511 4.9107 6.051 6.051 0 0 0 6.5146 2.9001A5.9847 5.9847 0 0 0 13.2599 24a6.0557 6.0557 0 0 0 5.7718-4.2058 5.9897 5.9897 0 0 0 3.9977-2.9001 6.0557 6.0557 0 0 0-.7475-7.0729zm-9.022 12.6081a4.4755 4.4755 0 0 1-2.8764-1.0408l.1419-.0804 4.7783-2.7582a.7948.7948 0 0 0 .3927-.6813v-6.7369l2.02 1.1683a.071.071 0 0 1 .038.052v5.5826a4.504 4.504 0 0 1-4.4945 4.4947zm-9.66-4.1254a4.4708 4.4708 0 0 1-.5346-3.0137l.142.0852 4.783 2.7582a.7712.7712 0 0 0 .7806 0l5.8428-3.3685v2.3324a.0804.0804 0 0 1-.0332.0615L9.74 19.9502a4.4992 4.4992 0 0 1-6.1402-1.6464zM2.3408 7.8956a4.485 4.485 0 0 1 2.3655-1.9728V11.6a.7664.7664 0 0 0 .3879.6765l5.8144 3.3543-2.0201 1.1683a.0757.0757 0 0 1-.071 0l-4.8303-2.7866A4.504 4.504 0 0 1 2.3408 7.8956zm16.0993 3.8558L12.5973 8.3829l2.02-1.1636a.0757.0757 0 0 1 .071 0l4.8303 2.7913a4.4944 4.4944 0 0 1-.6765 8.1042v-5.6772a.79.79 0 0 0-.4023-.682zm2.0107-3.0231l-.142-.0852-4.7735-2.7818a.7759.7759 0 0 0-.7854 0L9.409 9.2297V6.8974a.0662.0662 0 0 1 .0284-.0615l4.8303-2.7866a4.4992 4.4992 0 0 1 6.6802 4.66zM8.3065 12.863l-2.02-1.1635a.0804.0804 0 0 1-.038-.0567V6.0748a4.4992 4.4992 0 0 1 7.3757-3.4537l-.142.0805L8.704 5.4598a.7948.7948 0 0 0-.3927.6813zm1.0976-2.3654l2.602-1.4998 2.6069 1.4998v2.9994l-2.6069 1.4997-2.602-1.4997z\"/>"
    "</g>"
    "</svg>";

static const char *s_svg_ghost_app =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 64 64\">"
    "<rect x=\"1\" y=\"1\" width=\"62\" height=\"62\" rx=\"17\" fill=\"#1f1f1f\" stroke=\"#2a2a2a\"/>"
    "<g transform=\"translate(16 15) scale(1.35)\" fill=\"none\" stroke=\"#ffffff\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<path d=\"M4 21V10a8 8 0 0 1 16 0v11l-3-2-2 2-3-2-3 2-2-2z\"/><path d=\"M9 10v2m6-2v2\"/>"
    "</g></svg>";

static const char *s_svg_browser_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 64 64\">"
    "<rect x=\"1\" y=\"1\" width=\"62\" height=\"62\" rx=\"17\" fill=\"#f5f6f8\"/>"
    "<g transform=\"translate(10 10)\">"
    "<circle cx=\"22\" cy=\"22\" r=\"21\" fill=\"#34a853\"/>"
    "<path d=\"M22 1a21 21 0 0 1 18.2 10.5H22a10.5 10.5 0 0 0-9.1 15.75L3.8 11.5A21 21 0 0 1 22 1\" fill=\"#ea4335\"/>"
    "<path d=\"M40.2 11.5A21 21 0 0 1 22 43l9.1-15.75A10.5 10.5 0 0 0 22 11.5\" fill=\"#fbbc05\"/>"
    "<circle cx=\"22\" cy=\"22\" r=\"10.5\" fill=\"#f1f3f4\"/>"
    "<circle cx=\"22\" cy=\"22\" r=\"8.5\" fill=\"#4285f4\"/>"
    "</g></svg>";

static const char *s_svg_move_grip =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"8\" viewBox=\"0 0 24 8\" fill=\"none\">"
    "<rect x=\"2\" y=\"2\" width=\"20\" height=\"4\" rx=\"2\" fill=\"#525252\"/>"
    "</svg>";

static const char *s_svg_fullscreen_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"13\" height=\"13\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#a1a1aa\" stroke-width=\"2.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<polyline points=\"15 3 21 3 21 9\"/>"
    "<polyline points=\"9 21 3 21 3 15\"/>"
    "<line x1=\"21\" y1=\"3\" x2=\"14\" y2=\"10\"/>"
    "<line x1=\"3\" y1=\"21\" x2=\"10\" y2=\"14\"/>"
    "</svg>";

static const char *s_svg_top_close_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"13\" height=\"13\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#a1a1aa\" stroke-width=\"2.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<line x1=\"18\" y1=\"6\" x2=\"6\" y2=\"18\"/>"
    "<line x1=\"6\" y1=\"6\" x2=\"18\" y2=\"18\"/>"
    "</svg>";

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
    gboolean themed = !(svg_str == s_svg_chatgpt_icon || svg_str == s_svg_ghost_app || svg_str == s_svg_browser_icon);
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
static GtkWidget *s_status_bar = NULL;
static GtkWidget *s_bottom_nav = NULL;
static GtkWidget *s_home_search_eb = NULL;
static GtkWidget *s_home_sec_lbl = NULL;
static GtkWidget *s_home_apps_box = NULL;
static GtkWidget *s_theme_btn = NULL;
static GtkWidget *s_theme_btn_label = NULL;
static GPtrArray *s_app_labels = NULL;

static gboolean s_capture_hidden = FALSE;
static gboolean s_is_snapping = FALSE;
static guint    s_snap_revert_timer = 0;
static gboolean s_was_visible_during_snap = FALSE;

static GtkWidget *s_window = NULL;
static GtkWidget *s_root_box = NULL;
static GtkWidget *s_overlay = NULL;
static GtkWidget *s_stack = NULL;

static GtkWidget *s_url_entry = NULL;
static GtkWidget *s_progress_bar = NULL;
static GtkWidget *s_header_box = NULL;

static GtkWidget *s_test_entry = NULL;
static GtkWidget *s_ghost_start, *s_ghost_status, *s_ghost_controls, *s_ghost_eta;
static guint s_ghost_eta_timer = 0;
static GtkWidget *s_ghost_human, *s_ghost_safe, *s_ghost_modes[3], *s_ghost_mode_row;
static GtkWidget *s_ghost_count, *s_ghost_placeholder;
static guint s_ghost_countdown = 0;
static int s_ghost_seconds = 0;
static char *s_ghost_text_cache = NULL;

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
static GtkWidget *s_btn_top_fullscreen = NULL;
static GtkWidget *s_btn_top_close = NULL;

static GtkWidget *s_btn_back = NULL;
static GtkWidget *s_btn_reload = NULL;
static GtkWidget *s_reload_stack = NULL;
static GtkWidget *s_btn_home = NULL;
static GtkWidget *s_btn_universal_refresh = NULL;
static GtkWidget *s_btn_ghost = NULL;
static GtkWidget *s_btn_ghost_stack = NULL;
static guint      s_ghost_revert_timer = 0;
static GtkWidget *s_btn_snap = NULL;
static GtkWidget *s_snap_stack = NULL;

static GtkWidget *s_toast_box = NULL;
static GtkWidget *s_toast_label = NULL;
static guint      s_toast_timer_id = 0;

static GtkWidget *s_popup_container = NULL;
static GtkWidget *s_popup_child_view = NULL;

static Window     s_xid = None;
static gboolean   s_visible = FALSE;
static guint      s_clock_timer_id = 0;
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
    ".nav-pill.pill-mirror .nav-pill-dot, .nav-pill.pill-solid .nav-pill-dot { background-color: #737373; }"
    ".nav-pill.pill-excl .nav-pill-dot { background-color: #fde68a; }"
    ".nav-pill.pill-ghost .nav-pill-dot { background-color: #a855f7; }"
    ".nav-pill.pill-grab-alert .nav-pill-dot { background-color: #ef4444; }"
    /* ---- window controls ---- */
    ".move-capsule { padding: 4px 8px; border-radius: 6px; background: transparent; }"
    ".move-capsule:hover { background-color: #161616; }"
    ".top-ctrl-btn { background: transparent; background-image: none; box-shadow: none; border: none; padding: 3px; min-width: 20px; min-height: 20px; border-radius: 6px; }"
    ".top-ctrl-btn:hover { background-color: #1f1f1f; }"
    ".top-close-btn:hover { background-color: #ef4444; }"
    ".theme-btn { background: transparent; background-image: none; box-shadow: none; border: 1px solid #262626; border-radius: 6px; padding: 1px 7px; min-height: 0; }"
    ".theme-btn label { color: #e5e5e5; font-size: 10px; }"
    ".theme-btn:hover { background-color: #1f1f1f; }"
    ".resize-grip { opacity: 0.4; padding: 2px; }"
    ".resize-grip:hover { opacity: 1.0; }"
    /* ---- browser header ---- */
    ".header-box { padding: 6px 10px 8px; background-color: #000000; border-bottom: 1px solid #1a1a1a; }"
    ".url-bar-container { background-color: #121212; border: 1px solid #262626; border-radius: 8px; padding: 1px 4px 1px 10px; }"
    ".url-bar-container:focus-within { border-color: #ffffff; }"
    ".url-leading-icon { opacity: 0.5; }"
    ".url-entry { background-color: transparent; color: #ffffff; border: none; box-shadow: none; padding: 5px 6px; font-size: 12px; caret-color: #ffffff; }"
    ".url-entry:focus { background-color: transparent; border: none; box-shadow: none; }"
    ".url-action-btn { background: transparent; background-image: none; box-shadow: none; border: none; padding: 4px 6px; border-radius: 6px; }"
    ".url-action-btn:hover { background-color: #222222; }"
    ".load-progress, .load-progress trough { min-height: 2px; padding: 0; margin: 0; background: transparent; border: none; }"
    ".load-progress progress { min-height: 2px; border: none; border-radius: 0; background-color: #ffffff; }"
    /* ---- home ---- */
    ".home-page { background-color: #000000; }"
    ".home-search-bar { background-color: #121212; border: 1px solid #262626; border-radius: 8px; padding: 9px 12px; }"
    ".home-search-bar:hover { border-color: #525252; }"
    ".home-search-label { font-size: 12px; color: #737373; }"
    ".home-section-title { font-size: 9px; font-weight: 600; color: #666666; letter-spacing: 1px; }"
    ".app-button { background: transparent; background-image: none; box-shadow: none; border: none; padding: 8px 6px; margin: 0; border-radius: 10px; }"
    ".app-button:hover { background-color: #161616; }"
    ".app-button:active { background-color: #222222; }"
    ".app-label { font-size: 11px; font-weight: 400; color: #e5e5e5; margin-top: 5px; }"
    /* ---- bottom bar ---- */
    ".test-input-box { padding: 4px 10px; background-color: #000000; border-top: 1px solid #1a1a1a; }"
    ".test-input-entry { background-color: #121212; color: #ffffff; border: 1px solid #262626; border-radius: 8px; padding: 5px 10px; font-size: 11px; caret-color: #ffffff; box-shadow: none; }"
    ".test-input-entry:focus { border-color: #ffffff; }"
    ".bottom-nav { background-color: #000000; padding: 5px 10px 7px; border-top: 1px solid #1a1a1a; border-radius: 0 0 21px 21px; }"
    ".nav-button { background: transparent; background-image: none; box-shadow: none; border: none; padding: 5px 4px; min-height: 34px; min-width: 0; border-radius: 8px; }"
    ".nav-button label { font-size: 9px; color: #737373; }"
    ".nav-button:hover { background-color: #161616; }"
    ".nav-button:hover label { color: #d4d4d4; }"
    ".nav-button:active { background-color: #222222; }"
    ".nav-button:disabled { opacity: 0.3; }"
    ".nav-button.nav-selected { background-color: #1c1c1c; }"
    ".nav-button.nav-selected label { color: #ffffff; font-weight: 600; }"
    ".nav-refresh-btn { background: transparent; }"
    /* ---- toast / popup ---- */
    ".toast-box { background-color: #ffffff; border: none; border-radius: 8px; padding: 7px 16px; }"
    ".toast-label { color: #000000; font-size: 11.5px; font-weight: 600; }"
    ".popup-modal-layer { background-color: #000000; border-radius: 18px; }"
    ".popup-modal-bar { padding: 7px 12px; background-color: #0a0a0a; border-bottom: 1px solid #1a1a1a; }"
    ".popup-modal-title { color: #ffffff; font-size: 12px; font-weight: 600; }"
    ".popup-close-btn { background: transparent; border: none; padding: 4px 8px; border-radius: 6px; }"
    ".popup-close-btn:hover { background-color: #ef4444; }"
    /* ---- ghost writer ---- */
    ".ghost-page { background-color: #000000; color: #e5e5e5; }"
    ".ghost-page label { color: #a0a0a0; font-size: 11px; }"
    ".ghost-page .ghost-title { color: #ffffff; font-size: 20px; font-weight: 600; }"
    ".ghost-page .section-label { color: #737373; font-size: 9px; font-weight: 600; letter-spacing: 1px; }"
    ".ghost-page .editor-frame { background-color: #0c0c0c; border: 1px solid #222222; border-radius: 8px; }"
    ".ghost-page scrolledwindow { border-radius: 8px; }"
    ".ghost-page textview, .ghost-page textview text { background-color: transparent; color: #ffffff; font-size: 12px; caret-color: #ffffff; }"
    ".ghost-page textview text selection { background-color: #ffffff; color: #000000; }"
    ".ghost-page .eta { color: #a0a0a0; font-size: 10px; border: 1px solid #333333; border-radius: 6px; padding: 1px 7px; background: none; }"
    ".ghost-page .dim-label { color: #737373; font-size: 10px; }"
    ".ghost-page scrolledwindow, .ghost-page viewport { background-color: transparent; }"
    ".ghost-page .mode-btn { background-color: #121212; background-image: none; box-shadow: none; color: #a0a0a0; border: 1px solid #262626; border-radius: 8px; padding: 7px 4px; }"
    ".ghost-page .mode-btn label { color: #a0a0a0; font-size: 11px; }"
    ".ghost-page .mode-btn:hover { background-color: #1a1a1a; }"
    ".ghost-page .mode-btn:checked { background-color: #262626; border-color: #525252; }"
    ".ghost-page .mode-btn:checked label { color: #ffffff; font-weight: 600; }"
    ".ghost-page switch { background-color: #262626; background-image: none; border: none; border-radius: 999px; box-shadow: none; min-width: 38px; min-height: 20px; }"
    ".ghost-page switch:checked { background-color: #22c55e; }"
    ".ghost-page switch slider { background-color: #ffffff; background-image: none; border: none; border-radius: 999px; min-width: 16px; min-height: 16px; margin: 2px; box-shadow: none; }"
    ".ghost-page .editor-hint { color: #666666; font-family: monospace; font-size: 12px; }"
    ".ghost-page button { background-color: #121212; background-image: none; box-shadow: none; text-shadow: none; color: #e5e5e5; border: 1px solid #262626; border-radius: 8px; padding: 6px 10px; }"
    ".ghost-page button:hover { background-color: #1c1c1c; border-color: #404040; }"
    ".ghost-page button:active { background-color: #262626; }"
    ".ghost-page .quiet-button { background: transparent; border-color: transparent; padding: 3px 6px; }"
    ".ghost-page .quiet-button label { color: #a0a0a0; font-size: 10px; }"
    ".ghost-page .start-typing, .ghost-page button.suggested-action { background-color: #ffffff; background-image: none; border: 1px solid #ffffff; border-radius: 8px; padding: 10px; box-shadow: none; }"
    ".ghost-page .start-typing:hover, .ghost-page button.suggested-action:hover { background-color: #d4d4d4; border-color: #d4d4d4; }"
    ".ghost-page .start-typing label, .ghost-page button.suggested-action label { color: #000000; font-size: 12px; font-weight: 600; }"
    ".ghost-page spinbutton { background-color: #121212; color: #e5e5e5; border: 1px solid #262626; border-radius: 6px; box-shadow: none; font-size: 11px; min-height: 30px; }"
    ".ghost-page spinbutton button { background: transparent; border: none; padding: 1px; min-width: 11px; }"
    ".ghost-page spinbutton entry { color: #e5e5e5; min-width: 28px; padding: 2px; }"
    ".ghost-page check { background-color: #121212; border: 1px solid #525252; border-radius: 4px; box-shadow: none; }"
    ".ghost-page check:checked { background-color: #ffffff; background-image: none; color: #000000; border-color: #ffffff; }"
    ".ghost-page expander { padding: 8px 0; }"
    ".ghost-page expander arrow { color: #a0a0a0; }"
    ".ghost-page scrollbar trough { background: transparent; }"
    ".ghost-page scrollbar slider { background-color: #333333; min-width: 4px; border: none; border-radius: 4px; }"
    ".ghost-page scrollbar slider:hover { background-color: #525252; }";

static void update_x11_state_properties(void) {
    if (s_xid == None) return;
    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    Atom prop_state = XInternAtom(xdpy, "_CRONQ_STATE", False);
    Atom prop_text = XInternAtom(xdpy, "_CRONQ_ENTRY_TEXT", False);
    Atom prop_url = XInternAtom(xdpy, "_CRONQ_URL_TEXT", False);
    Atom prop_app = XInternAtom(xdpy, "_CRONQ_APP", False);
    Atom utf8_string = XInternAtom(xdpy, "UTF8_STRING", False);

    char state_str[128];
    snprintf(state_str, sizeof(state_str), "MODE=%s TARGET=%s GHOST=%s GRAB=%s APP=%s",
             input_get_key_mode() == KEY_MODE_EXCLUSIVE ? "EXCLUSIVE" : "MIRROR",
             input_get_typing_target() == TYPING_TARGET_OVERLAY ? "OVERLAY" : "NONE",
             input_get_ghost_mode() == GHOST_MODE_ON ? "GHOST" : "SOLID",
             input_get_grab_status() == GRAB_STATUS_ACTIVE ? "ACTIVE" :
             (input_get_grab_status() == GRAB_STATUS_FAILED ? "FAILED" : "NONE"),
             s_current_app);

    XChangeProperty(xdpy, s_xid, prop_state, utf8_string, 8, PropModeReplace,
                    (unsigned char *)state_str, strlen(state_str));

    const char *text = overlay_get_entry_text();
    XChangeProperty(xdpy, s_xid, prop_text, utf8_string, 8, PropModeReplace,
                    (unsigned char *)text, strlen(text));

    const char *url = s_url_entry ? gtk_entry_get_text(GTK_ENTRY(s_url_entry)) : "";
    XChangeProperty(xdpy, s_xid, prop_url, utf8_string, 8, PropModeReplace,
                    (unsigned char *)url, strlen(url));

    XChangeProperty(xdpy, s_xid, prop_app, utf8_string, 8, PropModeReplace,
                    (unsigned char *)s_current_app, strlen(s_current_app));
}

void overlay_update_status_ui(void) {
    if (!s_window) return;

    /* 1. Mode Pill */
    if (s_mode_pill_box && s_mode_pill) {
        KeyMode mode = input_get_key_mode();
        GtkStyleContext *sc = gtk_widget_get_style_context(s_mode_pill_box);
        gtk_style_context_remove_class(sc, "pill-mirror");
        gtk_style_context_remove_class(sc, "pill-excl");

        if (mode == KEY_MODE_EXCLUSIVE) {
            gtk_label_set_text(GTK_LABEL(s_mode_pill), "Exclusive");
            gtk_style_context_add_class(sc, "pill-excl");
            gtk_widget_set_tooltip_text(gtk_widget_get_parent(s_mode_pill_box), "Exclusive (click for Mirror)");
        } else {
            gtk_label_set_text(GTK_LABEL(s_mode_pill), "Mirror");
            gtk_style_context_add_class(sc, "pill-mirror");
            gtk_widget_set_tooltip_text(gtk_widget_get_parent(s_mode_pill_box), "Mirror (click for Exclusive)");
        }
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
            gtk_widget_set_tooltip_text(gtk_widget_get_parent(s_ghost_pill_box), "Ghost (click for Solid)");
        } else {
            gtk_label_set_text(GTK_LABEL(s_ghost_pill_label), "Solid");
            gtk_style_context_add_class(sc, "pill-solid");
            gtk_widget_set_tooltip_text(gtk_widget_get_parent(s_ghost_pill_box), "Solid (click for Ghost)");
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

    if (s_btn_home && s_btn_ghost) {
        GtkStyleContext *home = gtk_widget_get_style_context(s_btn_home);
        GtkStyleContext *ghost = gtk_widget_get_style_context(s_btn_ghost);
        gtk_style_context_remove_class(home, "nav-selected");
        gtk_style_context_remove_class(ghost, "nav-selected");
        if (strcmp(s_current_app, "home") == 0) gtk_style_context_add_class(home, "nav-selected");
        if (strcmp(s_current_app, "ghost") == 0) gtk_style_context_add_class(ghost, "nav-selected");
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

static gboolean on_clock_tick(gpointer user_data) {
    (void)user_data;
    if (s_visible) {
        overlay_update_status_ui();
    }
    return G_SOURCE_CONTINUE;
}

static void calculate_monitor_position(int *out_x, int *out_y) {
    const AppConfig *cfg = config_get();
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *mon = NULL;

    if (g_ascii_strcasecmp(cfg->monitor, "primary") == 0) {
        mon = gdk_display_get_primary_monitor(display);
    } else {
        int idx = atoi(cfg->monitor);
        int n_mon = gdk_display_get_n_monitors(display);
        if (idx >= 0 && idx < n_mon) {
            mon = gdk_display_get_monitor(display, idx);
        }
    }
    if (!mon) {
        mon = gdk_display_get_primary_monitor(display);
        if (!mon && gdk_display_get_n_monitors(display) > 0) {
            mon = gdk_display_get_monitor(display, 0);
        }
    }

    GdkRectangle geom = {0, 0, 1920, 1080};
    if (mon) {
        gdk_monitor_get_geometry(mon, &geom);
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
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *mon = display ? gdk_display_get_primary_monitor(display) : NULL;
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

void overlay_set_size(int width, int height) {
    if (!s_window) return;

    int screen_w = 1920, screen_h = 1080;
    get_monitor_bounds(&screen_w, &screen_h);

    if (width < OVERLAY_MIN_WIDTH) width = OVERLAY_MIN_WIDTH;
    if (width > screen_w) width = screen_w;
    if (height < OVERLAY_MIN_HEIGHT) height = OVERLAY_MIN_HEIGHT;
    if (height > screen_h) height = screen_h;

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
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *mon = display ? gdk_display_get_primary_monitor(display) : NULL;
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
    const AppConfig *cfg = config_get();
    int def_w = (cfg && cfg->width > 0) ? cfg->width : OVERLAY_WIDTH;
    int def_h = (cfg && cfg->height > 0) ? cfg->height : OVERLAY_HEIGHT;

    s_user_moved = FALSE;
    calculate_monitor_position(&s_pos_x, &s_pos_y);
    gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);
    overlay_set_size(def_w, def_h);
    s_is_maximized = FALSE;
    overlay_show_toast("Default size (360x640)");
}

void overlay_set_compact_mode(gboolean compact) {
    if (!s_window) return;
    if (s_is_maximized) return;

    int target_h = compact ? OVERLAY_COMPACT_HEIGHT : OVERLAY_EXPANDED_HEIGHT;
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

static void on_url_entry_activate(GtkEntry *entry, gpointer user_data) {
    (void)user_data;
    const char *text = gtk_entry_get_text(entry);
    if (text && text[0] != '\0') {
        browser_load_in_browser(text);
        gtk_widget_grab_focus(GTK_WIDGET(browser_get_browser_view()));
    }
}

static void on_test_entry_changed(GtkTextBuffer *editable, gpointer user_data) {
    (void)user_data;
    int count = gtk_text_buffer_get_char_count(editable);
    if (s_ghost_count) {
        char text[96];
        int lines = gtk_text_buffer_get_line_count(editable);
        g_snprintf(text, sizeof(text), "%d chars · %d line%s", count, lines, lines == 1 ? "" : "s");
        gtk_label_set_text(GTK_LABEL(s_ghost_count), text);
    }
    if (s_ghost_placeholder) gtk_widget_set_visible(s_ghost_placeholder, count == 0);
    update_x11_state_properties();
}

static void on_browser_uri_changed(const char *uri, gpointer user_data) {
    (void)user_data;
    if (s_url_entry && !gtk_widget_has_focus(s_url_entry)) {
        gtk_entry_set_text(GTK_ENTRY(s_url_entry), uri);
    }
    update_x11_state_properties();
}

static void on_browser_progress_changed(double progress, gpointer user_data) {
    (void)user_data;
    if (s_progress_bar) {
        if (progress >= 1.0 || progress <= 0.0) {
            gtk_widget_set_visible(s_progress_bar, FALSE);
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(s_progress_bar), 0.0);
        } else {
            gtk_widget_set_visible(s_progress_bar, TRUE);
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(s_progress_bar), progress);
        }
    }
}

static void on_browser_state_changed(bool is_loading, bool can_back, bool can_forward, gpointer user_data) {
    (void)user_data;
    (void)can_back;
    (void)can_forward;
    if (s_reload_stack) {
        gtk_stack_set_visible_child_name(GTK_STACK(s_reload_stack), is_loading ? "stop" : "reload");
    }
}

static void on_browser_clicked(gpointer user_data) {
    (void)user_data;
    input_notify_native_click(NULL);
}

void overlay_switch_app(const char *app_name) {
    if (!s_stack || !app_name) return;

    if (g_ascii_strcasecmp(app_name, "chatgpt") == 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(s_stack), "chatgpt");
        g_strlcpy(s_current_app, "chatgpt", sizeof(s_current_app));
        browser_set_active_view("chatgpt");
        overlay_set_compact_mode(FALSE);
        xutil_log("WINDOW", "Switched view to ChatGPT app.");
    } else if (g_ascii_strcasecmp(app_name, "browser") == 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(s_stack), "browser");
        g_strlcpy(s_current_app, "browser", sizeof(s_current_app));
        browser_set_active_view("browser");
        overlay_set_compact_mode(FALSE);
        xutil_log("WINDOW", "Switched view to Browser app.");
    } else if (g_ascii_strcasecmp(app_name, "ghost") == 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(s_stack), "ghost");
        g_strlcpy(s_current_app, "ghost", sizeof(s_current_app));
        overlay_set_compact_mode(FALSE);
    } else {
        gtk_stack_set_visible_child_name(GTK_STACK(s_stack), "home");
        g_strlcpy(s_current_app, "home", sizeof(s_current_app));
        browser_set_active_view("browser");
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
    if (strcmp(s_current_app, "chatgpt") == 0) {
        WebKitWebView *cg_view = browser_get_chatgpt_view();
        if (cg_view && webkit_web_view_can_go_back(cg_view)) {
            webkit_web_view_go_back(cg_view);
        } else {
            overlay_go_home();
        }
    } else if (strcmp(s_current_app, "browser") == 0) {
        WebKitWebView *br_view = browser_get_browser_view();
        if (br_view && webkit_web_view_can_go_back(br_view)) {
            webkit_web_view_go_back(br_view);
        } else {
            overlay_go_home();
        }
    } else {
        overlay_go_home();
    }
}

void overlay_reload(void) {
    if (strcmp(s_current_app, "chatgpt") == 0) {
        WebKitWebView *cg_view = browser_get_chatgpt_view();
        if (cg_view) {
            if (webkit_web_view_is_loading(cg_view)) webkit_web_view_stop_loading(cg_view);
            else webkit_web_view_reload(cg_view);
        }
    } else if (strcmp(s_current_app, "browser") == 0) {
        if (browser_is_loading()) browser_stop();
        else browser_reload();
    } else {
        if (s_window) gtk_widget_queue_draw(s_window);
    }
}

void overlay_universal_reset(void) {
    xutil_log("WINDOW", "Triggering Universal Reset (returning to factory-fresh state)...");

    /* 1. Close any open popup modals */
    overlay_close_popup_modal();

    /* 2. Clear and hide test/ghost input entry and URL bar */
    if (s_test_entry) {
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(s_test_entry)), "", -1);
    }
    if (s_ghost_countdown) { g_source_remove(s_ghost_countdown); s_ghost_countdown = 0; }
    ghost_writer_cancel();
    if (s_url_entry) {
        gtk_entry_set_text(GTK_ENTRY(s_url_entry), "");
    }

    s_user_moved = FALSE;

    /* 3. Reset input states (mirror mode, no target, ghost mode off) */
    input_release_grab();
    input_set_typing_target(TYPING_TARGET_NONE);
    input_set_key_mode(KEY_MODE_MIRROR);
    input_set_ghost_mode(GHOST_MODE_OFF);
    overlay_set_ghost(FALSE);
    overlay_set_ghost_button_state(GHOST_BTN_IDLE);

    /* 4. Reset browser subsystem (recreates web views with clean history) */
    browser_universal_reset();

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

static void on_btn_reload_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    overlay_reload();
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

void overlay_toggle_ghost_input(void) {
    overlay_switch_app("ghost");
    if (!ghost_is_typing() && !s_ghost_countdown) {
        gtk_widget_grab_focus(s_test_entry);
        input_set_typing_target(TYPING_TARGET_OVERLAY);
    }
}

static void on_btn_ghost_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    overlay_toggle_ghost_input();
}

void overlay_ghost_progress(size_t typed, size_t total) {
    if (!s_ghost_status) return;
    char text[96];
    g_snprintf(text, sizeof(text), "Typing %zu / %zu", typed, total);
    gtk_label_set_text(GTK_LABEL(s_ghost_status), text);
}

static void ghost_settings_save(void) {
    GhostTiming t = ghost_get_timing();
    t.human = gtk_switch_get_active(GTK_SWITCH(s_ghost_human));
    t.safe = gtk_switch_get_active(GTK_SWITCH(s_ghost_safe));
    for (int i = 0; i < 3; i++)
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s_ghost_modes[i]))) t.speed = i;
    ghost_set_timing(t);
}

static void on_ghost_mode_toggled(GtkToggleButton *button, gpointer data) {
    (void)data;
    if (gtk_toggle_button_get_active(button)) ghost_settings_save();
}

static void on_ghost_safe_toggled(GObject *sw, GParamSpec *pspec, gpointer data) {
    (void)sw; (void)pspec; (void)data;
    ghost_settings_save();
}

static void on_ghost_human_toggled(GObject *sw, GParamSpec *pspec, gpointer data) {
    (void)sw; (void)pspec; (void)data;
    gtk_widget_set_visible(s_ghost_mode_row, gtk_switch_get_active(GTK_SWITCH(s_ghost_human)));
    ghost_settings_save();
}

static gboolean ghost_eta_tick(gpointer data) {
    (void)data;
    int ms = ghost_eta_remaining_ms();
    if (ms < 0) {
        gtk_widget_hide(s_ghost_eta);
        return G_SOURCE_CONTINUE;   /* countdown still running; stopped by button state */
    }
    int sec = (ms + 999) / 1000;
    char text[32];
    if (sec < 60) g_snprintf(text, sizeof(text), "est: %ds", MAX(sec, 1));
    else if (sec < 3600) g_snprintf(text, sizeof(text), "est: %dm %02ds", sec / 60, sec % 60);
    else g_snprintf(text, sizeof(text), "est: %dh %02dm", sec / 3600, (sec % 3600) / 60);
    gtk_label_set_text(GTK_LABEL(s_ghost_eta), text);
    gtk_widget_show(s_ghost_eta);
    return G_SOURCE_CONTINUE;
}

static gboolean ghost_countdown_tick(gpointer data) {
    (void)data;
    if (--s_ghost_seconds > 0) {
        char text[96];
        g_snprintf(text, sizeof(text), "Starting in %d… click the target", s_ghost_seconds);
        gtk_label_set_text(GTK_LABEL(s_ghost_status), text);
        return G_SOURCE_CONTINUE;
    }
    s_ghost_countdown = 0;
    ghost_writer_run();
    return G_SOURCE_REMOVE;
}

static void on_ghost_start(GtkButton *button, gpointer data) {
    (void)button; (void)data;
    if (s_ghost_countdown) {
        g_source_remove(s_ghost_countdown); s_ghost_countdown = 0;
        overlay_set_ghost_button_state(GHOST_BTN_IDLE); return;
    }
    if (ghost_is_typing()) { ghost_writer_cancel(); return; }
    if (!*overlay_get_test_entry_text()) { overlay_show_toast("Nothing to type"); return; }
    ghost_settings_save();
    input_release_grab(); input_set_typing_target(TYPING_TARGET_NONE);
    hotkey_remove_clipboard_shield();
    overlay_set_ghost_button_state(GHOST_BTN_TYPING);
    gtk_button_set_label(GTK_BUTTON(s_ghost_start), "Cancel");
    gtk_label_set_text(GTK_LABEL(s_ghost_status), "Starting in 3… click the target");
    s_ghost_seconds = 3;
    s_ghost_countdown = g_timeout_add(1000, ghost_countdown_tick, NULL);
}

static GtkWidget *ghost_label(const char *text) {
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    return label;
}

static GtkWidget *create_ghost_page(void) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(page), 14);
    gtk_style_context_add_class(gtk_widget_get_style_context(page), "ghost-page");

    /* Everything but status + Start scrolls, so no control is ever out of reach. */
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *page_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(page_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(page_scroll), 80);
    gtk_container_add(GTK_CONTAINER(page_scroll), content);
    gtk_box_pack_start(GTK_BOX(page), page_scroll, TRUE, TRUE, 0);

    GtkWidget *title = ghost_label("Ghost Typer");
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "ghost-title");
    gtk_box_pack_start(GTK_BOX(content), title, FALSE, FALSE, 0);

    /* Editor grows with its text between a small minimum and a cap. */
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 72);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 180);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
    s_test_entry = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(s_test_entry), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(s_test_entry), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(s_test_entry), 12);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(s_test_entry), 12);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(s_test_entry), 10);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(s_test_entry), 10);
    g_signal_connect(s_test_entry, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    g_signal_connect(gtk_text_view_get_buffer(GTK_TEXT_VIEW(s_test_entry)), "changed", G_CALLBACK(on_test_entry_changed), NULL);
    gtk_container_add(GTK_CONTAINER(scroll), s_test_entry);
    GtkWidget *editor = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(editor), scroll);
    GtkWidget *frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(frame), "editor-frame");
    gtk_box_pack_start(GTK_BOX(frame), editor, TRUE, TRUE, 0);
    s_ghost_placeholder = ghost_label("Type here");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_ghost_placeholder), "editor-hint");
    gtk_widget_set_halign(s_ghost_placeholder, GTK_ALIGN_START);
    gtk_widget_set_valign(s_ghost_placeholder, GTK_ALIGN_START);
    gtk_widget_set_margin_start(s_ghost_placeholder, 13);
    gtk_widget_set_margin_top(s_ghost_placeholder, 11);
    gtk_overlay_add_overlay(GTK_OVERLAY(editor), s_ghost_placeholder);
    gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(editor), s_ghost_placeholder, TRUE);
    gtk_widget_set_no_show_all(s_ghost_placeholder, TRUE);
    gtk_widget_show(s_ghost_placeholder);
    gtk_box_pack_start(GTK_BOX(content), frame, FALSE, FALSE, 0);
    s_ghost_count = ghost_label("0 chars · 1 line");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_ghost_count), "dim-label");
    gtk_box_pack_start(GTK_BOX(content), s_ghost_count, FALSE, FALSE, 0);

    s_ghost_controls = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GhostTiming saved = ghost_get_timing();

    GtkWidget *human_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(human_row), ghost_label("Human-like"), TRUE, TRUE, 0);
    s_ghost_human = gtk_switch_new();
    gtk_widget_set_can_focus(s_ghost_human, FALSE);
    gtk_widget_set_valign(s_ghost_human, GTK_ALIGN_CENTER);
    gtk_switch_set_active(GTK_SWITCH(s_ghost_human), saved.human);
    gtk_box_pack_start(GTK_BOX(human_row), s_ghost_human, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(s_ghost_controls), human_row, FALSE, FALSE, 0);

    /* Everything that only applies to human typing hides with the switch. */
    s_ghost_mode_row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *safe_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(safe_row), ghost_label("Extra safe"), TRUE, TRUE, 0);
    s_ghost_safe = gtk_switch_new();
    gtk_widget_set_can_focus(s_ghost_safe, FALSE);
    gtk_widget_set_valign(s_ghost_safe, GTK_ALIGN_CENTER);
    gtk_switch_set_active(GTK_SWITCH(s_ghost_safe), saved.safe);
    gtk_box_pack_start(GTK_BOX(safe_row), s_ghost_safe, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(s_ghost_mode_row), safe_row, FALSE, FALSE, 0);
    GtkWidget *modes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_set_homogeneous(GTK_BOX(modes), TRUE);
    const char *names[] = {"Slow", "Moderate", "Fast"};
    for (int i = 0; i < 3; i++) {
        s_ghost_modes[i] = i ? gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(s_ghost_modes[0]), names[i])
                             : gtk_radio_button_new_with_label(NULL, names[i]);
        gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(s_ghost_modes[i]), FALSE);
        gtk_widget_set_can_focus(s_ghost_modes[i], FALSE);
        gtk_style_context_add_class(gtk_widget_get_style_context(s_ghost_modes[i]), "mode-btn");
        gtk_box_pack_start(GTK_BOX(modes), s_ghost_modes[i], TRUE, TRUE, 0);
    }
    gtk_box_pack_start(GTK_BOX(s_ghost_mode_row), modes, FALSE, FALSE, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s_ghost_modes[saved.speed]), TRUE);
    for (int i = 0; i < 3; i++) {
        g_signal_connect(s_ghost_modes[i], "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
        g_signal_connect(s_ghost_modes[i], "toggled", G_CALLBACK(on_ghost_mode_toggled), NULL);
    }
    g_signal_connect(s_ghost_safe, "notify::active", G_CALLBACK(on_ghost_safe_toggled), NULL);
    g_signal_connect(s_ghost_safe, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_widget_show_all(s_ghost_mode_row);
    gtk_widget_set_no_show_all(s_ghost_mode_row, TRUE);
    gtk_widget_set_visible(s_ghost_mode_row, saved.human);
    gtk_box_pack_start(GTK_BOX(s_ghost_controls), s_ghost_mode_row, FALSE, FALSE, 0);
    g_signal_connect(s_ghost_human, "notify::active", G_CALLBACK(on_ghost_human_toggled), NULL);
    g_signal_connect(s_ghost_human, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(content), s_ghost_controls, FALSE, FALSE, 0);

    GtkWidget *status_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    s_ghost_status = ghost_label("Ready");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_ghost_status), "dim-label");
    gtk_box_pack_start(GTK_BOX(status_row), s_ghost_status, TRUE, TRUE, 0);
    s_ghost_eta = gtk_label_new("");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_ghost_eta), "eta");
    gtk_widget_set_no_show_all(s_ghost_eta, TRUE);
    gtk_box_pack_end(GTK_BOX(status_row), s_ghost_eta, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), status_row, FALSE, FALSE, 0);
    s_ghost_start = gtk_button_new_with_label("Start");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_ghost_start), "start-typing");
    gtk_widget_set_can_focus(s_ghost_start, FALSE);
    g_signal_connect(s_ghost_start, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    g_signal_connect(s_ghost_start, "clicked", G_CALLBACK(on_ghost_start), NULL);
    gtk_box_pack_start(GTK_BOX(page), s_ghost_start, FALSE, FALSE, 0);
    return page;
}

static gboolean on_ghost_revert_btn_label(gpointer user_data) {
    (void)user_data;
    s_ghost_revert_timer = 0;
    if (s_btn_ghost_stack) {
        gtk_stack_set_visible_child_name(GTK_STACK(s_btn_ghost_stack), "idle");
    }
    return G_SOURCE_REMOVE;
}

void overlay_set_ghost_button_state(GhostButtonState state) {
    gboolean typing = state == GHOST_BTN_TYPING;
    if (s_ghost_start) gtk_button_set_label(GTK_BUTTON(s_ghost_start), typing ? "Stop" : "Start");
    if (s_ghost_controls) gtk_widget_set_sensitive(s_ghost_controls, !typing);
    if (s_ghost_eta) {
        if (typing && !s_ghost_eta_timer) s_ghost_eta_timer = g_timeout_add(500, ghost_eta_tick, NULL);
        if (!typing) {
            if (s_ghost_eta_timer) { g_source_remove(s_ghost_eta_timer); s_ghost_eta_timer = 0; }
            gtk_widget_hide(s_ghost_eta);
        }
    }
    if (s_test_entry) gtk_text_view_set_editable(GTK_TEXT_VIEW(s_test_entry), !typing);
    if (s_ghost_status) gtk_label_set_text(GTK_LABEL(s_ghost_status),
        typing ? "Typing…" :
        state == GHOST_BTN_DONE ? "Done" :
        state == GHOST_BTN_FAILED ? "Stopped" : "Ready");
    if (!s_btn_ghost_stack) return;

    if (s_ghost_revert_timer > 0) {
        g_source_remove(s_ghost_revert_timer);
        s_ghost_revert_timer = 0;
    }

    switch (state) {
        case GHOST_BTN_IDLE:
            gtk_stack_set_visible_child_name(GTK_STACK(s_btn_ghost_stack), "idle");
            break;
        case GHOST_BTN_TYPING:
            gtk_stack_set_visible_child_name(GTK_STACK(s_btn_ghost_stack), "typing");
            break;
        case GHOST_BTN_DONE:
            gtk_stack_set_visible_child_name(GTK_STACK(s_btn_ghost_stack), "done");
            s_ghost_revert_timer = g_timeout_add(1200, on_ghost_revert_btn_label, NULL);
            break;
        case GHOST_BTN_FAILED:
            gtk_stack_set_visible_child_name(GTK_STACK(s_btn_ghost_stack), "failed");
            s_ghost_revert_timer = g_timeout_add(1200, on_ghost_revert_btn_label, NULL);
            break;
    }
}

static gboolean on_toast_dismiss_timeout(gpointer user_data) {
    (void)user_data;
    s_toast_timer_id = 0;
    if (s_toast_box) {
        gtk_widget_hide(s_toast_box);
    }
    return G_SOURCE_REMOVE;
}

void overlay_show_toast(const char *msg) {
    if (theme_is_camo()) return;   /* camouflage shows no toasts, as on Windows */
    if (!s_toast_box || !s_toast_label || !msg) return;

    gtk_label_set_text(GTK_LABEL(s_toast_label), msg);
    gtk_widget_show(s_toast_label);
    gtk_widget_show(s_toast_box);

    if (s_toast_timer_id > 0) {
        g_source_remove(s_toast_timer_id);
    }
    s_toast_timer_id = g_timeout_add(2200, on_toast_dismiss_timeout, NULL);
}

static GtkWidget *create_nav_pill(GtkWidget **out_box, GtkWidget **out_dot, GtkWidget **out_label, const char *initial_text) {
    GtkWidget *eb = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(eb), FALSE);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "nav-pill");

    GtkWidget *dot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
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
    input_cycle_key_mode();
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

static void on_btn_top_fullscreen_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    overlay_set_fullscreen();
}

static void on_btn_top_close_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    overlay_hide();
}


static void on_app_icon_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    const char *app = (const char *)user_data;
    if (g_ascii_strcasecmp(app, "reset") == 0) {
        overlay_universal_reset();
        return;
    }
    if (strcmp(app, "ghost") == 0) overlay_toggle_ghost_input();
    else overlay_switch_app(app);
}

static void on_home_search_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget; (void)event; (void)user_data;
    overlay_switch_app("browser");
    if (s_url_entry) {
        gtk_widget_grab_focus(s_url_entry);
    }
}

static GtkWidget *create_app_icon_widget(const char *label_text, const char *app_id) {
    GtkWidget *btn = gtk_button_new();
    gtk_widget_set_can_focus(btn, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(btn), "app-button");

    GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(col, GTK_ALIGN_CENTER);

    GtkWidget *img = NULL;
    if (strcmp(app_id, "chatgpt") == 0) {
        img = create_svg_image(s_svg_chatgpt_icon, 50, 50);
    } else if (strcmp(app_id, "ghost") == 0) {
        img = create_svg_image(s_svg_ghost_app, 50, 50);
    } else {
        img = create_svg_image(s_svg_browser_icon, 50, 50);
    }
    gtk_box_pack_start(GTK_BOX(col), img, FALSE, FALSE, 0);

    GtkWidget *lbl = gtk_label_new(label_text);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "app-label");
    gtk_box_pack_start(GTK_BOX(col), lbl, FALSE, FALSE, 0);
    if (!s_app_labels) s_app_labels = g_ptr_array_new();
    g_ptr_array_add(s_app_labels, lbl);

    gtk_container_add(GTK_CONTAINER(btn), col);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_app_icon_clicked), (gpointer)app_id);
    g_signal_connect(btn, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);

    return btn;
}

static GtkWidget *create_home_page(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "home-page");

    /* Top spacing (compact) */
    GtkWidget *top_spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(top_spacer, -1, 8);
    gtk_box_pack_start(GTK_BOX(box), top_spacer, FALSE, FALSE, 0);

    /* Search Bar Launcher */
    GtkWidget *search_eb = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(search_eb), FALSE);
    gtk_widget_set_margin_start(search_eb, 20);
    gtk_widget_set_margin_end(search_eb, 20);
    gtk_widget_set_margin_bottom(search_eb, 12);
    s_home_search_eb = search_eb;

    GtkWidget *search_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(search_box), "home-search-bar");

    GtkWidget *search_icon = create_svg_image(s_svg_search_icon, 16, 16);
    gtk_box_pack_start(GTK_BOX(search_box), search_icon, FALSE, FALSE, 0);

    GtkWidget *search_lbl = gtk_label_new("Search or type URL...");
    gtk_style_context_add_class(gtk_widget_get_style_context(search_lbl), "home-search-label");
    gtk_box_pack_start(GTK_BOX(search_box), search_lbl, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(search_eb), search_box);
    g_signal_connect(search_eb, "button-press-event", G_CALLBACK(on_home_search_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(box), search_eb, FALSE, FALSE, 0);

    /* Section Header */
    GtkWidget *sec_lbl = gtk_label_new("APPLICATIONS");
    gtk_style_context_add_class(gtk_widget_get_style_context(sec_lbl), "home-section-title");
    gtk_widget_set_halign(sec_lbl, GTK_ALIGN_START);
    gtk_widget_set_margin_start(sec_lbl, 24);
    gtk_widget_set_margin_bottom(sec_lbl, 6);
    s_home_sec_lbl = sec_lbl;
    gtk_box_pack_start(GTK_BOX(box), sec_lbl, FALSE, FALSE, 0);

    /* App Icons Grid: ChatGPT and Chrome */
    GtkWidget *apps_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_widget_set_halign(apps_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(apps_box, GTK_ALIGN_START);

    GtkWidget *btn_chatgpt = create_app_icon_widget("ChatGPT", "chatgpt");
    gtk_box_pack_start(GTK_BOX(apps_box), btn_chatgpt, FALSE, FALSE, 0);

    GtkWidget *btn_chrome = create_app_icon_widget("Chrome", "browser");
    gtk_box_pack_start(GTK_BOX(apps_box), btn_chrome, FALSE, FALSE, 0);
    GtkWidget *btn_ghost_app = create_app_icon_widget("Ghost Typer", "ghost");
    gtk_box_pack_start(GTK_BOX(apps_box), btn_ghost_app, FALSE, FALSE, 0);

    /* The icon row takes the free space so camouflage can centre it (like Windows) without moving anything else. */
    s_home_apps_box = apps_box;
    gtk_box_pack_start(GTK_BOX(box), apps_box, TRUE, TRUE, 0);

    return box;
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
        browser_clear_active_popup();
        xutil_log("WINDOW", "Closed popup modal layer.");
    }
}

gboolean overlay_has_popup_modal(void) {
    return (s_popup_container != NULL);
}

GtkWidget *overlay_get_popup_child_view(void) {
    return s_popup_child_view;
}

static void on_browser_popup_created(GtkWidget *popup_view, gpointer user_data) {
    (void)user_data;
    overlay_show_popup_modal(popup_view);
}

static void on_browser_popup_closed(GtkWidget *popup_view, gpointer user_data) {
    (void)user_data;
    (void)popup_view;
    overlay_close_popup_modal();
}

static void on_browser_divert(const char *uri, gpointer user_data) {
    (void)user_data;
    if (uri && uri[0] != '\0') {
        xutil_log("WINDOW", "Diverting external URI '%s' to Browser app.", uri);
        browser_load_in_browser(uri);
        overlay_switch_app("browser");
    }
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
    } else if ((focused && GTK_IS_TEXT_VIEW(focused)) ||
               (strcmp(s_current_app, "ghost") == 0 && s_test_entry)) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(
            focused && GTK_IS_TEXT_VIEW(focused) ? focused : s_test_entry));
        GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        GtkTextIter begin, end;
        switch (action) {
            case CLIPBOARD_ACTION_SELECT_ALL:
                gtk_text_buffer_get_bounds(buffer, &begin, &end);
                gtk_text_buffer_select_range(buffer, &begin, &end); break;
            case CLIPBOARD_ACTION_COPY: gtk_text_buffer_copy_clipboard(buffer, clipboard); break;
            case CLIPBOARD_ACTION_PASTE: gtk_text_buffer_paste_clipboard(buffer, clipboard, NULL, TRUE); break;
            case CLIPBOARD_ACTION_CUT: gtk_text_buffer_cut_clipboard(buffer, clipboard, TRUE); break;
        }
    } else {
        if (action == CLIPBOARD_ACTION_PASTE) {
            browser_paste();
        } else {
            const char *cmd = NULL;
            switch (action) {
                case CLIPBOARD_ACTION_SELECT_ALL: cmd = WEBKIT_EDITING_COMMAND_SELECT_ALL; break;
                case CLIPBOARD_ACTION_COPY:       cmd = WEBKIT_EDITING_COMMAND_COPY; break;
                case CLIPBOARD_ACTION_CUT:        cmd = WEBKIT_EDITING_COMMAND_CUT; break;
                default: break;
            }
            if (cmd) {
                browser_execute_editing_command(cmd);
            }
        }
    }
    update_x11_state_properties();
}

static void add_nav_content(GtkWidget *button, GtkWidget *icon, const char *text) {
    GtkWidget *column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_box_pack_start(GTK_BOX(column), icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(column), gtk_label_new(text), FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(button), column);
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

static void on_theme_btn_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    theme_cycle();
}

/* Re-skin everything after a theme change (CSS, icons, camouflage chrome visibility). */
void overlay_apply_theme(void) {
    if (!s_window) return;
    gboolean camo = theme_is_camo();
    reload_theme_css();
    refresh_themed_svgs();
    set_camo_hidden(s_status_bar, camo);
    set_camo_hidden(s_bottom_nav, camo);
    set_camo_hidden(s_header_box, camo);
    set_camo_hidden(s_home_search_eb, camo);
    set_camo_hidden(s_home_sec_lbl, camo);
    if (s_app_labels) {
        for (guint i = 0; i < s_app_labels->len; i++)
            set_camo_hidden(g_ptr_array_index(s_app_labels, i), camo);
    }
    if (s_home_apps_box) gtk_widget_set_valign(s_home_apps_box, camo ? GTK_ALIGN_CENTER : GTK_ALIGN_START);
    if (s_theme_btn_label) gtk_label_set_text(GTK_LABEL(s_theme_btn_label), theme_mode_label());
    gtk_widget_queue_draw(s_window);
    xutil_log("WINDOW", "Applied theme '%s' (camo=%d).", theme_mode_label(), camo);
}

gboolean window_init(gboolean start_visible) {
    const AppConfig *cfg = config_get();

    /* 1. Create GTK_WINDOW_POPUP -> Sets override_redirect at X11 level */
    s_window = gtk_window_new(GTK_WINDOW_POPUP);
    s_win_width = (cfg->width > 0) ? cfg->width : OVERLAY_WIDTH;
    s_win_height = OVERLAY_COMPACT_HEIGHT;
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
    gtk_window_set_accept_focus(GTK_WINDOW(s_window), TRUE);
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
    gtk_widget_set_halign(left_box, GTK_ALIGN_START);

    GtkWidget *mode_pill_eb = create_nav_pill(&s_mode_pill_box, &s_mode_dot, &s_mode_pill, "Mirror");
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
    gtk_widget_set_tooltip_text(s_grab_pill, "Keyboard grab blocked");
    gtk_box_pack_start(GTK_BOX(left_box), s_grab_pill, FALSE, FALSE, 0);
    gtk_widget_set_no_show_all(s_grab_pill, TRUE);
    gtk_widget_hide(s_grab_pill);

    /* Appearance button: cycles Dark -> Light -> Camo */
    s_theme_btn = gtk_button_new();
    gtk_widget_set_can_focus(s_theme_btn, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_theme_btn), "theme-btn");
    s_theme_btn_label = gtk_label_new(theme_mode_label());
    gtk_container_add(GTK_CONTAINER(s_theme_btn), s_theme_btn_label);
    gtk_widget_set_tooltip_text(s_theme_btn, "Appearance: Dark / Light / Camo (Alt+C camo, Alt+T text colour)");
    g_signal_connect(s_theme_btn, "clicked", G_CALLBACK(on_theme_btn_clicked), NULL);
    g_signal_connect(s_theme_btn, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(left_box), s_theme_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(status_bar), left_box, FALSE, FALSE, 0);

    /* Center: Move Handle */
    GtkWidget *center_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(status_bar), center_box, TRUE, TRUE, 0);

    s_move_handle_eb = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(s_move_handle_eb), FALSE);
    gtk_widget_add_events(s_move_handle_eb, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_BUTTON1_MOTION_MASK | GDK_POINTER_MOTION_MASK);
    gtk_widget_set_tooltip_text(s_move_handle_eb, "Hold & drag to move | Double-click to expand");
    gtk_widget_set_halign(s_move_handle_eb, GTK_ALIGN_CENTER);

    GtkWidget *move_capsule = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(move_capsule), "move-capsule");
    GtkWidget *grip_img = create_svg_image(s_svg_move_grip, 18, 12);
    gtk_box_pack_start(GTK_BOX(move_capsule), grip_img, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(s_move_handle_eb), move_capsule);

    g_signal_connect(s_move_handle_eb, "button-press-event", G_CALLBACK(on_header_button_press), NULL);
    g_signal_connect(s_move_handle_eb, "motion-notify-event", G_CALLBACK(on_header_motion_notify), NULL);
    g_signal_connect(s_move_handle_eb, "button-release-event", G_CALLBACK(on_header_button_release), NULL);

    gtk_box_pack_start(GTK_BOX(center_box), s_move_handle_eb, TRUE, TRUE, 0);

    /* Right Controls: Fullscreen toggle & Close button */
    GtkWidget *right_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_halign(right_box, GTK_ALIGN_END);

    s_btn_top_fullscreen = gtk_button_new();
    gtk_widget_set_can_focus(s_btn_top_fullscreen, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_btn_top_fullscreen), "top-ctrl-btn");
    gtk_widget_set_tooltip_text(s_btn_top_fullscreen, "Toggle Fullscreen");
    GtkWidget *img_fs = create_svg_image(s_svg_fullscreen_icon, 14, 14);
    gtk_container_add(GTK_CONTAINER(s_btn_top_fullscreen), img_fs);
    g_signal_connect(s_btn_top_fullscreen, "clicked", G_CALLBACK(on_btn_top_fullscreen_clicked), NULL);
    g_signal_connect(s_btn_top_fullscreen, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(right_box), s_btn_top_fullscreen, FALSE, FALSE, 0);

    s_btn_top_close = gtk_button_new();
    gtk_widget_set_can_focus(s_btn_top_close, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_btn_top_close), "top-ctrl-btn");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_btn_top_close), "top-close-btn");
    gtk_widget_set_tooltip_text(s_btn_top_close, "Close Overlay");
    GtkWidget *img_close = create_svg_image(s_svg_top_close_icon, 14, 14);
    gtk_container_add(GTK_CONTAINER(s_btn_top_close), img_close);
    g_signal_connect(s_btn_top_close, "clicked", G_CALLBACK(on_btn_top_close_clicked), NULL);
    g_signal_connect(s_btn_top_close, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(right_box), s_btn_top_close, FALSE, FALSE, 0);

    gtk_box_pack_end(GTK_BOX(status_bar), right_box, FALSE, FALSE, 0);

    /* --- Central Viewport: GtkOverlay wrapping GtkStack --- */
    s_overlay = gtk_overlay_new();
    gtk_box_pack_start(GTK_BOX(s_root_box), s_overlay, TRUE, TRUE, 0);

    s_stack = gtk_stack_new();
    gtk_stack_set_homogeneous(GTK_STACK(s_stack), FALSE);
    gtk_stack_set_transition_type(GTK_STACK(s_stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_set_transition_duration(GTK_STACK(s_stack), 180);
    gtk_container_add(GTK_CONTAINER(s_overlay), s_stack);

    /* Toast Notification Container (Phase 7) */
    s_toast_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(s_toast_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(s_toast_box, GTK_ALIGN_END);
    gtk_widget_set_margin_bottom(s_toast_box, 14);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_toast_box), "toast-box");
    gtk_widget_set_no_show_all(s_toast_box, TRUE);

    s_toast_label = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(s_toast_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(s_toast_label), 34);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_toast_label), "toast-label");
    gtk_box_pack_start(GTK_BOX(s_toast_box), s_toast_label, TRUE, TRUE, 0);
    gtk_overlay_add_overlay(GTK_OVERLAY(s_overlay), s_toast_box);

    /* 1. Page: Home Screen */
    GtkWidget *home_page = create_home_page();
    gtk_stack_add_named(GTK_STACK(s_stack), home_page, "home");

    /* 2. Page: ChatGPT App (Full-screen WebKit inside phone, no URL bar) */
    GtkWidget *cg_widget = browser_get_chatgpt_widget();
    if (cg_widget) {
        gtk_widget_add_events(cg_widget, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
        g_signal_connect(cg_widget, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
        gtk_stack_add_named(GTK_STACK(s_stack), cg_widget, "chatgpt");
    }

    /* 3. Page: Browser App (Header Box + WebKit View) */
    GtkWidget *browser_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    s_header_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_header_box), "header-box");
    gtk_box_pack_start(GTK_BOX(browser_box), s_header_box, FALSE, FALSE, 0);

    GtkWidget *url_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(url_row), "url-bar-container");

    GtkWidget *url_icon = create_svg_image(s_svg_search_icon, 14, 14);
    gtk_style_context_add_class(gtk_widget_get_style_context(url_icon), "url-leading-icon");
    gtk_box_pack_start(GTK_BOX(url_row), url_icon, FALSE, FALSE, 2);

    s_url_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(s_url_entry), "Search or enter website...");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_url_entry), "url-entry");
    g_signal_connect(s_url_entry, "activate", G_CALLBACK(on_url_entry_activate), NULL);
    g_signal_connect(s_url_entry, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(url_row), s_url_entry, TRUE, TRUE, 0);

    /* Compact Reload Button in Browser URL Row */
    s_btn_reload = gtk_button_new();
    gtk_widget_set_can_focus(s_btn_reload, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_btn_reload), "url-action-btn");
    gtk_widget_set_tooltip_text(s_btn_reload, "Reload page");

    s_reload_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(s_reload_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(s_reload_stack), 100);

    GtkWidget *img_reload = create_svg_image(s_svg_reload_icon, 16, 16);
    GtkWidget *img_stop = create_svg_image(s_svg_stop_icon, 16, 16);
    gtk_stack_add_named(GTK_STACK(s_reload_stack), img_reload, "reload");
    gtk_stack_add_named(GTK_STACK(s_reload_stack), img_stop, "stop");
    gtk_container_add(GTK_CONTAINER(s_btn_reload), s_reload_stack);
    gtk_stack_set_visible_child_name(GTK_STACK(s_reload_stack), "reload");

    g_signal_connect(s_btn_reload, "clicked", G_CALLBACK(on_btn_reload_clicked), NULL);
    g_signal_connect(s_btn_reload, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(url_row), s_btn_reload, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(s_header_box), url_row, FALSE, FALSE, 0);

    s_progress_bar = gtk_progress_bar_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(s_progress_bar), "load-progress");
    gtk_widget_set_no_show_all(s_progress_bar, TRUE);
    gtk_widget_hide(s_progress_bar);
    gtk_box_pack_start(GTK_BOX(s_header_box), s_progress_bar, FALSE, FALSE, 0);

    GtkWidget *br_widget = browser_get_browser_widget();
    if (br_widget) {
        gtk_widget_add_events(br_widget, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
        g_signal_connect(br_widget, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
        gtk_box_pack_start(GTK_BOX(browser_box), br_widget, TRUE, TRUE, 0);
    }
    gtk_stack_add_named(GTK_STACK(s_stack), browser_box, "browser");

    /* Default view is HOME SCREEN */
    gtk_stack_set_visible_child_name(GTK_STACK(s_stack), "home");
    g_strlcpy(s_current_app, "home", sizeof(s_current_app));

    /* Connect Browser Callbacks */
    browser_set_uri_callback(on_browser_uri_changed, NULL);
    browser_set_progress_callback(on_browser_progress_changed, NULL);
    browser_set_state_callback(on_browser_state_changed, NULL);
    browser_set_popup_callbacks(on_browser_popup_created, on_browser_popup_closed, NULL);
    browser_set_divert_callback(on_browser_divert, NULL);
    browser_set_click_callback(on_browser_clicked, NULL);

    gtk_stack_add_named(GTK_STACK(s_stack), create_ghost_page(), "ghost");

    /* --- Bottom Navigation Bar --- */
    GtkWidget *bottom_nav = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    s_bottom_nav = bottom_nav;
    gtk_box_set_homogeneous(GTK_BOX(bottom_nav), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(bottom_nav), "bottom-nav");
    gtk_box_pack_start(GTK_BOX(s_root_box), bottom_nav, FALSE, FALSE, 0);

    s_btn_back = gtk_button_new();
    gtk_widget_set_can_focus(s_btn_back, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_btn_back), "nav-button");
    gtk_widget_set_sensitive(s_btn_back, FALSE);
    GtkWidget *img_back = create_svg_image(s_svg_back_icon, 20, 20);
    add_nav_content(s_btn_back, img_back, "Back");
    g_signal_connect(s_btn_back, "clicked", G_CALLBACK(on_btn_back_clicked), NULL);
    g_signal_connect(s_btn_back, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(bottom_nav), s_btn_back, TRUE, TRUE, 0);

    s_btn_home = gtk_button_new();
    gtk_widget_set_can_focus(s_btn_home, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_btn_home), "nav-button");
    GtkWidget *img_home = create_svg_image(s_svg_home_icon, 20, 20);
    add_nav_content(s_btn_home, img_home, "Home");
    g_signal_connect(s_btn_home, "clicked", G_CALLBACK(on_btn_home_clicked), NULL);
    g_signal_connect(s_btn_home, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(bottom_nav), s_btn_home, TRUE, TRUE, 0);

    /* Universal Refresh Button (New State) */
    s_btn_universal_refresh = gtk_button_new();
    gtk_widget_set_can_focus(s_btn_universal_refresh, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_btn_universal_refresh), "nav-button");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_btn_universal_refresh), "nav-refresh-btn");
    gtk_widget_set_tooltip_text(s_btn_universal_refresh, "Universal Refresh (Reset to New State)");
    GtkWidget *img_univ_refresh = create_svg_image(s_svg_universal_refresh_icon, 20, 20);
    add_nav_content(s_btn_universal_refresh, img_univ_refresh, "Reset");
    g_signal_connect(s_btn_universal_refresh, "clicked", G_CALLBACK(on_btn_universal_refresh_clicked), NULL);
    g_signal_connect(s_btn_universal_refresh, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(bottom_nav), s_btn_universal_refresh, TRUE, TRUE, 0);

    /* Ghost Writer Button (Phase 7) */
    s_btn_ghost = gtk_button_new();
    gtk_widget_set_can_focus(s_btn_ghost, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_btn_ghost), "nav-button");
    gtk_widget_set_tooltip_text(s_btn_ghost, "Open Ghost Typer");

    s_btn_ghost_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(s_btn_ghost_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(s_btn_ghost_stack), 100);

    GtkWidget *img_ghost_idle = create_svg_image(s_svg_ghost_idle, 20, 20);
    GtkWidget *img_ghost_typing = create_svg_image(s_svg_ghost_typing, 20, 20);
    GtkWidget *img_ghost_done = create_svg_image(s_svg_check_icon, 20, 20);
    GtkWidget *img_ghost_failed = create_svg_image(s_svg_failed_icon, 20, 20);

    gtk_stack_add_named(GTK_STACK(s_btn_ghost_stack), img_ghost_idle, "idle");
    gtk_stack_add_named(GTK_STACK(s_btn_ghost_stack), img_ghost_typing, "typing");
    gtk_stack_add_named(GTK_STACK(s_btn_ghost_stack), img_ghost_done, "done");
    gtk_stack_add_named(GTK_STACK(s_btn_ghost_stack), img_ghost_failed, "failed");

    add_nav_content(s_btn_ghost, s_btn_ghost_stack, "Typer");
    gtk_stack_set_visible_child_name(GTK_STACK(s_btn_ghost_stack), "idle");

    g_signal_connect(s_btn_ghost, "clicked", G_CALLBACK(on_btn_ghost_clicked), NULL);
    g_signal_connect(s_btn_ghost, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(bottom_nav), s_btn_ghost, TRUE, TRUE, 0);

    /* Snap Button (Phase 6) */
    s_btn_snap = gtk_button_new();
    gtk_widget_set_can_focus(s_btn_snap, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_btn_snap), "nav-button");
    gtk_widget_set_tooltip_text(s_btn_snap, "Capture Screen (Snap)");
    gtk_widget_set_sensitive(s_btn_snap, TRUE);

    s_snap_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(s_snap_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(s_snap_stack), 100);

    GtkWidget *img_snap_cam = create_svg_image(s_svg_camera_icon, 20, 20);
    GtkWidget *img_snap_running = create_svg_image(s_svg_snap_running, 20, 20);
    GtkWidget *img_snap_done = create_svg_image(s_svg_check_icon, 20, 20);

    gtk_stack_add_named(GTK_STACK(s_snap_stack), img_snap_cam, "camera");
    gtk_stack_add_named(GTK_STACK(s_snap_stack), img_snap_running, "snapping");
    gtk_stack_add_named(GTK_STACK(s_snap_stack), img_snap_done, "done");

    add_nav_content(s_btn_snap, s_snap_stack, "Snap");
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

    /* Refresh status & clock every second */
    s_clock_timer_id = g_timeout_add_seconds(1, on_clock_tick, NULL);

    if (start_visible) {
        overlay_show();
    } else {
        overlay_hide();
    }

    return TRUE;
}

void overlay_set_capture_hidden(gboolean hidden) {
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

    if (s_clock_timer_id == 0) {
        s_clock_timer_id = g_timeout_add_seconds(1, on_clock_tick, NULL);
    }

    browser_resume();
    overlay_update_status_ui();
    raise_start();
}

void overlay_hide(void) {
    if (!s_window) return;

    raise_stop();
    input_release_grab();
    input_notify_overlay_hidden();
    browser_pause();

    if (s_clock_timer_id > 0) {
        g_source_remove(s_clock_timer_id);
        s_clock_timer_id = 0;
    }

    gtk_widget_hide(s_window);
    s_visible = FALSE;
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
    calculate_monitor_position(&s_pos_x, &s_pos_y);

    s_win_width = cfg->width;
    s_win_height = cfg->height;
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
    gdk_event_put(event);
    gdk_event_free(event);
}

GtkWidget *overlay_get_window_widget(void) {
    return s_window;
}

GtkWidget *overlay_get_entry_widget(void) {
    if (strcmp(s_current_app, "browser") == 0) {
        if (s_url_entry && gtk_widget_has_focus(s_url_entry)) {
            return s_url_entry;
        }
    }
    return s_test_entry;
}

GtkWidget *overlay_get_test_entry(void) {
    return s_test_entry;
}

GtkWidget *overlay_get_url_entry(void) {
    return s_url_entry;
}

const char *overlay_get_test_entry_text(void) {
    if (!s_test_entry) return "";
    GtkTextIter start, end;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(s_test_entry));
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    g_free(s_ghost_text_cache);
    s_ghost_text_cache = gtk_text_buffer_get_text(buffer, &start, &end, TRUE);
    return s_ghost_text_cache;
}

const char *overlay_get_entry_text(void) {
    if (strcmp(s_current_app, "browser") == 0) {
        if (s_url_entry && gtk_widget_has_focus(s_url_entry)) {
            return gtk_entry_get_text(GTK_ENTRY(s_url_entry));
        }
        if (s_test_entry && gtk_widget_has_focus(s_test_entry)) {
            return overlay_get_test_entry_text();
        }
        if (s_url_entry) {
            return gtk_entry_get_text(GTK_ENTRY(s_url_entry));
        }
    }
    if (!s_test_entry) return "";
    return overlay_get_test_entry_text();
}

void overlay_clear_entry_text(void) {
    if (s_test_entry) {
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(s_test_entry)), "", -1);
        overlay_update_status_ui();
    }
}

void overlay_set_entry_text(const char *text) {
    if (s_test_entry && text) {
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(s_test_entry)), text, -1);
        overlay_update_status_ui();
    }
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

static void save_and_prune_snap(GdkPixbuf *pb, const AppConfig *cfg) {
    const char *pics_dir = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
    char *dir_buf = NULL;
    if (pics_dir && pics_dir[0] != '\0') {
        dir_buf = g_build_filename(pics_dir, "cronq", NULL);
    } else {
        dir_buf = g_build_filename(g_get_home_dir(), "Pictures", "cronq", NULL);
    }

    if (g_mkdir_with_parents(dir_buf, 0755) != 0) {
        xutil_log("WINDOW", "Failed to create directory %s: %s", dir_buf, g_strerror(errno));
        g_free(dir_buf);
        return;
    }

    GDateTime *now = g_date_time_new_now_local();
    char *dt_str = g_date_time_format(now, "%Y%m%d-%H%M%S");
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
    if (gdk_pixbuf_save(pb, filepath, "png", &err, "compression", "9", NULL)) {
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

        /* 2. Backward-compatible link to /tmp with UID safety check */
        struct stat st;
        if (lstat("/tmp/cronq_latest_snap.png", &st) == 0) {
            if (st.st_uid == getuid()) {
                unlink("/tmp/cronq_latest_snap.png");
            }
        }
        if (symlink(filepath, "/tmp/cronq_latest_snap.png") != 0) {
            gchar *data = NULL;
            gsize len = 0;
            if (g_file_get_contents(filepath, &data, &len, NULL)) {
                g_file_set_contents("/tmp/cronq_latest_snap.png", data, len, NULL);
                g_free(data);
            }
        }
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

        g_ptr_array_sort(files, (GCompareFunc)g_strcmp0);

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
    if (s_was_visible_during_snap && s_xid != None) {
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
        GdkMonitor *mon = gdk_display_get_primary_monitor(display);
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

    if (pb) {
        xutil_log("WINDOW", "Captured snap: %dx%d at (%d, %d)", rw, rh, rx, ry);

        /* Set to X11 CLIPBOARD */
        GtkClipboard *clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        gtk_clipboard_set_image(clip, pb);
        gtk_clipboard_store(clip);

        /* Save to disk if enabled */
        if (cfg->snap_save) {
            save_and_prune_snap(pb, cfg);
        }

        g_object_unref(pb);

        if (s_snap_stack) {
            gtk_stack_set_visible_child_name(GTK_STACK(s_snap_stack), "done");
            if (s_snap_revert_timer > 0) {
                g_source_remove(s_snap_revert_timer);
            }
            s_snap_revert_timer = g_timeout_add(1500, on_snap_revert_btn_label, NULL);
        }
    } else {
        xutil_log("WINDOW", "Failed to capture screen pixbuf!");
        if (s_snap_stack) {
            gtk_stack_set_visible_child_name(GTK_STACK(s_snap_stack), "camera");
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
    if (s_ghost_countdown) { g_source_remove(s_ghost_countdown); s_ghost_countdown = 0; }
    ghost_writer_cancel();
    g_clear_pointer(&s_ghost_text_cache, g_free);
    if (s_clock_timer_id > 0) {
        g_source_remove(s_clock_timer_id);
        s_clock_timer_id = 0;
    }
    if (s_snap_revert_timer > 0) {
        g_source_remove(s_snap_revert_timer);
        s_snap_revert_timer = 0;
    }
    if (s_ghost_revert_timer > 0) {
        g_source_remove(s_ghost_revert_timer);
        s_ghost_revert_timer = 0;
    }
    if (s_toast_timer_id > 0) {
        g_source_remove(s_toast_timer_id);
        s_toast_timer_id = 0;
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
