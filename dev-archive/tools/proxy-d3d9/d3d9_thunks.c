/*
 * d3d9_thunks.c - forwarders for the sixteen d3d9 exports this proxy does not implement (32-bit).
 * Added 2026-10-01 by /pd; the same file is used by alan-wake-vr and psychonauts-vr.
 *
 * WHY: the system d3d9.dll exports seventeen named functions; a proxy that exports only Direct3DCreate9 hands
 * NULL to anything else the game (or an overlay) resolves against it. On Dead Space 2 that was an access
 * violation at address 0 on start-up (dead-space-2-vr, 2026-09-14). Prototype carries the fix already
 * (prototype-vr dev-archive/tools/proxy-d3d9/src/thunks.c); this is the same idea, self-contained.
 *
 * HOW: each thunk is naked assembly. It saves every register and the flags, asks thunk_resolve() for the real
 * function (loaded from the system folder on first use, so the proxy's own start-up code is untouched), logs
 * the first call of each export, restores everything and jumps to the real function with the caller's stack
 * exactly as it was. Correct for any calling convention and signature, including the undocumented ones.
 * Log: <THUNK_LOG_NAME> beside the exe, first call of each export only.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#ifndef THUNK_LOG_NAME
#define THUNK_LOG_NAME "d3d9_thunks_log.txt"
#endif
#define THUNK_COUNT 16

const char *const g_thunk_name[THUNK_COUNT] = {
    "D3DPERF_BeginEvent", "D3DPERF_EndEvent", "D3DPERF_GetStatus", "D3DPERF_QueryRepeatFrame",
    "D3DPERF_SetMarker", "D3DPERF_SetOptions", "D3DPERF_SetRegion", "DebugSetLevel", "DebugSetMute",
    "Direct3D9EnableMaximizedWindowedModeShim", "Direct3DCreate9Ex", "Direct3DCreate9On12",
    "Direct3DCreate9On12Ex", "Direct3DShaderValidatorCreate9", "PSGPError", "PSGPSampleTexture",
};
void *g_thunk_target[THUNK_COUNT];
static HMODULE g_system_d3d9;
static unsigned char g_seen[THUNK_COUNT];

static void thunk_log(const char *fmt, const char *a, const char *b) {
    char path[MAX_PATH * 2];
    DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    char *slash = n ? strrchr(path, '\\') : NULL;
    if (!slash) return;
    strcpy(slash + 1, THUNK_LOG_NAME);
    FILE *f = fopen(path, "a");
    if (!f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "[%02u:%02u:%02u.%03u] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    fprintf(f, fmt, a, b);
    fputc('\n', f);
    fclose(f);
}

/* Called from inside a thunk with every register saved. Fills g_thunk_target[idx] on first use. */
void thunk_resolve(int idx) {
    if (idx < 0 || idx >= THUNK_COUNT || g_seen[idx]) return;
    g_seen[idx] = 1;
    if (!g_system_d3d9) {
        char sys[MAX_PATH];
        UINT n = GetSystemDirectoryA(sys, MAX_PATH);
        if (n && n + sizeof "\\d3d9.dll" < MAX_PATH) {
            strcat(sys, "\\d3d9.dll");
            g_system_d3d9 = LoadLibraryA(sys);     /* already loaded by the proxy: this only adds a reference */
        }
    }
    g_thunk_target[idx] = g_system_d3d9 ? (void *)GetProcAddress(g_system_d3d9, g_thunk_name[idx]) : NULL;
    thunk_log("THUNK: the game called %s%s", g_thunk_name[idx],
              g_thunk_target[idx] ? " - forwarded to the system d3d9.dll" : " - NO real target, this call will fail");
}

/* pushal/popal + pushfl/popfl keep every register and the flags; the cdecl argument to thunk_resolve is pushed
 * and removed here, so the caller's frame is untouched when we jump. Must stay hand-written. */
#define THUNK(idx)                              \
    __asm__(".globl _Thunk_" #idx "\n"          \
            "_Thunk_" #idx ":\n"                \
            "  pushal\n"                        \
            "  pushfl\n"                        \
            "  pushl $" #idx "\n"               \
            "  call _thunk_resolve\n"           \
            "  addl $4, %esp\n"                 \
            "  popfl\n"                         \
            "  popal\n"                         \
            "  jmpl *(_g_thunk_target + " #idx "*4)\n")

THUNK(0);  THUNK(1);  THUNK(2);  THUNK(3);
THUNK(4);  THUNK(5);  THUNK(6);  THUNK(7);
THUNK(8);  THUNK(9);  THUNK(10); THUNK(11);
THUNK(12); THUNK(13); THUNK(14); THUNK(15);
