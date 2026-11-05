#include "browser.h"
#include "config.h"
#include "theme.h"
#include "input.h"
#include "xutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>

static WebKitWebsiteDataManager *s_data_mgr = NULL;
static WebKitWebContext         *s_context = NULL;

static WebKitWebView             *s_view_browser = NULL;
static GtkWidget                 *s_scroll_browser = NULL;

static WebKitWebView             *s_view_chatgpt = NULL;
static GtkWidget                 *s_scroll_chatgpt = NULL;

static WebKitWebView             *s_active_popup = NULL;
static void browser_theme_view(WebKitWebView *view);
static char                       s_active_app[32] = "browser";

static BrowserProgressCallback   s_progress_cb = NULL;
static gpointer                  s_progress_ud = NULL;

static BrowserTitleCallback      s_title_cb = NULL;
static gpointer                  s_title_ud = NULL;

static BrowserUriCallback        s_uri_cb = NULL;
static gpointer                  s_uri_ud = NULL;

static BrowserStateCallback      s_state_cb = NULL;
static gpointer                  s_state_ud = NULL;

static BrowserPopupCreatedCallback s_popup_created_cb = NULL;
static BrowserPopupClosedCallback  s_popup_closed_cb = NULL;
static gpointer                    s_popup_ud = NULL;

static BrowserDivertCallback     s_divert_cb = NULL;
static gpointer                  s_divert_ud = NULL;

static BrowserClickCallback      s_click_cb = NULL;
static gpointer                  s_click_ud = NULL;

static const char *s_chatgpt_enter_script =
    "(function() {\n"
    "  function injectCaretStyle() {\n"
    "    if (document.getElementById('__cronq_caret_style')) return;\n"
    "    var style = document.createElement('style');\n"
    "    style.id = '__cronq_caret_style';\n"
    "    style.textContent = 'input, textarea, [contenteditable], #prompt-textarea, .ProseMirror { caret-color: #ffffff !important; }';\n"
    "    (document.head || document.documentElement).appendChild(style);\n"
    "  }\n"
    "  injectCaretStyle();\n"
    "  if (document.readyState === 'loading') {\n"
    "    document.addEventListener('DOMContentLoaded', injectCaretStyle);\n"
    "  }\n"
    "\n"
    "  if (window.__cronq_chatgpt_enter_installed) return;\n"
    "  window.__cronq_chatgpt_enter_installed = true;\n"
    "\n"
    "  function findSendButton() {\n"
    "    var btn = document.querySelector('button[data-testid=\"send-button\"]') ||\n"
    "              document.querySelector('button[data-testid=\"fruitjuice-send-button\"]');\n"
    "    if (btn && !btn.disabled) return btn;\n"
    "\n"
    "    btn = document.querySelector('button[aria-label*=\"Send\" i]') ||\n"
    "          document.querySelector('button[aria-label*=\"Submit\" i]');\n"
    "    if (btn && !btn.disabled) return btn;\n"
    "\n"
    "    var promptEl = document.querySelector('#prompt-textarea') || document.querySelector('textarea');\n"
    "    if (promptEl) {\n"
    "      var form = promptEl.closest('form');\n"
    "      if (form) {\n"
    "        var btns = Array.from(form.querySelectorAll('button:not(:disabled)'));\n"
    "        for (var i = 0; i < btns.length; i++) {\n"
    "          var b = btns[i];\n"
    "          var aria = (b.getAttribute('aria-label') || '').toLowerCase();\n"
    "          var testid = (b.getAttribute('data-testid') || '').toLowerCase();\n"
    "          if (aria.includes('add') || aria.includes('attach') || aria.includes('voice') || aria.includes('mic')) continue;\n"
    "          if (testid.includes('voice') || testid.includes('mic') || testid.includes('attach')) continue;\n"
    "          return b;\n"
    "        }\n"
    "      }\n"
    "    }\n"
    "    return null;\n"
    "  }\n"
    "\n"
    "  function triggerSubmit() {\n"
    "    var btn = findSendButton();\n"
    "    if (btn && !btn.disabled) {\n"
    "      btn.focus();\n"
    "      btn.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, cancelable: true, view: window }));\n"
    "      btn.dispatchEvent(new MouseEvent('mousedown', { bubbles: true, cancelable: true, view: window }));\n"
    "      btn.dispatchEvent(new PointerEvent('pointerup', { bubbles: true, cancelable: true, view: window }));\n"
    "      btn.dispatchEvent(new MouseEvent('mouseup', { bubbles: true, cancelable: true, view: window }));\n"
    "      btn.click();\n"
    "      return true;\n"
    "    }\n"
    "    var promptEl = document.querySelector('#prompt-textarea') || document.querySelector('textarea');\n"
    "    var form = promptEl ? promptEl.closest('form') : document.querySelector('form');\n"
    "    if (form && typeof form.requestSubmit === 'function') {\n"
    "      try { form.requestSubmit(); return true; } catch(err) {}\n"
    "    }\n"
    "    return false;\n"
    "  }\n"
    "\n"
    "  window.addEventListener('keydown', function(e) {\n"
    "    if (e.key === 'Enter' && !e.shiftKey && !e.ctrlKey && !e.altKey && !e.metaKey && !e.isComposing) {\n"
    "      var target = e.target;\n"
    "      if (!target) return;\n"
    "      var isPrompt = (target.id === 'prompt-textarea') ||\n"
    "                     (target.tagName === 'TEXTAREA') ||\n"
    "                     (target.isContentEditable) ||\n"
    "                     (target.closest && target.closest('#prompt-textarea'));\n"
    "      if (isPrompt) {\n"
    "        e.preventDefault();\n"
    "        e.stopPropagation();\n"
    "        e.stopImmediatePropagation();\n"
    "        triggerSubmit();\n"
    "      }\n"
    "    }\n"
    "  }, true);\n"
    "\n"
    "  document.addEventListener('pointerup', function(e) {\n"
    "    var btn = e.target && e.target.closest && (e.target.closest('button[data-testid=\"send-button\"]') || e.target.closest('button[data-testid=\"fruitjuice-send-button\"]'));\n"
    "    if (btn && !btn.disabled) {\n"
    "      setTimeout(function() { triggerSubmit(); }, 50);\n"
    "    }\n"
    "  }, true);\n"
    "})();";

