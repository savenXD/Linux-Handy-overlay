#include "chat_view.h"
#include "config.h"
#include "theme.h"
#include "input.h"
#include "hotkey.h"
#include <gdk/gdkx.h>
#include "xutil.h"
#include "window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>

static WebKitWebsiteDataManager *s_data_mgr = NULL;
static WebKitWebContext         *s_context = NULL;

static WebKitWebView             *s_view_chatgpt = NULL;
static GtkWidget                 *s_scroll_chatgpt = NULL;

static WebKitWebView             *s_active_popup = NULL;
static void chat_view_theme_view(WebKitWebView *view);
static char                       s_active_app[32] = "home";

static ChatViewPopupCreatedCallback s_popup_created_cb = NULL;
static ChatViewPopupClosedCallback  s_popup_closed_cb = NULL;
static gpointer                    s_popup_ud = NULL;

static const char *s_chatgpt_enter_script =
#include "chatgpt_script.inc"
;

static void on_load_changed(WebKitWebView *web_view, WebKitLoadEvent load_event, gpointer user_data) {
    (void)user_data;
    if(load_event==WEBKIT_LOAD_COMMITTED) {
        const char *uri=webkit_web_view_get_uri(web_view);
        if(uri && *uri)g_object_set_data_full(G_OBJECT(web_view),"cronq-last-uri",g_strdup(uri),g_free);
    }
    if (load_event == WEBKIT_LOAD_FINISHED && web_view == s_view_chatgpt) {
        webkit_web_view_evaluate_javascript(web_view, s_chatgpt_enter_script, -1, NULL, NULL, NULL, NULL, NULL);
        if (input_get_typing_target() == TYPING_TARGET_OVERLAY) chat_view_prepare_typing();
    }
}

static void on_popup_close(WebKitWebView *web_view, gpointer user_data) {
    (void)user_data;
    xutil_log("CHAT", "Popup WebView emitted 'close' signal.");
    if (s_active_popup == web_view) {
        s_active_popup = NULL;
    }
    if (s_popup_closed_cb) {
        s_popup_closed_cb(GTK_WIDGET(web_view), s_popup_ud);
    }
}

static void on_popup_destroy(GtkWidget *widget, gpointer user_data) {
    (void)user_data;
    if (s_active_popup == (WebKitWebView *)widget) {
        s_active_popup = NULL;
        }
}

static void on_web_process_terminated(WebKitWebView *view, WebKitWebProcessTerminationReason reason, gpointer user_data);
static void configure_webview(WebKitWebView *view,const AppConfig *cfg);

static GtkWidget *on_web_view_create(WebKitWebView *web_view, WebKitNavigationAction *action, gpointer user_data) {
    (void)user_data;
    WebKitNavigationType nav_type = webkit_navigation_action_get_navigation_type(action);
    WebKitURIRequest *req = webkit_navigation_action_get_request(action);
    const char *uri = req ? webkit_uri_request_get_uri(req) : NULL;

    xutil_log("CHAT", "WebView 'create' signal fired: nav_type=%d uri='%s'",
              (int)nav_type, uri ? uri : "");

    /* If it's a simple link click with target=_blank, load in-place within the same view */
    if (nav_type == WEBKIT_NAVIGATION_TYPE_LINK_CLICKED && uri && strlen(uri) > 0 && strcmp(uri, "about:blank") != 0) {
        webkit_web_view_load_uri(web_view, uri);
        return NULL;
    }

    /* Instantiate a related WebKitWebView for popups/OAuth flows */
    WebKitWebView *popup = WEBKIT_WEB_VIEW(webkit_web_view_new_with_related_view(web_view));
    WebKitSettings *settings = webkit_web_view_get_settings(web_view);
    webkit_web_view_set_settings(popup, settings);

    configure_webview(popup,config_get());

    s_active_popup = popup;
    g_signal_connect(popup, "close", G_CALLBACK(on_popup_close), NULL);
    g_signal_connect(popup, "destroy", G_CALLBACK(on_popup_destroy), NULL);


    if (s_popup_created_cb) {
        s_popup_created_cb(GTK_WIDGET(popup), s_popup_ud);
    }

    return GTK_WIDGET(popup);
}

