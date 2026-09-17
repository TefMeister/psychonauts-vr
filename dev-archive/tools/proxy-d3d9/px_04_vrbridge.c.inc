
static void VRBridge_ReleaseEyeBuffers(VRBridgeEyeState *eye)
{
    int s;
    for (s = 0; s < 2; s++) {
        if (eye->tex11[s])  { eye->tex11[s]->lpVtbl->Release(eye->tex11[s]); eye->tex11[s] = NULL; }
        if (eye->queryA[s]) { eye->queryA[s]->lpVtbl->Release(eye->queryA[s]); eye->queryA[s] = NULL; }
        if (eye->surfA[s])  { eye->surfA[s]->lpVtbl->Release(eye->surfA[s]); eye->surfA[s] = NULL; }
        if (eye->texA[s])   { eye->texA[s]->lpVtbl->Release(eye->texA[s]); eye->texA[s] = NULL; }
        if (eye->sysmemB[s]) { eye->sysmemB[s]->lpVtbl->Release(eye->sysmemB[s]); eye->sysmemB[s] = NULL; }
        eye->pendingB[s] = FALSE;
        eye->pendingA[s] = FALSE;
        eye->handleA[s] = NULL;
    }
    eye->hop1Count = 0;
    eye->frameCount = 0;
}

/* Creates one eye's double-buffered pipeline surfaces at the given dimensions (matching the
 * game's current eye render targets - g_bbWidth/g_bbHeight). Defensive throughout: any failure
 * releases what was partially created and returns FALSE, leaving the VR path inert for this eye
 * without touching anything else. */
static BOOL VRBridge_CreateEyeBuffers(VRBridgeEyeState *eye, EVREye vrEye, UINT w, UINT h)
{
    int s;
    HRESULT hr;

    eye->vrEye = vrEye;

    for (s = 0; s < 2; s++) {
        hr = g_pDevice->lpVtbl->CreateOffscreenPlainSurface(g_pDevice, w, h, D3DFMT_A8R8G8B8,
                                                              D3DPOOL_SYSTEMMEM, &eye->sysmemB[s], NULL);
        if (FAILED(hr)) { LogLine("VRBridge: CreateOffscreenPlainSurface (sysmemB[%d]) failed hr=0x%08lX", s, (unsigned long)hr); return FALSE; }

        hr = g_pVRDeviceA->lpVtbl->CreateTexture(g_pVRDeviceA, w, h, 1, D3DUSAGE_RENDERTARGET,
                                                  D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &eye->texA[s], &eye->handleA[s]);
        if (FAILED(hr)) { LogLine("VRBridge: CreateTexture (texA[%d]) failed hr=0x%08lX", s, (unsigned long)hr); return FALSE; }

        hr = eye->texA[s]->lpVtbl->GetSurfaceLevel(eye->texA[s], 0, &eye->surfA[s]);
        if (FAILED(hr)) { LogLine("VRBridge: GetSurfaceLevel (surfA[%d]) failed hr=0x%08lX", s, (unsigned long)hr); return FALSE; }

        hr = g_pVRDeviceA->lpVtbl->CreateQuery(g_pVRDeviceA, D3DQUERYTYPE_EVENT, &eye->queryA[s]);
        if (FAILED(hr)) { LogLine("VRBridge: CreateQuery (queryA[%d]) failed hr=0x%08lX", s, (unsigned long)hr); return FALSE; }

        hr = g_pVRDevice11->lpVtbl->OpenSharedResource(g_pVRDevice11, eye->handleA[s], &IID_ID3D11Texture2D, (void **)&eye->tex11[s]);
        if (FAILED(hr)) { LogLine("VRBridge: OpenSharedResource (tex11[%d]) failed hr=0x%08lX", s, (unsigned long)hr); return FALSE; }
    }

    LogLine("VRBridge: eye buffers created OK (eEye=%d, %ux%u)", (int)vrEye, w, h);
    return TRUE;
}

/* notes/32 (Task 1): query real per-eye geometry from IVRSystem and cache it (converted to this
 * game's world-unit scale) for Hook_SetVertexShaderConstantF to consume instead of the hardcoded
 * STEREO_HALF_IPD/focus-distance estimate. Called once from VRBridge_Init, right after IVRSystem
 * is confirmed ready. Fully defensive: any missing function pointer leaves g_vrGeomValid FALSE and
 * every caller transparently keeps using the pre-existing hardcoded fallback - this can never make
 * the stereo correction worse than before this session, only better when real data is available.
 * See notes/32 Sec1 for the exact real numbers this returned against the live null-driver runtime
 * (confirmed once via tools/vr-bridge/poc_ipd_query BEFORE this code was written) and Sec2 for the
 * k = (l+r)/2 derivation. */
