/* ---- notes/35: one-frame RT/Clear/draw trace (PSYVR_TRACE_FRAME=1) -------------------------- */
typedef HRESULT (STDMETHODCALLTYPE *SetRenderTarget_t)(IDirect3DDevice9 *This, DWORD idx, IDirect3DSurface9 *pRT);
typedef HRESULT (STDMETHODCALLTYPE *Clear_t)(IDirect3DDevice9 *This, DWORD Count, CONST D3DRECT *pRects,
                                             DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil);
typedef HRESULT (STDMETHODCALLTYPE *DrawPrimitive_t)(IDirect3DDevice9 *This, D3DPRIMITIVETYPE t, UINT s, UINT c);
typedef HRESULT (STDMETHODCALLTYPE *DrawIndexedPrimitive_t)(IDirect3DDevice9 *This, D3DPRIMITIVETYPE t,
                                                            INT b, UINT mi, UINT nv, UINT si, UINT pc);
typedef HRESULT (STDMETHODCALLTYPE *DrawPrimitiveUP_t)(IDirect3DDevice9 *This, D3DPRIMITIVETYPE t, UINT c,
                                                       CONST void *d, UINT stride);
typedef HRESULT (STDMETHODCALLTYPE *DrawIndexedPrimitiveUP_t)(IDirect3DDevice9 *This, D3DPRIMITIVETYPE t,
                                                              UINT mi, UINT nv, UINT pc, CONST void *id,
                                                              D3DFORMAT ifmt, CONST void *vd, UINT stride);
static SetRenderTarget_t g_pRealSetRenderTarget = NULL;
static Clear_t g_pRealClear = NULL;
static DrawPrimitive_t g_pRealDrawPrimitive = NULL;
static DrawIndexedPrimitive_t g_pRealDrawIndexedPrimitive = NULL;
static DrawPrimitiveUP_t g_pRealDrawPrimitiveUP = NULL;
static DrawIndexedPrimitiveUP_t g_pRealDrawIndexedPrimitiveUP = NULL;

typedef HRESULT (STDMETHODCALLTYPE *StretchRect_t)(IDirect3DDevice9 *This, IDirect3DSurface9 *pSrc,
                                                   CONST RECT *pSrcRect, IDirect3DSurface9 *pDst,
                                                   CONST RECT *pDstRect, D3DTEXTUREFILTERTYPE Filter);
static StretchRect_t g_pRealStretchRect = NULL;

static const char *TraceSurfName(IDirect3DSurface9 *s)
{
    if (s == NULL) return "NULL";
    if (s == g_pEye1Surf) return "EYE1";
    if (s == g_pEye2Surf) return "EYE2";
    if (s == g_pRealBackBuffer) return "BACKBUF";
    if (s == g_pEye1DepthStencil) return "EYE1DS";
    if (s == g_pEye2DepthStencil) return "EYE2DS";
    if (s == g_pRealDepthStencil) return "REALDS";
    return "other";
}

static void TraceFlushDraws(void)
{
    LONG n = InterlockedExchange(&g_traceDrawCount, 0);
    if (n > 0) LogLine("TRACE:   ... %ld draw calls (phase=%ld)", n, g_stereoPhase);
}
void TraceFlushDrawsFwd(void) { TraceFlushDraws(); } /* for the CandB callbacks defined earlier */