bool chat_view_is_domain_allowed_for_chatgpt(const char *uri) {
    if (!uri || uri[0] == '\0') return true;
    if (g_str_has_prefix(uri, "about:") ||
        g_str_has_prefix(uri, "data:") ||
        g_str_has_prefix(uri, "blob:") ||
        g_str_has_prefix(uri, "javascript:") ||
        g_str_has_prefix(uri, "file:")) {
        return true;
    }

    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_NONE, NULL);
    if (!parsed) return true;
    const char *host = g_uri_get_host(parsed);
    if (!host || host[0] == '\0') {
        g_uri_unref(parsed);
        return true;
    }

    /* Fast-path: Common OAuth, identity, and security challenge providers for ChatGPT */
    if (strstr(host, "google.") != NULL ||
        g_str_has_suffix(host, ".google.com") ||
        g_str_has_suffix(host, "gstatic.com") ||
        g_str_has_suffix(host, "googleusercontent.com") ||
        g_str_has_suffix(host, "googleapis.com") ||
        g_str_has_suffix(host, "recaptcha.net") ||
        g_str_has_suffix(host, "cloudflare.com") ||
        g_str_has_suffix(host, "openai.com") ||
        g_str_has_suffix(host, "oaistatic.com") ||
        g_str_has_suffix(host, "oaiusercontent.com") ||
        g_str_has_suffix(host, "auth0.com") ||
        g_str_has_suffix(host, "microsoft.com") ||
        g_str_has_suffix(host, "microsoftonline.com") ||
        g_str_has_suffix(host, "live.com") ||
        g_str_has_suffix(host, "msftauth.net") ||
        g_str_has_suffix(host, "apple.com")) {
        g_uri_unref(parsed);
        return true;
    }

    const AppConfig *cfg = config_get();
    char list_copy[512];
    g_strlcpy(list_copy, cfg->chatgpt_allowlist, sizeof(list_copy));

    char *saveptr = NULL;
    char *token = strtok_r(list_copy, ",; ", &saveptr);
    bool allowed = false;

    while (token) {
        char *trimmed = g_strstrip(token);
        if (trimmed[0] != '\0') {
            if (g_ascii_strcasecmp(host, trimmed) == 0) {
                allowed = true;
                break;
            }
            size_t hlen = strlen(host);
            size_t tlen = strlen(trimmed);
            if (hlen > tlen + 1 && host[hlen - tlen - 1] == '.') {
                if (g_ascii_strcasecmp(host + (hlen - tlen), trimmed) == 0) {
                    allowed = true;
                    break;
                }
            }
            if (strstr(host, trimmed) != NULL) {
                allowed = true;
                break;
            }
        }
        token = strtok_r(NULL, ",; ", &saveptr);
    }

    g_uri_unref(parsed);
    return allowed;
}

static gboolean on_decide_policy(WebKitWebView *web_view, WebKitPolicyDecision *decision, WebKitPolicyDecisionType type, gpointer user_data) {
    (void)user_data;
    if (type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
        WebKitNavigationPolicyDecision *nav_dec = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
        WebKitNavigationAction *action = webkit_navigation_policy_decision_get_navigation_action(nav_dec);
        WebKitNavigationType nav_type = webkit_navigation_action_get_navigation_type(action);
        WebKitURIRequest *req = webkit_navigation_action_get_request(action);
        if (req && nav_type == WEBKIT_NAVIGATION_TYPE_LINK_CLICKED) {
            const char *uri = webkit_uri_request_get_uri(req);
            if (uri && strlen(uri) > 0 && strcmp(uri, "about:blank") != 0) {
                xutil_log("CHAT", "In-place loading target=_blank link: '%s'", uri);
                webkit_web_view_load_uri(web_view, uri);
                webkit_policy_decision_ignore(decision);
                return TRUE;
            }
        }
    } else if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
        WebKitNavigationPolicyDecision *nav_dec = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
        WebKitNavigationAction *action = webkit_navigation_policy_decision_get_navigation_action(nav_dec);
        WebKitURIRequest *req = webkit_navigation_action_get_request(action);
        if (req && web_view == s_view_chatgpt) {
            const char *uri = webkit_uri_request_get_uri(req);
            bool is_auth = (uri && (strstr(uri, "oauth") || strstr(uri, "auth0") ||
                                    strstr(uri, "accounts.google") || strstr(uri, "signin") ||
                                    strstr(uri, "challenge") || strstr(uri, "login") ||
                                    strstr(uri, "SetSID")));
            if (uri && !is_auth && !chat_view_is_domain_allowed_for_chatgpt(uri)) {
                xutil_log("CHAT", "Blocked external ChatGPT navigation: %s", uri);
                webkit_policy_decision_ignore(decision);
                overlay_show_toast("External website navigation is unavailable");
                return TRUE;
            }
        }
    }
    return FALSE; /* Proceed with default policy */
}