static void VRBridge_QueryRealGeometry(void)
{
    int e;

    if (!g_pVRGetFloatProp || !g_pVRGetEyeToHeadTransform || !g_pVRGetProjectionRaw) {
        LogLine("VRBridge_QueryRealGeometry: IVRSystem function pointers not ready - keeping hardcoded STEREO_HALF_IPD/focus-distance fallback");
        return;
    }

    {
        ETrackedPropertyError propErr = ETrackedPropertyError_TrackedProp_Success;
        float ipdMeters = g_pVRGetFloatProp(g_pVRSystem, k_unTrackedDeviceIndex_Hmd,
                                             ETrackedDeviceProperty_Prop_UserIpdMeters_Float, &propErr);
        LogLine("VRBridge_QueryRealGeometry: real IPD = %.6f m (%.2f mm), err=%d",
                ipdMeters, ipdMeters * 1000.0f, (int)propErr);
    }

    for (e = 0; e < 2; e++) {
        EVREye vrEye = (e == 0) ? EVREye_Eye_Left : EVREye_Eye_Right;
        HmdMatrix34_t m;
        float l = 0.0f, r = 0.0f, t = 0.0f, b = 0.0f;

        ZeroMemory(&m, sizeof(m));
        g_pVRGetEyeToHeadTransform(g_pVRSystem, &m, vrEye);
        /* notes/32 Sec2: m.m[0][3] is the eye's X offset from the head origin, in meters, SIGNED
         * (negative=left of head-forward axis, positive=right) - this directly IS `d` (the eye
         * offset the stereo correction inserts along the camera's right vector) once converted to
         * world units. No separate sign multiply needed, unlike STEREO_HALF_IPD*sign - a real
         * headset's two eye offsets need not be exactly symmetric. */
        g_realHalfIPD[e] = m.m[0][3] * WORLD_UNITS_PER_METER;

        g_pVRGetProjectionRaw(g_pVRSystem, vrEye, &l, &r, &t, &b);
        /* notes/32 Sec2: k = (l+r)/2 - the raw tangent-space frustum-center offset directly maps
         * onto this file's existing shear coefficient `k` (X' = X + k*Z inserted before the game's
         * own symmetric-width Proj is applied) because a genuine asymmetric frustum with fixed
         * angular width (r-l unchanged) is mathematically identical to a symmetric frustum sheared
         * by its center offset - the standard "shift-lens" stereo-camera equivalence. Derivation:
         * the frustum-center ray satisfies X/(-Z) = (l+r)/2 in eye space (RH, Z negative in front);
         * the correction's own "clip_x==0" condition is X + k*Z == 0 -> X = -k*Z; equating the two
         * gives k = (l+r)/2 directly, no unit conversion needed (both sides are dimensionless
         * tangent ratios already in the same world-unit-per-world-unit space this file already
         * works in). */
        g_realShearK[e] = (l + r) * 0.5f;
        g_realShearValid[e] = (fabsf(g_realShearK[e]) > 1e-4f);
        /* notes/43: keep the raw tangents - the submit path crops the eye texture to exactly
         * this frustum so the compositor's angular mapping is 1:1 regardless of PSYVR_FOV_SCALE. */
        g_realProjRaw[e][0] = l; g_realProjRaw[e][1] = r;
        g_realProjRaw[e][2] = t; g_realProjRaw[e][3] = b;
        if (fabsf(l) + fabsf(r) > 0.01f && fabsf(t) + fabsf(b) > 0.01f) {
            if (e == 1 && g_realProjRawValid) g_realProjRawValid = TRUE; /* both eyes sane */
            if (e == 0) g_realProjRawValid = TRUE;                      /* provisional until eye 1 */
        } else {
            g_realProjRawValid = FALSE;
        }

        LogLine("VRBridge_QueryRealGeometry: eye=%d eyeToHead.x=%.6fm (%.3f world units) "
                "projRaw l=%.4f r=%.4f t=%.4f b=%.4f centerOffset=%.6f%s",
                e, m.m[0][3], g_realHalfIPD[e], l, r, t, b, g_realShearK[e],
                g_realShearValid[e] ? "" : " (symmetric - no real off-axis data, keeping focus-distance k fallback)");

        /* notes/40 Issue 1: the v0.1.4 suggested-PSYVR_FOV_SCALE log lived here, guarded by
         * g_projYScale > 0 - but this function runs at bridge init, BEFORE the game has ever
         * built a projection matrix, so the guard was always false and the line never printed
         * on real hardware. Stash the HMD vertical FOV instead; BPM_OnEntry logs the suggestion
         * one-shot the first time it caches the game projection with this stash available. */
        if (e == 0) {
            g_hmdFovyRad = atanf(fabsf(t)) + atanf(fabsf(b));
            LogLine("VRBridge_QueryRealGeometry: HMD vertical FOV=%.1f deg stashed - suggested "
                    "PSYVR_FOV_SCALE prints at first BPM cache", g_hmdFovyRad * 57.29578f);
        }
    }

    if (g_pVRGetRecommendedRTSize) {
        uint32_t rw = 0, rh = 0;
        g_pVRGetRecommendedRTSize(g_pVRSystem, &rw, &rh);
        LogLine("VRBridge_QueryRealGeometry: GetRecommendedRenderTargetSize = %u x %u per eye "
                "(current VR-submit eye buffers are %ux%u = backbuffer %ux%u at %ux render scale - notes/35)",
                rw, rh, g_eyeWidth, g_eyeHeight, g_bbWidth, g_bbHeight, g_eyeScale);
    }

    g_vrGeomValid = TRUE;
    LogLine("VRBridge_QueryRealGeometry: g_vrGeomValid = TRUE - stereo correction now uses real "
            "OpenVR-sourced IPD/eye-offset values instead of the hardcoded STEREO_HALF_IPD constant");
}