static void notify_state_changed(void) {
    WebKitWebView *active = browser_get_active_view();
    if (s_state_cb && active) {
        bool is_loading = webkit_web_view_is_loading(active);
        bool can_back = webkit_web_view_can_go_back(active);
        bool can_forward = webkit_web_view_can_go_forward(active);
        s_state_cb(is_loading, can_back, can_forward, s_state_ud);
    }
}

static void on_load_changed(WebKitWebView *web_view, WebKitLoadEvent load_event, gpointer user_data) {
    (void)user_data;
    if (web_view == browser_get_active_view()) {
        notify_state_changed();
    }

    if (load_event == WEBKIT_LOAD_COMMITTED) {
        const char *uri = webkit_web_view_get_uri(web_view);
        if (web_view == browser_get_active_view() && s_uri_cb && uri) {
            s_uri_cb(uri, s_uri_ud);
        }
    } else if (load_event == WEBKIT_LOAD_FINISHED) {
        const char *title = webkit_web_view_get_title(web_view);
        if (web_view == browser_get_active_view() && s_title_cb && title) {
            s_title_cb(title, s_title_ud);
        }
        if (web_view == s_view_chatgpt) {
#if WEBKIT_CHECK_VERSION(2, 40, 0)
            webkit_web_view_evaluate_javascript(s_view_chatgpt, s_chatgpt_enter_script, -1, NULL, NULL, NULL, NULL, NULL);
#else
            webkit_web_view_run_javascript(s_view_chatgpt, s_chatgpt_enter_script, NULL, NULL, NULL);
#endif
        }
    }
}

static void on_notify_progress(GObject *object, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    (void)user_data;
    WebKitWebView *view = WEBKIT_WEB_VIEW(object);
    if (view == browser_get_active_view() && s_progress_cb) {
        double p = webkit_web_view_get_estimated_load_progress(view);
        s_progress_cb(p, s_progress_ud);
    }
}

static void on_notify_title(GObject *object, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    (void)user_data;
    WebKitWebView *view = WEBKIT_WEB_VIEW(object);
    if (view == browser_get_active_view() && s_title_cb) {
        const char *title = webkit_web_view_get_title(view);
        s_title_cb(title ? title : "", s_title_ud);
    }
}