typedef struct { guint timer; guint failures; gint64 last_crash; WebKitWebView *view; } Recovery;
static void recovery_free(gpointer data) {
    Recovery *r=data;
    if(r->timer) g_source_remove(r->timer);
    g_free(r);
}
static gboolean recover_view(gpointer data) {
    Recovery *r=data;
    r->timer=0;
    const char *uri=webkit_web_view_get_uri(r->view);
    if(uri && *uri) webkit_web_view_reload(r->view);
    else {
        const char *saved=g_object_get_data(G_OBJECT(r->view),"cronq-last-uri");
        webkit_web_view_load_uri(r->view,saved?saved:(r->view==s_view_chatgpt?"https://chatgpt.com":"about:blank"));
    }
    return G_SOURCE_REMOVE;
}
static void on_web_process_terminated(WebKitWebView *view, WebKitWebProcessTerminationReason reason, gpointer user_data) {
    (void)user_data;
    input_release_grab();
    Recovery *r=g_object_get_data(G_OBJECT(view),"cronq-recovery");
    if(!r) { r=g_new0(Recovery,1);r->view=view;g_object_set_data_full(G_OBJECT(view),"cronq-recovery",r,recovery_free); }
    gint64 now=g_get_monotonic_time();
    if(now-r->last_crash>60000000) r->failures=0;
    r->last_crash=now;
    guint attempt=r->failures;
    if(r->failures<5)r->failures++;
    guint delay=MIN(30u,1u<<attempt);
    if(r->timer) g_source_remove(r->timer);
    r->timer=g_timeout_add_seconds(delay,recover_view,r);
    xutil_log_err("CHAT","WebKit process terminated (reason %d); recovering in %us",reason,delay);
    overlay_show_toast("ChatGPT is recovering");
}

