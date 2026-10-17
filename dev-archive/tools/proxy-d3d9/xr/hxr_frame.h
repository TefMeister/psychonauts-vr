#pragma once
/* hxr_frame - hands one stereo picture at a time from the GAME thread to the HEADSET thread, on the CPU
 * (2026-10-07, /lm hard-reset-vr reader). Pure C + one lock, no Direct3D: test/hxr_frame_test.c runs it alone.
 *
 * Why on the CPU: the game's Direct3D 9 (not 9Ex) surfaces cannot be shared with a Direct3D 11 device, and Metro
 * showed some devices refuse keyed-mutex shared textures. So the game thread reads each finished frame back into
 * memory and the headset thread uploads it to its own device.
 *
 * Three slots (one being written, one newest finished, one being read), so NEITHER thread ever waits for the other:
 * the lock is held only to swap slot numbers, never during a copy. The writer always finds a free slot; the reader
 * always gets the newest finished frame; frames the reader never took are simply overwritten. Each slot holds both
 * eyes, tightly packed (row pitch = w * 4), left eye first. */
#include <stdint.h>
#include "hr_pose.h"

#define HXR_SLOTS 3

typedef struct {
    uint32_t w, h;          /* one eye */
    uint32_t d3d_format;    /* the D3D9 format the bytes came from (D3DFMT_X8R8G8B8 / A8R8G8B8) */
    long serial;            /* 1, 2, 3 ... per finished frame */
    uint8_t *eye[2];        /* left, right: w * h * 4 bytes each */
    HrPose pose;            /* the head pose this picture was drawn with (pose.valid 0 = none: use the current one);
                               set by the writer before _publish */
} HxrFrame;

void hxr_frame_init(void);

/* GAME THREAD. Get a slot sized for w x h to fill; returns NULL only if memory runs out. Finish with _publish. */
HxrFrame *hxr_frame_begin_write(uint32_t w, uint32_t h, uint32_t d3d_format);
void hxr_frame_publish(HxrFrame *f);

/* HEADSET THREAD. The newest finished frame if its serial is newer than `seen`, else NULL. The slot is the reader's
 * alone until _release (it may convert the bytes in place). */
HxrFrame *hxr_frame_take(long seen);
void hxr_frame_release(HxrFrame *f);

long hxr_frame_serial(void);    /* serial of the newest finished frame (0 = none yet) */
