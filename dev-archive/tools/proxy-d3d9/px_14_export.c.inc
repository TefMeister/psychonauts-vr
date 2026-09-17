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

    /* notes/31: when the VR bridge is active, WaitGetPoses (called once/frame, required for
     * IVRCompositor::Submit to succeed - see the VRBridge_OnFrameComposited comment) is ITSELF a
     * real, measured ~25ms/frame blocking wait, paced by the OpenVR compositor - confirmed via
     * direct A/B instrumentation (skipping it entirely restored the full non-bridge framerate, but
     * broke Submit with VRCompositorError_DoNotHaveFocus, proving it's required, not optional). The
     * game's own desktop-mirror swap chain requesting D3DPRESENT_INTERVAL_DEFAULT (vsync-on,
     * confirmed via the log line above: PresentationInterval=0x0) stacks a SECOND, independent
     * blocking wait (~14ms at this monitor's refresh rate) serially after WaitGetPoses's own wait -
     * two separate frame-pacing mechanisms compounding, when only one (WaitGetPoses, the real VR
     * timing source) should gate the frame. Forcing the desktop swap chain to
     * D3DPRESENT_INTERVAL_IMMEDIATE removes the redundant second wait; the on-screen window may tear
     * (irrelevant - it is only ever a monitor preview once a real HMD is presenting the actual view
     * via Submit). Only applied when g_vrSubmitEnabled, so the non-VR path is completely unaffected. */
    if (g_vrSubmitEnabled && pPresentationParameters &&
        pPresentationParameters->PresentationInterval != D3DPRESENT_INTERVAL_IMMEDIATE) {
        LogLine("VRBridge: forcing PresentationInterval 0x%X -> D3DPRESENT_INTERVAL_IMMEDIATE (avoid double frame-pacing against WaitGetPoses)",
                pPresentationParameters->PresentationInterval);
        pPresentationParameters->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    }

    hr = g_pRealCreateDevice(This, Adapter, DeviceType, hFocusWindow, BehaviorFlags,
                              pPresentationParameters, ppReturnedDeviceInterface);

    LogLine("Real CreateDevice returned hr=0x%08lX, IDirect3DDevice9*=0x%p",
            (unsigned long)hr,
            (ppReturnedDeviceInterface && SUCCEEDED(hr)) ? (void *)*ppReturnedDeviceInterface : NULL);

    if (SUCCEEDED(hr) && ppReturnedDeviceInterface && *ppReturnedDeviceInterface) {
        InstallPresentHook(*ppReturnedDeviceInterface);
        InstallSetVSConstFHook(*ppReturnedDeviceInterface);
        if (pPresentationParameters) {
            SetupStereoSurfaces(*ppReturnedDeviceInterface, pPresentationParameters);
        }

        /* notes/65: capture the game's real window handle and, if PSYVR_SUPPRESS_AUTOPAUSE=1,
         * subclass it so focus-loss messages never reach the game's own WndProc - see
         * Hook_GameWndProc's comment for why. */
        g_gameHwnd = hFocusWindow ? hFocusWindow
                     : (pPresentationParameters ? pPresentationParameters->hDeviceWindow : NULL);
        if (g_suppressAutoPause && g_gameHwnd && !g_origWndProc) {
            g_origWndProc = (WNDPROC)SetWindowLongPtrA(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)Hook_GameWndProc);
            LogLine("AutoPauseSuppress: subclassed game window %p, orig WndProc=%p",
                    (void *)g_gameHwnd, (void *)g_origWndProc);
        }
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

    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        InitializeCriticalSection(&g_logLock);
        g_logLockInit = TRUE;
        LogLine("==== psychonautsvr proxy d3d9.dll: DLL_PROCESS_ATTACH (pid=%lu) ====", GetCurrentProcessId());
        if (!InstallInlineHooks()) {
            LogLine("ERROR: InstallInlineHooks failed - stereo prototype disabled, proxy still runs as pure observer");
        }
        VRBridge_ReadEnableFlag(); /* notes/28: PSYVR_ENABLE_SUBMIT=1 opts in, default off */
        break;
    case DLL_PROCESS_DETACH:
        LogLine("==== psychonautsvr proxy d3d9.dll: DLL_PROCESS_DETACH (pid=%lu, %s) ====",
                GetCurrentProcessId(), lpvReserved ? "process terminating" : "dynamic unload");
        /* notes/33 §4 zombie fix: lpvReserved != NULL means the process is terminating -
         * ExitProcess has ALREADY killed every other thread (vrclient's IPC threads, D3D
         * driver workers) before this runs. Any teardown that waits on them - and
         * VR_ShutdownInternal/device Release both can - waits on corpses and hangs forever,
         * leaving an unkillable 1-thread zombie. Per the documented DllMain contract, do
         * nothing here and let the OS reclaim everything. Full teardown only runs on a
         * dynamic FreeLibrary unload (lpvReserved == NULL), where other threads still live. */
        if (lpvReserved != NULL)
            break;
        VRBridge_Shutdown();
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
