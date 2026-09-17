/* notes/35 diagnostic: write one render-target surface as a 32bpp BMP into %TEMP%. Uses a
 * transient sysmem surface + GetRenderTargetData (same mechanism the VR readback path already
 * proved cheap) and plain fwrite. Top-down BMP via negative biHeight - no row flipping. */
static void DumpSurfaceBMP(IDirect3DSurface9 *surf, UINT w, UINT h, const char *name)
{
    IDirect3DSurface9 *sys = NULL;
    D3DLOCKED_RECT lr;
    char path[MAX_PATH];
    DWORD tlen;
    HRESULT hr;

    if (!g_pDevice || !surf || w == 0 || h == 0) return;
    tlen = GetTempPathA(sizeof(path), path);
    if (tlen == 0 || tlen > sizeof(path) - 32) return;
    strcat(path, name);

    hr = g_pDevice->lpVtbl->CreateOffscreenPlainSurface(g_pDevice, w, h, D3DFMT_A8R8G8B8,
                                                          D3DPOOL_SYSTEMMEM, &sys, NULL);
    if (FAILED(hr)) { LogLine("DumpSurfaceBMP: scratch create failed hr=0x%08lX", (unsigned long)hr); return; }
    hr = g_pDevice->lpVtbl->GetRenderTargetData(g_pDevice, surf, sys);
    if (SUCCEEDED(hr) && SUCCEEDED(sys->lpVtbl->LockRect(sys, &lr, NULL, D3DLOCK_READONLY))) {
        FILE *f = fopen(path, "wb");
        if (f) {
            UINT row;
            BITMAPFILEHEADER bfh;
            BITMAPINFOHEADER bih;
            memset(&bfh, 0, sizeof(bfh)); memset(&bih, 0, sizeof(bih));
            bfh.bfType = 0x4D42; /* 'BM' */
            bfh.bfOffBits = sizeof(bfh) + sizeof(bih);
            bfh.bfSize = bfh.bfOffBits + w * h * 4;
            bih.biSize = sizeof(bih);
            bih.biWidth = (LONG)w;
            bih.biHeight = -(LONG)h; /* top-down */
            bih.biPlanes = 1;
            bih.biBitCount = 32;
            bih.biCompression = BI_RGB;
            fwrite(&bfh, sizeof(bfh), 1, f);
            fwrite(&bih, sizeof(bih), 1, f);
            for (row = 0; row < h; row++)
                fwrite((BYTE *)lr.pBits + row * lr.Pitch, w * 4, 1, f);
            fclose(f);
            LogLine("DumpSurfaceBMP: wrote %s (%ux%u)", path, w, h);
        }
        sys->lpVtbl->UnlockRect(sys);
    } else {
        LogLine("DumpSurfaceBMP: GetRenderTargetData/Lock failed hr=0x%08lX", (unsigned long)hr);
    }
    sys->lpVtbl->Release(sys);
}

