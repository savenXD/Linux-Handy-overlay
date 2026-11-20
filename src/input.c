#include "input.h"
#include "window.h"
#include "chat_view.h"
#include "hotkey.h"
#include "config.h"
#include "xutil.h"

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POINTER_FALLBACK_MS  35

static TypingTarget s_typing_target = TYPING_TARGET_NONE;
static GhostMode    s_ghost_mode = GHOST_MODE_OFF;
static GrabStatus   s_grab_status = GRAB_STATUS_NONE;

static guint        s_fallback_timer_id = 0;
static gint64       s_last_native_click_time = 0;
static int          s_pending_fallback_button = 0;
static int          s_pending_fallback_root_x = 0;
static int          s_pending_fallback_root_y = 0;

gboolean input_init(void) {
    s_typing_target = TYPING_TARGET_NONE;
    s_ghost_mode = GHOST_MODE_OFF;
    s_grab_status = GRAB_STATUS_NONE;
    s_fallback_timer_id = 0;
    s_last_native_click_time = 0;

    xutil_log("INPUT", "Input subsystem initialized (normal application typing, SOLID).");
    return TRUE;
}

void input_toggle_typing_target(void) {
    input_set_typing_target(s_typing_target==TYPING_TARGET_OVERLAY?TYPING_TARGET_NONE:TYPING_TARGET_OVERLAY);
}
TypingTarget input_get_typing_target(void) { return s_typing_target; }
void input_set_typing_target(TypingTarget target) {
    if (target==TYPING_TARGET_OVERLAY && (!overlay_is_visible() || hotkey_is_locked())) return;
    if (target==s_typing_target) return;
    if (target==TYPING_TARGET_OVERLAY) {
        if (!hotkey_capture_keyboard()) {
            s_grab_status=GRAB_STATUS_FAILED;
            overlay_show_toast("Keyboard capture unavailable; typing stays in your app");
            overlay_queue_draw();return;
        }
        s_grab_status=GRAB_STATUS_ACTIVE;s_typing_target=target;
        overlay_set_active_focus(TRUE);
        chat_view_prepare_typing();
    } else {
        s_typing_target=TYPING_TARGET_NONE;
        hotkey_release_keyboard();s_grab_status=GRAB_STATUS_NONE;
        overlay_set_active_focus(FALSE);
    }
    xutil_log("INPUT","Typing target: %s",s_typing_target==TYPING_TARGET_OVERLAY?"OVERLAY":"NORMAL");
    overlay_queue_draw();
}

GhostMode input_get_ghost_mode(void) {
    return s_ghost_mode;
}

void input_set_ghost_mode(GhostMode ghost) {
    if (s_ghost_mode == ghost) return;
    s_ghost_mode = ghost;

    if (ghost == GHOST_MODE_ON) {
        xutil_log("INPUT", "Ghost mode ENABLED (transparent click-through).");
        if (s_typing_target == TYPING_TARGET_OVERLAY) {
            input_set_typing_target(TYPING_TARGET_NONE);
        }
        overlay_set_opacity(0.50);
        overlay_set_ghost(TRUE);
    } else {
        xutil_log("INPUT", "Ghost mode DISABLED (solid overlay restored).");
        overlay_set_opacity(1.0);
        overlay_set_ghost(FALSE);
    }
    overlay_queue_draw();
}

void input_toggle_ghost_mode(void) {
    if (s_ghost_mode == GHOST_MODE_OFF) {
        input_set_ghost_mode(GHOST_MODE_ON);
    } else {
        input_set_ghost_mode(GHOST_MODE_OFF);
    }
}

GrabStatus input_get_grab_status(void) {
    return s_grab_status;
}

void input_release_grab(void) { input_set_typing_target(TYPING_TARGET_NONE); }

void input_notify_native_click(GdkEventButton *event) {
    (void)event;
    s_last_native_click_time = g_get_monotonic_time();

    if (s_fallback_timer_id > 0) {
        g_source_remove(s_fallback_timer_id);
        s_fallback_timer_id = 0;
    }

}