/* One-time init: private D3D9Ex device matched to the game's adapter, D3D11 device, OpenVR init +
 * IVRCompositor. Called lazily from SetupStereoSurfaces the first time g_vrSubmitEnabled is TRUE
 * and the game's own device/backbuffer dims are known. Never called more than once per process
 * (guarded by g_vrBridgeInitAttempted) - a failed attempt is NOT retried every frame/Reset. */
static void VRBridge_Init(IDirect3DDevice9 *pGameDevice, UINT w, UINT h)
{
    HRESULT hr;
    LUID gameLuid;
    IDirect3D9 *pGameD3D9 = NULL;
    IDXGIFactory1 *pFactory = NULL;
    IDXGIAdapter1 *pChosenAdapter = NULL;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL gotLevel;
    UINT i;

    g_vrBridgeInitAttempted = TRUE;

    /* notes/33 §4: once SteamVR has asked us to quit, never re-connect (e.g. via a later Reset
     * re-running SetupStereoSurfaces after the quit-time teardown cleared the attempted flag). */
    if (g_vrQuitRequested) { LogLine("VRBridge_Init: refused - SteamVR quit was already requested this process"); return; }

    if (!g_hRealD3D9) { LogLine("VRBridge_Init: ERROR real d3d9.dll not loaded"); return; }
    g_pRealDirect3DCreate9Ex = (Direct3DCreate9Ex_t)GetProcAddress(g_hRealD3D9, "Direct3DCreate9Ex");
    if (!g_pRealDirect3DCreate9Ex) { LogLine("VRBridge_Init: ERROR Direct3DCreate9Ex not exported by real d3d9.dll"); return; }

    hr = g_pRealDirect3DCreate9Ex(D3D_SDK_VERSION, &g_pVRD3D9Ex);
    if (FAILED(hr) || !g_pVRD3D9Ex) { LogLine("VRBridge_Init: Direct3DCreate9Ex failed hr=0x%08lX", (unsigned long)hr); return; }

    /* Match Device A to the SAME physical adapter the game's own device is using (required for a
     * valid shared handle even on a single-GPU machine - notes/25 Sec3a). Get the game device's
     * adapter LUID via its own IDirect3D9 (GetDirect3D), not assumed to be adapter 0. */
    hr = pGameDevice->lpVtbl->GetDirect3D(pGameDevice, &pGameD3D9);
    if (FAILED(hr) || !pGameD3D9) { LogLine("VRBridge_Init: GetDirect3D (game device) failed hr=0x%08lX", (unsigned long)hr); return; }
    {
        D3DADAPTER_IDENTIFIER9 ident;
        UINT gameAdapter = D3DADAPTER_DEFAULT; /* CreateDevice's Adapter arg isn't retrievable from the device itself;
                                                    D3DADAPTER_DEFAULT (0) matches this project's confirmed single-GPU setup
                                                    (notes/25/27: NVIDIA GTX 1660 SUPER, the only adapter). */
        hr = pGameD3D9->lpVtbl->GetAdapterIdentifier(pGameD3D9, gameAdapter, 0, &ident);
        if (FAILED(hr)) { LogLine("VRBridge_Init: GetAdapterIdentifier failed hr=0x%08lX", (unsigned long)hr); pGameD3D9->lpVtbl->Release(pGameD3D9); return; }
        LogLine("VRBridge_Init: game device adapter = \"%s\"", ident.Description);
    }
    pGameD3D9->lpVtbl->Release(pGameD3D9);

    hr = g_pVRD3D9Ex->lpVtbl->GetAdapterLUID(g_pVRD3D9Ex, D3DADAPTER_DEFAULT, &gameLuid);
    if (FAILED(hr)) { LogLine("VRBridge_Init: GetAdapterLUID failed hr=0x%08lX", (unsigned long)hr); return; }

    {
        WNDCLASSEXA wc;
        HWND hwnd;
        D3DPRESENT_PARAMETERS pp;

        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcA;
        wc.hInstance = GetModuleHandleA(NULL);
        wc.lpszClassName = "PsyVR_BridgeDeviceA";
        RegisterClassExA(&wc);
        hwnd = CreateWindowExA(0, wc.lpszClassName, "PsyVR bridge device A", WS_OVERLAPPEDWINDOW,
                                0, 0, 64, 64, NULL, NULL, wc.hInstance, NULL);
        if (!hwnd) { LogLine("VRBridge_Init: CreateWindowExA failed"); return; }

        ZeroMemory(&pp, sizeof(pp));
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = hwnd;
        pp.BackBufferFormat = D3DFMT_UNKNOWN;
        pp.BackBufferWidth = 64;
        pp.BackBufferHeight = 64;
        pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

        hr = g_pVRD3D9Ex->lpVtbl->CreateDeviceEx(g_pVRD3D9Ex, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
            D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE,
            &pp, NULL, &g_pVRDeviceA);
        if (FAILED(hr) || !g_pVRDeviceA) { LogLine("VRBridge_Init: CreateDeviceEx (Device A) failed hr=0x%08lX", (unsigned long)hr); return; }
    }
    LogLine("VRBridge_Init: private D3D9Ex Device A created OK");

    hr = g_pVRDeviceA->lpVtbl->CreateOffscreenPlainSurface(g_pVRDeviceA, w, h, D3DFMT_A8R8G8B8,
                                                             D3DPOOL_SYSTEMMEM, &g_pVRSysmemAScratch, NULL);
    if (FAILED(hr)) { LogLine("VRBridge_Init: CreateOffscreenPlainSurface (scratch) failed hr=0x%08lX", (unsigned long)hr); return; }

    hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&pFactory);
    if (FAILED(hr)) { LogLine("VRBridge_Init: CreateDXGIFactory1 failed hr=0x%08lX", (unsigned long)hr); return; }
    for (i = 0;; i++) {
        IDXGIAdapter1 *pAdapter = NULL;
        DXGI_ADAPTER_DESC1 desc;
        hr = pFactory->lpVtbl->EnumAdapters1(pFactory, i, &pAdapter);
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        pAdapter->lpVtbl->GetDesc1(pAdapter, &desc);
        if (desc.AdapterLuid.LowPart == gameLuid.LowPart && desc.AdapterLuid.HighPart == gameLuid.HighPart) {
            pChosenAdapter = pAdapter;
            break;
        }
        pAdapter->lpVtbl->Release(pAdapter);
    }
    if (!pChosenAdapter) { LogLine("VRBridge_Init: no matching DXGI adapter found"); pFactory->lpVtbl->Release(pFactory); return; }

    hr = D3D11CreateDevice((IDXGIAdapter *)pChosenAdapter, D3D_DRIVER_TYPE_UNKNOWN, NULL,
                            D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels),
                            D3D11_SDK_VERSION, &g_pVRDevice11, &gotLevel, &g_pVRContext11);
    pChosenAdapter->lpVtbl->Release(pChosenAdapter);
    pFactory->lpVtbl->Release(pFactory);
    if (FAILED(hr)) { LogLine("VRBridge_Init: D3D11CreateDevice failed hr=0x%08lX", (unsigned long)hr); return; }
    LogLine("VRBridge_Init: D3D11 device created OK, feature level=0x%X", gotLevel);

    {
        EVRInitError err = EVRInitError_VRInitError_None;
        uint32_t token = VR_InitInternal2(&err, EVRApplicationType_VRApplication_Scene, NULL);
        LogLine("VRBridge_Init: VR_InitInternal2 -> token=%u error=%d (%s)", token, (int)err, VR_GetVRInitErrorAsEnglishDescription(err));
        if (!(err == EVRInitError_VRInitError_None && token != 0)) return;
    }
    {
        EVRInitError compErr = EVRInitError_VRInitError_None;
        void **vtbl;
        g_pVRCompositor = VR_GetGenericInterface(IVRCompositor_Version, &compErr);
        if (!g_pVRCompositor) { LogLine("VRBridge_Init: VR_GetGenericInterface(IVRCompositor) failed error=%d", (int)compErr); return; }
        vtbl = (void **)VRBridge_RealVtable(g_pVRCompositor);
        g_pVRWaitGetPoses = (VRBridge_WaitGetPoses_t)vtbl[2];
        g_pVRSubmit = (VRBridge_Submit_t)vtbl[6];
        LogLine("VRBridge_Init: IVRCompositor ready (vtable=%p)", (void *)vtbl);
    }
    {
        /* notes/32 (Task 1): IVRSystem, for real IPD/eye-transform/off-axis-frustum/render-target-
         * size queries - a genuine improvement even without a physical headset (see the file-header
         * Milestone 9 comment), but NOT load-bearing for Submit itself (IVRCompositor above is) -
         * so a failure here logs and continues rather than aborting VRBridge_Init early; the
         * existing hardcoded stereo-correction fallback (g_vrGeomValid stays FALSE) covers it. */
        EVRInitError sysErr = EVRInitError_VRInitError_None;
        g_pVRSystem = VR_GetGenericInterface(IVRSystem_Version, &sysErr);
        if (!g_pVRSystem) {
            LogLine("VRBridge_Init: VR_GetGenericInterface(IVRSystem) failed error=%d - real geometry queries unavailable, keeping hardcoded stereo-correction fallback", (int)sysErr);
        } else {
            void **sysVtbl = (void **)VRBridge_RealVtable(g_pVRSystem);
            g_pVRGetRecommendedRTSize = (VRBridge_GetRecommendedRenderTargetSize_t)sysVtbl[0];
            g_pVRGetProjectionRaw = (VRBridge_GetProjectionRaw_t)sysVtbl[2];
            g_pVRGetEyeToHeadTransform = (VRBridge_GetEyeToHeadTransform_t)sysVtbl[5];
            g_pVRGetFloatProp = (VRBridge_GetFloatTrackedDeviceProperty_t)sysVtbl[23];
            g_pVRGetStringProp = (VRBridge_GetStringTrackedDeviceProperty_t)sysVtbl[28];
            g_pVRPollNextEvent = (VRBridge_PollNextEvent_t)sysVtbl[30];
            g_pVRAcknowledgeQuit = (VRBridge_AcknowledgeQuit_t)sysVtbl[47];
            LogLine("VRBridge_Init: IVRSystem ready (vtable=%p)", (void *)sysVtbl);
            /* notes/33 §3 asked for HMD identity in the log (the headset model never appeared
             * anywhere during the first physical test). Device index 0 is always the HMD. */
            {
                char idSys[128], idModel[128];
                ETrackedPropertyError perr = ETrackedPropertyError_TrackedProp_Success;
                idSys[0] = idModel[0] = '\0';
                g_pVRGetStringProp(g_pVRSystem, 0, ETrackedDeviceProperty_Prop_TrackingSystemName_String, idSys, sizeof(idSys), &perr);
                g_pVRGetStringProp(g_pVRSystem, 0, ETrackedDeviceProperty_Prop_ModelNumber_String, idModel, sizeof(idModel), &perr);
                LogLine("VRBridge_Init: HMD identity: trackingSystem=\"%s\" model=\"%s\"", idSys, idModel);
            }
            VRBridge_QueryRealGeometry();
        }
    }

    if (!VRBridge_CreateEyeBuffers(&g_vrEye1, EVREye_Eye_Left, w, h) ||
        !VRBridge_CreateEyeBuffers(&g_vrEye2, EVREye_Eye_Right, w, h)) {
        LogLine("VRBridge_Init: eye buffer creation failed - VR bridge NOT ready");
        return;
    }

    g_vrBufWidth = w;
    g_vrBufHeight = h;
    g_vrBridgeReady = TRUE;
    LogLine("VRBridge_Init: SUCCESS - g_vrBridgeReady = TRUE (%ux%u per eye)", w, h);
}

