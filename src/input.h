#ifndef INPUT_H
#define INPUT_H

#include <glib.h>
#include <gtk/gtk.h>
#include <stdbool.h>
#include <X11/Xlib.h>

typedef enum {
    TYPING_TARGET_NONE = 0,
    TYPING_TARGET_OVERLAY = 1
} TypingTarget;

typedef enum {
    GHOST_MODE_OFF = 0,
    GHOST_MODE_ON = 1
} GhostMode;

typedef enum {
    GRAB_STATUS_NONE = 0,
    GRAB_STATUS_ACTIVE = 1,
    GRAB_STATUS_FAILED = 2
} GrabStatus;

gboolean     input_init(void);
void         input_cleanup(void);

void         input_toggle_typing_target(void);

TypingTarget input_get_typing_target(void);
void         input_set_typing_target(TypingTarget target);

GhostMode    input_get_ghost_mode(void);
void         input_set_ghost_mode(GhostMode ghost);
void         input_toggle_ghost_mode(void);

GrabStatus   input_get_grab_status(void);
void         input_release_grab(void);

void         input_notify_native_click(GdkEventButton *event);
void         input_notify_overlay_hidden(void);

void         input_handle_raw_key(int keycode, bool is_press);
void         input_handle_raw_button(int button, bool is_press, int root_x, int root_y);

#endif /* INPUT_H */