void input_notify_overlay_hidden(void) {
    /* Visibility is independent of the explicitly selected typing destination. */
    if (s_fallback_timer_id) {
        g_source_remove(s_fallback_timer_id);
        s_fallback_timer_id = 0;
    }
}

static gboolean on_pointer_fallback_check(gpointer user_data) {
    (void)user_data;
    s_fallback_timer_id = 0;

    gint64 now = g_get_monotonic_time();
    /* If no native event reached us within 30ms (30,000 microseconds) */
    if (now - s_last_native_click_time > 30000) {
        xutil_log("INPUT", "Pointer-grab fallback engaged (foreign pointer grab detected). Synthesizing click.");

        int ox, oy, ow, oh;
        overlay_get_geometry(&ox, &oy, &ow, &oh);
        int rel_x = s_pending_fallback_root_x - ox;
        int rel_y = s_pending_fallback_root_y - oy;

        GtkWidget *target_widget=overlay_has_popup_modal()?overlay_get_popup_child_view():GTK_WIDGET(chat_view_get_active_view());
        if(!target_widget) target_widget=overlay_get_window_widget();
        int target_x=rel_x,target_y=rel_y;
        gtk_widget_translate_coordinates(overlay_get_window_widget(),target_widget,rel_x,rel_y,&target_x,&target_y);

        if (target_widget && gtk_widget_get_window(target_widget)) {
            GdkDisplay *display = gdk_display_get_default();
            GdkSeat *seat = gdk_display_get_default_seat(display);
            GdkDevice *pointer = gdk_seat_get_pointer(seat);
            guint32 ev_time = (guint32)(g_get_monotonic_time() / 1000);

            GdkEvent *ev_press = gdk_event_new(GDK_BUTTON_PRESS);
            ev_press->button.window = g_object_ref(gtk_widget_get_window(target_widget));
            ev_press->button.send_event = FALSE;
            ev_press->button.time = ev_time;
            ev_press->button.x = target_x;
            ev_press->button.y = target_y;
            ev_press->button.x_root = s_pending_fallback_root_x;
            ev_press->button.y_root = s_pending_fallback_root_y;
            ev_press->button.button = s_pending_fallback_button;
            ev_press->button.state = GDK_BUTTON1_MASK;
            gdk_event_set_device(ev_press, pointer);

            gtk_widget_event(target_widget, ev_press);
            gdk_event_free(ev_press);

            GdkEvent *ev_rel = gdk_event_new(GDK_BUTTON_RELEASE);
            ev_rel->button.window = g_object_ref(gtk_widget_get_window(target_widget));
            ev_rel->button.send_event = FALSE;
            ev_rel->button.time = ev_time;
            ev_rel->button.x = target_x;
            ev_rel->button.y = target_y;
            ev_rel->button.x_root = s_pending_fallback_root_x;
            ev_rel->button.y_root = s_pending_fallback_root_y;
            ev_rel->button.button = s_pending_fallback_button;
            ev_rel->button.state = 0;
            gdk_event_set_device(ev_rel, pointer);

            gtk_widget_event(target_widget, ev_rel);
            gdk_event_free(ev_rel);

            gtk_widget_grab_focus(target_widget);
        }

    }
    return G_SOURCE_REMOVE;
}

void input_handle_raw_button(int button, bool is_press, int root_x, int root_y) {
    int ox, oy, ow, oh;
    overlay_get_geometry(&ox, &oy, &ow, &oh);

    bool is_inside = overlay_is_visible() && s_ghost_mode == GHOST_MODE_OFF && (root_x >= ox && root_x < ox + ow &&
                      root_y >= oy && root_y < oy + oh);

    if (is_inside) {

        if (is_press) {
            if (button >= 1 && button <= 3) {
                s_pending_fallback_button = button;
                s_pending_fallback_root_x = root_x;
                s_pending_fallback_root_y = root_y;

                if (s_fallback_timer_id == 0) {
                    s_fallback_timer_id = g_timeout_add(POINTER_FALLBACK_MS, on_pointer_fallback_check, NULL);
                }
            }

            }
    } else {
        if (is_press && button >= 1 && button <= 3) {
            input_set_typing_target(TYPING_TARGET_NONE);
        }
    }
}

