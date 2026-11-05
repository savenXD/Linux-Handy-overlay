#ifndef KILLSWITCH_H
#define KILLSWITCH_H

#include <glib.h>
#include <stdbool.h>

/* Alt+Shift+K: red confirmation box. Enter confirms (wipe), Esc or 15 s timeout cancels. */
void killswitch_prompt_show(void);
void killswitch_prompt_cancel(void);
void killswitch_prompt_confirm(void);   /* performs the wipe; no-op if the prompt is not showing */
bool killswitch_prompt_active(void);

#endif /* KILLSWITCH_H */
