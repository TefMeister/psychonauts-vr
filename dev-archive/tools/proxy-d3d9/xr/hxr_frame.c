/* hxr_frame.c - the three-slot CPU handover between the game thread and the headset thread. See hxr_frame.h. */
#include <windows.h>
#include <stdlib.h>
#include "hxr_frame.h"

static CRITICAL_SECTION g_lock;
static LONG g_inited;
static HxrFrame g_slot[HXR_SLOTS];
static size_t g_cap[HXR_SLOTS];          /* bytes allocated per slot (both eyes) */
static int g_latest = -1, g_reading = -1, g_writing = -1;
static long g_serial;

void hxr_frame_init(void) {
    if (InterlockedCompareExchange(&g_inited, 2, 0) == 0) {   /* 2 = being set up, 1 = ready */
        InitializeCriticalSection(&g_lock);
        InterlockedExchange(&g_inited, 1);
    }
    while (g_inited != 1) Sleep(0);
}

HxrFrame *hxr_frame_begin_write(uint32_t w, uint32_t h, uint32_t d3d_format) {
    int i, s = -1;
    hxr_frame_init();
    EnterCriticalSection(&g_lock);
    for (i = 0; i < HXR_SLOTS && s < 0; ++i)
        if (i != g_latest && i != g_reading) s = i;   /* with three slots one is always free */
    g_writing = s;
    LeaveCriticalSection(&g_lock);
    if (s < 0) return NULL;
    size_t eye_bytes = (size_t)w * h * 4u, need = eye_bytes * 2u;
    HxrFrame *f = &g_slot[s];
    if (g_cap[s] < need) {
        uint8_t *p = (uint8_t *)realloc(f->eye[0], need);
        if (!p) { g_writing = -1; return NULL; }
        f->eye[0] = p;
        g_cap[s] = need;
    }
    f->eye[1] = f->eye[0] + eye_bytes;
    f->w = w; f->h = h; f->d3d_format = d3d_format;
    return f;
}

void hxr_frame_publish(HxrFrame *f) {
    MemoryBarrier();                     /* cheap insurance: every pixel store lands before the slot is offered */
    EnterCriticalSection(&g_lock);
    f->serial = ++g_serial;
    g_latest = (int)(f - g_slot);
    g_writing = -1;
    LeaveCriticalSection(&g_lock);
}

HxrFrame *hxr_frame_take(long seen) {
    HxrFrame *f = NULL;
    hxr_frame_init();
    EnterCriticalSection(&g_lock);
    if (g_reading < 0 && g_latest >= 0 && g_slot[g_latest].serial > seen) {
        g_reading = g_latest;
        f = &g_slot[g_reading];
    }
    LeaveCriticalSection(&g_lock);
    return f;
}

void hxr_frame_release(HxrFrame *f) {
    (void)f;
    EnterCriticalSection(&g_lock);
    g_reading = -1;
    LeaveCriticalSection(&g_lock);
}

long hxr_frame_serial(void) {
    long s;
    hxr_frame_init();
    EnterCriticalSection(&g_lock);
    s = g_serial;
    LeaveCriticalSection(&g_lock);
    return s;
}