static HRESULT STDMETHODCALLTYPE Hook_Present(
    IDirect3DDevice9 *This,
    CONST RECT *pSourceRect,
    CONST RECT *pDestRect,
    HWND hDestWindowOverride,
    CONST RGNDATA *pDirtyRegion)
{
    LONG frame = InterlockedIncrement(&g_frameCounter);
    DWORD now = GetTickCount();
    BOOL doLog = (g_lastPresentLogTick == 0 || (DWORD)(now - g_lastPresentLogTick) >= 1000);

    if (g_traceActive) {
        TraceFlushDrawsFwd();
        LogLine("TRACE: Present() hit phase=%ld eye2Presented=%ld%s", g_stereoPhase, g_eye2Presented,
                (g_stereoPhase == STEREO_PHASE_EYE1) ? "  -> SUPPRESSED (eye1 internal)" :
                (g_stereoPhase == STEREO_PHASE_EYE2 && g_eye2Presented) ? "  -> SUPPRESSED (extra eye2)" : "");
    }

    /* CandB's own nested call tree calls Present internally (discovered
     * this session - see the comment above CandB_BeforeEye1_asm). While
     * eye 1's call is in flight, its internal Present fires too early (eye
     * 2 hasn't been drawn yet) - fully suppress it, don't call through to
     * the real Present at all, just report success and let CandB's own
     * body continue running. */
    if (g_stereoPhase == STEREO_PHASE_EYE1) {
        return D3D_OK;
    }

    /* notes/20: suppress any EXTRA internal Present hits during eye 2's
     * pass beyond the first - see the comment on g_eye2Presented above
     * CandB_BeforeEye1_asm for why this matters in real gameplay (it never
     * triggered at the title screen, where each eye's pass only ever
     * called Present once). */
    if (g_stereoPhase == STEREO_PHASE_EYE2 && g_eye2Presented) {
        return D3D_OK;
    }

    if (g_stereoPhase == STEREO_PHASE_EYE2 && g_stereoReady) {
        /* Both eyes are now fully rendered (eye 1 completed normally
         * including its own suppressed Present above; eye 2 just finished
         * drawing right up to this Present call) - the real compositing
         * moment: stitch both offscreen eye surfaces into the left/right
         * halves of the real backbuffer before letting this one real
         * hardware Present go through. */
        g_eye2Presented = 1;

        /* notes/35 diagnostic (PSYVR_DUMP_EYES=1): dump both eye surfaces at the exact moment
         * the composite reads them - ground truth for the notes/23 black-left-eye bug (is eye1's
         * SURFACE black, or does the content get lost later?). ~5s throttle. */
        if (g_dumpEyes) {
            static DWORD s_lastDump = 0;
            DWORD dnow = GetTickCount();
            if (s_lastDump == 0 || (DWORD)(dnow - s_lastDump) >= 5000) {
                DumpSurfaceBMP(g_pEye1Surf, g_eyeWidth, g_eyeHeight, "psyvr_eye1.bmp");
                DumpSurfaceBMP(g_pEye2Surf, g_eyeWidth, g_eyeHeight, "psyvr_eye2.bmp");
                s_lastDump = dnow;
            }
        }

        /* notes/36: flush the vertex-shader-constant register histogram (~5s cadence). One line
         * per window: reg:count(xMaxVecs) for every register touched during eye phases. */
        if (g_regHisto) {
            static DWORD s_lastHisto = 0;
            DWORD hnow = GetTickCount();
            if (s_lastHisto == 0 || (DWORD)(hnow - s_lastHisto) >= 5000) {
                char line[900];
                int pos = 0, reg;
                for (reg = 0; reg < 256 && pos < (int)sizeof(line) - 32; reg++) {
                    LONG c = InterlockedExchange(&g_regHistoCount[reg], 0);
                    if (c > 0) {
                        pos += snprintf(line + pos, sizeof(line) - pos, " r%d:%ldx%u", reg, c,
                                        (unsigned)g_regHistoVecMax[reg]);
                        g_regHistoVecMax[reg] = 0;
                    }
                }
                if (pos > 0) LogLine("REGHISTO (eye-phase uploads this window):%s", line);
                pos = 0;
                for (reg = 0; reg < 64 && pos < (int)sizeof(line) - 48; reg++) {
                    LONG c = InterlockedExchange(&g_regComboCount[reg], 0);
                    if (c > 0) {
                        pos += snprintf(line + pos, sizeof(line) - pos, " [%s%s%s%s%s%s]:%ld",
                                        (reg & REGBIT_R6) ? "r6 " : "", (reg & REGBIT_R10) ? "r10 " : "",
                                        (reg & REGBIT_R13) ? "r13 " : "", (reg & REGBIT_R16) ? "r16 " : "",
                                        (reg & REGBIT_R64) ? "r64 " : "", (reg & REGBIT_R96) ? "r96 " : "", c);
                    }
                }
                if (pos > 0) LogLine("REGCOMBO (per-draw register sets):%s", line);
                s_lastHisto = hnow;
            }
        }
        RECT srcFull;
        RECT dstLeft;
        RECT dstRight;
        HRESULT hrL, hrR;

        srcFull.left = 0; srcFull.top = 0; srcFull.right = (LONG)g_eyeWidth; srcFull.bottom = (LONG)g_eyeHeight;
        dstLeft.left = 0; dstLeft.top = 0; dstLeft.right = (LONG)(g_bbWidth / 2); dstLeft.bottom = (LONG)g_bbHeight;
        dstRight.left = (LONG)(g_bbWidth / 2); dstRight.top = 0; dstRight.right = (LONG)g_bbWidth; dstRight.bottom = (LONG)g_bbHeight;

        hrL = This->lpVtbl->StretchRect(This, g_pEye1Surf, &srcFull, g_pRealBackBuffer, &dstLeft, D3DTEXF_LINEAR);
        hrR = This->lpVtbl->StretchRect(This, g_pEye2Surf, &srcFull, g_pRealBackBuffer, &dstRight, D3DTEXF_LINEAR);

        if (doLog) {
            /* notes/21: exact eye1/eye2 register-6 draw-call counts for THIS
             * real frame (not a throttled race sample like notes/20's
             * phase=1/phase=2 log line) - see comment on g_svscfCountEye1/2.
             * Also logs whether the game itself asked for a partial-rect
             * Present (pSourceRect/pDirtyRegion non-NULL) - real evidence for
             * whether the "forced full-backbuffer Present" fix below is
             * actually correcting anything, rather than being a no-op. */
            LogLine("Present() composite (phase EYE2): StretchRect L=0x%08lX R=0x%08lX | svscfEye1=%ld svscfEye2=%ld | origSrcRect=%p origDestRect=%p origDirtyRgn=%p",
                     (unsigned long)hrL, (unsigned long)hrR,
                     g_svscfCountEye1, g_svscfCountEye2,
                     (void *)pSourceRect, (void *)pDestRect, (void *)pDirtyRegion);
        }
        g_svscfCountEye1 = 0;
        g_svscfCountEye2 = 0;
    }

    g_frameCamCached = FALSE;

    if (doLog) {
        LogLine("Present() hit - total frame #%ld phase=%ld (throttled ~1 log/sec)", frame, g_stereoPhase);
        g_lastPresentLogTick = now;
    }

    /* notes/21: the real hardware Present is always forced to a full-
     * backbuffer blit (NULL source/dirty rect), regardless of what the game
     * itself passed in. Rationale: our composite step just unconditionally
     * overwrote the ENTIRE real backbuffer with fresh content via two full
     * StretchRects above - if the game's own Present call assumed a partial/
     * dirty-rect optimization based on what ITS OWN (single-eye, non-stereo)
     * rendering changed this frame, passing that same partial rect through
     * here could cause the hardware Present to only actually flip PART of
     * our new side-by-side image to the screen, leaving the rest showing
     * stale pixels from a prior frame - a plausible, concrete explanation
     * for a symptom that looks exactly like "one half of the screen frozen"
     * while the other updates normally. Forcing NULL/NULL/NULL here is
     * always safe for our use case (we always redraw 100% of the backbuffer
     * every real frame) regardless of whether this specific mechanism turns
     * out to be the actual cause - see the origSrcRect/origDestRect/
     * origDirtyRgn log fields above to confirm post-relaunch whether the
     * game was ever actually passing non-NULL values here. hDestWindowOverride
     * is passed through unmodified (unrelated to dirty-rect behavior). */
    /* notes/31: time the REAL hardware Present call itself - unconditional (not gated on
     * g_vrSubmitEnabled) so the SAME instrumented build can also measure a true VR-bridge-OFF
     * baseline with the identical direct methodology, not just when the bridge is active. This was
     * added after the per-span WaitGetPoses/GetRenderTargetData/readback-chain timing (above/below)
     * came back summing to only ~1.3ms/frame while the aggregate regression looked far larger - the
     * missing cost has to be somewhere, and the real hardware Present call (which blocks on the
     * GPU/compositor/vsync) is the next most likely place a GPU-side cost could surface, even
     * though none of our own CPU-side D3D9 calls measure as slow. This also turned out to be the
     * only DIRECT (non-double-counted) per-real-frame counter in this file - see the g_frameCounter
     * comment revision below. */
    {
        static VRBridgeTimingStat s_statRealPresent;
        LARGE_INTEGER tP0, tP1;
        HRESULT presentHr;
        QueryPerformanceCounter(&tP0);
        presentHr = g_pRealPresent(This, NULL, NULL, hDestWindowOverride, NULL);
        QueryPerformanceCounter(&tP1);
        VRBridge_RecordSpan(&s_statRealPresent, tP1.QuadPart - tP0.QuadPart, "RealPresentCall");

        /* notes/31: WaitGetPoses moved here (see VRBridge_PumpPoses's own comment) - called right
         * after the real hardware Present returns, i.e. at the very start of the next frame's work,
         * so its ~25ms compositor wait can overlap with the game's own CPU-side simulation for that
         * next frame instead of sitting entirely after CandB has already finished rendering it. */
        VRBridge_PumpPoses();

        /* notes/51: monitor-only first-person preview. VRBridge_PumpPoses is inert without SteamVR,
         * so when the bridge is NOT active but first person is requested, drive the FP build here
         * with a frozen (identity) head orientation. The register-6 patch then renders first person
         * on the flat monitor (no headset), so the shoulder anchor can be seen + tuned. When the
         * bridge IS active the pump above already ran the real head-tracked FP, so this is skipped. */
        /* notes/68 part 3: camfollow also needs the tracking path evaluated, or
         * head yaw is never computed and the follow camera sits at 0 degrees
         * forever. Without SteamVR this is the ONLY site that runs it, so a
         * monitor test of the head-follow camera (PSYVR_FAKE_POSE supplying the
         * sway) depends on this condition. g_fpPreviewMode stays OFF for the
         * follow case - it is an FP-anchor flag, and setting it would drag the
         * first-person build into a test that is not about first person. */
        if ((g_firstPerson || g_camFollowHead) && !(g_vrSubmitEnabled && g_vrBridgeReady)) {
            g_fpPreviewMode = g_firstPerson ? TRUE : FALSE;
            VRBridge_UpdateHeadTracking(NULL);
            g_fpPreviewMode = FALSE;
        }

        return presentHr;
    }
}