/* Called from SetupStereoSurfaces (device creation AND every Reset) once the existing monitor-
 * composite path's own surfaces are confirmed ready. Lazily inits Device A/D3D11/OpenVR on first
 * call; on every call (including after a Reset with new dimensions) ensures the eye buffers match
 * the CURRENT g_bbWidth/g_bbHeight, recreating them if the size changed. Fully defensive - any
 * failure leaves g_vrBridgeReady FALSE and the monitor composite path is completely unaffected. */
static void VRBridge_OnStereoSurfacesReady(IDirect3DDevice9 *pGameDevice, UINT w, UINT h)
{
    if (!g_vrSubmitEnabled) return;

    if (!g_vrBridgeInitAttempted) {
        VRBridge_Init(pGameDevice, w, h);
        return;
    }
    if (!g_pVRDeviceA || !g_pVRDevice11) return; /* init failed earlier - stay inert, don't retry every frame */

    if (g_vrBridgeReady && (w != g_vrBufWidth || h != g_vrBufHeight)) {
        LogLine("VRBridge: dimensions changed (%ux%u -> %ux%u), recreating eye buffers", g_vrBufWidth, g_vrBufHeight, w, h);
        g_vrBridgeReady = FALSE;
        VRBridge_ReleaseEyeBuffers(&g_vrEye1);
        VRBridge_ReleaseEyeBuffers(&g_vrEye2);
        if (VRBridge_CreateEyeBuffers(&g_vrEye1, EVREye_Eye_Left, w, h) &&
            VRBridge_CreateEyeBuffers(&g_vrEye2, EVREye_Eye_Right, w, h)) {
            g_vrBufWidth = w; g_vrBufHeight = h;
            g_vrBridgeReady = TRUE;
        }
    }
}

