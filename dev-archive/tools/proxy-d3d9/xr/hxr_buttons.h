#pragma once
/* hxr_buttons - the controllers' triggers, grips, sticks and buttons, read on the headset thread (2026-10-16, Tefa:
 * "can you start wiring in the motion controllers as well?"). hxr.c owns the session and the action set; this file
 * adds the button actions to that set, gives the bindings for each controller family, and reads them each frame.
 * Only the headset thread calls these (OpenXR is never called from the game thread).
 *
 * The bindings for one controller family must go in ONE suggest call together with the poses (a second call for the
 * same family replaces the first), so hxb_bindings() APPENDS to the list hxr.c already holds. A family whose button
 * paths the runtime refuses is retried by hxr.c with the poses alone, so the hands never stop working because of this. */
#include <openxr/openxr.h>
#include "hr_pose.h"

/* Create the button actions in `set` (before it is attached). 0 = could not: the poses still work. */
int hxb_create(XrInstance inst, XrActionSet set, PFN_xrGetInstanceProcAddr gipa);
/* Append this family's button bindings to out[n..max); returns the new count. Unknown family: nothing added. */
uint32_t hxb_bindings(XrInstance inst, const char *profile, XrActionSuggestedBinding *out, uint32_t n, uint32_t max);
/* After xrSyncActions: read everything into *out (valid 1 when the actions were readable). */
void hxb_read(XrSession sess, HrButtons *out);
