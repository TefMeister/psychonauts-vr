/*
 * Minimal logging proxy d3d9.dll for Psychonauts VR reverse-engineering.
 *
 * Purpose: validate that a DLL dropped into the game directory as "d3d9.dll"
 * gets loaded by the game and that its single imported entry point,
 * Direct3DCreate9, gets called - WITHOUT changing any game behavior. It loads
 * the real system d3d9.dll and forwards the call unmodified.
 *
 * Milestone 2 (this revision): also vtable-hooks IDirect3D9::CreateDevice and
 * IDirect3DDevice9::Present, purely to observe (log) D3DPRESENT_PARAMETERS and
 * confirm Present fires every frame. Still no behavior changes - every hook
 * calls straight through to the real implementation and returns its result
 * unmodified.
 *
 * Vtable indices used (0-based, standard COM: slot 0/1/2 are always
 * QueryInterface/AddRef/Release):
 *   IDirect3D9::CreateDevice        = slot 16
 *   IDirect3DDevice9::Present       = slot 17
 * These are NOT guessed - they come from two independent, cross-checked
 * sources: (1) counting fields in the IDirect3D9Vtbl / IDirect3DDevice9Vtbl
 * struct definitions in mingw-w64's own d3d9.h (bundled with the LLVM-MinGW
 * toolchain used to build this DLL - see the STDMETHOD() ordering in that
 * header), and (2) a prior live x64dbg session that read the *actual* vtable
 * out of process memory and breakpointed both slots successfully (see
 * notes/04-live-debug-findings.md, "vtable slot 16 read live + breakpoint
 * hit" / "slot 17 read live + ... breakpoint hit confirmed"). Rather than
 * hardcode raw slot numbers into pointer arithmetic (easy to get subtly
 * wrong), the hooks below patch the named function-pointer fields of the
 * real d3d9.h vtbl structs directly (This->lpVtbl->CreateDevice = ...,
 * ->lpVtbl->Present = ...) so the compiler - not manual offset math -
 * guarantees the correct slot is patched.
 *
 * Build target: 32-bit (i686), matching the 32-bit Psychonauts.exe.
 */

#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdarg.h>

typedef IDirect3D9 *(WINAPI *Direct3DCreate9_t)(UINT SDKVersion);
typedef HRESULT (STDMETHODCALLTYPE *CreateDevice_t)(
    IDirect3D9 *This,
    UINT Adapter,
    D3DDEVTYPE DeviceType,
    HWND hFocusWindow,
    DWORD BehaviorFlags,
    D3DPRESENT_PARAMETERS *pPresentationParameters,
    IDirect3DDevice9 **ppReturnedDeviceInterface);
typedef HRESULT (STDMETHODCALLTYPE *Present_t)(
    IDirect3DDevice9 *This,
    CONST RECT *pSourceRect,
    CONST RECT *pDestRect,
    HWND hDestWindowOverride,
    CONST RGNDATA *pDirtyRegion);

static HMODULE g_hRealD3D9 = NULL;
static Direct3DCreate9_t g_pRealDirect3DCreate9 = NULL;
static CreateDevice_t g_pRealCreateDevice = NULL;
static Present_t g_pRealPresent = NULL;
static BOOL g_d3d9Hooked = FALSE;      /* IDirect3D9::CreateDevice patched? */
static BOOL g_deviceHooked = FALSE;    /* IDirect3DDevice9::Present patched? */
static volatile LONG g_frameCounter = 0;
static DWORD g_lastPresentLogTick = 0;
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

/* ---- Present hook (IDirect3DDevice9 vtable slot 17) -------------------- */
/*
 * Fires once per frame. Logging every single call would spam the log file
 * and add per-frame disk I/O under the same critical section other hooks
 * use, so this throttles to roughly once per second (wall-clock, via
 * GetTickCount - robust to any framerate) while still incrementing a total
 * frame counter every call, so the log shows real elapsed-frame deltas.
 */
