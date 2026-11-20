#include "window.h"
#include "hotkey.h"
#include "raise.h"
#include "input.h"
#include "config.h"
#include "chat_view.h"
#include "theme.h"
#include "killswitch.h"
#include "capture_guard.h"
#include "xutil.h"
#include "lifecycle.h"

#include <X11/Xlib.h>
#include <gtk/gtk.h>
#include <glib-unix.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static int         s_ipc_listen_fd = -1;
static GIOChannel *s_ipc_channel = NULL;
static guint       s_ipc_watch_id = 0;
static char        s_ipc_sock_path[1024] = {0};

static gboolean on_shutdown_signal(gpointer user_data);

static void get_ipc_socket_path(char *out, size_t size) {
    const char *data_dir = g_get_user_data_dir();
    snprintf(out, size, "%s/cronq/ipc.sock", data_dir);
}

static bool send_ipc_command(const char *cmd, char *response_out, size_t response_size) {
    char sock_path[1024];
    get_ipc_socket_path(sock_path, sizeof(sock_path));

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    g_strlcpy(addr.sun_path, sock_path, sizeof(addr.sun_path));

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return false;
    }

    /* Send command line */
    size_t len = strlen(cmd);
    if (write(fd, cmd, len) < 0) {
        close(fd);
        return false;
    }
    if (len == 0 || cmd[len - 1] != '\n') {
        if (write(fd, "\n", 1) < 0) {
            close(fd);
            return false;
        }
    }

    /* Read response */
    if (response_out && response_size > 0) {
        size_t total = 0;
        while (total + 1 < response_size) {
            ssize_t n = read(fd, response_out + total, response_size - 1 - total);
            if (n <= 0) break;
            total += n;
            if (response_out[total - 1] == '\n') break;
        }
        response_out[total] = '\0';
    }

    close(fd);
    return true;
}

