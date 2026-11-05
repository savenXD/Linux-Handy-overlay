#include "capture_guard.h"
#include "config.h"
#include "window.h"
#include "xutil.h"

#include <X11/Xlib.h>
#include <X11/extensions/record.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <dirent.h>
#include <ctype.h>

#define RESTORE_DELAY_MS 2500      /* overlay returns after this long without any capture request */
#define CLIENT_ID_MASK   0x1FFFFF  /* resource-id bits belonging to one client on Xorg */

static Display   *s_ctrl_dpy = NULL;
static Display   *s_data_dpy = NULL;
static XRecordContext s_ctx = 0;
static GSource   *s_source = NULL;
static Window     s_root = None;
static int        s_shm_major = 0;
static bool       s_active = false;
static guint      s_restore_id = 0;
static guint64    s_events = 0;
static bool       s_proc_hold = false;    /* a known screen-recorder program is running */
static bool       s_applied = false;      /* overlay currently held back */
static guint      s_proc_timer = 0;

typedef struct { GSource source; Display *dpy; GPollFD fd; } GuardSource;

bool capture_guard_is_active(void) { return s_active; }

static void apply_hold(void) {
    bool want = s_active || s_proc_hold;
    if (want == s_applied) return;
    s_applied = want;
    overlay_set_capture_hidden(want);
}

static gboolean on_restore_timeout(gpointer data) {
    (void)data;
    s_restore_id = 0;
    s_active = false;
    xutil_log("CAPTURE", "No screen capture for %d ms: overlay restored.", RESTORE_DELAY_MS);
    apply_hold();
    return G_SOURCE_REMOVE;
}

/* A capture request that covers the overlay was seen. */
static void note_capture(void) {
    if (!s_active) {
        s_active = true;
        xutil_log("CAPTURE", "Screen capture detected: hiding overlay from the capture.");
        apply_hold();
    }
    if (s_restore_id) g_source_remove(s_restore_id);
    s_restore_id = g_timeout_add(RESTORE_DELAY_MS, on_restore_timeout, NULL);
}

/* Known recorder programs. Holding the overlay back as soon as one runs closes the first-frame gap that
 * request-based detection cannot (the first screen read is answered before we can react). */
static bool is_recorder_process(const char *comm, const char *pid) {
    static const char *names[] = { "obs", "obs-studio", "simplescreenrec", "kazam", "peek", "vokoscreenNG",
                                   "recordmydesktop", "byzanz-record", "wf-recorder", "ssr-glinject", NULL };
    for (int i = 0; names[i]; i++) if (g_ascii_strcasecmp(comm, names[i]) == 0) return true;
    if (strcmp(comm, "ffmpeg") == 0) {   /* only when it is grabbing the screen, not transcoding */
        char path[320], buf[2048];
        snprintf(path, sizeof(path), "/proc/%s/cmdline", pid);
        FILE *f = fopen(path, "r");
        if (!f) return false;
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        for (size_t i = 0; i < n; i++) if (buf[i] == '\0') buf[i] = ' ';
        buf[n] = '\0';
        return strstr(buf, "x11grab") || strstr(buf, "kmsgrab") || strstr(buf, "gdigrab");
    }
    return false;
}

static gboolean on_proc_scan(gpointer data) {
    (void)data;
    bool found = false;
    DIR *d = opendir("/proc");
    if (d) {
        struct dirent *e;
        while (!found && (e = readdir(d))) {
            if (!isdigit((unsigned char)e->d_name[0])) continue;
            char path[320], comm[64] = {0};
            snprintf(path, sizeof(path), "/proc/%s/comm", e->d_name);
            FILE *f = fopen(path, "r");
            if (!f) continue;
            if (fgets(comm, sizeof(comm), f)) comm[strcspn(comm, "\n")] = '\0';
            fclose(f);
            found = is_recorder_process(comm, e->d_name);
        }
        closedir(d);
    }
    if (found != s_proc_hold) {
        s_proc_hold = found;
        xutil_log("CAPTURE", found ? "Screen recorder program running: overlay held back." : "Screen recorder program gone.");
        apply_hold();
    }
    return G_SOURCE_CONTINUE;
}

static void record_cb(XPointer closure, XRecordInterceptData *data) {
    (void)closure;
    if (data->category == XRecordFromClient && data->data_len >= 4 /* words */) {
        const unsigned char *req = data->data;
        bool is_getimage = (req[0] == 73);                                   /* core GetImage */
        bool is_shm_get = (s_shm_major && req[0] == s_shm_major && req[1] == 4);  /* MIT-SHM ShmGetImage */
        if ((is_getimage || is_shm_get) && !data->client_swapped) {
            Window drawable = *(const uint32_t *)(req + 4);
            short x = *(const short *)(req + 8);
            short y = *(const short *)(req + 10);
            unsigned short w = *(const unsigned short *)(req + 12);
            unsigned short h = *(const unsigned short *)(req + 14);
            Window own = overlay_get_xid();
            bool ours = own != None && (data->id_base == (own & ~CLIENT_ID_MASK));
            if (drawable == s_root && !ours) {
                int ox, oy, ow, oh;
                overlay_get_geometry(&ox, &oy, &ow, &oh);
                bool overlaps = (x < ox + ow) && (x + (int)w > ox) && (y < oy + oh) && (y + (int)h > oy);
                s_events++;
                if (overlaps && overlay_is_visible()) note_capture();
                else if (overlaps && s_active) note_capture();   /* keep the hold alive while it continues */
            }
        }
    }
    XRecordFreeData(data);
}

