/*
 * Minimal logging proxy d3d9.dll for Psychonauts VR reverse-engineering.
 *
 * Purpose: validate that a DLL dropped into the game directory as "d3d9.dll"
 * gets loaded by the game and that its single imported entry point,
 * Direct3DCreate9, gets called - WITHOUT changing any game behavior. It loads
 * the real system d3d9.dll and forwards the call unmodified. No vtable
 * hooking, no interface wrapping yet - that's the next milestone.
 *
 * Build target: 32-bit (i686), matching the 32-bit Psychonauts.exe.
 */

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

typedef void *(WINAPI *Direct3DCreate9_t)(UINT SDKVersion);

static HMODULE g_hRealD3D9 = NULL;
static Direct3DCreate9_t g_pRealDirect3DCreate9 = NULL;
static CRITICAL_SECTION g_logLock;
static BOOL g_logLockInit = FALSE;

/* Build the log file path: %TEMP%\psychonautsvr_proxy.log */
static void GetLogPath(char *outPath, DWORD outSize)
{
    char tempPath[MAX_PATH];
    DWORD len = GetTempPathA(MAX_PATH, tempPath);
    if (len == 0 || len >= MAX_PATH) {
        /* Fallback: current directory (next to the DLL) */
        lstrcpynA(outPath, "psychonautsvr_proxy.log", (int)outSize);
        return;
    }
    _snprintf(outPath, outSize, "%spsychonautsvr_proxy.log", tempPath);
}

static void LogLine(const char *fmt, ...)
{
    char msg[1024];
    char line[1200];
    char logPath[MAX_PATH];
    SYSTEMTIME st;
    va_list args;
    FILE *f;

    va_start(args, fmt);
    _vsnprintf(msg, sizeof(msg) - 1, fmt, args);
    msg[sizeof(msg) - 1] = '\0';
    va_end(args);

    GetLocalTime(&st);
    _snprintf(line, sizeof(line) - 1,
              "[%04d-%02d-%02d %02d:%02d:%02d.%03d] %s\r\n",
              st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
              msg);
    line[sizeof(line) - 1] = '\0';

    GetLogPath(logPath, sizeof(logPath));

    if (g_logLockInit) EnterCriticalSection(&g_logLock);

    f = fopen(logPath, "a");
    if (f) {
        fputs(line, f);
        fclose(f);
    }

    if (g_logLockInit) LeaveCriticalSection(&g_logLock);
}

/* Load the REAL d3d9.dll from the Windows system directory, never a bare
 * name lookup (which could resolve back to this proxy if it were ever
 * placed somewhere earlier in the search order, e.g. the game folder
 * itself). */
static BOOL LoadRealD3D9(void)
{
    char sysDir[MAX_PATH];
    char fullPath[MAX_PATH];
    UINT len;

    if (g_hRealD3D9 != NULL) return TRUE;

    len = GetSystemDirectoryA(sysDir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        LogLine("ERROR: GetSystemDirectoryA failed (len=%u, err=%lu)", len, GetLastError());
        return FALSE;
    }

    _snprintf(fullPath, sizeof(fullPath) - 1, "%s\\d3d9.dll", sysDir);
    fullPath[sizeof(fullPath) - 1] = '\0';

    g_hRealD3D9 = LoadLibraryA(fullPath);
    if (g_hRealD3D9 == NULL) {
        LogLine("ERROR: LoadLibraryA(\"%s\") failed, err=%lu", fullPath, GetLastError());
        return FALSE;
    }

    LogLine("Loaded real d3d9.dll from \"%s\" (hModule=0x%p)", fullPath, (void *)g_hRealD3D9);

    g_pRealDirect3DCreate9 = (Direct3DCreate9_t)GetProcAddress(g_hRealD3D9, "Direct3DCreate9");
    if (g_pRealDirect3DCreate9 == NULL) {
        LogLine("ERROR: GetProcAddress(Direct3DCreate9) failed, err=%lu", GetLastError());
        return FALSE;
    }

    LogLine("Resolved real Direct3DCreate9 at 0x%p", (void *)g_pRealDirect3DCreate9);
    return TRUE;
}

/* The one and only export the game imports from d3d9.dll. */
__declspec(dllexport) void *WINAPI Direct3DCreate9(UINT SDKVersion)
{
    void *result;

    LogLine("Direct3DCreate9(SDKVersion=0x%X) called - forwarding to real d3d9.dll", SDKVersion);

    if (!LoadRealD3D9() || g_pRealDirect3DCreate9 == NULL) {
        LogLine("ERROR: cannot forward Direct3DCreate9 - real d3d9.dll/proc not available");
        return NULL;
    }

    result = g_pRealDirect3DCreate9(SDKVersion);

    LogLine("Real Direct3DCreate9 returned IDirect3D9* = 0x%p", result);

    return result;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    (void)hinstDLL;
    (void)lpvReserved;

    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        InitializeCriticalSection(&g_logLock);
        g_logLockInit = TRUE;
        LogLine("==== psychonautsvr proxy d3d9.dll: DLL_PROCESS_ATTACH (pid=%lu) ====", GetCurrentProcessId());
        break;
    case DLL_PROCESS_DETACH:
        LogLine("==== psychonautsvr proxy d3d9.dll: DLL_PROCESS_DETACH (pid=%lu) ====", GetCurrentProcessId());
        if (g_hRealD3D9) {
            FreeLibrary(g_hRealD3D9);
            g_hRealD3D9 = NULL;
        }
        if (g_logLockInit) {
            DeleteCriticalSection(&g_logLock);
            g_logLockInit = FALSE;
        }
        break;
    default:
        break;
    }
    return TRUE;
}
