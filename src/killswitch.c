#include "killswitch.h"
#include "hotkey.h"
#include "window.h"
#include "xutil.h"

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib/gstdio.h>
#include <errno.h>

#define KILL_PROMPT_TIMEOUT_SEC 15

static GtkWidget *s_prompt_win = NULL;
static guint      s_timeout_id = 0;
static guint      s_raise_id = 0;
static bool       s_confirmed = false;

/* Runs detached after Cronq exits. Paths come from the environment (set by killswitch_wipe),
 * every rm -rf target is a directory named "cronq" built from the XDG base dirs. */
static const char *s_cleanup_script =
    "#!/bin/sh\n"
    "sleep 1\n"
    "pkill -f cronq-supervisor 2>/dev/null\n"
    "pkill -f cronq-superisor 2>/dev/null\n"
    "pkill -x cronq 2>/dev/null\n"
    "sleep 1\n"
    "pkill -9 -f cronq-supervisor 2>/dev/null\n"
    "pkill -9 -f cronq-superisor 2>/dev/null\n"
    "pkill -9 -x cronq 2>/dev/null\n"
    "[ -n \"$CQ_BIN\" ] && rm -f \"$CQ_BIN/cronq\" \"$CQ_BIN/cronq-supervisor\" \"$CQ_BIN/cronq-superisor\"\n"
    "[ -n \"$CQ_AUTOSTART\" ] && rm -f \"$CQ_AUTOSTART/cronq.desktop\"\n"
    "[ -n \"$CQ_APPS\" ] && rm -f \"$CQ_APPS/cronq.desktop\"\n"
    "[ -n \"$CQ_ICON\" ] && rm -f \"$CQ_ICON\"\n"
    "for d in \"$CQ_CONFIG\" \"$CQ_DATA\" \"$CQ_CACHE\" \"$CQ_RUNTIME\" \"$CQ_PICTURES\"; do\n"
    "  case \"$d\" in */cronq) [ -d \"$d\" ] && rm -rf \"$d\" ;; esac\n"
    "done\n"
    "rm -f /tmp/cronq*.log /tmp/cronq*.sock 2>/dev/null\n"
    "command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database \"$CQ_APPS\" 2>/dev/null\n"
    "rm -f \"$0\"\n";

static void child_setup(gpointer data) {
    (void)data;
    setsid();
}

static void killswitch_wipe(void) {
    char *tmpl = g_strdup_printf("%s/cqks-XXXXXX", g_get_tmp_dir());
    int fd = g_mkstemp(tmpl);
    if (fd < 0) {
        xutil_log_err("KILL", "Could not create cleanup script: %s", g_strerror(errno));
        g_free(tmpl);
        return;
    }
    ssize_t w = write(fd, s_cleanup_script, strlen(s_cleanup_script));
    (void)w;
    close(fd);
    g_chmod(tmpl, 0700);

    const char *pics = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
    char *pictures = pics ? g_build_filename(pics, "cronq", NULL)
                          : g_build_filename(g_get_home_dir(), "Pictures", "cronq", NULL);
    char *bin = g_build_filename(g_get_home_dir(), ".local", "bin", NULL);
    char *autostart = g_build_filename(g_get_user_config_dir(), "autostart", NULL);
    char *apps = g_build_filename(g_get_user_data_dir(), "applications", NULL);
    char *icon = g_build_filename(g_get_user_data_dir(), "icons", "hicolor", "scalable", "apps", "cronq.svg", NULL);
    char *cfg = g_build_filename(g_get_user_config_dir(), "cronq", NULL);
    char *data = g_build_filename(g_get_user_data_dir(), "cronq", NULL);
    char *cache = g_build_filename(g_get_user_cache_dir(), "cronq", NULL);
    char *runtime = g_build_filename(g_get_user_runtime_dir(), "cronq", NULL);

    char **envp = g_get_environ();
    envp = g_environ_setenv(envp, "CQ_BIN", bin, TRUE);
    envp = g_environ_setenv(envp, "CQ_AUTOSTART", autostart, TRUE);
    envp = g_environ_setenv(envp, "CQ_APPS", apps, TRUE);
    envp = g_environ_setenv(envp, "CQ_ICON", icon, TRUE);
    envp = g_environ_setenv(envp, "CQ_CONFIG", cfg, TRUE);
    envp = g_environ_setenv(envp, "CQ_DATA", data, TRUE);
    envp = g_environ_setenv(envp, "CQ_CACHE", cache, TRUE);
    envp = g_environ_setenv(envp, "CQ_RUNTIME", runtime, TRUE);
    envp = g_environ_setenv(envp, "CQ_PICTURES", pictures, TRUE);

    char *argv[] = { (char *)"/bin/sh", tmpl, NULL };
    GError *err = NULL;
    /* GLib spawn closes fds and detaches via setsid so the script survives our exit. */
    if (!g_spawn_async(NULL, argv, envp,
                       G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_DO_NOT_REAP_CHILD,
                       child_setup, NULL, NULL, &err)) {
        xutil_log_err("KILL", "Failed to start cleanup script: %s", err->message);
        g_error_free(err);
    } else {
        xutil_log("KILL", "Kill switch confirmed: cleanup script started, exiting.");
    }

    g_strfreev(envp);
    g_free(tmpl); g_free(pictures); g_free(bin); g_free(autostart); g_free(apps);
    g_free(icon); g_free(cfg); g_free(data); g_free(cache); g_free(runtime);
}

