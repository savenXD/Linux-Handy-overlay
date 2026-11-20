#define _GNU_SOURCE
#include "raise.h"
#include "window.h"
#include "xutil.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/randr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOKEN_RATE       60.0   /* max raises per second */
#define TOKEN_BURST      20.0    /* max burst allowance */
#define COOLDOWN_USEC    3000000 /* 3 s cooldown when tripped */
#define COOLDOWN_MS      3000    /* 3 s timer */
#define WATCHDOG_MS      100     /* 100 ms safety polling */

static Display   *s_raise_dpy = NULL;
static Window     s_root = None;
static int        s_rr_event_base = 0;
static int        s_rr_error_base = 0;

static GSource   *s_raise_source = NULL;
static guint      s_watchdog_id = 0;
static guint      s_cooldown_timer_id = 0;
static gboolean   s_active = FALSE;

/* Token bucket state for loop guard */
static double     s_tokens = TOKEN_BURST;
static gint64     s_last_refill_time = 0;
static gint64     s_cooldown_until = 0;
static gint64     s_last_guard_log_time = 0;

/* Statistics */
static guint64    s_raise_count = 0;
static guint64    s_trip_count = 0;

static void check_and_raise(void);
static gboolean on_cooldown_expired(gpointer user_data);

static bool token_bucket_consume(void) {
    gint64 now = g_get_monotonic_time();

    if (now < s_cooldown_until) {
        return false; /* Still in cooldown */
    }

    if (s_last_refill_time == 0) {
        s_last_refill_time = now;
        s_tokens = TOKEN_BURST;
    }

    double elapsed = (double)(now - s_last_refill_time) / 1000000.0;
    s_tokens += elapsed * TOKEN_RATE;
    if (s_tokens > TOKEN_BURST) {
        s_tokens = TOKEN_BURST;
    }
    s_last_refill_time = now;

    if (s_tokens >= 1.0) {
        s_tokens -= 1.0;
        return true;
    }

    return false;
}

static void log_offending_window(Window offender) {
    if (!s_raise_dpy || offender == None) return;

    XClassHint hint;
    memset(&hint, 0, sizeof(hint));
    XGetClassHint(s_raise_dpy, offender, &hint);

    XWindowAttributes attr;
    memset(&attr, 0, sizeof(attr));
    XGetWindowAttributes(s_raise_dpy, offender, &attr);

    xutil_log("LOOP_GUARD", "Offending window: XID=0x%lx, WM_CLASS='%s'/'%s', OverrideRedirect=%s, Geom=%dx%d+%d+%d",
              (unsigned long)offender,
              hint.res_name ? hint.res_name : "none",
              hint.res_class ? hint.res_class : "none",
              attr.override_redirect ? "YES" : "NO",
              attr.width, attr.height, attr.x, attr.y);

    if (hint.res_name) XFree(hint.res_name);
    if (hint.res_class) XFree(hint.res_class);
}

static void check_and_raise(void) {
    if (!s_active || !overlay_is_visible()) return;

    Window overlay_xid = overlay_get_xid();
    if (overlay_xid == None || !s_raise_dpy) return;

    Window root_ret, parent_ret;
    Window *children = NULL;
    unsigned int nchildren = 0;

    if (!XQueryTree(s_raise_dpy, s_root, &root_ret, &parent_ret, &children, &nchildren)) {
        return;
    }

    if (nchildren > 0 && children) {
        Window topmost = children[nchildren - 1];
        if (topmost != overlay_xid) {
            if (token_bucket_consume()) {
                XRaiseWindow(s_raise_dpy, overlay_xid);
                XFlush(s_raise_dpy);
                s_raise_count++;
            } else {
                /* Loop guard tripped */
                gint64 now = g_get_monotonic_time();
                s_cooldown_until = now + COOLDOWN_USEC;
                s_trip_count++;

                if (now - s_last_guard_log_time > 1000000) {
                    s_last_guard_log_time = now;
                    xutil_log_err("LOOP_GUARD", "Raise limit exceeded (burst/rate saturated). Entering %dms cooldown.", COOLDOWN_MS);
                    log_offending_window(topmost);
                }

                if (s_cooldown_timer_id == 0) {
                    s_cooldown_timer_id = g_timeout_add(COOLDOWN_MS, on_cooldown_expired, NULL);
                }
            }
        }
    }

    if (children) {
        XFree(children);
    }
}

static gboolean on_cooldown_expired(gpointer user_data) {
    (void)user_data;
    s_cooldown_timer_id = 0;
    check_and_raise();
    return G_SOURCE_REMOVE;
}

static gboolean on_watchdog_tick(gpointer user_data) {
    (void)user_data;
    if (s_active && overlay_is_visible()) {
        check_and_raise();
    }
    return G_SOURCE_CONTINUE;
}

