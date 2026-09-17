
/* Trampoline entry points (allocated executable memory, filled in by
 * InstallInlineHooks). Calling through these runs the REAL, unmodified
 * original function body exactly as if the inline patch had never been
 * applied - see MakeTrampoline(). Declared with exact asm-label names (via
 * the `asm("name")` GCC/clang extension) so the naked hook functions below
 * can reference them by a guaranteed, un-mangled symbol name. */
static void *BVM_Trampoline_asm asm("BVM_Trampoline_asm") = NULL;
static void *BPM_Trampoline_asm asm("BPM_Trampoline_asm") = NULL;
static void *CandB_Trampoline_asm asm("CandB_Trampoline_asm") = NULL;
/* Only ever written/read from the raw asm blocks below (never referenced
 * from C code), so the compiler's dead-store elimination would otherwise
 * strip it entirely - __attribute__((used)) forces it to be kept and
 * emitted with external linkage under its exact asm-label name. */
__attribute__((used)) DWORD CandB_This_asm asm("CandB_This_asm") = 0;


typedef void (__cdecl *BuildViewMatrixFn)(void *pOut, Vec3 *pEye, Vec3 *pAt, Vec3 *pUp);
static BuildViewMatrixFn g_realBuildViewMatrix = NULL;

/* ---- small vector helpers ---- */
static Vec3 Vec3Sub(Vec3 a, Vec3 b) { Vec3 r; r.x = a.x - b.x; r.y = a.y - b.y; r.z = a.z - b.z; return r; }
static Vec3 Vec3Cross(Vec3 a, Vec3 b)
{
    Vec3 r;
    r.x = a.y * b.z - a.z * b.y;
    r.y = a.z * b.x - a.x * b.z;
    r.z = a.x * b.y - a.y * b.x;
    return r;
}
static Vec3 Vec3Normalize(Vec3 v)
{
    float len = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    Vec3 r;
    if (len < 1e-6f) { r.x = 1.0f; r.y = 0.0f; r.z = 0.0f; return r; }
    r.x = v.x / len; r.y = v.y / len; r.z = v.z / len;
    return r;
}

/* ---- C-side helpers called from the naked asm hooks (asm-labeled so the
 * raw asm blocks below can `call` them by an exact, guaranteed name) ---- */

/* Fires on every real BuildViewMatrix call. Only the FIRST call of each
 * real Present frame does anything: caches a clean (never mutated) copy of
 * eye/at/up plus the pOutMatrix pointer the game's own pipeline reads from,
 * and computes the camera's right vector. The real call is always let
 * through completely unmodified - deliberately NOT mutating *pEye in place,
 * to sidestep the "persistent pointer, writes carry forward frame-to-frame"
 * compounding behavior documented in notes/09. */