/* The per-eye, per-real-frame pump: hop 1 (game device readback -> promote to Device A when
 * ready, non-blocking) then hop 2 (check Device A's oldest pending upload; if its fence is
 * signaled, Submit that texture to the compositor). Mirrors
 * tools/vr-bridge/poc_dual_device_shared/dual_device_poc.c's proven Part 2 loop exactly, just
 * driven once per real Present-frame instead of a fixed iteration count. Never blocks: every
 * readiness check is a single non-blocking Lock/GetData call, and an unready buffer is simply
 * skipped this frame (dropped/retried next frame) rather than waited on. */
static void VRBridge_PumpEye(VRBridgeEyeState *eye, IDirect3DSurface9 *pGameEyeSurf)
{
    /* bCur/bPrev MUST be driven by a counter that advances every call (frameCount), NOT by
     * hop1Count - hop1Count only advances once a promotion actually succeeds (see below), so
     * deriving bCur from it created a deadlock (notes/29): bCur stuck at 0 forever, sysmemB[1]
     * never written, pendingB[bPrev] never TRUE, promotion (and therefore Submit) never reached. */
    int bCur = eye->frameCount % 2;
    int bPrev = (bCur + 1) % 2;
    HRESULT hr;
    eye->frameCount++;

    /* --- Hop 1a: kick off this frame's readback into the CURRENT sysmemB slot. ---
     * notes/31: timed alone (GetRenderTargetData call only) to separate its cost from the
     * LockRect/memcpy/UpdateSurface chain below, which notes/28 already measured as sub-0.1ms in
     * isolation against a synthetic Clear()-only workload but never against the real game scene. */
    {
        LARGE_INTEGER tG0, tG1;
        char label[32];
        QueryPerformanceCounter(&tG0);
        hr = g_pDevice->lpVtbl->GetRenderTargetData(g_pDevice, pGameEyeSurf, eye->sysmemB[bCur]);
        QueryPerformanceCounter(&tG1);
        _snprintf(label, sizeof(label), "GetRenderTargetData[%d]", (int)eye->vrEye);
        VRBridge_RecordSpan(&eye->statGRTD, tG1.QuadPart - tG0.QuadPart, label);
    }
    if (FAILED(hr)) return; /* leave pendingB[bCur] as-is; try again next frame */
    eye->pendingB[bCur] = TRUE;

    /* --- Hop 1b: non-blocking check of the OTHER (previous-frame) sysmemB slot; if ready, promote
     * it into Device A's next free slot. --- */
    if (eye->pendingB[bPrev]) {
        D3DLOCKED_RECT lockedB;
        LARGE_INTEGER tR0, tR1;
        BOOL didWork = FALSE;
        HRESULT lockHr;
        QueryPerformanceCounter(&tR0);
        lockHr = eye->sysmemB[bPrev]->lpVtbl->LockRect(eye->sysmemB[bPrev], &lockedB, NULL,
                                                        D3DLOCK_READONLY | D3DLOCK_DONOTWAIT);
        if (lockHr == S_OK) {
            didWork = TRUE;
            int aCur = eye->hop1Count % 2;
            BOOL aSlotFree = TRUE;
            if (eye->pendingA[aCur]) {
                HRESULT qHr = eye->queryA[aCur]->lpVtbl->GetData(eye->queryA[aCur], NULL, 0, 0);
                aSlotFree = (qHr == S_OK);
            }
            if (aSlotFree) {
                D3DLOCKED_RECT lockedA;
                hr = g_pVRSysmemAScratch->lpVtbl->LockRect(g_pVRSysmemAScratch, &lockedA, NULL, 0);
                if (SUCCEEDED(hr)) {
                    UINT y;
                    for (y = 0; y < g_vrBufHeight; y++) {
                        memcpy((BYTE *)lockedA.pBits + (size_t)y * lockedA.Pitch,
                               (BYTE *)lockedB.pBits + (size_t)y * lockedB.Pitch,
                               (size_t)g_vrBufWidth * 4);
                    }
                    g_pVRSysmemAScratch->lpVtbl->UnlockRect(g_pVRSysmemAScratch);

                    hr = g_pVRDeviceA->lpVtbl->UpdateSurface(g_pVRDeviceA, g_pVRSysmemAScratch, NULL, eye->surfA[aCur], NULL);
                    if (SUCCEEDED(hr)) {
                        eye->queryA[aCur]->lpVtbl->Issue(eye->queryA[aCur], D3DISSUE_END);
                        eye->queryA[aCur]->lpVtbl->GetData(eye->queryA[aCur], NULL, 0, D3DGETDATA_FLUSH); /* kick, non-blocking */
                        eye->pendingA[aCur] = TRUE;
                        eye->hop1Count++;
                    }
                }
            }
            /* else: Device A backpressure - drop this frame's promotion, no wait, try again next frame */
            eye->sysmemB[bPrev]->lpVtbl->UnlockRect(eye->sysmemB[bPrev]);
        }
        /* else lockHr != S_OK (e.g. D3DERR_WASSTILLDRAWING): the readback isn't done yet - do NOT
         * call UnlockRect (no successful Lock to match it), just leave this slot pending and check
         * again next frame. */
        if (didWork) {
            /* notes/31: only record the span when the chain actually ran end-to-end (LockRect
             * succeeded AND the rest executed) - a D3DERR_WASSTILLDRAWING skip is a near-zero-cost
             * non-blocking poll, not part of the real readback-chain cost being measured here. */
            char label[32];
            QueryPerformanceCounter(&tR1);
            _snprintf(label, sizeof(label), "ReadbackChain[%d]", (int)eye->vrEye);
            VRBridge_RecordSpan(&eye->statReadback, tR1.QuadPart - tR0.QuadPart, label);
        }
    }

    /* --- Hop 2: non-blocking check of the oldest Device-A slot with a pending upload; if its
     * fence is signaled, Submit that texture to the compositor. --- */
    if (eye->hop1Count >= 2 && g_pVRSubmit) {
        int aConsume = (eye->hop1Count - 2) % 2;
        if (eye->pendingA[aConsume]) {
            HRESULT qHr = eye->queryA[aConsume]->lpVtbl->GetData(eye->queryA[aConsume], NULL, 0, 0);
            if (qHr == S_OK) {
                Texture_t tex;
                EVRCompositorError subErr;
                tex.handle = (void *)eye->tex11[aConsume];
                tex.eType = ETextureType_TextureType_DirectX;
                tex.eColorSpace = EColorSpace_ColorSpace_Auto;
                /* notes/43: tangent-matched crop - NULL (full texture, the old behavior) whenever
                 * real geometry isn't available or PSYVR_SUBMIT_BOUNDS=0. */
                subErr = g_pVRSubmit(g_pVRCompositor, eye->vrEye, &tex,
                                     VRBridge_SubmitBounds((eye->vrEye == EVREye_Eye_Left) ? 0 : 1),
                                     EVRSubmitFlags_Submit_Default);
                if (subErr != EVRCompositorError_VRCompositorError_None) {
                    static DWORD s_lastErrLog = 0;
                    DWORD now = GetTickCount();
                    if (s_lastErrLog == 0 || (DWORD)(now - s_lastErrLog) >= 2000) {
                        LogLine("VRBridge: Submit(eye=%d) error=%d", (int)eye->vrEye, (int)subErr);
                        s_lastErrLog = now;
                    }
                } else {
                    /* notes/29: the success path previously had NO log statement at all, which is
                     * why 1700+ real frames of (it turned out, never-firing) per-frame submission
                     * produced zero log evidence either way - throttled ~1/sec per eye so this is
                     * verifiable going forward without flooding the log every frame. */
                    static DWORD s_lastOkLog[2] = { 0, 0 };
                    DWORD now = GetTickCount();
                    int idx = (eye->vrEye == EVREye_Eye_Left) ? 0 : 1;
                    if (s_lastOkLog[idx] == 0 || (DWORD)(now - s_lastOkLog[idx]) >= 1000) {
                        LogLine("VRBridge: Submit(eye=%d) OK (frame=%d)", (int)eye->vrEye, eye->frameCount);
                        s_lastOkLog[idx] = now;
                    }
                }
            }
            /* else: not ready yet - skip this frame's submit for this eye, no wait, try again next frame */
        }
    }
}