static void raise_process_events(void) {
    if (!s_raise_dpy || !s_active) return;

    gboolean dirty = FALSE;
    while (XPending(s_raise_dpy) > 0) {
        XEvent ev;
        XNextEvent(s_raise_dpy, &ev);

        if (ev.type == s_rr_event_base + RRScreenChangeNotify) {
            XRRUpdateConfiguration(&ev);
            xutil_log("RAISE", "RandR Screen change detected. Re-asserting overlay geometry.");
            overlay_reposition();
            dirty = TRUE;
        } else if (ev.type == ConfigureNotify ||
                   ev.type == CirculateNotify ||
                   ev.type == MapNotify ||
                   ev.type == CreateNotify ||
                   ev.type == ReparentNotify) {
            dirty = TRUE;
        }
    }

    if (dirty) {
        check_and_raise();
    }
}

/* Custom GSource watching s_raise_dpy socket FD */
typedef struct {
    GSource  source;
    Display *dpy;
    GPollFD  poll_fd;
} RaiseGSource;

static gboolean raise_source_prepare(GSource *source, gint *timeout) {
    RaiseGSource *rsrc = (RaiseGSource *)source;
    *timeout = -1;
    return (XPending(rsrc->dpy) > 0);
}

static gboolean raise_source_check(GSource *source) {
    RaiseGSource *rsrc = (RaiseGSource *)source;
    if (rsrc->poll_fd.revents & (G_IO_IN | G_IO_HUP | G_IO_ERR)) {
        return TRUE;
    }
    return (XPending(rsrc->dpy) > 0);
}

static gboolean raise_source_dispatch(GSource *source, GSourceFunc callback, gpointer user_data) {
    (void)source;
    (void)callback;
    (void)user_data;
    raise_process_events();
    return G_SOURCE_CONTINUE;
}

static GSourceFuncs s_raise_source_funcs = {
    raise_source_prepare,
    raise_source_check,
    raise_source_dispatch,
    NULL, NULL, NULL
};

gboolean raise_init(void) {
    s_raise_dpy = XOpenDisplay(NULL);
    if (!s_raise_dpy) {
        xutil_log_err("RAISE", "Failed to open dedicated X11 display connection for restack watcher!");
        return FALSE;
    }

    int screen = DefaultScreen(s_raise_dpy);
    s_root = RootWindow(s_raise_dpy, screen);

    /* 1. Select SubstructureNotifyMask on root window (shared, non-conflicting) */
    XSelectInput(s_raise_dpy, s_root, SubstructureNotifyMask);

    /* 2. Select RandR Screen Change notifications */
    if (XRRQueryExtension(s_raise_dpy, &s_rr_event_base, &s_rr_error_base)) {
        XRRSelectInput(s_raise_dpy, s_root, RRScreenChangeNotifyMask);
        xutil_log("RAISE", "RandR extension active (event base %d). Subscribed to screen changes.", s_rr_event_base);
    } else {
        xutil_log_err("RAISE", "RandR extension not available.");
    }
    XSync(s_raise_dpy, False);

    xutil_log("RAISE", "Restack watcher initialized on dedicated Display.");
    return TRUE;
}

void raise_start(void) {
    if (!s_raise_dpy || s_active) return;
    s_active = TRUE;

    /* Reset token bucket and cooldown */
    s_tokens = TOKEN_BURST;
    s_last_refill_time = g_get_monotonic_time();
    s_cooldown_until = 0;

    /* Drain any stale events queued in X buffer while hidden */
    while (XPending(s_raise_dpy) > 0) {
        XEvent ev;
        XNextEvent(s_raise_dpy, &ev);
    }

    /* Attach dedicated Display FD to GLib main loop */
    GSource *source = g_source_new(&s_raise_source_funcs, sizeof(RaiseGSource));
    RaiseGSource *rsrc = (RaiseGSource *)source;
    rsrc->dpy = s_raise_dpy;
    rsrc->poll_fd.fd = ConnectionNumber(s_raise_dpy);
    rsrc->poll_fd.events = G_IO_IN | G_IO_HUP | G_IO_ERR;
    rsrc->poll_fd.revents = 0;

    g_source_add_poll(source, &rsrc->poll_fd);
    g_source_attach(source, NULL);
    s_raise_source = source;

    /* Start 100 ms safety watchdog */
    s_watchdog_id = g_timeout_add(WATCHDOG_MS, on_watchdog_tick, NULL);

    xutil_log("RAISE", "Restack watcher started (event listener + 100ms watchdog attached).");

    /* Immediate raise check */
    check_and_raise();
}

void raise_stop(void) {
    if (!s_active) return;
    s_active = FALSE;

    if (s_cooldown_timer_id > 0) {
        g_source_remove(s_cooldown_timer_id);
        s_cooldown_timer_id = 0;
    }

    if (s_watchdog_id > 0) {
        g_source_remove(s_watchdog_id);
        s_watchdog_id = 0;
    }

    if (s_raise_source) {
        g_source_destroy(s_raise_source);
        g_source_unref(s_raise_source);
        s_raise_source = NULL;
    }

    xutil_log("RAISE", "Restack watcher stopped (all timers and event sources detached).");
}

void raise_cleanup(void) {
    raise_stop();

    if (s_raise_dpy) {
        XCloseDisplay(s_raise_dpy);
        s_raise_dpy = NULL;
    }
    s_root = None;
    xutil_log("RAISE", "Restack watcher cleaned up.");
}

guint64 raise_get_count(void) {
    return s_raise_count;
}

guint64 raise_get_trip_count(void) {
    return s_trip_count;
}