void __cdecl BVM_OnEntry_asm(void *pOutMatrix, Vec3 *pEye, Vec3 *pAt, Vec3 *pUp) asm("BVM_OnEntry_asm");
void __cdecl BVM_OnEntry_asm(void *pOutMatrix, Vec3 *pEye, Vec3 *pAt, Vec3 *pUp)
{
    if (g_frameCamCached) return;
    if (!pEye || !pAt || !pUp) return;

    g_baseEye = *pEye;
    g_baseAt = *pAt;
    g_baseUp = *pUp;
    g_camPOutMatrix = pOutMatrix;

    {
        Vec3 fwd = Vec3Normalize(Vec3Sub(g_baseAt, g_baseEye));
        g_rightVec = Vec3Normalize(Vec3Cross(fwd, g_baseUp));
    }

    /* notes/24: off-axis convergence distance for THIS frame - see the
     * comment on STEREO_FOCUS_DISTANCE_MIN/DEFAULT and the top-of-file
     * Milestone 7 note for the full reasoning. Uses the real eye->at
     * distance, clamped away from zero/near-zero so a degenerate close
     * `at` point can't produce an extreme shear. */
    {
        Vec3 eyeToAt = Vec3Sub(g_baseAt, g_baseEye);
        float dist = sqrtf(eyeToAt.x * eyeToAt.x + eyeToAt.y * eyeToAt.y + eyeToAt.z * eyeToAt.z);
        g_focusDistance = (dist > STEREO_FOCUS_DISTANCE_MIN) ? dist : STEREO_FOCUS_DISTANCE_MIN;
    }

    g_frameCamCached = TRUE;

    /* notes/49: rebuild P^-1 * V^-1 for this frame (undoes view+proj so a per-draw WVP yields the
     * draw's World matrix), and reset the per-frame Raz-origin search. Only when first person is on
     * (skips the cost entirely otherwise). V^-1 (view->world) rows are the camera basis + eye;
     * P^-1 mirrors the projection built in the head-tracking path. */
    if (g_firstPerson && g_projXScaleValid && g_projYScale != 0.0f) {
        Vec3 fwd = Vec3Normalize(Vec3Sub(g_baseAt, g_baseEye));
        Vec3 xaxis = g_rightVec;                       /* = normalize(cross(fwd, up)) - notes/34 */
        Vec3 zaxis; Vec3 yaxis;
        float Vinv[16], Pinv[16];
        float xS = g_projXScale, yS = g_projYScale;
        float A = g_projZFar / (g_projZNear - g_projZFar);
        float B = g_projZNear * g_projZFar / (g_projZNear - g_projZFar);
        zaxis.x = -fwd.x; zaxis.y = -fwd.y; zaxis.z = -fwd.z;   /* normalize(eye-at) */
        yaxis = Vec3Cross(zaxis, xaxis);
        Vinv[0]=xaxis.x; Vinv[1]=xaxis.y; Vinv[2]=xaxis.z; Vinv[3]=0.0f;
        Vinv[4]=yaxis.x; Vinv[5]=yaxis.y; Vinv[6]=yaxis.z; Vinv[7]=0.0f;
        Vinv[8]=zaxis.x; Vinv[9]=zaxis.y; Vinv[10]=zaxis.z; Vinv[11]=0.0f;
        Vinv[12]=g_baseEye.x; Vinv[13]=g_baseEye.y; Vinv[14]=g_baseEye.z; Vinv[15]=1.0f;
        { int i; for (i=0;i<16;i++) Pinv[i]=0.0f; }
        Pinv[0]=1.0f/xS; Pinv[5]=1.0f/yS; Pinv[11]=1.0f/B; Pinv[14]=-1.0f; Pinv[15]=A/B;
        Mat4MulRow(g_pinvVinv, Pinv, Vinv);            /* World = WVP * (P^-1 * V^-1) */
        g_pinvVinvValid = TRUE;
        g_razSumX = g_razSumY = g_razSumZ = 0.0f;      /* start this frame's centroid accumulation */
        g_razCount = 0;
        g_razNearValid = FALSE;                        /* notes/51: reset this frame's nearest-to-eye Raz search */
    }

    {
        static DWORD s_lastLog = 0;
        DWORD now = GetTickCount();
        if (s_lastLog == 0 || (DWORD)(now - s_lastLog) >= 2000) {
            LogLine("BVM cache SET: pOut=0x%p eye=(%.2f,%.2f,%.2f) at=(%.2f,%.2f,%.2f) right=(%.4f,%.4f,%.4f)",
                    pOutMatrix, g_baseEye.x, g_baseEye.y, g_baseEye.z,
                    g_baseAt.x, g_baseAt.y, g_baseAt.z,
                    g_rightVec.x, g_rightVec.y, g_rightVec.z);
            s_lastLog = now;
        }
    }
}

