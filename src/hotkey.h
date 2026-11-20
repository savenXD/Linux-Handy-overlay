#ifndef HOTKEY_H
#define HOTKEY_H

#include <glib.h>
#include <gdk/gdk.h>
#include <stdbool.h>

gboolean        hotkey_init(void);
void            hotkey_cleanup(void);
void            hotkey_reload_grabs(void);

bool            hotkey_capture_keyboard(void);
void            hotkey_release_keyboard(void);
GdkModifierType hotkey_get_active_modifiers(void);

/* Lock: hides the overlay and disables every hotkey except Alt+K, Alt+X (unlock chord) and Alt+Shift+K. */
void            hotkey_set_locked(bool locked);
bool            hotkey_is_locked(void);

/* Kill-switch prompt keys: while the prompt is visible Enter/Esc are grabbed globally. */
void            hotkey_set_prompt_grabs(bool on);

#endif /* HOTKEY_H */
