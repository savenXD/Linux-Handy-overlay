#ifndef CAPTURE_GUARD_H
#define CAPTURE_GUARD_H

#include <glib.h>
#include <stdbool.h>

/* Hide-from-screen-capture guard (X11).
 *
 * X11 has no per-window "exclude from capture" flag like Windows' WDA_EXCLUDEFROMCAPTURE: the screen
 * you see and the screen a recorder reads are the same pixels. The guard therefore watches the X server
 * (X Record extension) for other programs reading the screen area under the overlay (core GetImage and
 * MIT-SHM ShmGetImage on the root window: ffmpeg x11grab, OBS XSHM, Zoom, browser screen sharing, ...)
 * and unmaps the overlay for as long as the capture continues, then maps it again. */

gboolean capture_guard_init(void);
void     capture_guard_cleanup(void);
void     capture_guard_reload_config(void);
bool     capture_guard_is_active(void);   /* true while the overlay is being held back for a capture */

#endif /* CAPTURE_GUARD_H */