/* Fires on every real BuildProjectionMatrix call (rare - notes/07/13 both
 * found this fires only when FOV/aspect/resolution actually change, not once
 * per frame). Computes and caches xScale (the value D3DXMatrixPerspectiveFovRH
 * would put in the projection matrix's [0][0]) directly from the entry
 * arguments, replicating the exact rawFov->fovy conversion already
 * disassembled in notes/07 (fovy = rawFov/ADDR_FOV_DIV_CONST*ADDR_FOV_MUL_CONST,
 * then xScale = cot(fovy/2)/Aspect) - see the comment on g_projXScale above
 * for why this replaced an earlier pointer-caching approach. */
void __cdecl BPM_OnEntry_asm(float rawFov, float aspect, float zn, float zf) asm("BPM_OnEntry_asm");
void __cdecl BPM_OnEntry_asm(float rawFov, float aspect, float zn, float zf)
{
    double divConst = *ADDR_FOV_DIV_CONST;
    float mulConst = *ADDR_FOV_MUL_CONST;
    float fovy;

    if (aspect <= 0.0f || divConst == 0.0) return;

    fovy = (float)((double)rawFov / divConst) * mulConst;
    if (fovy <= 0.0f || fovy >= 3.14159265f) return; /* sanity guard */

    g_projXScale = 1.0f / (tanf(fovy * 0.5f) * aspect);
    g_projYScale = g_projXScale * aspect; /* cot(fovy/2) - notes/34 head tracking needs the full P */
    g_projXScaleValid = TRUE;

    /* notes/40 Issue 1: deferred suggested-FOV log - QueryRealGeometry runs before any BPM
     * cache exists, so it stashes the HMD tangent FOV and the first cache fires the log here.
     * Suggested value is relative to the game DEFAULT fovy (current fovy divided back by the
     * active scale), so it stays correct even when a scale is already applied this session. */
    if (g_hmdFovyRad > 0.0f && !g_fovSuggestLogged &&
        InterlockedExchange(&g_fovSuggestLogged, 1) == 0) {
        float fovyBase = (g_fovScaleAsm > 0.01f) ? fovy / g_fovScaleAsm : fovy;
        LogLine("BPM: HMD vertical FOV=%.1f deg, game default fovy=%.1f deg -> "
                "suggested PSYVR_FOV_SCALE=%.2f (currently %.2f)",
                g_hmdFovyRad * 57.29578f, fovyBase * 57.29578f,
                (fovyBase > 0.01f) ? g_hmdFovyRad / fovyBase : 0.0f, g_fovScaleAsm);
    }

    /* notes/24: zn/zf for the off-axis A/B terms - sanity-guarded the same
     * way as fovy above rather than trusting arbitrary stack contents. */
    if (zn > 0.0f && zf > zn) {
        g_projZNear = zn;
        g_projZFar = zf;
    }

    {
        static DWORD s_lastLog = 0;
        DWORD now = GetTickCount();
        if (s_lastLog == 0 || (DWORD)(now - s_lastLog) >= 2000) {
            LogLine("BPM cache SET: rawFov=%.3f aspect=%.4f fovy=%.4f xScale=%.4f zn=%.3f zf=%.3f",
                    rawFov, aspect, fovy, g_projXScale, g_projZNear, g_projZFar);
            s_lastLog = now;
        }
    }
}

/* Switches the active render target to the given eye's dedicated offscreen
 * surface and overwrites the cached camera's view matrix in place by
 * directly re-invoking BuildViewMatrix's real, unmodified body (through its
 * trampoline) with a fresh eye position computed from the cached clean base
 * - never from a read-back already-offset value. */