/* notes/31: split from the original single VRBridge_OnFrameComposited (which called WaitGetPoses
 * AND the per-eye pump together, both from CandB_AfterBoth_asm - i.e. AFTER both eyes had already
 * finished rendering). Direct instrumentation this session found WaitGetPoses costs a real,
 * consistent ~25ms/call, confirmed REQUIRED for Submit to succeed (skipping it entirely restores
 * full non-bridge framerate but breaks Submit with VRCompositorError_DoNotHaveFocus - see the
 * PresentationInterval comment on Hook_CreateDevice for the parallel double-pacing finding).
 * Standard OpenVR usage calls WaitGetPoses as early as possible each frame (right after the
 * previous Present, at the TOP of the next frame's work) specifically so its compositor-side wait
 * overlaps with the game's own CPU-side per-frame simulation, instead of sitting as pure added
 * tail latency after rendering is already done. This file's hook points make that possible: call
 * VRBridge_PumpPoses() from Hook_Present right after the real hardware Present succeeds (this
 * frame is fully done; the NEXT frame's CPU-side game logic starts immediately after, giving the
 * wait real work to overlap with) and keep VRBridge_PumpEyes() (which needs the actual rendered eye
 * surfaces) at its original call site in CandB_AfterBoth_asm. */
/* notes/33 §4 second half: SteamVR sends VREvent_Quit when the user exits SteamVR while the game
 * is running, then KILLS scene apps that neither acknowledge nor exit - and a vrserver that
 * vanishes mid-session leaves the next WaitGetPoses blocked forever. Poll the event queue once per
 * frame (cheap, non-blocking, runs on the same render thread as every other VR call in this file -
 * no concurrency to reason about) and on any quit-class event: acknowledge, then tear the whole
 * bridge down immediately - NOT under loader lock, all threads alive, vrserver still answering -
 * so the game simply drops back to flat monitor-stereo rendering and a later game exit has nothing
 * VR-related left to clean up. */