static void on_notify_uri(GObject *object, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    (void)user_data;
    WebKitWebView *view = WEBKIT_WEB_VIEW(object);
    if (view == browser_get_active_view() && s_uri_cb) {
        const char *uri = webkit_web_view_get_uri(view);
        s_uri_cb(uri ? uri : "", s_uri_ud);
    }
}

static void on_popup_close(WebKitWebView *web_view, gpointer user_data) {
    (void)user_data;
    xutil_log("BROWSER", "Popup WebView emitted 'close' signal.");
    if (s_active_popup == web_view) {
        s_active_popup = NULL;
    }
    if (s_popup_closed_cb) {
        s_popup_closed_cb(GTK_WIDGET(web_view), s_popup_ud);
    }
    notify_state_changed();
}

static void on_popup_destroy(GtkWidget *widget, gpointer user_data) {
    (void)user_data;
    if (s_active_popup == (WebKitWebView *)widget) {
        s_active_popup = NULL;
        notify_state_changed();
    }
}

static void on_web_process_terminated(WebKitWebView *view, WebKitWebProcessTerminationReason reason, gpointer user_data);

static GtkWidget *on_web_view_create(WebKitWebView *web_view, WebKitNavigationAction *action, gpointer user_data) {
    (void)user_data;
    WebKitNavigationType nav_type = webkit_navigation_action_get_navigation_type(action);
    WebKitURIRequest *req = webkit_navigation_action_get_request(action);
    const char *uri = req ? webkit_uri_request_get_uri(req) : NULL;

    xutil_log("BROWSER", "WebView 'create' signal fired: nav_type=%d uri='%s'",
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

    browser_theme_view(popup);

    s_active_popup = popup;
    g_signal_connect(popup, "close", G_CALLBACK(on_popup_close), NULL);
    g_signal_connect(popup, "destroy", G_CALLBACK(on_popup_destroy), NULL);
    g_signal_connect(popup, "web-process-terminated", G_CALLBACK(on_web_process_terminated), NULL);

    if (s_popup_created_cb) {
        s_popup_created_cb(GTK_WIDGET(popup), s_popup_ud);
    }

    return GTK_WIDGET(popup);
}

bool browser_is_domain_allowed_for_chatgpt(const char *uri) {
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
                xutil_log("BROWSER", "In-place loading target=_blank link: '%s'", uri);
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
            if (uri && !is_auth && !browser_is_domain_allowed_for_chatgpt(uri)) {
                xutil_log("BROWSER", "ChatGPT navigating to external domain '%s' -> Diverting to Browser app.", uri);
                webkit_policy_decision_ignore(decision);
                if (s_divert_cb) {
                    s_divert_cb(uri, s_divert_ud);
                }
                return TRUE;
            }
        }
    }
    return FALSE; /* Proceed with default policy */
}

static gboolean on_webview_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    (void)event;
    (void)user_data;
    if (s_click_cb) {
        s_click_cb(s_click_ud);
    }
    return FALSE; /* allow WebKit to process click */
}

static void on_web_process_terminated(WebKitWebView *view, WebKitWebProcessTerminationReason reason, gpointer user_data) {
    (void)user_data;
    const char *reason_str = "unknown";
    if (reason == WEBKIT_WEB_PROCESS_CRASHED) reason_str = "crashed";
    else if (reason == WEBKIT_WEB_PROCESS_EXCEEDED_MEMORY_LIMIT) reason_str = "exceeded memory limit";
    else if (reason == WEBKIT_WEB_PROCESS_TERMINATED_BY_API) reason_str = "terminated by API";

    const char *uri = webkit_web_view_get_uri(view);
    xutil_log_err("BROWSER", "WebKit WebProcess terminated (reason: %s) while displaying '%s'. Auto-recovering...",
                  reason_str, uri ? uri : "unknown");

    if (uri && uri[0] != '\0') {
        webkit_web_view_reload(view);
    } else {
        if (view == s_view_chatgpt) {
            webkit_web_view_load_uri(view, "https://chatgpt.com");
        } else {
            const AppConfig *cfg = config_get();
            const char *fallback = (cfg && cfg->home_url[0] != '\0') ? cfg->home_url : "about:blank";
            webkit_web_view_load_uri(view, fallback);
        }
    }
}