static gboolean guard_prepare(GSource *src, gint *timeout) {
    (void)src; *timeout = -1; return FALSE;
}

static gboolean guard_check(GSource *src) {
    return (((GuardSource *)src)->fd.revents & (G_IO_IN | G_IO_HUP | G_IO_ERR)) != 0;
}

static gboolean guard_dispatch(GSource *src, GSourceFunc cb, gpointer ud) {
    (void)cb; (void)ud;
    GuardSource *gs = (GuardSource *)src;
    gs->fd.revents = 0;
    XRecordProcessReplies(gs->dpy);
    return G_SOURCE_CONTINUE;
}

static GSourceFuncs s_funcs = { guard_prepare, guard_check, guard_dispatch, NULL, NULL, NULL };

static void guard_stop(void) {
    if (s_restore_id) { g_source_remove(s_restore_id); s_restore_id = 0; }
    s_active = false; s_proc_hold = false; apply_hold();
    if (s_proc_timer) { g_source_remove(s_proc_timer); s_proc_timer = 0; }
    if (s_source) { g_source_destroy(s_source); g_source_unref(s_source); s_source = NULL; }
    if (s_ctrl_dpy && s_ctx) { XRecordDisableContext(s_ctrl_dpy, s_ctx); XRecordFreeContext(s_ctrl_dpy, s_ctx); XSync(s_ctrl_dpy, False); }
    s_ctx = 0;
    if (s_data_dpy) { XCloseDisplay(s_data_dpy); s_data_dpy = NULL; }
    if (s_ctrl_dpy) { XCloseDisplay(s_ctrl_dpy); s_ctrl_dpy = NULL; }
}

static gboolean guard_start(void) {
    s_ctrl_dpy = XOpenDisplay(NULL);
    s_data_dpy = XOpenDisplay(NULL);
    if (!s_ctrl_dpy || !s_data_dpy) { guard_stop(); return FALSE; }

    int major, minor;
    if (!XRecordQueryVersion(s_ctrl_dpy, &major, &minor)) {
        xutil_log_err("CAPTURE", "X Record extension unavailable: capture guard disabled.");
        guard_stop();
        return FALSE;
    }
    s_root = DefaultRootWindow(s_ctrl_dpy);
    int ev, err;
    if (!XQueryExtension(s_ctrl_dpy, "MIT-SHM", &s_shm_major, &ev, &err)) s_shm_major = 0;

    XRecordRange *range = XRecordAllocRange();
    if (!range) { guard_stop(); return FALSE; }
    range->core_requests.first = 73;    /* GetImage */
    range->core_requests.last = 73;
    if (s_shm_major) {
        range->ext_requests.ext_major.first = s_shm_major;
        range->ext_requests.ext_major.last = s_shm_major;
        range->ext_requests.ext_minor.first = 4;   /* ShmGetImage */
        range->ext_requests.ext_minor.last = 4;
    }
    XRecordClientSpec spec = XRecordAllClients;
    s_ctx = XRecordCreateContext(s_ctrl_dpy, 0, &spec, 1, &range, 1);
    XFree(range);
    XSync(s_ctrl_dpy, False);
    if (!s_ctx) { guard_stop(); return FALSE; }

    if (!XRecordEnableContextAsync(s_data_dpy, s_ctx, record_cb, NULL)) {
        xutil_log_err("CAPTURE", "Could not enable record context.");
        guard_stop();
        return FALSE;
    }
    GSource *src = g_source_new(&s_funcs, sizeof(GuardSource));
    GuardSource *gs = (GuardSource *)src;
    gs->dpy = s_data_dpy;
    gs->fd.fd = ConnectionNumber(s_data_dpy);
    gs->fd.events = G_IO_IN | G_IO_HUP | G_IO_ERR;
    g_source_add_poll(src, &gs->fd);
    g_source_attach(src, NULL);
    s_source = src;
    s_proc_timer = g_timeout_add(250, on_proc_scan, NULL);
    xutil_log("CAPTURE", "Capture guard armed (X Record %d.%d, MIT-SHM opcode %d; recorder-process watch on).", major, minor, s_shm_major);
    return TRUE;
}

gboolean capture_guard_init(void) {
    if (!config_get()->hide_from_capture) {
        xutil_log("CAPTURE", "Capture guard disabled by [Privacy] hide_from_capture = false.");
        return TRUE;
    }
    return guard_start();
}

void capture_guard_reload_config(void) {
    bool want = config_get()->hide_from_capture;
    bool have = s_ctx != 0;
    if (want && !have) guard_start();
    else if (!want && have) { guard_stop(); xutil_log("CAPTURE", "Capture guard disabled."); }
}

void capture_guard_cleanup(void) {
    guard_stop();
}