/* notes/21: the game runs Windowed=1 (confirmed in the CreateDevice log) -
 * in windowed mode a real device-lost/Reset cycle is much less commonly
 * triggered by plain Alt+Tab focus loss than in fullscreen-exclusive mode,
 * but nothing in this codebase handled Reset() AT ALL before this fix, which
 * is a real, well-known D3D9 correctness gap regardless of exactly when it
 * fires: Reset() invalidates every D3DPOOL_DEFAULT resource on the device,
 * including g_pRealBackBuffer/g_pEye1Surf/g_pEye2Surf (all created via
 * CreateRenderTarget/GetBackBuffer, both implicitly D3DPOOL_DEFAULT) - any
 * of our own code that runs after a real Reset without us first releasing
 * and then recreating those three surfaces would be operating on stale/
 * dangling COM pointers. Depending on driver behavior this can silently
 * produce exactly the kind of asymmetric, hard-to-explain per-surface
 * symptoms reported live this session (one eye's surface still showing
 * stale/frozen content because the GPU memory it referenced happened not to
 * be reclaimed yet, the other looking corrupted/dark because its memory WAS
 * reused) - a strong, concrete, mechanism-level candidate for "freeze
 * persists beyond just the game's own focus-loss auto-pause" specifically
 * because the user has been alt-tabbing to communicate during this exact
 * session, a variable no prior session (title-screen only) ever exercised.
 * Fix: release all three surfaces (and mark stereo not-ready, which every
 * other hook already guards on) before forwarding to the real Reset, then
 * recreate them via the same SetupStereoSurfaces() used at device creation. */