/* ChatGPT follows the same four appearance palettes as the native controls. */
static char *appearance_script(void) {
    gboolean light=!theme_is_light_text_on_dark();
    const char *bg=light?"#ffffff":"#000000";
    char *palette=g_strdup_printf("{\"light\":%s,\"background\":\"%s\",\"surface\":\"%s\",\"raised\":\"%s\",\"text\":\"%s\",\"muted\":\"%s\",\"border\":\"%s\",\"minimal\":%s}",
        light?"true":"false",bg,light?"#f4f4f4":"#121212",light?"#e5e5e5":"#222222",theme_fg_hex(),light?"#666666":"#a0a0a0",light?"#bbbbbb":"#444444",theme_is_camo()?"true":"false");
    char *js=g_strconcat("(()=>{if(location.hostname!=='chatgpt.com'&&!location.hostname.endsWith('.chatgpt.com'))return;const palette=",palette,
";\n"
"if(!window.__cronqApplyAppearance){\n"
"  let current, style, observer;\n"
"  const apply=()=>{\n"
"    const root=document.documentElement;if(!root||!current)return;\n"
"    if(!observer){observer=new MutationObserver(apply);observer.observe(root,{attributes:true,attributeFilter:['class','style']});}\n"
"    const mode=current.light?'light':'dark', other=current.light?'dark':'light';\n"
"    if(root.classList.contains(other))root.classList.remove(other);\n"
"    if(!root.classList.contains(mode))root.classList.add(mode);\n"
"    if(root.style.colorScheme!==mode)root.style.colorScheme=mode;\n"
"    if(!style||!style.isConnected){style=document.createElement('style');style.id='cronq-appearance';(document.head||root).appendChild(style);}\n"
"    const p=current;\n"
"    const css=`:root{color-scheme:${mode}!important;--bg-primary:${p.background}!important;--bg-secondary:${p.surface}!important;--bg-tertiary:${p.raised}!important;--main-surface-primary:${p.background}!important;--main-surface-secondary:${p.surface}!important;--main-surface-tertiary:${p.raised}!important;--sidebar-surface-primary:${p.surface}!important;--text-primary:${p.text}!important;--text-secondary:${p.muted}!important;--text-tertiary:${p.muted}!important;--border-light:${p.border}!important;--border-medium:${p.border}!important}html,body{background:${p.background}!important;color:${p.text}!important}main{background-color:${p.background}!important}`;\n"
"    const minimal=p.minimal ? `\n"
"      header,nav,aside,footer,#stage-slideover-sidebar,#stage-sidebar,\n"
"      [data-testid=\"sidebar\"],[data-testid=\"model-switcher-dropdown-button\"],\n"
"      [data-testid=\"conversation-options-button\"],[data-testid=\"share-chat-button\"],\n"
"      [data-testid*=\"avatar\"],[data-testid=\"thread-bottom-disclaimer\"],[data-testid=\"chatgpt-logo\"],svg[aria-label=\"ChatGPT\"],\n"
"      article button,article [role=\"group\"],.avatar {display:none!important}\n"
"      html,body,body :where(div,section,article,main,form,header,footer,nav,aside,textarea,[contenteditable]){background:transparent!important;border-color:transparent!important;box-shadow:none!important;border-radius:0!important}\n"
"      body,body :where(p,span,li,div,textarea,[contenteditable],h1,h2,h3,h4,pre,code,button,a){color:${p.text}!important}\n"
"      main{padding-top:0!important}\n"
"      textarea:focus-visible,[contenteditable]:focus-visible{outline:none!important}\n"
"      button[data-testid=\"send-button\"] svg{display:none!important}\n"
"      button[data-testid=\"send-button\"]::after{content:'Send';font-size:12px}\n"
"      button[data-testid=\"send-button\"]{color:${p.text}!important;background:transparent!important;border:0!important}\n"
"    ` : '';\n"
"    if(style.textContent!==css+minimal)style.textContent=css+minimal;\n"
"  };\n"
"  window.__cronqApplyAppearance=p=>{current=p;apply();};\n"
"  document.addEventListener('DOMContentLoaded',apply,{once:true});\n"
"}\n"
"window.__cronqApplyAppearance(palette);})();\n",NULL);
    g_free(palette);return js;
}

/* Native WebKit canvas alpha/color follows the active appearance. */
static void chat_view_theme_view(WebKitWebView *view) {
    if (!view) return;
    ThemeMode mode = theme_get_mode();
    GdkRGBA bg;
    if (theme_is_camo())           { bg.red = bg.green = bg.blue = 0.0; bg.alpha = 0.0; }
    else if (mode == THEME_LIGHT)  { bg.red = bg.green = bg.blue = 1.0; bg.alpha = 1.0; }
    else                           { bg.red = bg.green = bg.blue = 0.0; bg.alpha = 1.0; }
    webkit_web_view_set_background_color(view, &bg);
}


static void chatgpt_install_scripts(bool run_now) {
    if (!s_view_chatgpt) return;
    WebKitUserContentManager *ucm = webkit_web_view_get_user_content_manager(s_view_chatgpt);
    webkit_user_content_manager_remove_all_scripts(ucm);

    WebKitUserScript *enter = webkit_user_script_new(s_chatgpt_enter_script, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
                                                     WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END, NULL, NULL);
    webkit_user_content_manager_add_script(ucm, enter);
    webkit_user_script_unref(enter);

    char *js=appearance_script();
    WebKitUserScript *th = webkit_user_script_new(js, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
                                                  WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, NULL, NULL);
    webkit_user_content_manager_add_script(ucm, th);
    webkit_user_script_unref(th);
    if (run_now) {   /* live, no reload */
#if WEBKIT_CHECK_VERSION(2, 40, 0)
        webkit_web_view_evaluate_javascript(s_view_chatgpt, js, -1, NULL, NULL, NULL, NULL, NULL);
#else
        webkit_web_view_run_javascript(s_view_chatgpt, js, NULL, NULL, NULL);
#endif
    }
    g_free(js);
}

void chat_view_apply_theme(int mode) {
    (void)mode;
    chat_view_theme_view(s_view_chatgpt);
    if (s_active_popup && WEBKIT_IS_WEB_VIEW(s_active_popup)) chat_view_theme_view(s_active_popup);
    chatgpt_install_scripts(true);
}

