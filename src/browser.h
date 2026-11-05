#ifndef BROWSER_H
#define BROWSER_H

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <stdbool.h>

typedef void (*BrowserProgressCallback)(double progress, gpointer user_data);
typedef void (*BrowserTitleCallback)(const char *title, gpointer user_data);
typedef void (*BrowserUriCallback)(const char *uri, gpointer user_data);
typedef void (*BrowserStateCallback)(bool is_loading, bool can_back, bool can_forward, gpointer user_data);
typedef void (*BrowserPopupCreatedCallback)(GtkWidget *popup_view, gpointer user_data);
typedef void (*BrowserPopupClosedCallback)(GtkWidget *popup_view, gpointer user_data);
typedef void (*BrowserDivertCallback)(const char *uri, gpointer user_data);
typedef void (*BrowserClickCallback)(gpointer user_data);

bool           browser_init(void);
void           browser_cleanup(void);

GtkWidget     *browser_get_browser_widget(void);
WebKitWebView *browser_get_browser_view(void);

GtkWidget     *browser_get_chatgpt_widget(void);
WebKitWebView *browser_get_chatgpt_view(void);

WebKitWebView *browser_get_active_view(void);
void           browser_set_active_view(const char *app_name);
void           browser_clear_active_popup(void);

void           browser_load(const char *input);
void           browser_load_in_browser(const char *input);
void           browser_load_in_chatgpt(const char *input);

void           browser_go_back(void);
void           browser_go_forward(void);
void           browser_reload(void);
void           browser_stop(void);
void           browser_go_home(void);

bool           browser_can_go_back(void);
bool           browser_can_go_forward(void);
bool           browser_is_loading(void);
double         browser_get_progress(void);
const char    *browser_get_uri(void);
const char    *browser_get_title(void);

void           browser_pause(void);
void           browser_resume(void);

void           browser_execute_editing_command(const char *command);
void           browser_paste(void);
bool           browser_is_domain_allowed_for_chatgpt(const char *uri);

void           browser_set_progress_callback(BrowserProgressCallback cb, gpointer user_data);
void           browser_set_title_callback(BrowserTitleCallback cb, gpointer user_data);
void           browser_set_uri_callback(BrowserUriCallback cb, gpointer user_data);
void           browser_set_state_callback(BrowserStateCallback cb, gpointer user_data);
void           browser_set_popup_callbacks(BrowserPopupCreatedCallback on_created,
                                           BrowserPopupClosedCallback on_closed,
                                           gpointer user_data);
void           browser_set_divert_callback(BrowserDivertCallback cb, gpointer user_data);
void           browser_set_click_callback(BrowserClickCallback cb, gpointer user_data);

/* Backward compatibility helpers */
GtkWidget     *browser_get_widget(void);
WebKitWebView *browser_get_view(void);

void           browser_universal_reset(void);
void           browser_apply_theme(int mode);

#endif /* BROWSER_H */