static HRESULT STDMETHODCALLTYPE Hook_Reset(
    IDirect3DDevice9 *This,
    D3DPRESENT_PARAMETERS *pPresentationParameters)
{
    HRESULT hr;

    LogLine("Reset() called - releasing stereo surfaces before real Reset (Windowed=%d %ux%u)",
            pPresentationParameters ? pPresentationParameters->Windowed : -1,
            pPresentationParameters ? pPresentationParameters->BackBufferWidth : 0,
            pPresentationParameters ? pPresentationParameters->BackBufferHeight : 0);

    g_stereoReady = FALSE;
    if (g_pRealBackBuffer)    { g_pRealBackBuffer->lpVtbl->Release(g_pRealBackBuffer); g_pRealBackBuffer = NULL; }
    if (g_pEye1Surf)          { g_pEye1Surf->lpVtbl->Release(g_pEye1Surf); g_pEye1Surf = NULL; }
    if (g_pEye2Surf)          { g_pEye2Surf->lpVtbl->Release(g_pEye2Surf); g_pEye2Surf = NULL; }
    /* notes/22: the two new private per-eye depth-stencil surfaces (and the captured
     * original auto depth-stencil) are D3DPOOL_DEFAULT too - same Reset-invalidation
     * hazard as the three surfaces above, same fix. */
    if (g_pRealDepthStencil)  { g_pRealDepthStencil->lpVtbl->Release(g_pRealDepthStencil); g_pRealDepthStencil = NULL; }
    if (g_pEye1DepthStencil)  { g_pEye1DepthStencil->lpVtbl->Release(g_pEye1DepthStencil); g_pEye1DepthStencil = NULL; }
    if (g_pEye2DepthStencil)  { g_pEye2DepthStencil->lpVtbl->Release(g_pEye2DepthStencil); g_pEye2DepthStencil = NULL; }

    /* notes/31: same PresentationInterval override as Hook_CreateDevice - a Reset (e.g. Alt+Tab)
     * could otherwise revert the desktop swap chain to vsync-on, re-introducing the redundant
     * double-pacing against WaitGetPoses. */
    if (g_vrSubmitEnabled && pPresentationParameters &&
        pPresentationParameters->PresentationInterval != D3DPRESENT_INTERVAL_IMMEDIATE) {
        pPresentationParameters->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    }

    hr = g_pRealReset(This, pPresentationParameters);

    LogLine("Real Reset returned hr=0x%08lX", (unsigned long)hr);

    if (SUCCEEDED(hr) && pPresentationParameters) {
        SetupStereoSurfaces(This, pPresentationParameters);
    }

    return hr;
}