static const char *s_hover_focus_script =
#include "../shared/hover_focus_script.inc"
;
typedef struct { WebKitWebView *view; int x,y; } HoverRequest;
static gboolean hover_pointer(WebKitWebView *view,int *x,int *y) {
    GtkWidget *widget=GTK_WIDGET(view);
    if(!gtk_widget_get_mapped(widget)||!overlay_is_visible()||hotkey_is_locked()||
       input_get_ghost_mode()==GHOST_MODE_ON||chat_view_get_active_view()!=view) return FALSE;
    GdkWindow *window=gtk_widget_get_window(widget);
    if(!window)return FALSE;
    GdkSeat *seat=gdk_display_get_default_seat(gtk_widget_get_display(widget));
    if(!seat)return FALSE;
    GdkDevice *pointer=gdk_seat_get_pointer(seat);
    gint wx,wy;gdk_window_get_device_position(window,pointer,&wx,&wy,NULL);
    /* A windowless widget shares its ancestor's GdkWindow. */
    if(!gtk_widget_get_has_window(widget)) {
        GtkAllocation allocation;gtk_widget_get_allocation(widget,&allocation);
        wx-=allocation.x;wy-=allocation.y;
    }
    if(wx<0||wy<0||wx>=gtk_widget_get_allocated_width(widget)||wy>=gtk_widget_get_allocated_height(widget))return FALSE;
    *x=wx;*y=wy;return TRUE;
}
static void hover_finished(GObject *object,GAsyncResult *result,gpointer data) {
    HoverRequest *request=data;GError *error=NULL;
    JSCValue *value=webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(object),result,&error);
    int x,y;
    if(value&&jsc_value_to_boolean(value)&&hover_pointer(request->view,&x,&y)&&
       x==request->x&&y==request->y&&input_get_typing_target()==TYPING_TARGET_NONE)
        input_set_typing_target(TYPING_TARGET_OVERLAY);
    if(value)g_object_unref(value);
    g_clear_error(&error);g_object_unref(request->view);g_free(request);
}
static void on_mouse_target_changed(WebKitWebView *view,WebKitHitTestResult *hit,guint modifiers,gpointer data) {
    (void)modifiers;(void)data;
    int x,y;
    GdkPoint *last=g_object_get_data(G_OBJECT(view),"cronq-hover-point");
    if(!last){last=g_new(GdkPoint,1);last->x=last->y=-1;g_object_set_data_full(G_OBJECT(view),"cronq-hover-point",last,g_free);}
    if(!hover_pointer(view,&x,&y)){last->x=last->y=-1;return;}
    if(last->x==x&&last->y==y)return;
    last->x=x;last->y=y;
    if(!webkit_hit_test_result_context_is_editable(hit)||input_get_typing_target()!=TYPING_TARGET_NONE||!hover_pointer(view,&x,&y))return;
    HoverRequest *request=g_new0(HoverRequest,1);request->view=g_object_ref(view);request->x=x;request->y=y;
    char *script=g_strdup_printf("%s(%d,%d,%d,%d)",s_hover_focus_script,x,y,
        gtk_widget_get_allocated_width(GTK_WIDGET(view)),gtk_widget_get_allocated_height(GTK_WIDGET(view)));
    webkit_web_view_evaluate_javascript(view,script,-1,NULL,NULL,NULL,hover_finished,request);g_free(script);
}