static HRESULT STDMETHODCALLTYPE Hook_SetRenderTarget(IDirect3DDevice9 *This, DWORD idx, IDirect3DSurface9 *pRT)
{
    /* notes/35: THE notes/23 black-left-eye fix. The engine records "the screen" RT pointer once
     * per frame while the REAL backbuffer is still bound (post-present phase), restores that
     * recorded pointer mid-pass after its render-to-texture work, then re-records after its own
     * (suppressed) internal Present - so during eye 1's pass its "restore the screen" bind targets
     * the REAL BACKBUFFER and the whole menu scene (73+ draws, traced live) lands there instead of
     * in EYE1, which stays at its cleared black. Eye 2's pass re-recorded after BeforeEye2 and
     * correctly restores EYE2. Fix: while an eye pass is in flight, binding the real backbuffer
     * MEANS binding that eye's target - redirect it. Inert for render paths (gameplay) that never
     * bind the backbuffer mid-pass, and for idle phase (AfterBoth's own legitimate restore). */
    if (idx == 0 && pRT == g_pRealBackBuffer && g_stereoReady) {
        IDirect3DSurface9 *redir = NULL;
        if (g_stereoPhase == STEREO_PHASE_EYE1) redir = g_pEye1Surf;
        else if (g_stereoPhase == STEREO_PHASE_EYE2) redir = g_pEye2Surf;
        if (redir) {
            static DWORD s_lastLog = 0;
            DWORD rnow = GetTickCount();
            if (s_lastLog == 0 || (DWORD)(rnow - s_lastLog) >= 2000) {
                LogLine("SetRenderTarget redirect: game bound BACKBUF during eye phase %ld -> %s (notes/23 fix)",
                        g_stereoPhase, (g_stereoPhase == STEREO_PHASE_EYE1) ? "EYE1" : "EYE2");
                s_lastLog = rnow;
            }
            if (g_traceActive) {
                TraceFlushDraws();
                LogLine("TRACE: SetRenderTarget(%lu, %p=BACKBUF) phase=%ld  -> REDIRECTED to %s",
                        (unsigned long)idx, (void *)pRT, g_stereoPhase, TraceSurfName(redir));
            }
            return g_pRealSetRenderTarget(This, idx, redir);
        }
    }
    if (g_traceActive) {
        TraceFlushDraws();
        LogLine("TRACE: SetRenderTarget(%lu, %p=%s) phase=%ld", (unsigned long)idx, (void *)pRT,
                TraceSurfName(pRT), g_stereoPhase);
    }
    return g_pRealSetRenderTarget(This, idx, pRT);
}

static HRESULT STDMETHODCALLTYPE Hook_Clear(IDirect3DDevice9 *This, DWORD Count, CONST D3DRECT *pRects,
                                            DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil)
{
    if (g_traceActive) {
        IDirect3DSurface9 *cur = NULL;
        This->lpVtbl->GetRenderTarget(This, 0, &cur);
        TraceFlushDraws();
        LogLine("TRACE: Clear(flags=0x%lX color=0x%08lX) on RT=%s phase=%ld",
                (unsigned long)Flags, (unsigned long)Color, TraceSurfName(cur), g_stereoPhase);
        if (cur) cur->lpVtbl->Release(cur);
    }
    return g_pRealClear(This, Count, pRects, Flags, Color, Z, Stencil);
}

typedef HRESULT (STDMETHODCALLTYPE *SetDepthStencilSurface_t)(IDirect3DDevice9 *This, IDirect3DSurface9 *pS);
typedef HRESULT (STDMETHODCALLTYPE *GetRenderTarget_t)(IDirect3DDevice9 *This, DWORD idx, IDirect3DSurface9 **ppRT);
static SetDepthStencilSurface_t g_pRealSetDepthStencilSurface = NULL;
static GetRenderTarget_t g_pRealGetRenderTarget = NULL;

static HRESULT STDMETHODCALLTYPE Hook_SetDepthStencilSurface(IDirect3DDevice9 *This, IDirect3DSurface9 *pS)
{
    /* notes/35 (black-left-eye fix, part 3): same principle as the RT redirect - during an eye
     * pass, binding the REAL depth-stencil means binding that eye's private one. A mid-pass
     * restore of the real DS breaks depth testing for everything drawn after it (each eye's
     * geometry was depth-laid into its own private DS - notes/22). */
    if (pS == g_pRealDepthStencil && g_stereoReady && pS != NULL) {
        IDirect3DSurface9 *redir = NULL;
        if (g_stereoPhase == STEREO_PHASE_EYE1) redir = g_pEye1DepthStencil;
        else if (g_stereoPhase == STEREO_PHASE_EYE2) redir = g_pEye2DepthStencil;
        if (redir) {
            if (g_traceActive) {
                TraceFlushDraws();
                LogLine("TRACE: SetDepthStencilSurface(REALDS) phase=%ld  -> REDIRECTED to %s",
                        g_stereoPhase, TraceSurfName(redir));
            }
            return g_pRealSetDepthStencilSurface(This, redir);
        }
    }
    if (g_traceActive) {
        TraceFlushDraws();
        LogLine("TRACE: SetDepthStencilSurface(%p=%s) phase=%ld", (void *)pS, TraceSurfName(pS), g_stereoPhase);
    }
    return g_pRealSetDepthStencilSurface(This, pS);
}