/* notes/35: with eye render targets bigger than the backbuffer, any viewport the GAME sets in
 * backbuffer pixel units during an eye pass would confine rendering to the top-left corner of
 * the scaled target. D3D9 auto-sets a full-RT viewport on every SetRenderTarget (covering the
 * common case), but this hook covers explicit game viewports (letterboxing, partial-screen
 * effects): while an eye pass is in flight and scale > 1, scale X/Y/Width/Height up to match.
 * Outside eye phases (real backbuffer bound) viewports pass through untouched. */
typedef HRESULT (STDMETHODCALLTYPE *SetViewport_t)(IDirect3DDevice9 *This, CONST D3DVIEWPORT9 *pViewport);
static SetViewport_t g_pRealSetViewport = NULL;

/* ---- notes/42: UI viewport shrink (HUD invisible at FOV scale > 1) ------------------------- */
/* While a UI-signature shader is bound during an eye phase, render through a center-shrunk
 * viewport so the screen-space HUD keeps its native angular footprint instead of inheriting the
 * FOV widening. g_uiVpSaved holds the viewport to restore when a non-UI shader binds again; a
 * phase transition invalidates the applied state (the eye RT bind resets the viewport). */
static BOOL g_uiVpApplied = FALSE;
static D3DVIEWPORT9 g_uiVpSaved;

