#ifndef CHAT_VIEW_H
#define CHAT_VIEW_H
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <stdbool.h>
typedef void (*ChatViewPopupCreatedCallback)(GtkWidget *, gpointer);
typedef void (*ChatViewPopupClosedCallback)(GtkWidget *, gpointer);
bool chat_view_init(void);
void chat_view_cleanup(void);
GtkWidget *chat_view_get_chatgpt_widget(void);
WebKitWebView *chat_view_get_chatgpt_view(void);
WebKitWebView *chat_view_get_active_view(void);
void chat_view_set_active_view(const char *name);
void chat_view_clear_active_popup(void);
void chat_view_go_back(void);
void chat_view_reload(void);
void chat_view_stop(void);
bool chat_view_can_go_back(void);
void chat_view_pause(void);
void chat_view_resume(void);
void chat_view_execute_editing_command(const char *command);
void chat_view_paste(void);
bool chat_view_is_domain_allowed_for_chatgpt(const char *uri);
void chat_view_set_popup_callbacks(ChatViewPopupCreatedCallback, ChatViewPopupClosedCallback, gpointer);
void chat_view_universal_reset(void);
void chat_view_apply_theme(int mode);
void chat_view_prepare_typing(void);
#endif