/* ---- Theming of web content ----
 * ChatGPT and other sites use their own light/dark setting (ChatGPT: Settings > Appearance). Cronq only
 * overrides ChatGPT in camouflage mode, where it needs a transparent, text-only view (as on Windows). */
static const char *s_camo_chatgpt_js =
    "(()=>{if(location.hostname!=='chatgpt.com'&&!location.hostname.endsWith('.chatgpt.com'))return;"
    "const TEXT='%s';"
    "if(!window.__cronqCamo){"
    "let style;"
    "const apply=()=>{const root=document.documentElement;if(!root)return;"
    "if(!style||!style.isConnected){style=document.createElement('style');style.id='cronq-camo';(document.head||root).appendChild(style);}"
    "const css=`header,nav,aside,footer,#stage-slideover-sidebar,#stage-sidebar,"
    "[data-testid=\"sidebar\"],[data-testid=\"model-switcher-dropdown-button\"],"
    "[data-testid=\"conversation-options-button\"],[data-testid=\"share-chat-button\"],"
    "[data-testid*=\"avatar\"],[data-testid=\"thread-bottom-disclaimer\"],[data-testid=\"chatgpt-logo\"],svg[aria-label=\"ChatGPT\"],"
    "article button,article [role=\"group\"],.avatar{display:none!important}"
    "html,body,body :where(div,section,article,main,form,header,footer,nav,aside,textarea,[contenteditable]){background:transparent!important;border-color:transparent!important;box-shadow:none!important;border-radius:0!important}"
    "body,body :where(p,span,li,div,textarea,[contenteditable],h1,h2,h3,h4,pre,code,button,a){color:${window.__cronqCamoText}!important}"
    "main{padding-top:0!important}"
    "textarea:focus-visible,[contenteditable]:focus-visible{outline:none!important}"
    "button[data-testid=\"send-button\"] svg{display:none!important}"
    "button[data-testid=\"send-button\"]::after{content:'Send';font-size:12px}"
    "button[data-testid=\"send-button\"]{color:${window.__cronqCamoText}!important;background:transparent!important;border:0!important}`;"
    "if(style.textContent!==css)style.textContent=css;};"
    "window.__cronqCamo=(t)=>{window.__cronqCamoText=t;apply();};"
    "document.addEventListener('DOMContentLoaded',apply,{once:true});}"
    "window.__cronqCamo(TEXT);})();";