/* notes/22: THE ACTUAL ROOT CAUSE of the dark-eye/frozen-eye symptoms (notes/20/21).
 * notes/14's own background-layer-bug investigation already recorded the decisive clue
 * and didn't fully connect it: "clearing BOTH eyes flipped which eye's background was
 * missing (eye1 went blank, eye2's fixed itself) rather than fixing both." That is the
 * exact signature of two render passes sharing ONE physical depth-stencil surface -
 * whichever eye's Clear() runs LAST in a given frame gets a properly-reset depth buffer
 * for its own draws, and the OTHER eye's draws run against the previous frame's leftover
 * depth values (mostly-near-plane content from the eye that rendered/cleared last),
 * causing widespread depth-test rejections. Since notes/20/21, only eye2 ever cleared
 * (explicitClear=TRUE) and eye1 never did at all - so every frame, eye1 rendered against
 * stale depth left over from EYE2's PREVIOUS frame (rejecting geometry that should have
 * drawn = missing/stale-looking content, reads as "frozen"), while eye2 cleared to solid
 * black immediately before drawing (so any depth-rejected or simply undrawn pixels show
 * through as black = "dark"). This was never a CandB internal-logic bug and had nothing
 * to do with draw-call counts (notes/22 confirmed those are exactly equal between eyes
 * via a live gameplay session's exact per-frame counters - see notes/22 for the full
 * derivation) - it was a resource-sharing bug in this file the whole time.
 *
 * Fix: give each eye its OWN private depth-stencil surface (created in
 * SetupStereoSurfaces, same lifecycle as the color render targets) and bind it via
 * SetDepthStencilSurface alongside SetRenderTarget. With no shared physical resource
 * left, both eyes can now be safely Cleared (color+depth+stencil) every frame with zero
 * cross-eye contamination - notes/14's "flip" symptom simply cannot happen anymore. */