static gboolean on_ipc_client_connected(GIOChannel *source, GIOCondition condition, gpointer user_data) {
    (void)source;
    (void)condition;
    (void)user_data;

    if (s_ipc_listen_fd < 0) return G_SOURCE_REMOVE;

    int client_fd = accept(s_ipc_listen_fd, NULL, NULL);
    if (client_fd < 0) return G_SOURCE_CONTINUE;
    fcntl(client_fd, F_SETFD, FD_CLOEXEC);

    size_t buf_cap = 131072;
    char *buf = g_malloc0(buf_cap);
    size_t total = 0;
    while (total + 1 < buf_cap) {
        ssize_t n = read(client_fd, buf + total, buf_cap - 1 - total);
        if (n <= 0) break;
        total += n;
        if (buf[total - 1] == '\n') break;
    }
    if (total > 0) {
        buf[total] = '\0';
        char *line = g_strstrip(buf);
        xutil_log("IPC", "Received command: '%.64s%s'", line, strlen(line) > 64 ? "..." : "");

        char resp[512] = "OK\n";

        if (g_ascii_strcasecmp(line, "TOGGLE") == 0) {
            overlay_toggle();
        } else if (g_ascii_strcasecmp(line, "SHOW") == 0) {
            overlay_show();
        } else if (g_ascii_strcasecmp(line, "HIDE") == 0) {
            overlay_hide();
        } else if (g_ascii_strcasecmp(line, "KILL") == 0 ||
                   g_ascii_strcasecmp(line, "QUIT") == 0 ||
                   g_ascii_strcasecmp(line, "EXIT") == 0) {
            xutil_log("MAIN", "Termination requested via IPC.");
            snprintf(resp, sizeof(resp), "OK\n");
            ssize_t w = write(client_fd, resp, strlen(resp));
            (void)w;
            g_free(buf);
            close(client_fd);
            g_idle_add(on_shutdown_signal, NULL);
            return G_SOURCE_CONTINUE;
        } else if (g_ascii_strcasecmp(line, "MODE normal") == 0) {
            input_set_typing_target(TYPING_TARGET_NONE);
        } else if (g_ascii_strcasecmp(line, "MODE overlay") == 0) {
            input_set_typing_target(TYPING_TARGET_OVERLAY);
        } else if (g_ascii_strcasecmp(line, "MODE") == 0) {
            input_toggle_typing_target();
        } else if (g_ascii_strcasecmp(line, "GHOST on") == 0) {
            input_set_ghost_mode(GHOST_MODE_ON);
        } else if (g_ascii_strcasecmp(line, "GHOST off") == 0) {
            input_set_ghost_mode(GHOST_MODE_OFF);
        } else if (g_ascii_strcasecmp(line, "GHOST") == 0) {
            input_toggle_ghost_mode();
        } else if (g_str_has_prefix(line, "APP ")) {
            const char *app = line + 4;
            while (*app == ' ') app++;
            if (!g_ascii_strcasecmp(app,"home") || !g_ascii_strcasecmp(app,"chatgpt")) overlay_switch_app(app);
            else snprintf(resp,sizeof(resp),"UNKNOWN_APP\n");
        } else if (g_ascii_strcasecmp(line, "BACK") == 0) {
            overlay_back();
        } else if (g_ascii_strcasecmp(line, "HOME") == 0) {
            overlay_go_home();
        } else if (g_ascii_strcasecmp(line, "RELOAD") == 0) {
            overlay_reload();
        } else if (g_ascii_strcasecmp(line, "UNIVERSAL_RESET") == 0 ||
                   g_ascii_strcasecmp(line, "RESET") == 0) {
            overlay_universal_reset();
        } else if (g_ascii_strcasecmp(line, "HAS_MODAL") == 0) {
            snprintf(resp, sizeof(resp), "%s\n", overlay_has_popup_modal() ? "TRUE" : "FALSE");
        } else if (g_ascii_strcasecmp(line, "CLOSE_MODAL") == 0) {
            overlay_close_popup_modal();
        } else if (g_ascii_strcasecmp(line, "SNAP") == 0) {
            overlay_snap();
        } else if (g_ascii_strcasecmp(line, "PASTE") == 0) {
            overlay_execute_clipboard_action(CLIPBOARD_ACTION_PASTE);
        } else if (g_str_has_prefix(line, "SET_OPACITY ")) {
            double op = g_ascii_strtod(line + 12, NULL);
            overlay_set_opacity(op);
        } else if (g_ascii_strcasecmp(line, "GET_OPACITY") == 0) {
            snprintf(resp, sizeof(resp), "%.2f\n", overlay_get_opacity());
        } else if (g_str_has_prefix(line, "SET_SIZE ")) {
            int w = 0, h = 0;
            if (sscanf(line + 9, "%d %d", &w, &h) == 2) {
                overlay_set_size(w, h);
            }
        } else if (g_ascii_strcasecmp(line, "FULLSCREEN") == 0) {
            overlay_set_fullscreen();
        } else if (g_ascii_strcasecmp(line, "DEFAULT_SIZE") == 0) {
            overlay_set_default_size();
        } else if (g_ascii_strcasecmp(line, "GET_GEOMETRY") == 0) {
            int x, y, w, h;
            overlay_get_geometry(&x, &y, &w, &h);
            snprintf(resp, sizeof(resp), "X=%d Y=%d W=%d H=%d\n", x, y, w, h);
        } else if (g_ascii_strcasecmp(line, "RELOAD_CONFIG") == 0) {
            config_reload();
            overlay_apply_config();
            theme_reload_from_config();
            capture_guard_reload_config();
            hotkey_reload_grabs();
        } else if (g_ascii_strcasecmp(line, "GET_THEME") == 0) {
            snprintf(resp, sizeof(resp), "THEME=%d LABEL=%s\n", (int)theme_get_mode(), theme_mode_label());
        } else if (g_str_has_prefix(line, "SET_THEME ")) {
            theme_set_mode((ThemeMode)atoi(line + 10));
        } else if (g_ascii_strcasecmp(line, "CAMO") == 0) {
            theme_toggle_camo();
        } else if (g_ascii_strcasecmp(line, "TEXT_COLOR") == 0) {
            theme_toggle_text();
        } else if (g_ascii_strcasecmp(line, "DOCK_LEFT") == 0) {
            overlay_dock_corner("bottom-left");
        } else if (g_ascii_strcasecmp(line, "DOCK_RIGHT") == 0) {
            overlay_dock_corner("bottom-right");
        } else if (g_ascii_strcasecmp(line, "LOCK") == 0) {
            hotkey_set_locked(true);
        } else if (g_ascii_strcasecmp(line, "UNLOCK") == 0) {
            hotkey_set_locked(false);
        } else if (g_ascii_strcasecmp(line, "GET_CAPTURE") == 0) {
            snprintf(resp, sizeof(resp), "HELD=%s\n", capture_guard_is_active() ? "TRUE" : "FALSE");
        } else if (g_ascii_strcasecmp(line, "GET_LOCK") == 0) {
            snprintf(resp, sizeof(resp), "LOCKED=%s\n", hotkey_is_locked() ? "TRUE" : "FALSE");
        } else if (g_ascii_strcasecmp(line, "KILL_PROMPT") == 0) {
            killswitch_prompt_show();
        } else if (g_ascii_strcasecmp(line, "KILL_PROMPT_CANCEL") == 0) {
            killswitch_prompt_cancel();
        } else if (g_ascii_strcasecmp(line, "KILL_PROMPT_ACTIVE") == 0) {
            snprintf(resp, sizeof(resp), "%s\n", killswitch_prompt_active() ? "TRUE" : "FALSE");
        } else if (g_ascii_strcasecmp(line, "STATUS") == 0) {
            snprintf(resp, sizeof(resp), "MODE=%s TARGET=%s GHOST=%s GRAB=%s VISIBLE=%s APP=%s LOCKED=%s APPEARANCE=%d\n",
                     input_get_typing_target()==TYPING_TARGET_OVERLAY ? "OVERLAY" : "NORMAL",
                     input_get_typing_target() == TYPING_TARGET_OVERLAY ? "OVERLAY" : "NONE",
                     input_get_ghost_mode() == GHOST_MODE_ON ? "GHOST" : "SOLID",
                     input_get_grab_status() == GRAB_STATUS_ACTIVE ? "ACTIVE" :
                     (input_get_grab_status() == GRAB_STATUS_FAILED ? "FAILED" : "NONE"),
                     overlay_is_visible() ? "TRUE" : "FALSE",
                     overlay_get_current_app(), hotkey_is_locked()?"TRUE":"FALSE",(int)theme_get_mode());
        } else {
            snprintf(resp, sizeof(resp), "UNKNOWN_COMMAND\n");
        }

        ssize_t w = write(client_fd, resp, strlen(resp));
        (void)w;
    }

    g_free(buf);
    close(client_fd);
    return G_SOURCE_CONTINUE;
}