static gboolean deny_context_menu(WebKitWebView *view,WebKitContextMenu *menu,GdkEvent *event,WebKitHitTestResult *hit,gpointer data) {
    (void)view;(void)menu;(void)event;(void)hit;(void)data;return TRUE;
}
static gboolean deny_permission(WebKitWebView *view,WebKitPermissionRequest *request,gpointer data) {
    (void)data;
    if(view==s_view_chatgpt && WEBKIT_IS_CLIPBOARD_PERMISSION_REQUEST(request)) webkit_permission_request_allow(request);
    else webkit_permission_request_deny(request);
    return TRUE;
}
static gboolean dismiss_script_dialog(WebKitWebView *view,WebKitScriptDialog *dialog,gpointer data) {
    (void)view;(void)data;
    if(webkit_script_dialog_get_dialog_type(dialog)==WEBKIT_SCRIPT_DIALOG_BEFORE_UNLOAD_CONFIRM)
        webkit_script_dialog_confirm_set_confirmed(dialog,TRUE);
    return TRUE;
}
static void configure_webview(WebKitWebView *view, const AppConfig *cfg) {
    WebKitSettings *settings = webkit_web_view_get_settings(view);
    (void)cfg; /* ChatGPT uses WebKit's current desktop identity. */
    webkit_settings_set_enable_developer_extras(settings, FALSE);
    webkit_settings_set_javascript_can_open_windows_automatically(settings, TRUE);
    webkit_settings_set_enable_smooth_scrolling(settings, TRUE);
    webkit_settings_set_enable_media_stream(settings, TRUE);
    webkit_settings_set_enable_webaudio(settings, TRUE);

    chat_view_theme_view(view);
    g_signal_connect(view,"mouse-target-changed",G_CALLBACK(on_mouse_target_changed),NULL);
    g_signal_connect(view,"context-menu",G_CALLBACK(deny_context_menu),NULL);
    g_signal_connect(view,"permission-request",G_CALLBACK(deny_permission),NULL);
    g_signal_connect(view,"script-dialog",G_CALLBACK(dismiss_script_dialog),NULL);

    gtk_widget_set_has_tooltip(GTK_WIDGET(view),FALSE);
    gtk_widget_add_events(GTK_WIDGET(view), GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);

    g_signal_connect(view, "load-changed", G_CALLBACK(on_load_changed), NULL);
    g_signal_connect(view, "create", G_CALLBACK(on_web_view_create), NULL);
    g_signal_connect(view, "decide-policy", G_CALLBACK(on_decide_policy), NULL);
    g_signal_connect(view, "web-process-terminated", G_CALLBACK(on_web_process_terminated), NULL);
}

static GtkWidget *create_scrolled_container(WebKitWebView *view, int width) {
    (void)width;
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_NONE);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), FALSE);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroll), FALSE);
    gtk_widget_set_hexpand(GTK_WIDGET(view), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(view), TRUE);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(view));
    return scroll;
}