static void VRBridge_Shutdown(void); /* defined below */
static void VRBridge_UpdateHeadTracking(const HmdMatrix34_t *pose34); /* defined below the projection-cache globals it needs */

static void VRBridge_PollQuitEvents(void)
{
    struct VREvent_t ev;
    if (!g_pVRPollNextEvent || !g_pVRSystem || g_vrQuitRequested) return;
    while (g_pVRPollNextEvent(g_pVRSystem, &ev, (uint32_t)sizeof(ev))) {
        if (ev.eventType == EVREventType_VREvent_Quit ||
            ev.eventType == EVREventType_VREvent_ProcessQuit ||
            ev.eventType == EVREventType_VREvent_DriverRequestedQuit) {
            LogLine("VRBridge: SteamVR quit-class event %u received - acknowledging and shutting the VR bridge down now (game continues in monitor mode)", ev.eventType);
            g_vrQuitRequested = TRUE;
            if (g_pVRAcknowledgeQuit) g_pVRAcknowledgeQuit(g_pVRSystem);
            VRBridge_Shutdown();
            LogLine("VRBridge: quit-time teardown complete");
            return;
        }
    }
}

static void VRBridge_PumpPoses(void)
{
    if (!g_vrSubmitEnabled || !g_vrBridgeReady) return;
    VRBridge_PollQuitEvents();
    if (!g_vrBridgeReady) return; /* quit-time teardown may have just run */
    if (!g_pVRWaitGetPoses || g_vrSkipWaitPoses) return;

    {
        TrackedDevicePose_t renderPoses[64], gamePoses[64];
        LARGE_INTEGER tW0, tW1;
        ZeroMemory(renderPoses, sizeof(renderPoses));
        ZeroMemory(gamePoses, sizeof(gamePoses));
        QueryPerformanceCounter(&tW0);
        g_pVRWaitGetPoses(g_pVRCompositor, renderPoses, 64, gamePoses, 64);
        QueryPerformanceCounter(&tW1);
        VRBridge_RecordSpan(&g_statWaitGetPoses, tW1.QuadPart - tW0.QuadPart, "WaitGetPoses");

        /* notes/34: the HMD pose (device index 0) was being discarded here every frame - feed it
         * to the head-tracking path instead. Defined below the projection-cache globals it needs;
         * same render thread as everything else in this file. */
        VRBridge_UpdateHeadTracking(renderPoses[0].bPoseIsValid ? &renderPoses[0].mDeviceToAbsoluteTracking : NULL);
    }
}