static HRESULT STDMETHODCALLTYPE Hook_Present(
    IDirect3DDevice9 *This,
    CONST RECT *pSourceRect,
    CONST RECT *pDestRect,
    HWND hDestWindowOverride,
    CONST RGNDATA *pDirtyRegion)
{
    LONG frame = InterlockedIncrement(&g_frameCounter);
    DWORD now = GetTickCount();

    if (g_lastPresentLogTick == 0 || (DWORD)(now - g_lastPresentLogTick) >= 1000) {
        LogLine("Present() hit - total frame #%ld (throttled ~1 log/sec)", frame);
        g_lastPresentLogTick = now;
    }

    return g_pRealPresent(This, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

/* Patch IDirect3DDevice9::Present (vtbl slot 17) to point at our hook.
 * The vtable pointed to by This->lpVtbl is a single shared, normally
 * read-only structure (typically in the real d3d9.dll's .rdata), so it must
 * be made writable with VirtualProtect before patching and restored after.
 * Guarded by g_deviceHooked so a second CreateDevice call (should one ever
 * happen) doesn't re-patch an already-patched vtable. */
static void InstallPresentHook(IDirect3DDevice9 *pDevice)
{
    IDirect3DDevice9Vtbl *vtbl;
    DWORD oldProtect;

    if (g_deviceHooked || pDevice == NULL) return;

    vtbl = (IDirect3DDevice9Vtbl *)pDevice->lpVtbl;

    if (!VirtualProtect(vtbl, sizeof(*vtbl), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        LogLine("ERROR: VirtualProtect failed patching IDirect3DDevice9 vtable, err=%lu", GetLastError());
        return;
    }

    g_pRealPresent = vtbl->Present;
    vtbl->Present = Hook_Present;

    VirtualProtect(vtbl, sizeof(*vtbl), oldProtect, &oldProtect);

    g_deviceHooked = TRUE;
    LogLine("Hooked IDirect3DDevice9::Present (vtable slot 17), original=0x%p", (void *)g_pRealPresent);
}

/* ---- CreateDevice hook (IDirect3D9 vtable slot 16) ---------------------- */
static HRESULT STDMETHODCALLTYPE Hook_CreateDevice(
    IDirect3D9 *This,
    UINT Adapter,
    D3DDEVTYPE DeviceType,
    HWND hFocusWindow,
    DWORD BehaviorFlags,
    D3DPRESENT_PARAMETERS *pPresentationParameters,
    IDirect3DDevice9 **ppReturnedDeviceInterface)
{
    HRESULT hr;

    LogLine("CreateDevice() called: Adapter=%u DeviceType=%d hFocusWindow=0x%p BehaviorFlags=0x%lX",
            Adapter, (int)DeviceType, (void *)hFocusWindow, (unsigned long)BehaviorFlags);

    if (pPresentationParameters) {
        LogLine("  D3DPRESENT_PARAMETERS: Windowed=%d BackBufferWidth=%u BackBufferHeight=%u "
                "BackBufferFormat=%d BackBufferCount=%u hDeviceWindow=0x%p SwapEffect=%d "
                "EnableAutoDepthStencil=%d AutoDepthStencilFormat=%d "
                "FullScreen_RefreshRateInHz=%u PresentationInterval=0x%X Flags=0x%lX",
                pPresentationParameters->Windowed,
                pPresentationParameters->BackBufferWidth,
                pPresentationParameters->BackBufferHeight,
                (int)pPresentationParameters->BackBufferFormat,
                pPresentationParameters->BackBufferCount,
                (void *)pPresentationParameters->hDeviceWindow,
                (int)pPresentationParameters->SwapEffect,
                pPresentationParameters->EnableAutoDepthStencil,
                (int)pPresentationParameters->AutoDepthStencilFormat,
                pPresentationParameters->FullScreen_RefreshRateInHz,
                pPresentationParameters->PresentationInterval,
                (unsigned long)pPresentationParameters->Flags);
    } else {
        LogLine("  WARNING: pPresentationParameters is NULL");
    }

    hr = g_pRealCreateDevice(This, Adapter, DeviceType, hFocusWindow, BehaviorFlags,
                              pPresentationParameters, ppReturnedDeviceInterface);

    LogLine("Real CreateDevice returned hr=0x%08lX, IDirect3DDevice9*=0x%p",
            (unsigned long)hr,
            (ppReturnedDeviceInterface && SUCCEEDED(hr)) ? (void *)*ppReturnedDeviceInterface : NULL);

    if (SUCCEEDED(hr) && ppReturnedDeviceInterface && *ppReturnedDeviceInterface) {
        InstallPresentHook(*ppReturnedDeviceInterface);
    }

    return hr;
}

/* Patch IDirect3D9::CreateDevice (vtbl slot 16) to point at our hook. Same
 * VirtualProtect-then-restore pattern and same-object idempotency guard as
 * InstallPresentHook above. */
static void InstallCreateDeviceHook(IDirect3D9 *pD3D9)
{
    IDirect3D9Vtbl *vtbl;
    DWORD oldProtect;

    if (g_d3d9Hooked || pD3D9 == NULL) return;

    vtbl = (IDirect3D9Vtbl *)pD3D9->lpVtbl;

    if (!VirtualProtect(vtbl, sizeof(*vtbl), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        LogLine("ERROR: VirtualProtect failed patching IDirect3D9 vtable, err=%lu", GetLastError());
        return;
    }

    g_pRealCreateDevice = vtbl->CreateDevice;
    vtbl->CreateDevice = Hook_CreateDevice;

    VirtualProtect(vtbl, sizeof(*vtbl), oldProtect, &oldProtect);

    g_d3d9Hooked = TRUE;
    LogLine("Hooked IDirect3D9::CreateDevice (vtable slot 16), original=0x%p", (void *)g_pRealCreateDevice);
}

/* The one and only export the game imports from d3d9.dll. */
__declspec(dllexport) IDirect3D9 *WINAPI Direct3DCreate9(UINT SDKVersion)
{
    IDirect3D9 *result;

    LogLine("Direct3DCreate9(SDKVersion=0x%X) called - forwarding to real d3d9.dll", SDKVersion);

    if (!LoadRealD3D9() || g_pRealDirect3DCreate9 == NULL) {
        LogLine("ERROR: cannot forward Direct3DCreate9 - real d3d9.dll/proc not available");
        return NULL;
    }

    result = g_pRealDirect3DCreate9(SDKVersion);

    LogLine("Real Direct3DCreate9 returned IDirect3D9* = 0x%p", (void *)result);

    if (result) {
        InstallCreateDeviceHook(result);
    }

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