bool chat_view_init(void) {
    const AppConfig *cfg = config_get();

    /* 1. Setup persistent storage paths in ~/.local/share/cronq */
    const char *data_home = g_get_user_data_dir();
    char *base_dir = g_build_filename(data_home, "cronq", NULL);
    char *data_dir = g_build_filename(base_dir, "data", NULL);
    char *cache_dir = g_build_filename(base_dir, "cache", NULL);
    char *cookie_file = g_build_filename(base_dir, "cookies.sqlite", NULL);

    g_mkdir_with_parents(data_dir, 0700);
    g_mkdir_with_parents(cache_dir, 0700);

    /* 2. Create persistent WebKitWebsiteDataManager */
    s_data_mgr = webkit_website_data_manager_new(
        "base-data-directory", data_dir,
        "base-cache-directory", cache_dir,
        NULL
    );

    /* Persistent Cookies (SQLite for modern SameSite & Partitioned auth) */
    WebKitCookieManager *cookie_mgr = webkit_website_data_manager_get_cookie_manager(s_data_mgr);
    webkit_cookie_manager_set_persistent_storage(cookie_mgr, cookie_file, WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
    webkit_cookie_manager_set_accept_policy(cookie_mgr, WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);

    /* 3. Create WebKitWebContext shared across both apps */
    s_context = webkit_web_context_new_with_website_data_manager(s_data_mgr);

    g_free(cookie_file);
    g_free(cache_dir);
    g_free(data_dir);

    /* 5. Create ChatGPT App WebView & Scrolled Container */
    s_view_chatgpt = WEBKIT_WEB_VIEW(webkit_web_view_new_with_context(s_context));
    configure_webview(s_view_chatgpt, cfg);
    s_scroll_chatgpt = create_scrolled_container(s_view_chatgpt, cfg->width);

    /* Enter-to-Submit + theme scripts for the ChatGPT view */
    chatgpt_install_scripts(false);

    /* Preload ChatGPT */
    webkit_web_view_load_uri(s_view_chatgpt, "https://chatgpt.com");

    xutil_log("CHAT", "ChatGPT WebKitGTK subsystem initialized (persistent session at '%s').", base_dir);
    g_free(base_dir);
    return true;
}

GtkWidget *chat_view_get_chatgpt_widget(void) {
    return s_scroll_chatgpt;
}

WebKitWebView *chat_view_get_chatgpt_view(void) {
    return s_view_chatgpt;
}

WebKitWebView *chat_view_get_active_view(void) {
    if (s_active_popup) {
        if (WEBKIT_IS_WEB_VIEW(s_active_popup) && !gtk_widget_in_destruction(GTK_WIDGET(s_active_popup))) {
            return s_active_popup;
        }
        s_active_popup = NULL;
    }
    if (strcmp(s_active_app, "chatgpt") == 0) return s_view_chatgpt;
    return NULL;
}

void chat_view_clear_active_popup(void) {
    s_active_popup = NULL;
}

void chat_view_set_active_view(const char *app_name) {
    if (app_name && strlen(app_name) > 0) {
        g_strlcpy(s_active_app, app_name, sizeof(s_active_app));
    }
}

void chat_view_go_back(void) {
    WebKitWebView *active = chat_view_get_active_view();
    if (active && webkit_web_view_can_go_back(active)) {
        webkit_web_view_go_back(active);
    }
}

void chat_view_reload(void) {
    WebKitWebView *active = chat_view_get_active_view();
    if (active && WEBKIT_IS_WEB_VIEW(active)) {
        webkit_web_view_reload(active);
    }
}

void chat_view_stop(void) {
    WebKitWebView *active = chat_view_get_active_view();
    if (active && WEBKIT_IS_WEB_VIEW(active)) {
        webkit_web_view_stop_loading(active);
    }
}

void chat_view_universal_reset(void) {
    /* 1. Clear any active popup */
    if (s_active_popup) {
        if (WEBKIT_IS_WEB_VIEW(s_active_popup) && !gtk_widget_in_destruction(GTK_WIDGET(s_active_popup))) {
            gtk_widget_destroy(GTK_WIDGET(s_active_popup));
        }
        s_active_popup = NULL;
    }

    /* Preserve the persistent profile and return ChatGPT to its start page. */
    if (s_view_chatgpt) {
        webkit_web_view_load_uri(s_view_chatgpt, "https://chatgpt.com");
    }

    g_strlcpy(s_active_app, "home", sizeof(s_active_app));
    xutil_log("CHAT", "Universal reset executed: returned ChatGPT to its start page; login retained.");
}

bool chat_view_can_go_back(void) {
    WebKitWebView *active = chat_view_get_active_view();
    return (active && WEBKIT_IS_WEB_VIEW(active) && webkit_web_view_can_go_back(active));
}

void chat_view_pause(void) {
    if (s_view_chatgpt) webkit_web_view_set_is_muted(s_view_chatgpt, TRUE);
    if (s_active_popup) webkit_web_view_set_is_muted(s_active_popup, TRUE);
}

void chat_view_resume(void) {
    if (s_view_chatgpt) webkit_web_view_set_is_muted(s_view_chatgpt, FALSE);
    if (s_active_popup) webkit_web_view_set_is_muted(s_active_popup, FALSE);
}

void chat_view_execute_editing_command(const char *command) {
    WebKitWebView *active = chat_view_get_active_view();
    if (active && command) {
        xutil_log("CHAT", "Executing editing command '%s' on active view.", command);
        webkit_web_view_execute_editing_command(active, command);
    }
}

void chat_view_paste(void) {
    WebKitWebView *active = chat_view_get_active_view();
    if (!active) return;

    GtkClipboard *clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gboolean has_image = FALSE;

    /* Check whether an image is available on the clipboard */
    if (gtk_clipboard_wait_is_image_available(clip)) {
        has_image = TRUE;
    } else {
        GdkAtom *targets = NULL;
        gint n_targets = 0;
        if (gtk_clipboard_wait_for_targets(clip, &targets, &n_targets)) {
            for (int i = 0; i < n_targets; i++) {
                gchar *tname = gdk_atom_name(targets[i]);
                if (tname) {
                    if (g_ascii_strcasecmp(tname, "image/png") == 0 ||
                        g_str_has_prefix(tname, "image/")) {
                        has_image = TRUE;
                        g_free(tname);
                        break;
                    }
                    g_free(tname);
                }
            }
            g_free(targets);
        }
    }

    if (has_image) {
        GdkPixbuf *pb = gtk_clipboard_wait_for_image(clip);
        gchar *b64 = NULL;

        if (pb) {
            gchar *buffer = NULL;
            gsize buffer_size = 0;
            GError *err = NULL;
            if (gdk_pixbuf_save_to_bufferv(pb, &buffer, &buffer_size, "png", NULL, NULL, &err)) {
                b64 = g_base64_encode((const guchar *)buffer, buffer_size);
                g_free(buffer);
            } else if (err) {
                xutil_log("CHAT", "Failed to encode clipboard pixbuf to PNG: %s", err->message);
                g_error_free(err);
            }
            g_object_unref(pb);
        }

        if (b64) {
            xutil_log("CHAT", "Injecting synthetic image paste event (b64 length=%zu)", strlen(b64));
            gchar *js = g_strdup_printf(
                "(function() {\n"
                "  try {\n"
                "    var b64 = \"%s\";\n"
                "    var byteChars = atob(b64);\n"
                "    var byteNums = new Array(byteChars.length);\n"
                "    for (var i = 0; i < byteChars.length; i++) {\n"
                "      byteNums[i] = byteChars.charCodeAt(i);\n"
                "    }\n"
                "    var byteArray = new Uint8Array(byteNums);\n"
                "    var blob = new Blob([byteArray], { type: 'image/png' });\n"
                "    var file = new File([blob], 'screenshot.png', { type: 'image/png', lastModified: Date.now() });\n"
                "    var dt = new DataTransfer();\n"
                "    dt.items.add(file);\n"
                "    var target = document.activeElement || document.body;\n"
                "    var pasteEvent = new ClipboardEvent('paste', {\n"
                "      clipboardData: dt,\n"
                "      bubbles: true,\n"
                "      cancelable: true\n"
                "    });\n"
                "    target.dispatchEvent(pasteEvent);\n"
                "  } catch(e) {\n"
                "    console.error('cronq image paste error:', e);\n"
                "  }\n"
                "})();",
                b64
            );
#if WEBKIT_CHECK_VERSION(2, 40, 0)
            webkit_web_view_evaluate_javascript(active, js, -1, NULL, NULL, NULL, NULL, NULL);
#else
            webkit_web_view_run_javascript(active, js, NULL, NULL, NULL);
#endif
            g_free(js);
            g_free(b64);
            return;
        }
        overlay_show_toast("Could not read the copied image");
        return;
    }

    /* Fallback or non-image paste: native WebKit editing command */
    xutil_log("CHAT", "Executing native paste editing command on active view.");
    webkit_web_view_execute_editing_command(active, WEBKIT_EDITING_COMMAND_PASTE);
}

void chat_view_set_popup_callbacks(ChatViewPopupCreatedCallback on_created,
                                 ChatViewPopupClosedCallback on_closed,
                                 gpointer user_data) {
    s_popup_created_cb = on_created;
    s_popup_closed_cb = on_closed;
    s_popup_ud = user_data;
}

void chat_view_cleanup(void) {
    if (s_view_chatgpt) {
        gtk_widget_destroy(s_scroll_chatgpt);
        s_view_chatgpt = NULL;
        s_scroll_chatgpt = NULL;
    }
    if (s_active_popup) {
        s_active_popup = NULL;
    }
    if (s_context) {
        g_object_unref(s_context);
        s_context = NULL;
    }
    if (s_data_mgr) {
        g_object_unref(s_data_mgr);
        s_data_mgr = NULL;
    }
    xutil_log("CHAT", "WebKitGTK ChatGPT subsystem cleaned up.");
}

void chat_view_prepare_typing(void) {
    WebKitWebView *view=chat_view_get_active_view();
    if(!view) return;
    GtkWidget *win=overlay_get_window_widget();
    if(win) gtk_window_set_focus(GTK_WINDOW(win),GTK_WIDGET(view));
    const char *script="(()=>{const ok=e=>e&&!e.disabled&&!e.readOnly&&e.getClientRects().length&&(e.isContentEditable||e.tagName==='TEXTAREA'||(e.tagName==='INPUT'&&/^(text|email|search|password|tel|number|url)$/i.test(e.type)));if(ok(document.activeElement))return;const p=document.getElementById('prompt-textarea');const e=ok(p)?p:[...document.querySelectorAll('input,textarea,[contenteditable]')].find(ok);if(e)e.focus({preventScroll:true});})()";
    webkit_web_view_evaluate_javascript(view,script,-1,NULL,NULL,NULL,NULL,NULL);
}
