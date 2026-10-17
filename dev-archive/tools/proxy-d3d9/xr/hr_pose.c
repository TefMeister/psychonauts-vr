/* hr_pose.c - the newest head pose, handed from the headset thread to the game thread under a lock. */
#include <windows.h>
#include "hr_pose.h"

static CRITICAL_SECTION g_lock;
static volatile LONG g_init;
static HrPose g_latest;
static volatile LONG g_count;

static void init(void) {
    if (InterlockedCompareExchange(&g_init, 2, 0) == 0) {   /* 2 = being set up, 1 = ready */
        InitializeCriticalSection(&g_lock);
        InterlockedExchange(&g_init, 1);
    }
    while (g_init != 1) Sleep(0);
}

void pose_publish(const HrPose *p) {
    init();
    EnterCriticalSection(&g_lock);
    g_latest = *p;
    g_latest.seq = InterlockedIncrement(&g_count);
    LeaveCriticalSection(&g_lock);
}

int pose_latest(HrPose *out) {
    init();
    EnterCriticalSection(&g_lock);
    *out = g_latest;
    LeaveCriticalSection(&g_lock);
    return out->valid && out->seq > 0;
}

long pose_count(void) { return g_count; }
