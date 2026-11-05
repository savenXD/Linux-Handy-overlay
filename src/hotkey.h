#ifndef HOTKEY_H
#define HOTKEY_H

#include <glib.h>
#include <gdk/gdk.h>
#include <stdbool.h>

gboolean        hotkey_init(void);
void            hotkey_cleanup(void);
void            hotkey_reload_grabs(void);

void            hotkey_install_clipboard_shield(void);
void            hotkey_remove_clipboard_shield(void);
bool            hotkey_is_clipboard_shield_active(void);
void            hotkey_drain_pending_events(void);
void            hotkey_sync_raw_modifiers(void);
GdkModifierType hotkey_get_active_modifiers(void);

/* Lock: hides the overlay and disables every hotkey except Alt+K, Alt+X (unlock chord) and Alt+Shift+K. */
void            hotkey_set_locked(bool locked);
bool            hotkey_is_locked(void);

/* Kill-switch prompt keys: while the prompt is visible Enter/Esc are grabbed globally. */
void            hotkey_set_prompt_grabs(bool on);

#endif /* HOTKEY_H */