/* Called once per real Present-frame from CandB_AfterBoth_asm, once both eyes have finished
 * rendering into g_pEye1Surf/g_pEye2Surf - unchanged from before except WaitGetPoses itself moved
 * out (see VRBridge_PumpPoses above). */
static void VRBridge_OnFrameComposited(void)
{
    if (!g_vrSubmitEnabled || !g_vrBridgeReady) return;
    if (!g_pEye1Surf || !g_pEye2Surf) return;

    if (!g_vrSkipPumpEye) {
        VRBridge_PumpEye(&g_vrEye1, g_pEye1Surf);
        VRBridge_PumpEye(&g_vrEye2, g_pEye2Surf);
    }
}

static void VRBridge_Shutdown(void)
{
    if (!g_vrBridgeInitAttempted) return;
    LogLine("VRBridge_Shutdown: releasing VR bridge resources");

    VRBridge_ReleaseEyeBuffers(&g_vrEye1);
    VRBridge_ReleaseEyeBuffers(&g_vrEye2);

    if (g_pVRCompositor) { VR_ShutdownInternal(); g_pVRCompositor = NULL; }
    if (g_pVRSysmemAScratch) { g_pVRSysmemAScratch->lpVtbl->Release(g_pVRSysmemAScratch); g_pVRSysmemAScratch = NULL; }
    if (g_pVRContext11) { g_pVRContext11->lpVtbl->Release(g_pVRContext11); g_pVRContext11 = NULL; }
    if (g_pVRDevice11) { g_pVRDevice11->lpVtbl->Release(g_pVRDevice11); g_pVRDevice11 = NULL; }
    if (g_pVRDeviceA) { g_pVRDeviceA->lpVtbl->Release(g_pVRDeviceA); g_pVRDeviceA = NULL; }
    if (g_pVRD3D9Ex) { g_pVRD3D9Ex->lpVtbl->Release(g_pVRD3D9Ex); g_pVRD3D9Ex = NULL; }

    /* notes/32: g_pVRSystem is owned by the same VR_InitInternal2/VR_ShutdownInternal lifecycle as
     * g_pVRCompositor (both come from the same VR_GetGenericInterface pool) - no separate release
     * call exists or is needed, just drop the cached pointers/function pointers and the derived
     * geometry cache so a future re-Init starts clean. */
    g_pVRSystem = NULL;
    g_pVRGetRecommendedRTSize = NULL;
    g_pVRGetProjectionRaw = NULL;
    g_pVRGetEyeToHeadTransform = NULL;
    g_pVRGetFloatProp = NULL;
    g_pVRGetStringProp = NULL;
    g_pVRPollNextEvent = NULL;
    g_pVRAcknowledgeQuit = NULL;
    g_vrGeomValid = FALSE;

    /* notes/34: head-tracking state dies with the bridge - the view snaps back to the game's own
     * untracked camera (correct for both the quit-event teardown and a dynamic unload). */
    g_trackYValid = FALSE;
    g_trackRefValid = FALSE;

    g_vrBridgeReady = FALSE;
    g_vrBridgeInitAttempted = FALSE;
}