/* Canvas colour only; pages keep their own colour scheme. */
static void browser_theme_view(WebKitWebView *view) {
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

    if (!theme_is_camo()) {
        /* Leaving camouflage: drop the injected style in place; the page keeps its state (no reload). */
        if (run_now) {
            const char *undo = "(()=>{const s=document.getElementById('cronq-camo');if(s)s.remove();delete window.__cronqCamo;})();";
#if WEBKIT_CHECK_VERSION(2, 40, 0)
            webkit_web_view_evaluate_javascript(s_view_chatgpt, undo, -1, NULL, NULL, NULL, NULL, NULL);
#else
            webkit_web_view_run_javascript(s_view_chatgpt, undo, NULL, NULL, NULL);
#endif
        }
        return;
    }
    char *js = g_strdup_printf(s_camo_chatgpt_js, theme_fg_hex());
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

void browser_apply_theme(int mode) {
    (void)mode;
    browser_theme_view(s_view_browser);
    browser_theme_view(s_view_chatgpt);
    if (s_active_popup && WEBKIT_IS_WEB_VIEW(s_active_popup)) browser_theme_view(s_active_popup);
    chatgpt_install_scripts(true);
}

static void configure_webview(WebKitWebView *view, const AppConfig *cfg) {
    WebKitSettings *settings = webkit_web_view_get_settings(view);
    webkit_settings_set_user_agent(settings, cfg->user_agent);
    webkit_settings_set_enable_developer_extras(settings, TRUE);
    webkit_settings_set_javascript_can_open_windows_automatically(settings, TRUE);
    webkit_settings_set_enable_smooth_scrolling(settings, TRUE);
    webkit_settings_set_enable_media_stream(settings, TRUE);
    webkit_settings_set_enable_webaudio(settings, TRUE);

    browser_theme_view(view);

    gtk_widget_add_events(GTK_WIDGET(view), GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    g_signal_connect(view, "button-press-event", G_CALLBACK(on_webview_button_press), NULL);

    g_signal_connect(view, "load-changed", G_CALLBACK(on_load_changed), NULL);
    g_signal_connect(view, "notify::estimated-load-progress", G_CALLBACK(on_notify_progress), NULL);
    g_signal_connect(view, "notify::title", G_CALLBACK(on_notify_title), NULL);
    g_signal_connect(view, "notify::uri", G_CALLBACK(on_notify_uri), NULL);
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

bool browser_init(void) {
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

    /* 4. Create Browser App WebView & Scrolled Container */
    s_view_browser = WEBKIT_WEB_VIEW(webkit_web_view_new_with_context(s_context));
    configure_webview(s_view_browser, cfg);
    s_scroll_browser = create_scrolled_container(s_view_browser, cfg->width);

    /* 5. Create ChatGPT App WebView & Scrolled Container */
    s_view_chatgpt = WEBKIT_WEB_VIEW(webkit_web_view_new_with_context(s_context));
    configure_webview(s_view_chatgpt, cfg);
    s_scroll_chatgpt = create_scrolled_container(s_view_chatgpt, cfg->width);

    /* Enter-to-Submit + theme scripts for the ChatGPT view */
    chatgpt_install_scripts(false);

    /* Preload ChatGPT */
    webkit_web_view_load_uri(s_view_chatgpt, "https://chatgpt.com");

    /* Preload Browser home URL */
    if (cfg->home_url[0] != '\0') {
        browser_load_in_browser(cfg->home_url);
    }

    xutil_log("BROWSER", "Dual-app WebKitGTK subsystem initialized (persistent session at '%s').", base_dir);
    g_free(base_dir);
    return true;
}

GtkWidget *browser_get_browser_widget(void) {
    return s_scroll_browser;
}

WebKitWebView *browser_get_browser_view(void) {
    return s_view_browser;
}

GtkWidget *browser_get_chatgpt_widget(void) {
    return s_scroll_chatgpt;
}

WebKitWebView *browser_get_chatgpt_view(void) {
    return s_view_chatgpt;
}

GtkWidget *browser_get_widget(void) {
    return s_scroll_browser;
}

WebKitWebView *browser_get_view(void) {
    return s_view_browser;
}

WebKitWebView *browser_get_active_view(void) {
    if (s_active_popup) {
        if (WEBKIT_IS_WEB_VIEW(s_active_popup) && !gtk_widget_in_destruction(GTK_WIDGET(s_active_popup))) {
            return s_active_popup;
        }
        s_active_popup = NULL;
    }
    if (strcmp(s_active_app, "chatgpt") == 0) return s_view_chatgpt;
    return s_view_browser;
}

void browser_clear_active_popup(void) {
    s_active_popup = NULL;
    notify_state_changed();
}

void browser_set_active_view(const char *app_name) {
    if (app_name && strlen(app_name) > 0) {
        g_strlcpy(s_active_app, app_name, sizeof(s_active_app));
    }
    notify_state_changed();
}

static void load_uri_internal(WebKitWebView *view, const char *input) {
    if (!view || !input || input[0] == '\0') return;

    char *trimmed = g_strstrip(g_strdup(input));
    if (trimmed[0] == '\0') {
        g_free(trimmed);
        return;
    }

    if (g_str_has_prefix(trimmed, "http://") ||
        g_str_has_prefix(trimmed, "https://") ||
        g_str_has_prefix(trimmed, "file://") ||
        g_str_has_prefix(trimmed, "about:")) {
        webkit_web_view_load_uri(view, trimmed);
    } else if (!strchr(trimmed, ' ') && (strchr(trimmed, '.') != NULL || g_str_has_prefix(trimmed, "localhost"))) {
        char full_url[1024];
        snprintf(full_url, sizeof(full_url), "https://%s", trimmed);
        webkit_web_view_load_uri(view, full_url);
    } else {
        char *escaped = g_uri_escape_string(trimmed, NULL, TRUE);
        char search_url[1024];
        const char *tmpl = config_get()->search_url_template;
        if (strstr(tmpl, "%s")) {
            snprintf(search_url, sizeof(search_url), tmpl, escaped);
        } else {
            snprintf(search_url, sizeof(search_url), "https://duckduckgo.com/?q=%s", escaped);
        }
        g_free(escaped);
        webkit_web_view_load_uri(view, search_url);
    }
    g_free(trimmed);
}

void browser_load(const char *input) {
    WebKitWebView *active = browser_get_active_view();
    load_uri_internal(active, input);
}

void browser_load_in_browser(const char *input) {
    load_uri_internal(s_view_browser, input);
}

void browser_load_in_chatgpt(const char *input) {
    load_uri_internal(s_view_chatgpt, input);
}

void browser_go_back(void) {
    WebKitWebView *active = browser_get_active_view();
    if (active && webkit_web_view_can_go_back(active)) {
        webkit_web_view_go_back(active);
    }
}

void browser_go_forward(void) {
    WebKitWebView *active = browser_get_active_view();
    if (active && webkit_web_view_can_go_forward(active)) {
        webkit_web_view_go_forward(active);
    }
}

void browser_reload(void) {
    WebKitWebView *active = browser_get_active_view();
    if (active && WEBKIT_IS_WEB_VIEW(active)) {
        webkit_web_view_reload(active);
    }
}

void browser_stop(void) {
    WebKitWebView *active = browser_get_active_view();
    if (active && WEBKIT_IS_WEB_VIEW(active)) {
        webkit_web_view_stop_loading(active);
    }
}

void browser_go_home(void) {
    const AppConfig *cfg = config_get();
    if (cfg->home_url[0] != '\0') {
        browser_load_in_browser(cfg->home_url);
    }
}

void browser_universal_reset(void) {
    const AppConfig *cfg = config_get();

    /* 1. Clear any active popup */
    if (s_active_popup) {
        if (WEBKIT_IS_WEB_VIEW(s_active_popup) && !gtk_widget_in_destruction(GTK_WIDGET(s_active_popup))) {
            gtk_widget_destroy(GTK_WIDGET(s_active_popup));
        }
        s_active_popup = NULL;
    }

    /* 2. Reset Browser App WebView (recreating child resets history cleanly) */
    if (s_scroll_browser) {
        GtkWidget *old_br = gtk_bin_get_child(GTK_BIN(s_scroll_browser));
        if (old_br) {
            gtk_container_remove(GTK_CONTAINER(s_scroll_browser), old_br);
        }
        s_view_browser = WEBKIT_WEB_VIEW(webkit_web_view_new_with_context(s_context));
        configure_webview(s_view_browser, cfg);
        gtk_container_add(GTK_CONTAINER(s_scroll_browser), GTK_WIDGET(s_view_browser));
        gtk_widget_show_all(s_scroll_browser);
        if (cfg->home_url[0] != '\0') {
            browser_load_in_browser(cfg->home_url);
        }
    }

    /* 3. Reset ChatGPT App WebView: Preserve login session, navigate back to clean home prompt */
    if (s_view_chatgpt) {
        webkit_web_view_load_uri(s_view_chatgpt, "https://chatgpt.com");
    }

    g_strlcpy(s_active_app, "browser", sizeof(s_active_app));
    notify_state_changed();
    xutil_log("BROWSER", "Universal reset executed: web views re-created, history cleared.");
}

bool browser_can_go_back(void) {
    WebKitWebView *active = browser_get_active_view();
    return (active && WEBKIT_IS_WEB_VIEW(active) && webkit_web_view_can_go_back(active));
}

bool browser_can_go_forward(void) {
    WebKitWebView *active = browser_get_active_view();
    return (active && WEBKIT_IS_WEB_VIEW(active) && webkit_web_view_can_go_forward(active));
}

bool browser_is_loading(void) {
    WebKitWebView *active = browser_get_active_view();
    return (active && WEBKIT_IS_WEB_VIEW(active) && webkit_web_view_is_loading(active));
}

double browser_get_progress(void) {
    WebKitWebView *active = browser_get_active_view();
    if (!active || !WEBKIT_IS_WEB_VIEW(active)) return 1.0;
    return webkit_web_view_get_estimated_load_progress(active);
}

const char *browser_get_uri(void) {
    WebKitWebView *active = browser_get_active_view();
    if (!active || !WEBKIT_IS_WEB_VIEW(active)) return "";
    const char *u = webkit_web_view_get_uri(active);
    return u ? u : "";
}

const char *browser_get_title(void) {
    WebKitWebView *active = browser_get_active_view();
    if (!active || !WEBKIT_IS_WEB_VIEW(active)) return "";
    const char *t = webkit_web_view_get_title(active);
    return t ? t : "";
}

void browser_pause(void) {
    if (s_view_browser) webkit_web_view_set_is_muted(s_view_browser, TRUE);
    if (s_view_chatgpt) webkit_web_view_set_is_muted(s_view_chatgpt, TRUE);
    if (s_active_popup) webkit_web_view_set_is_muted(s_active_popup, TRUE);
}

void browser_resume(void) {
    if (s_view_browser) webkit_web_view_set_is_muted(s_view_browser, FALSE);
    if (s_view_chatgpt) webkit_web_view_set_is_muted(s_view_chatgpt, FALSE);
    if (s_active_popup) webkit_web_view_set_is_muted(s_active_popup, FALSE);
}

void browser_execute_editing_command(const char *command) {
    WebKitWebView *active = browser_get_active_view();
    if (active && command) {
        xutil_log("BROWSER", "Executing editing command '%s' on active view.", command);
        webkit_web_view_execute_editing_command(active, command);
    }
}

void browser_paste(void) {
    WebKitWebView *active = browser_get_active_view();
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
                xutil_log("BROWSER", "Failed to encode clipboard pixbuf to PNG: %s", err->message);
                g_error_free(err);
            }
            g_object_unref(pb);
        }

        /* Fallback: If pixbuf extraction failed, try latest snap file in user runtime dir then /tmp */
        if (!b64) {
            const char *rdir = g_get_user_runtime_dir();
            char *user_snap = rdir ? g_build_filename(rdir, "cronq", "latest_snap.png", NULL) : NULL;
            const char *snap_target = (user_snap && g_file_test(user_snap, G_FILE_TEST_EXISTS)) ? user_snap : "/tmp/cronq_latest_snap.png";
            if (g_file_test(snap_target, G_FILE_TEST_EXISTS)) {
                gchar *contents = NULL;
                gsize length = 0;
                if (g_file_get_contents(snap_target, &contents, &length, NULL)) {
                    b64 = g_base64_encode((const guchar *)contents, length);
                    g_free(contents);
                }
            }
            g_free(user_snap);
        }

        if (b64) {
            xutil_log("BROWSER", "Injecting synthetic image paste event (b64 length=%zu)", strlen(b64));
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
    }

    /* Fallback or non-image paste: native WebKit editing command */
    xutil_log("BROWSER", "Executing native paste editing command on active view.");
    webkit_web_view_execute_editing_command(active, WEBKIT_EDITING_COMMAND_PASTE);
}

void browser_set_progress_callback(BrowserProgressCallback cb, gpointer user_data) {
    s_progress_cb = cb;
    s_progress_ud = user_data;
}

void browser_set_title_callback(BrowserTitleCallback cb, gpointer user_data) {
    s_title_cb = cb;
    s_title_ud = user_data;
}

void browser_set_uri_callback(BrowserUriCallback cb, gpointer user_data) {
    s_uri_cb = cb;
    s_uri_ud = user_data;
}

void browser_set_state_callback(BrowserStateCallback cb, gpointer user_data) {
    s_state_cb = cb;
    s_state_ud = user_data;
}

void browser_set_popup_callbacks(BrowserPopupCreatedCallback on_created,
                                 BrowserPopupClosedCallback on_closed,
                                 gpointer user_data) {
    s_popup_created_cb = on_created;
    s_popup_closed_cb = on_closed;
    s_popup_ud = user_data;
}

void browser_set_divert_callback(BrowserDivertCallback cb, gpointer user_data) {
    s_divert_cb = cb;
    s_divert_ud = user_data;
}

void browser_set_click_callback(BrowserClickCallback cb, gpointer user_data) {
    s_click_cb = cb;
    s_click_ud = user_data;
}

void browser_cleanup(void) {
    if (s_view_browser) {
        gtk_widget_destroy(s_scroll_browser);
        s_view_browser = NULL;
        s_scroll_browser = NULL;
    }
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
    xutil_log("BROWSER", "WebKitGTK dual-app subsystem cleaned up.");
}