static void prompt_destroy(void) {
    if (s_timeout_id) { g_source_remove(s_timeout_id); s_timeout_id = 0; }
    if (s_raise_id)   { g_source_remove(s_raise_id);   s_raise_id = 0; }
    hotkey_set_prompt_grabs(false);
    if (s_prompt_win) {
        gtk_widget_destroy(s_prompt_win);
        s_prompt_win = NULL;
    }
}

static gboolean on_prompt_timeout(gpointer data) {
    (void)data;
    s_timeout_id = 0;
    xutil_log("KILL", "Kill prompt timed out (%d s): cancelled.", KILL_PROMPT_TIMEOUT_SEC);
    killswitch_prompt_cancel();
    return G_SOURCE_REMOVE;
}

static gboolean on_prompt_raise(gpointer data) {
    (void)data;
    if (!s_prompt_win) return G_SOURCE_REMOVE;
    GdkWindow *gw = gtk_widget_get_window(s_prompt_win);
    if (gw) gdk_window_raise(gw);
    return G_SOURCE_CONTINUE;
}

void killswitch_prompt_show(void) {
    if (s_prompt_win) return;
    s_confirmed = false;

    s_prompt_win = gtk_window_new(GTK_WINDOW_POPUP);   /* override-redirect: never takes focus */
    gtk_window_set_accept_focus(GTK_WINDOW(s_prompt_win), FALSE);
    gtk_window_set_keep_above(GTK_WINDOW(s_prompt_win), TRUE);

    GtkCssProvider *prov = gtk_css_provider_new();
    gtk_css_provider_load_from_data(prov,
        ".kill-box { background-color: #b91c1c; border: 2px solid #fecaca; border-radius: 12px; padding: 22px 34px; }"
        ".kill-title { color: #ffffff; font-size: 20px; font-weight: 700; }"
        ".kill-sub { color: #fee2e2; font-size: 13px; }", -1, NULL);
    gtk_style_context_add_provider_for_screen(gtk_widget_get_screen(s_prompt_win),
                                              GTK_STYLE_PROVIDER(prov), GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(prov);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "kill-box");
    GtkWidget *title = gtk_label_new("Kill Cronq permanently?");
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "kill-title");
    char sub_txt[160];
    snprintf(sub_txt, sizeof(sub_txt),
             "Enter = remove Cronq and all its data    Esc = cancel\n(cancels automatically in %d seconds)",
             KILL_PROMPT_TIMEOUT_SEC);
    GtkWidget *sub = gtk_label_new(sub_txt);
    gtk_label_set_justify(GTK_LABEL(sub), GTK_JUSTIFY_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(sub), "kill-sub");
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), sub, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(s_prompt_win), box);
    /* The red box is only drawn when the overlay is on screen. Pressed while hidden (or locked) the
     * prompt stays invisible but armed: Enter still confirms, Esc or the timeout cancels. */
    bool visible_prompt = overlay_is_visible();
    if (!visible_prompt) {
        hotkey_set_prompt_grabs(true);
        s_timeout_id = g_timeout_add(KILL_PROMPT_TIMEOUT_SEC * 1000, on_prompt_timeout, NULL);
        xutil_log("KILL", "Kill prompt armed silently (overlay hidden).");
        return;
    }
    gtk_widget_show_all(s_prompt_win);

    /* Centre on the primary monitor */
    GdkDisplay *disp = gdk_display_get_default();
    GdkMonitor *mon = gdk_display_get_primary_monitor(disp);
    if (!mon && gdk_display_get_n_monitors(disp) > 0) mon = gdk_display_get_monitor(disp, 0);
    GdkRectangle geom = {0, 0, 1920, 1080};
    if (mon) gdk_monitor_get_geometry(mon, &geom);
    gint pw = 0, ph = 0;
    gtk_window_get_size(GTK_WINDOW(s_prompt_win), &pw, &ph);
    GtkRequisition req;
    gtk_widget_get_preferred_size(s_prompt_win, NULL, &req);
    if (pw < req.width) pw = req.width;
    if (ph < req.height) ph = req.height;
    gtk_window_move(GTK_WINDOW(s_prompt_win), geom.x + (geom.width - pw) / 2, geom.y + (geom.height - ph) / 2);
    gtk_widget_show_all(s_prompt_win);
    GdkWindow *gw = gtk_widget_get_window(s_prompt_win);
    if (gw) gdk_window_raise(gw);

    hotkey_set_prompt_grabs(true);
    s_timeout_id = g_timeout_add(KILL_PROMPT_TIMEOUT_SEC * 1000, on_prompt_timeout, NULL);
    s_raise_id = g_timeout_add(500, on_prompt_raise, NULL);
    xutil_log("KILL", "Kill prompt shown.");
}

void killswitch_prompt_cancel(void) {
    if (!s_prompt_win) return;
    prompt_destroy();
    xutil_log("KILL", "Kill prompt cancelled.");
}

bool killswitch_prompt_active(void) {
    return s_prompt_win != NULL;
}

void killswitch_prompt_confirm(void) {
    if (!s_prompt_win || s_confirmed) return;
    s_confirmed = true;
    prompt_destroy();
    killswitch_wipe();
    gtk_main_quit();
}