static void SetEyeAndTarget(float sign, IDirect3DSurface9 *targetSurf, IDirect3DSurface9 *depthSurf)
{
    if (!g_stereoReady || !g_pDevice || !targetSurf || !depthSurf) return;

    g_pDevice->lpVtbl->SetRenderTarget(g_pDevice, 0, targetSurf);
    g_pDevice->lpVtbl->SetDepthStencilSurface(g_pDevice, depthSurf);

    g_pDevice->lpVtbl->Clear(g_pDevice, 0, NULL,
                              D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
                              D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

    /* notes/20 (real-gameplay session): this CPU-side re-invocation of
     * BuildViewMatrix through g_camPOutMatrix was previously kept as a
     * "belt and suspenders" injection alongside notes/14's real fix (the
     * SetVertexShaderConstantF correction below), on the strength of
     * notes/09's finding that BuildViewMatrix's pOutMatrix is a genuinely
     * persistent, stable object field (confirmed via a single, always-
     * identical address, 0x0019EAF0, across the whole title-screen
     * session). That assumption does NOT hold in real gameplay: this
     * session's live proxy log (188 "BVM cache SET" samples from an
     * actively-running gameplay session) shows pOutMatrix alternating
     * between at least two distinct addresses (0x0019EAF0, the vast
     * majority, and 0x0019E380, 6/188 samples) - i.e. BuildViewMatrix has
     * more than one live call site/context in gameplay, unlike the title
     * screen's single attract-mode camera. That is exactly the same class
     * of bug notes/14 Sec3 already found and fixed for
     * BuildProjectionMatrix's output pointer (a caller stack temp, unsafe
     * to cache-and-write-later) - g_camPOutMatrix is captured once per
     * frame (first BuildViewMatrix hit) and then this function calls the
     * REAL BuildViewMatrix body a second and third time (once per eye)
     * through that cached pointer, from deep inside CandB's own nested
     * call tree - by then the original caller that owned that stack slot
     * may have already returned, so this write can land on stack memory
     * that unrelated, currently-executing gameplay code has since reused,
     * i.e. a real, live-evidenced memory-corruption risk that was never
     * exercised at the title screen. Since notes/14 already established
     * this rewrite has "no confirmed effect on its own" on the actual
     * GPU-driven image (the real, load-bearing fix is the
     * SetVertexShaderConstantF register-6 correction below, which does not
     * depend on this pointer at all) - it is disabled outright rather than
     * gated, trading away a redundant, unsafe-in-gameplay CPU-side write
     * for zero loss of the actual stereo effect. */
    (void)sign;
}

/* IMPORTANT DISCOVERY (this session, see notes/13): CandB's own nested call
 * tree - not CandB/CandA's own bodies (notes/11 found zero D3D-vtable-
 * pattern calls at THAT level), but one of the 89 combined un-individually-
 * disassembled nested helpers - itself calls the real Present internally.
 * Confirmed empirically: a re-entrancy flag read back TRUE from inside
 * Hook_Present while execution was still nested inside CandB's own
 * double-call region. This means each CandB invocation is really a
 * "render this eye AND flip it to the screen" unit, not a pure render-only
 * unit as notes/11's CandB/CandA-level-only analysis assumed - the flip
 * just happens too deep in the call tree for that pass to have seen it.
 *
 * Fix: track which of the two calls is in flight with a 3-state phase
 * instead of a boolean. During eye 1's call, its internal Present is fully
 * suppressed (no composite, no real present - eye 2 hasn't been drawn yet,
 * presenting now would just flip stale/partial content). During eye 2's
 * call, ITS internal Present is repurposed as the actual "both eyes are
 * now done" signal: that's where the real composite (StretchRect eye1+eye2
 * into backbuffer halves) and the one real hardware Present happen. */
#define STEREO_PHASE_IDLE  0
#define STEREO_PHASE_EYE1  1
#define STEREO_PHASE_EYE2  2
static volatile LONG g_stereoPhase = STEREO_PHASE_IDLE;

/* notes/20 (real-gameplay session): the original design assumed CandB's
 * nested call tree calls Present internally EXACTLY ONCE per eye (true at
 * the title screen, per notes/13). Nothing enforced that assumption -
 * before this fix, EVERY internal Present hit while phase==EYE2 re-ran the
 * full StretchRect composite AND called the real hardware Present again,
 * using whatever partial content g_pEye2Surf held at that exact moment. If
 * gameplay's richer multi-pass rendering (notes/10 already found 8x
 * SetRenderTarget/3x Clear per frame even at the simple title screen scene)
 * causes CandB's nested tree to call Present more than once while rendering
 * eye 2, the FIRST such call would prematurely flip a still-INCOMPLETE eye2
 * render (missing whatever draws were still to come - lighting/detail
 * passes, etc.) to the actual screen, which reads exactly like the reported
 * "right half is very dark / doesn't look right" symptom; a following
 * legitimate composite from the SAME logical frame could then also
 * overwrite eye 1's already-correct half with a second flip before the
 * display actually samples it, contributing to the reported "left half
 * frozen" symptom (the visible frame becomes whichever premature/duplicate
 * flip last happened to win the race, not a clean single composite per
 * logical frame). Fixed by making the real composite+Present strictly
 * once-per-CandB-double-invoke: g_eye2Presented is reset at the start of
 * every cycle (BeforeEye1) and checked/set in Hook_Present's EYE2 branch;
 * any extra internal Present hits during EYE2 (beyond the first) are now
 * suppressed exactly like EYE1's, instead of re-compositing/re-presenting. */
static volatile LONG g_eye2Presented = 0;

/* notes/21: notes/20's own log evidence (SVSCF register-6 correction counts,
 * throttled-sample-based) showed a heavily skewed phase=1:phase=2 ratio
 * (167:13) and speculated this was consistent with eye 2's pass being cut
 * short by a premature internal Present - but a fresh live-log read this
 * session (after the notes/20 fix was deployed and live-tested) found the
 * SAME skew, now 109:15, essentially UNCHANGED by that fix. Since the fix
 * already closed the premature-extra-Present path, the skew's persistence
 * means it was never actually caused by that bug - it reflects a real,
 * still-unexplained difference in how much rendering work happens during
 * eye 1's CandB invocation vs eye 2's, independent of the Present-guard
 * logic. These two counters replace the old race-based "which phase wins
 * the 2-second throttle" sampling with an exact, unthrottled per-real-frame
 * count of how many times the register-6 correction actually applied in
 * each phase, logged once per real composite (see Hook_Present) - the goal
 * is to turn this from a suspected pattern into a hard, precise number the
 * next live-log read can use directly instead of re-deriving it from a
 * throttling artifact. */
static volatile LONG g_svscfCountEye1 = 0;
static volatile LONG g_svscfCountEye2 = 0;

void TraceFlushDrawsFwd(void); /* notes/35: defined with the trace hooks below */
void UIShift_ReconcileFwd(void); /* notes/36: defined with the UI-depth machinery below */
void UIVp_PhaseChangedFwd(void); /* notes/42: defined with the UI-viewport machinery below */

/* notes/67: Candidate 1 - yaw the GAME camera so the engine culls toward where
 * we are looking, instead of leaving an unrendered region behind the player.
 * Defined further down (it needs AutoGetCamera); declared here because this
 * hook site sits earlier in the file. Applied from BeforeEye1, i.e. BEFORE the
 * engine renders/culls the frame - writing after the fact (as the automation
 * tick does) is too late to change what gets drawn. */
static void ApplyCameraYaw(void);
/* notes/68: the same write, applied at a SELECTABLE hook site so all three can
 * be tested in one launch (see the definitions for why the site is the whole
 * question). Declared here because BeforeEye2 sits earlier in the file than the
 * definitions, which need AutoGetCamera. */
static void ApplyCameraYawAt(int site);
static void CamProbe(const char *where);
/* notes/67: defined with the DirectInput hooks further down; called from the
 * env-var config function, which sits earlier in this file. */
static void InstallMouseInject(void);

void __cdecl CandB_BeforeEye1_asm(void) asm("CandB_BeforeEye1_asm");
void __cdecl CandB_BeforeEye1_asm(void)
{
    ApplyCameraYaw();
    /* notes/35: one-full-cycle frame trace - armed here, disarmed at the NEXT BeforeEye1 so the
     * capture includes everything after the composite too (post-present UI drawing etc.). */
    if (g_traceFrames) {
        static DWORD s_lastTrace = 0;
        DWORD tnow = GetTickCount();
        if (g_traceActive) {
            TraceFlushDrawsFwd();
            LogLine("TRACE: ==== frame trace END (next BeforeEye1 reached) ====");
            g_traceActive = 0;
        } else if (s_lastTrace == 0 || (DWORD)(tnow - s_lastTrace) >= 5000) {
            LogLine("TRACE: ==== frame trace START (BeforeEye1) ====");
            g_traceActive = 1;
            s_lastTrace = tnow;
        }
    }

    g_stereoPhase = STEREO_PHASE_EYE1;
    g_eye2Presented = 0;
    SetEyeAndTarget(-1.0f, g_pEye1Surf, g_pEye1DepthStencil);
    UIShift_ReconcileFwd(); /* notes/36: phase changed - re-aim the UI depth shift */
    UIVp_PhaseChangedFwd(); /* notes/42: RT bind reset the viewport - re-shrink if UI still bound */
}

void __cdecl CandB_BeforeEye2_asm(void) asm("CandB_BeforeEye2_asm");
void __cdecl CandB_BeforeEye2_asm(void)
{
    if (g_traceActive) { TraceFlushDrawsFwd(); LogLine("TRACE: -- BeforeEye2 --"); }
    g_stereoPhase = STEREO_PHASE_EYE2;
    SetEyeAndTarget(+1.0f, g_pEye2Surf, g_pEye2DepthStencil);
    UIShift_ReconcileFwd(); /* notes/36 */
    UIVp_PhaseChangedFwd(); /* notes/42 */

    /* notes/68: the first real CandB has now RETURNED, so the engine's camera
     * update for this frame is done. If notes/59 is right that the draw chain is
     * a sibling of CandB rather than nested inside it, a write here lands after
     * the update and before the draws - which is exactly what the BeforeEye1
     * attempt could not do. Probe first, then write, so the log shows both the
     * engine's value and ours. */
    CamProbe("BeforeEye2");
    ApplyCameraYawAt(2);
    CamProbe("post-write-2");
}