static void UIVp_ShrinkFrom(D3DVIEWPORT9 *vp, float s)
{
    DWORD w = (DWORD)((float)vp->Width * s + 0.5f);
    DWORD h = (DWORD)((float)vp->Height * s + 0.5f);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    vp->X += (vp->Width - w) / 2;
    vp->Y += (vp->Height - h) / 2;
    vp->Width = w;
    vp->Height = h;
}

static BOOL UIVp_ShrinkWanted(void)
{
    return g_curShaderIsUI &&
           (g_stereoPhase == STEREO_PHASE_EYE1 || g_stereoPhase == STEREO_PHASE_EYE2) &&
           g_uiVpScale > 0.0f && g_uiVpScale < 0.9995f;
}

static void UIVp_Reconcile(IDirect3DDevice9 *dev)
{
    BOOL want = UIVp_ShrinkWanted();
    if (want && !g_uiVpApplied) {
        D3DVIEWPORT9 vp;
        if (dev && g_pRealSetViewport &&
            SUCCEEDED(dev->lpVtbl->GetViewport(dev, &vp))) {
            g_uiVpSaved = vp;
            UIVp_ShrinkFrom(&vp, g_uiVpScale);
            g_pRealSetViewport(dev, &vp);
            g_uiVpApplied = TRUE;
        }
    } else if (!want && g_uiVpApplied) {
        if (dev && g_pRealSetViewport)
            g_pRealSetViewport(dev, &g_uiVpSaved);
        g_uiVpApplied = FALSE;
    }
}
void UIVp_PhaseChangedFwd(void); /* defined after g_pDevice's declaration region, next to UIShift_ReconcileFwd */

static HRESULT STDMETHODCALLTYPE Hook_SetViewport(IDirect3DDevice9 *This, CONST D3DVIEWPORT9 *pViewport)
{
    if (g_stereoPhase != STEREO_PHASE_IDLE && g_eyeScale > 1 && pViewport != NULL) {
        D3DVIEWPORT9 vp = *pViewport;
        vp.X *= g_eyeScale;
        vp.Y *= g_eyeScale;
        vp.Width *= g_eyeScale;
        vp.Height *= g_eyeScale;
        {
            static DWORD s_lastLog = 0;
            DWORD now = GetTickCount();
            if (s_lastLog == 0 || (DWORD)(now - s_lastLog) >= 2000) {
                LogLine("SetViewport (eye phase %ld): game vp %lux%lu@(%lu,%lu) -> scaled %lux%lu@(%lu,%lu)",
                        g_stereoPhase,
                        (unsigned long)pViewport->Width, (unsigned long)pViewport->Height,
                        (unsigned long)pViewport->X, (unsigned long)pViewport->Y,
                        (unsigned long)vp.Width, (unsigned long)vp.Height,
                        (unsigned long)vp.X, (unsigned long)vp.Y);
                s_lastLog = now;
            }
        }
        /* notes/42: a game viewport arriving while the UI shrink is active becomes the new
         * restore base; keep the shrink applied on top of it. */
        if (g_uiVpApplied) {
            g_uiVpSaved = vp;
            UIVp_ShrinkFrom(&vp, g_uiVpScale);
        }
        return g_pRealSetViewport(This, &vp);
    }
    if (g_uiVpApplied && pViewport != NULL) {
        D3DVIEWPORT9 vp = *pViewport;
        g_uiVpSaved = vp;
        UIVp_ShrinkFrom(&vp, g_uiVpScale);
        return g_pRealSetViewport(This, &vp);
    }
    return g_pRealSetViewport(This, pViewport);
}