static GdkModifierType get_current_modifiers(void) {
    GdkModifierType state = hotkey_get_active_modifiers();
    GdkDisplay *display = gdk_display_get_default();
    if (display) {
        GdkKeymap *km = gdk_keymap_get_for_display(display);
        if (km) {
            if (gdk_keymap_get_caps_lock_state(km)) {
                state |= GDK_LOCK_MASK;
            }
            if (gdk_keymap_get_num_lock_state(km)) {
                state |= GDK_MOD2_MASK;
            }
        }
    }
    return state;
}

void input_handle_raw_key(int keycode, bool is_press) {

    /* If typing target is not overlay, keys go only to the focused desktop app */
    if (s_typing_target != TYPING_TARGET_OVERLAY) {
        return;
    }


    /* Find target widget: check focused widget in overlay window */
    GtkWidget *target = NULL;
    GtkWidget *win = overlay_get_window_widget();
    if (win) {
        GtkWidget *f = gtk_window_get_focus(GTK_WINDOW(win));
        if (f && (GTK_IS_EDITABLE(f) || GTK_IS_TEXT_VIEW(f) || WEBKIT_IS_WEB_VIEW(f))) {
            target = f;
        }
    }
    if (!target) {
        target = GTK_WIDGET(chat_view_get_active_view());
    }
    if (!target) {
        target = GTK_WIDGET(chat_view_get_active_view());
    }
    if (!target || !gtk_widget_get_window(target)) {
        target = GTK_WIDGET(chat_view_get_active_view());
    }
    if (!target || !gtk_widget_get_window(target)) {
        return;
    }

    GdkDisplay *display = gdk_display_get_default();
    GdkKeymap *keymap = gdk_keymap_get_for_display(display);
    GdkModifierType state = get_current_modifiers();

    guint keyval = 0;
    gdk_keymap_translate_keyboard_state(keymap, keycode, state, 0, &keyval, NULL, NULL, NULL);

    if (keyval == 0) return;

    /* Don't send solitary modifier key releases to native control / ChatGPT */
    if (keyval == GDK_KEY_Control_L || keyval == GDK_KEY_Control_R ||
        keyval == GDK_KEY_Shift_L   || keyval == GDK_KEY_Shift_R ||
        keyval == GDK_KEY_Alt_L     || keyval == GDK_KEY_Alt_R ||
        keyval == GDK_KEY_Meta_L    || keyval == GDK_KEY_Meta_R) {
        return;
    }

    gunichar c = gdk_keyval_to_unicode(keyval);
    char str[7] = {0};
    if (c > 0) {
        g_unichar_to_utf8(c, str);
    } else if (keyval == GDK_KEY_BackSpace) {
        str[0] = '\b';
    } else if (keyval == GDK_KEY_Return) {
        str[0] = '\r';
    } else if (keyval == GDK_KEY_Tab) {
        str[0] = '\t';
    }

    GdkSeat *seat = gdk_display_get_default_seat(display);
    GdkDevice *keyboard = gdk_seat_get_keyboard(seat);
    guint32 ev_time = (guint32)(g_get_monotonic_time() / 1000);

    GdkEvent *ev = gdk_event_new(is_press ? GDK_KEY_PRESS : GDK_KEY_RELEASE);
    ev->key.window = g_object_ref(gtk_widget_get_window(target));
    ev->key.send_event = FALSE;
    ev->key.time = ev_time;
    ev->key.state = state;
    ev->key.keyval = keyval;
    ev->key.hardware_keycode = keycode;
    ev->key.length = strlen(str);
    ev->key.string = g_strdup(str);
    gdk_event_set_device(ev, keyboard);

    gtk_widget_event(target, ev);
    gdk_event_free(ev);
    /* Routing a key does not change the typing target or status indicators.
     * Their transition handlers update the UI; rebuilding them here invalidates
     * GTK styles and writes X11 properties twice for every typed character. */
}

void input_cleanup(void) {
    if (s_fallback_timer_id > 0) {
        g_source_remove(s_fallback_timer_id);
        s_fallback_timer_id = 0;
    }
    input_release_grab();
    xutil_log("INPUT", "Input subsystem cleaned up.");
}