static bool ipc_server_init(void) {
    get_ipc_socket_path(s_ipc_sock_path, sizeof(s_ipc_sock_path));

    char *dir = g_path_get_dirname(s_ipc_sock_path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);

    /* Test if socket is actively listening */
    int test_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (test_fd >= 0) {
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        g_strlcpy(addr.sun_path, s_ipc_sock_path, sizeof(addr.sun_path));
        if (connect(test_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            close(test_fd);
            xutil_log_err("MAIN", "Another instance of Cronq is already running on '%s'.", s_ipc_sock_path);
            return false;
        }
        close(test_fd);
    }
    unlink(s_ipc_sock_path);

    s_ipc_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s_ipc_listen_fd < 0) {
        xutil_log_err("MAIN", "Failed to create IPC socket: %s", strerror(errno));
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    g_strlcpy(addr.sun_path, s_ipc_sock_path, sizeof(addr.sun_path));

    if (bind(s_ipc_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        xutil_log_err("MAIN", "Failed to bind IPC socket to '%s': %s", s_ipc_sock_path, strerror(errno));
        close(s_ipc_listen_fd);
        s_ipc_listen_fd = -1;
        return false;
    }

    if (listen(s_ipc_listen_fd, 5) < 0) {
        xutil_log_err("MAIN", "Failed to listen on IPC socket: %s", strerror(errno));
        close(s_ipc_listen_fd);
        s_ipc_listen_fd = -1;
        return false;
    }

    s_ipc_channel = g_io_channel_unix_new(s_ipc_listen_fd);
    s_ipc_watch_id = g_io_add_watch(s_ipc_channel, G_IO_IN | G_IO_HUP, on_ipc_client_connected, NULL);

    xutil_log("MAIN", "IPC socket listening on '%s'.", s_ipc_sock_path);
    return true;
}

static void ipc_server_cleanup(void) {
    if (s_ipc_watch_id > 0) {
        g_source_remove(s_ipc_watch_id);
        s_ipc_watch_id = 0;
    }
    if (s_ipc_channel) {
        g_io_channel_unref(s_ipc_channel);
        s_ipc_channel = NULL;
    }
    if (s_ipc_listen_fd >= 0) {
        close(s_ipc_listen_fd);
        s_ipc_listen_fd = -1;
    }
    if (s_ipc_sock_path[0] != '\0') {
        unlink(s_ipc_sock_path);
    }
}

static gboolean on_sighup_signal(gpointer user_data) {
    (void)user_data;
    xutil_log("MAIN", "SIGHUP received. Reloading configuration...");
    config_reload();
    overlay_apply_config();
    theme_reload_from_config();
    capture_guard_reload_config();
    hotkey_reload_grabs();
    return G_SOURCE_CONTINUE;
}

static gboolean on_shutdown_signal(gpointer user_data) {
    (void)user_data;
    xutil_log("MAIN", "Shutdown signal received. Performing clean teardown...");

    lifecycle_cleanup();
    capture_guard_cleanup();
    overlay_hide();
    input_cleanup();
    raise_cleanup();
    hotkey_cleanup();
    chat_view_cleanup();
    window_cleanup();
    config_cleanup();
    ipc_server_cleanup();

    if (gtk_main_level() > 0) {
        gtk_main_quit();
    }
    xutil_log("MAIN", "Clean exit completed.");
    return G_SOURCE_REMOVE;
}

int main(int argc, char **argv) {
    /* Line-buffered stdout/stderr for immediate log visibility */
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    if(argc==2 && (!strcmp(argv[1],"--version") || !strcmp(argv[1],"version"))) {
        puts("Cronq " CRONQ_VERSION); return 0;
    }
    if(argc>=2 && (!strcmp(argv[1],"update") || !strcmp(argv[1],"--update"))) {
        if(argc>3 || (argc==3 && strcmp(argv[2],"--yes"))) { fputs("Usage: cronq update [--yes]\n",stderr);return 1; }
        char *binary=g_file_read_link("/proc/self/exe",NULL);
        if(!binary) { fputs("Cannot locate Cronq executable\n",stderr);return 1; }
        char *dir=g_path_get_dirname(binary);
        char *script=g_build_filename(dir,"..","scripts","cronq-update.py",NULL);
        if(!g_file_test(script,G_FILE_TEST_IS_REGULAR)) {
            g_free(script);script=g_build_filename(g_get_user_data_dir(),"cronq","update.py",NULL);
        }
        if(!g_file_test(script,G_FILE_TEST_IS_REGULAR)) { fputs("Updater is not installed\n",stderr);return 1; }
        execlp("python3","python3",script,"--current",CRONQ_VERSION,"--binary",binary,
               argc==3?"--yes":NULL,(char *)NULL);
        perror("Cannot launch updater"); return 1;
    }

    /* If no arguments passed: if daemon is already running, bring it up! */
    if (argc == 1) {
        char resp[256] = {0};
        if (send_ipc_command("SHOW", resp, sizeof(resp))) {
            printf("Cronq: Brought up.\n");
            return 0;
        }
    }

    /* Check CLI arguments first */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--kill") == 0 || strcmp(argv[i], "kill") == 0 ||
            strcmp(argv[i], "--stop") == 0 || strcmp(argv[i], "stop") == 0 ||
            strcmp(argv[i], "--quit") == 0 || strcmp(argv[i], "quit") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("KILL", resp, sizeof(resp))) {
                printf("Cronq: Killed.\n");
                return 0;
            } else {
                printf("Cronq is not running.\n");
                return 0;
            }
        } else if (strcmp(argv[i], "--toggle") == 0 || strcmp(argv[i], "toggle") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("TOGGLE", resp, sizeof(resp))) {
                printf("Cronq: Toggled visibility.\n");
                return 0;
            } else {
                fprintf(stderr, "Error: Cronq is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--hide") == 0 || strcmp(argv[i], "hide") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("HIDE", resp, sizeof(resp))) {
                printf("Cronq: Overlay hidden.\n");
                return 0;
            } else {
                fprintf(stderr, "Error: Cronq is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--mode") == 0) {
            char cmd[64] = "MODE";
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                const char *mode=argv[++i];
                if(strcmp(mode,"normal") && strcmp(mode,"overlay")){fprintf(stderr,"Error: --mode expects normal or overlay.\n");return 1;}
                snprintf(cmd, sizeof(cmd), "MODE %s", mode);
            }
            char resp[256] = {0};
            if (send_ipc_command(cmd, resp, sizeof(resp))) {
                printf("Cronq: %s\n", resp);
                return 0;
            } else {
                fprintf(stderr, "Error: Cronq is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--ghost") == 0) {
            char cmd[64] = "GHOST";
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                snprintf(cmd, sizeof(cmd), "GHOST %s", argv[++i]);
            }
            char resp[256] = {0};
            if (send_ipc_command(cmd, resp, sizeof(resp))) {
                printf("Cronq: %s\n", resp);
                return 0;
            } else {
                fprintf(stderr, "Error: Cronq is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--app") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --app requires an app name (home, chatgpt).\n");
                return 1;
            }
            const char *app=argv[++i];
            if(strcmp(app,"home") && strcmp(app,"chatgpt")){fprintf(stderr,"Error: --app expects home or chatgpt.\n");return 1;}
            char cmd[64];
            snprintf(cmd, sizeof(cmd), "APP %s", app);
            char resp[256] = {0};
            if (send_ipc_command(cmd, resp, sizeof(resp))) {
                printf("Cronq: Switched to '%s'.\n", argv[i]);
                return 0;
            } else {
                fprintf(stderr, "Error: Cronq is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--back") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("BACK", resp, sizeof(resp))) {
                return 0;
            } else {
                fprintf(stderr, "Error: Cronq is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--home") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("HOME", resp, sizeof(resp))) {
                return 0;
            } else {
                fprintf(stderr, "Error: Cronq is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--reload") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("RELOAD", resp, sizeof(resp))) {
                return 0;
            } else {
                fprintf(stderr, "Error: Cronq is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--has-modal") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("HAS_MODAL", resp, sizeof(resp))) {
                printf("%s", resp);
                return 0;
            } else {
                fprintf(stderr, "Error: Cronq is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--close-modal") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("CLOSE_MODAL", resp, sizeof(resp))) {
                return 0;
            } else {
                fprintf(stderr, "Error: Cronq is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--snap") == 0 || strcmp(argv[i], "snap") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("SNAP", resp, sizeof(resp))) {
                return 0;
            } else {
                fprintf(stderr, "Error: Cronq is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--paste") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("PASTE", resp, sizeof(resp))) {
                return 0;
            } else {
                fprintf(stderr, "Error: Cronq is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--reset") == 0 || strcmp(argv[i], "--refresh-all") == 0 || strcmp(argv[i], "reset") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("UNIVERSAL_RESET", resp, sizeof(resp))) {
                printf("Cronq: Universal reset triggered (new state).\n");
                return 0;
            } else {
                fprintf(stderr, "Error: Cronq is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--reload-config") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("RELOAD_CONFIG", resp, sizeof(resp))) {
                printf("Cronq: Configuration reloaded.\n");
                return 0;
            } else {
                fprintf(stderr, "Error: Cronq is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--opacity") == 0 && i + 1 < argc) {
            char cmd[64];
            snprintf(cmd, sizeof(cmd), "SET_OPACITY %s", argv[i + 1]);
            char resp[256] = {0};
            if (send_ipc_command(cmd, resp, sizeof(resp))) {
                return 0;
            } else {
                fprintf(stderr, "Error: Cronq is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--size") == 0 && i + 2 < argc) {
            char cmd[64];
            snprintf(cmd, sizeof(cmd), "SET_SIZE %s %s", argv[i + 1], argv[i + 2]);
            char resp[256] = {0};
            if (send_ipc_command(cmd, resp, sizeof(resp))) {
                return 0;
            } else {
                fprintf(stderr, "Error: Cronq is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--get-geometry") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("GET_GEOMETRY", resp, sizeof(resp))) {
                printf("%s", resp);
                return 0;
            } else {
                fprintf(stderr, "Error: Cronq is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--status") == 0 || strcmp(argv[i], "status") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("STATUS", resp, sizeof(resp))) {
                printf("%s", resp);
                return 0;
            } else {
                fprintf(stderr, "Error: Cronq is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--fullscreen") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("FULLSCREEN", resp, sizeof(resp))) {
                return 0;
            } else {
                fprintf(stderr, "Error: Cronq is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--default-size") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("DEFAULT_SIZE", resp, sizeof(resp))) {
                return 0;
            } else {
                fprintf(stderr, "Error: Cronq is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: cronq [OPTIONS] or %s [OPTIONS]\n", argv[0]);
            printf("Quick commands:\n");
            printf("  cronq                 Bring up overlay (launch if not running)\n");
            printf("  cronq kill            Kill running overlay daemon\n");
            printf("Options / Subcommands:\n");
            printf("  --show, show                  Show / bring up overlay\n");
            printf("  --hide, hide                  Hide running overlay\n");
            printf("  --toggle, toggle              Toggle visibility of running overlay\n");
            printf("  --kill, kill                  Kill running overlay daemon\n");
            printf("  --status, status              Query running overlay status\n");
            printf("  --snap, snap                  Capture screenshot (excluding overlay) to clipboard & disk\n");
            printf("  --mode [normal|overlay]       Set or toggle typing target (Alt+M)\n");
            printf("  --ghost [on|off]              Set or toggle GHOST click-through mode\n");
            printf("  --app <name>                  Switch app view (home, chatgpt)\n");
            printf("  --back                        Navigate back in current app (or home if at start)\n");
            printf("  --home                        Return to Home screen\n");
            printf("  --reload                      Reload active webview\n");
            printf("  --reset, reset                Reset overlay to new-like state (clear history, reload apps)\n");
            printf("  --opacity <val>               Set overlay opacity (0.20 to 1.00)\n");
            printf("  --size <w> <h>                Set overlay dimensions in pixels\n");
            printf("  --fullscreen                  Toggle fullscreen mode\n");
            printf("  --default-size                Fit default size to monitor work area\n");
            printf("  --get-geometry                Print current overlay geometry (X, Y, W, H)\n");
            printf("  --has-modal                   Query if an internal modal popup is displayed\n");
            printf("  --close-modal                 Close internal modal popup if displayed\n");
            printf("  --paste                       Paste clipboard into ChatGPT\n");
            printf("  --reload-config               Reload configuration from config.ini\n");
            printf("  --version                     Print version\n");
            printf("  update [--yes]                Check, verify and install a release\n");
            printf("  --help, -h                    Show this help text\n");
            return 0;
        } else if(strcmp(argv[i],"--show") && strcmp(argv[i],"show") && strcmp(argv[i],"--daemon")) {
            fprintf(stderr,"Error: unknown option: %s\n",argv[i]);return 1;
        }
    }

    /* Check --show or show for already running instance */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--show") == 0 || strcmp(argv[i], "show") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("SHOW", resp, sizeof(resp))) {
                printf("Cronq: Overlay shown.\n");
                return 0;
            }
            /* Not running yet; proceed to launch daemon with start_visible = TRUE */
            break;
        }
    }

    crash_report_init();
    xutil_log("MAIN", "=== Starting Cronq " CRONQ_VERSION " ===");

    /* 0. Initialize Xlib multi-threading support for multiple Display connections */
    XInitThreads();

    /* 1. Install non-fatal X error handlers */
    xutil_install_error_handlers();

    /* 2. Initialize GTK */
    g_setenv("GDK_BACKEND","x11",TRUE);
    gtk_init(&argc, &argv);

    /* 3. Initialize Configuration */
    if (!config_init()) {
        xutil_log_err("MAIN", "Failed to initialize configuration.");
        return 1;
    }

    const AppConfig *cfg = config_get();
    theme_init(cfg->appearance_mode);
    gboolean start_visible = cfg->start_visible;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--show") == 0 || strcmp(argv[i], "show") == 0) {
            start_visible = TRUE;
        }
    }

    /* 4. Single-instance IPC socket setup */
    if (!ipc_server_init()) {
        config_cleanup();
        return 1;
    }

    /* 5. Initialize Input Subsystem */
    if (!input_init()) {
        xutil_log_err("MAIN", "Failed to initialize input subsystem.");
        ipc_server_cleanup();
        config_cleanup();
        return 1;
    }

    /* 6. Initialize WebKitGTK ChatView Subsystem */
    if (!chat_view_init()) {
        xutil_log_err("MAIN", "Failed to initialize ChatGPT subsystem.");
        input_cleanup();
        ipc_server_cleanup();
        config_cleanup();
        return 1;
    }

    /* 7. Initialize Restack Watcher */
    if (!raise_init()) {
        xutil_log_err("MAIN", "Failed to initialize restack watcher.");
        chat_view_cleanup();
        input_cleanup();
        ipc_server_cleanup();
        config_cleanup();
        return 1;
    }

    /* 8. Initialize Window */
    if (!window_init(start_visible)) {
        xutil_log_err("MAIN", "Failed to initialize overlay window.");
        raise_cleanup();
        chat_view_cleanup();
        input_cleanup();
        ipc_server_cleanup();
        config_cleanup();
        return 1;
    }

    /* 9. Initialize Global Hotkey System */
    if (!hotkey_init()) {
        xutil_log_err("MAIN", "Failed to initialize global hotkey system.");
        window_cleanup();
        raise_cleanup();
        chat_view_cleanup();
        input_cleanup();
        ipc_server_cleanup();
        config_cleanup();
        return 1;
    }

    /* 9b. Capture guard: hide the overlay while other programs capture the screen */
    capture_guard_init();
    lifecycle_init();

    /* 10. Setup UNIX signal handlers */
    g_unix_signal_add(SIGINT, on_shutdown_signal, NULL);
    g_unix_signal_add(SIGTERM, on_shutdown_signal, NULL);
    g_unix_signal_add(SIGHUP, on_sighup_signal, NULL);
    xutil_log("MAIN", "Installed SIGINT/SIGTERM/SIGHUP handlers via g_unix_signal_add.");

    if (!start_visible) {
        printf("\n========================================================================\n");
        printf(" Cronq started in BACKGROUND mode (default: HIDDEN)\n");
        printf(" - Press [Alt] + [%c] to toggle visibility.\n", cfg->toggle_key);
        printf(" - Press [Alt] + [%c] to toggle NORMAL / OVERLAY typing.\n", cfg->mode_key);
        printf(" - Press [Alt] + [%c] to toggle GHOST mode.\n", cfg->ghost_key);
        printf(" - CLI control: %s --toggle | --show | --hide | --status\n", argv[0]);
        printf(" - Press [Ctrl] + [C] to exit.\n");
        printf("========================================================================\n\n");
    }

    /* 11. Run GLib main loop */
    xutil_log("MAIN", "Entering main loop.");
    gtk_main();

    xutil_log("MAIN", "Exited main loop cleanly.");
    return 0;
}