static HRESULT STDMETHODCALLTYPE Hook_GetRenderTarget(IDirect3DDevice9 *This, DWORD idx, IDirect3DSurface9 **ppRT)
{
    HRESULT hr = g_pRealGetRenderTarget(This, idx, ppRT);
    if (g_traceActive && SUCCEEDED(hr) && ppRT) {
        TraceFlushDraws();
        LogLine("TRACE: GetRenderTarget(%lu) -> %p=%s phase=%ld", (unsigned long)idx, (void *)*ppRT,
                TraceSurfName(*ppRT), g_stereoPhase);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE Hook_StretchRect(IDirect3DDevice9 *This, IDirect3DSurface9 *pSrc,
                                                  CONST RECT *pSrcRect, IDirect3DSurface9 *pDst,
                                                  CONST RECT *pDstRect, D3DTEXTUREFILTERTYPE Filter)
{
    /* notes/35 (black-left-eye fix, part 2): the engine's post-process chain reads "the screen"
     * back via StretchRect. During an eye pass the screen IS the eye surface - redirect a
     * backbuffer SOURCE the same way Hook_SetRenderTarget redirects a backbuffer bind. Source
     * rects are scaled to the eye surface's dims (they differ when g_eyeScale > 1). */
    if (pSrc == g_pRealBackBuffer && g_stereoReady &&
        (g_stereoPhase == STEREO_PHASE_EYE1 || g_stereoPhase == STEREO_PHASE_EYE2)) {
        IDirect3DSurface9 *eyeSrc = (g_stereoPhase == STEREO_PHASE_EYE1) ? g_pEye1Surf : g_pEye2Surf;
        RECT sc;
        CONST RECT *pUse = pSrcRect;
        if (pSrcRect && g_eyeScale > 1) {
            sc.left = pSrcRect->left * (LONG)g_eyeScale;
            sc.top = pSrcRect->top * (LONG)g_eyeScale;
            sc.right = pSrcRect->right * (LONG)g_eyeScale;
            sc.bottom = pSrcRect->bottom * (LONG)g_eyeScale;
            pUse = &sc;
        }
        if (g_traceActive) {
            TraceFlushDraws();
            LogLine("TRACE: StretchRect(src=BACKBUF -> dst=%s) phase=%ld  -> src REDIRECTED to %s",
                    TraceSurfName(pDst), g_stereoPhase, TraceSurfName(eyeSrc));
        }
        {
            static DWORD s_lastLog = 0;
            DWORD rnow = GetTickCount();
            if (s_lastLog == 0 || (DWORD)(rnow - s_lastLog) >= 2000) {
                LogLine("StretchRect redirect: game read BACKBUF during eye phase %ld -> reading %s instead (notes/23 fix)",
                        g_stereoPhase, (g_stereoPhase == STEREO_PHASE_EYE1) ? "EYE1" : "EYE2");
                s_lastLog = rnow;
            }
        }
        return g_pRealStretchRect(This, eyeSrc, pUse, pDst, pDstRect, Filter);
    }
    if (g_traceActive) {
        TraceFlushDraws();
        LogLine("TRACE: StretchRect(src=%s -> dst=%s) phase=%ld", TraceSurfName(pSrc), TraceSurfName(pDst), g_stereoPhase);
    }
    return g_pRealStretchRect(This, pSrc, pSrcRect, pDst, pDstRect, Filter);
}

/* notes/36: consume the since-last-draw register mask at every draw during eye phases. */
static void RegComboOnDraw(void)
{
    LONG mask;
    if (!g_regHisto || g_stereoPhase == STEREO_PHASE_IDLE) return;
    mask = InterlockedExchange(&g_regComboMask, 0);
    InterlockedIncrement(&g_regComboCount[mask & 63]);
}

/* notes/36: TRUE if the bytecode never reads constants c6..c9 as a source - the UI-shader
 * signature (all 10 screen-space shaders match, all 445 c6-transform shaders don't). vs_2_0
 * opcode tokens carry their operand count in bits 24..27; vs_1_1 (none shipped) returns FALSE. */
static BOOL VSBytecodeIsUIShader(CONST DWORD *pFunc)
{
    UINT i = 1;
    if ((pFunc[0] & 0xFFFF0000u) != 0xFFFE0000u) return FALSE;
    if ((pFunc[0] & 0xFFFFu) < 0x0200u) return FALSE;
    while (i < 16384) {
        DWORD t = pFunc[i];
        UINT op, nops, k;
        if (t == 0x0000FFFFu) break;
        if ((t & 0xFFFF) == 0xFFFE) { i += 1 + ((t >> 16) & 0x7FFF); continue; }
        op = t & 0xFFFF;
        nops = (t >> 24) & 0xF;
        if (op != 81 && op != 48 && op != 47 && op != 31) { /* skip def/defi/defb/dcl */
            for (k = 2; k <= nops && (i + k) < 16384; k++) { /* k=1 is the dest */
                DWORD o = pFunc[i + k];
                DWORD rt = ((o >> 28) & 0x7) | ((o >> 8) & 0x18);
                DWORD r = o & 0x7FF;
                if (rt == 2 && r >= 6 && r <= 9) return FALSE;
            }
        }
        i += 1 + nops;
    }
    return TRUE;
}

/* notes/36: dump every vertex shader's bytecode once (regHisto mode) - ground truth for which
 * constant registers each shader's transform math actually reads. Bytecode ends at 0x0000FFFF. */
typedef HRESULT (STDMETHODCALLTYPE *CreateVertexShader_t)(IDirect3DDevice9 *This, CONST DWORD *pFunction,
                                                          IDirect3DVertexShader9 **ppShader);
static CreateVertexShader_t g_pRealCreateVertexShader = NULL;

static HRESULT STDMETHODCALLTYPE Hook_CreateVertexShader(IDirect3DDevice9 *This, CONST DWORD *pFunction,
                                                         IDirect3DVertexShader9 **ppShader)
{
    HRESULT hr = g_pRealCreateVertexShader(This, pFunction, ppShader);
    if (SUCCEEDED(hr) && pFunction && ppShader && *ppShader && VSBytecodeIsUIShader(pFunction)) {
        LONG idx = InterlockedIncrement(&g_uiShaderCount) - 1;
        if (idx < UI_SHADER_MAX) {
            g_uiShaders[idx] = (void *)*ppShader;
            LogLine("CreateVertexShader: UI-signature shader #%ld registered (%p) for per-eye depth shift", idx, (void *)*ppShader);
        } else {
            InterlockedDecrement(&g_uiShaderCount);
        }
    }
    if (SUCCEEDED(hr) && pFunction && g_regHisto) {
        UINT n = 0;
        while (pFunction[n] != 0x0000FFFFu && n < 16384) n++;
        if (n < 16384) {
            LONG idx = InterlockedIncrement(&g_vsDumpIndex);
            char path[MAX_PATH];
            DWORD tlen = GetTempPathA(sizeof(path), path);
            n++;
            if (tlen != 0 && tlen < sizeof(path) - 48) {
                char name[48];
                FILE *f;
                snprintf(name, sizeof(name), "psyvr_vs_%02ld.bin", idx);
                strcat(path, name);
                f = fopen(path, "wb");
                if (f) { fwrite(pFunction, 4, n, f); fclose(f); }
                /* notes/52: remember which shader POINTER got which dump index, so a later draw-time
                 * hit (Hook_SetVertexShaderConstantF, on a c96 skinned upload) can say exactly which
                 * psyvr_vs_NN.bin is the skinned world shader worth disassembling offline. */
                if (ppShader && *ppShader && g_vsDumpMapCount < VS_DUMP_MAP_MAX) {
                    g_vsDumpPtr[g_vsDumpMapCount] = *ppShader;
                    g_vsDumpMapIdx[g_vsDumpMapCount] = (int)idx;
                    g_vsDumpMapCount++;
                }
                LogLine("CreateVertexShader #%ld: version=0x%08lX len=%u dwords -> %s",
                        idx, (unsigned long)pFunction[0], n, name);
            }
        }
    }
    return hr;
}

/* Reconciles the device's c50.x with the wanted shift. Called on shader binds and phase
 * transitions; uses the REAL SetVertexShaderConstantF to avoid recursing into our own hook. */
static void UIShift_Reconcile(IDirect3DDevice9 *dev)
{
    float want = UIShift_Wanted();
    if (want != g_uiShiftApplied && dev && g_pRealSetVSConstF) {
        float v[4];
        if (SUCCEEDED(dev->lpVtbl->GetVertexShaderConstantF(dev, 50, v, 1))) {
            v[0] += want - g_uiShiftApplied;
            g_pRealSetVSConstF(dev, 50, v, 1);
            g_uiShiftApplied = want;
        }
    }
}
void UIShift_ReconcileFwd(void) { if (g_pDevice) UIShift_Reconcile(g_pDevice); } /* for the CandB callbacks */

/* notes/42: phase transition - the eye/backbuffer RT bind in SetEyeAndTarget already reset the
 * device viewport to full-RT, so the applied flag is stale by definition: clear it (never
 * restore g_uiVpSaved across a phase change) and re-shrink if a UI shader is still bound. */
void UIVp_PhaseChangedFwd(void)
{
    g_uiVpApplied = FALSE;
    if (g_pDevice) UIVp_Reconcile(g_pDevice);
}

typedef HRESULT (STDMETHODCALLTYPE *SetVertexShader_t)(IDirect3DDevice9 *This, IDirect3DVertexShader9 *pShader);
static SetVertexShader_t g_pRealSetVertexShader = NULL;

static HRESULT STDMETHODCALLTYPE Hook_SetVertexShader(IDirect3DDevice9 *This, IDirect3DVertexShader9 *pShader)
{
    LONG i, n = g_uiShaderCount, isUI = 0;
    if (n > UI_SHADER_MAX) n = UI_SHADER_MAX;
    for (i = 0; i < n; i++) {
        if (g_uiShaders[i] == (void *)pShader) { isUI = 1; break; }
    }
    g_curShaderIsUI = isUI;
    g_curUIShaderIdx = isUI ? i : -1;
    g_currentVSPtr = pShader;  /* notes/52: track for the c96 shader-identity log below */
    UIVp_Reconcile(This);   /* notes/42: shrink/restore the viewport for UI draws */
    UIShift_Reconcile(This);
    return g_pRealSetVertexShader(This, pShader);
}

/* notes/67: identity of the texture bound to stage 0, as a bare pointer for
 * comparison only (never dereferenced). §11 names "per-draw geometry/texture
 * identity" as what UI-element separation needs that shader signature cannot
 * give - two draws through the same UI shader are told apart by which texture
 * (font atlas vs speech-bubble art) they sample. GetTexture AddRefs, so this
 * releases immediately; the pointer stays valid for comparison because the
 * texture is still bound. Only called while tracing. */
static void *TraceBoundTex0(IDirect3DDevice9 *dev)
{
    IDirect3DBaseTexture9 *tex = NULL;
    void *p = NULL;
    if (dev && dev->lpVtbl->GetTexture(dev, 0, &tex) == D3D_OK && tex) {
        p = (void *)tex;
        tex->lpVtbl->Release(tex);
    }
    return p;
}

/* notes/67: is this the dialogue/menu class of UI draw? See g_dlgDepthWorld for
 * how the shape test was established. Only meaningful while a UI shader is
 * bound. */
static int IsDialogueClassDraw(D3DPRIMITIVETYPE t, UINT prims)
{
    return g_curShaderIsUI && g_dlgDepthWorld > 0.0f &&
           t == D3DPT_TRIANGLELIST && prims > 1;
}

/* Push the dialogue depth for one draw, then restore. Two extra constant
 * uploads per dialogue draw; there are only a handful per eye, so the cost is
 * irrelevant next to keeping the HUD and the conversation on separate planes. */
static void UIDepth_PushDialogue(IDirect3DDevice9 *dev)
{
    g_uiDepthOverride = g_dlgDepthWorld;
    UIShift_Reconcile(dev);
}
static void UIDepth_Pop(IDirect3DDevice9 *dev)
{
    g_uiDepthOverride = 0.0f;
    UIShift_Reconcile(dev);
}

static HRESULT STDMETHODCALLTYPE Hook_DrawPrimitive(IDirect3DDevice9 *This, D3DPRIMITIVETYPE t, UINT s, UINT c)
{
    if (IsDialogueClassDraw(t, c)) {
        HRESULT hr;
        UIDepth_PushDialogue(This);
        hr = g_pRealDrawPrimitive(This, t, s, c);
        UIDepth_Pop(This);
        RegComboOnDraw();
        return hr;
    }
    if (g_traceActive) {
        LONG ord = InterlockedIncrement(&g_traceDrawCount);
        if (g_curShaderIsUI)
            LogLine("TRACE-UI: #%ld DrawPrimitive sh=%ld type=%d start=%u prims=%u phase=%ld c50=(%.4f,%.4f) tex0=%p",
                    ord, g_curUIShaderIdx, (int)t, s, c, g_stereoPhase,
                    g_lastC50[0], g_lastC50[1], TraceBoundTex0(This));
    }
    RegComboOnDraw();
    return g_pRealDrawPrimitive(This, t, s, c);
}
static HRESULT STDMETHODCALLTYPE Hook_DrawIndexedPrimitive(IDirect3DDevice9 *This, D3DPRIMITIVETYPE t,
                                                           INT b, UINT mi, UINT nv, UINT si, UINT pc)
{
    if (IsDialogueClassDraw(t, pc)) {
        HRESULT hr;
        UIDepth_PushDialogue(This);
        hr = g_pRealDrawIndexedPrimitive(This, t, b, mi, nv, si, pc);
        UIDepth_Pop(This);
        RegComboOnDraw();
        return hr;
    }
    if (g_traceActive) {
        LONG ord = InterlockedIncrement(&g_traceDrawCount);
        if (g_curShaderIsUI)
            LogLine("TRACE-UI: #%ld DrawIndexedPrimitive sh=%ld type=%d base=%d minIdx=%u nVerts=%u prims=%u phase=%ld c50=(%.4f,%.4f) tex0=%p",
                    ord, g_curUIShaderIdx, (int)t, b, mi, nv, pc, g_stereoPhase,
                    g_lastC50[0], g_lastC50[1], TraceBoundTex0(This));
    }
    RegComboOnDraw();
    return g_pRealDrawIndexedPrimitive(This, t, b, mi, nv, si, pc);
}
static HRESULT STDMETHODCALLTYPE Hook_DrawPrimitiveUP(IDirect3DDevice9 *This, D3DPRIMITIVETYPE t, UINT c,
                                                      CONST void *d, UINT stride)
{
    if (g_traceActive) {
        TraceFlushDraws();
        LogLine("TRACE: DrawPrimitiveUP(count=%u) phase=%ld  <- pretransformed/UP path", c, g_stereoPhase);
        if (g_curShaderIsUI && d != NULL && stride >= 8 && c >= 1) {
            const float *v0 = (const float *)d;
            const float *v2 = (const float *)((const BYTE *)d + 2 * stride);
            LogLine("TRACE-UI: DPUP sh=%ld type=%d prims=%u stride=%u v0=[%.4f %.4f %.4f %.4f] v2=[%.4f %.4f %.4f %.4f] c50=(%.4f,%.4f)",
                    g_curUIShaderIdx, (int)t, c, stride, v0[0], v0[1], (stride >= 12) ? v0[2] : 0.0f, (stride >= 16) ? v0[3] : 0.0f,
                    v2[0], v2[1], (stride >= 12) ? v2[2] : 0.0f, (stride >= 16) ? v2[3] : 0.0f,
                    g_lastC50[0], g_lastC50[1]);
        }
    }
    RegComboOnDraw();
    return g_pRealDrawPrimitiveUP(This, t, c, d, stride);
}
static HRESULT STDMETHODCALLTYPE Hook_DrawIndexedPrimitiveUP(IDirect3DDevice9 *This, D3DPRIMITIVETYPE t,
                                                             UINT mi, UINT nv, UINT pc, CONST void *id,
                                                             D3DFORMAT ifmt, CONST void *vd, UINT stride)
{
    if (g_traceActive) {
        TraceFlushDraws();
        LogLine("TRACE: DrawIndexedPrimitiveUP(prims=%u) phase=%ld  <- pretransformed/UP path", pc, g_stereoPhase);
        if (g_curShaderIsUI && vd != NULL && stride >= 8 && nv >= 3) {
            const float *v0 = (const float *)vd;
            const float *v2 = (const float *)((const BYTE *)vd + 2 * stride);
            LogLine("TRACE-UI: DIPUP sh=%ld type=%d nVerts=%u prims=%u stride=%u v0=[%.4f %.4f %.4f %.4f] v2=[%.4f %.4f %.4f %.4f] c50=(%.4f,%.4f)",
                    g_curUIShaderIdx, (int)t, nv, pc, stride, v0[0], v0[1], (stride >= 12) ? v0[2] : 0.0f, (stride >= 16) ? v0[3] : 0.0f,
                    v2[0], v2[1], (stride >= 12) ? v2[2] : 0.0f, (stride >= 16) ? v2[3] : 0.0f,
                    g_lastC50[0], g_lastC50[1]);
        }
    }
    RegComboOnDraw();
    return g_pRealDrawIndexedPrimitiveUP(This, t, mi, nv, pc, id, ifmt, vd, stride);
}

/* Patch IDirect3DDevice9::Present (vtbl slot 17) and ::Reset (vtbl slot 16)
 * to point at our hooks. The vtable pointed to by This->lpVtbl is a single
 * shared, normally read-only structure (typically in the real d3d9.dll's
 * .rdata), so it must be made writable with VirtualProtect before patching
 * and restored after. Guarded by g_deviceHooked so a second CreateDevice
 * call (should one ever happen) doesn't re-patch an already-patched vtable.
 * Both hooks are patched together under one VirtualProtect call since
 * they're adjacent slots on the same vtable struct. */
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
    g_pRealReset = vtbl->Reset;
    vtbl->Reset = Hook_Reset;
    g_pRealSetViewport = vtbl->SetViewport;
    vtbl->SetViewport = Hook_SetViewport; /* notes/35: eye-scale viewport correction, same vtable */
    /* notes/35: SetRenderTarget and StretchRect are ALWAYS hooked - they carry the notes/23
     * black-left-eye fix (backbuffer bind/read redirect during eye passes), not just tracing. */
    g_pRealSetRenderTarget = vtbl->SetRenderTarget;
    vtbl->SetRenderTarget = Hook_SetRenderTarget;
    g_pRealStretchRect = vtbl->StretchRect;
    vtbl->StretchRect = Hook_StretchRect;
    g_pRealSetDepthStencilSurface = vtbl->SetDepthStencilSurface;
    vtbl->SetDepthStencilSurface = Hook_SetDepthStencilSurface;
    g_pRealGetRenderTarget = vtbl->GetRenderTarget;
    vtbl->GetRenderTarget = Hook_GetRenderTarget;
    /* notes/36: CreateVertexShader/SetVertexShader are ALWAYS hooked - they carry the UI-depth
     * feature (UI-shader identification + bind tracking); the bytecode dump inside remains gated
     * behind PSYVR_REG_HISTO. */
    g_pRealCreateVertexShader = vtbl->CreateVertexShader;
    vtbl->CreateVertexShader = Hook_CreateVertexShader;
    g_pRealSetVertexShader = vtbl->SetVertexShader;
    vtbl->SetVertexShader = Hook_SetVertexShader;
    /* notes/67: the Draw* hooks are now installed UNCONDITIONALLY. They used to
     * be gated on the startup env vars below, which made the runtime "trace"
     * command silently useless: it flips g_traceFrames long after the vtable was
     * patched, so the hooks that do the logging were never in place and the
     * trace produced zero draw lines. Cost of always hooking is one predictable
     * branch per draw (the bodies early-out on !g_traceActive, and
     * RegComboOnDraw returns immediately unless g_regHisto). Clear stays gated -
     * nothing reads it at runtime. */
    g_pRealDrawPrimitive = vtbl->DrawPrimitive;
    vtbl->DrawPrimitive = Hook_DrawPrimitive;
    g_pRealDrawIndexedPrimitive = vtbl->DrawIndexedPrimitive;
    vtbl->DrawIndexedPrimitive = Hook_DrawIndexedPrimitive;
    g_pRealDrawPrimitiveUP = vtbl->DrawPrimitiveUP;
    vtbl->DrawPrimitiveUP = Hook_DrawPrimitiveUP;
    g_pRealDrawIndexedPrimitiveUP = vtbl->DrawIndexedPrimitiveUP;
    vtbl->DrawIndexedPrimitiveUP = Hook_DrawIndexedPrimitiveUP;
    if (g_traceFrames || g_dumpEyes || g_regHisto) {
        g_pRealClear = vtbl->Clear;
        vtbl->Clear = Hook_Clear;
    }
    LogLine("Draw* hooks installed (always); Clear hook %s",
            (g_traceFrames || g_dumpEyes || g_regHisto) ? "installed" : "skipped");

    VirtualProtect(vtbl, sizeof(*vtbl), oldProtect, &oldProtect);

    g_deviceHooked = TRUE;
    LogLine("Hooked IDirect3DDevice9::Present (vtable slot 17), original=0x%p; ::Reset (vtable slot 16), original=0x%p; ::SetViewport (slot 47), original=0x%p",
            (void *)g_pRealPresent, (void *)g_pRealReset, (void *)g_pRealSetViewport);
}

