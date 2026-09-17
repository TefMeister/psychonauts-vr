/* Register that carries the per-draw clip-space composite matrix identified
 * this session (notes/14) - StartRegister=6, always Vector4fCount=4,
 * consistently uploaded from one call site (exe+0x51D33E). Verified NOT to
 * be a simple World*View*Proj derivable from the tracked BuildViewMatrix
 * output (16 sampled uploads across 2 different live camera states all
 * failed a matrix-decomposition sanity check - see notes/14 for the full
 * derivation), so this correction is applied as an experiment: a uniform
 * additive shift to WVP's row3 (translation row) X component. This is the
 * closed-form result of inserting a view-space translation T(-d,0,0,0)
 * between View and Proj in a row-vector v*World*View*Proj pipeline - IF this
 * matrix truly ends in Proj as demonstrated by xScale below, that shift is
 * exactly the right correction for a rigid (non-toe-in) eye offset of d
 * along the camera's local right axis, regardless of what World/View
 * individually were, PROVIDED World*View itself is affine (true for any
 * ordinary rigid/scale model+view transform, i.e. its own column-3 is
 * [0,0,0,1] - see notes/18 for the full re-derivation that checks this).
 *
 * notes/18 CORRECTION (bug found and fixed this session): notes/17's live
 * stack trace proved this register's upload is not the raw row-major WVP
 * described above - it is Transpose(WVP), with the transpose happening
 * in-place in the game's own compositing code BEFORE SetVertexShaderConstantF
 * is ever called (so this hook only ever sees the already-transposed
 * matrix). The derivation above targets WVP's row3/col0 element
 * (flat index 4*3+0 = 12 in an UN-transposed row-major buffer), but since
 * upload[r][c] = WVP[c][r], that element lives at upload[0][3] = flat index
 * 4*0+3 = 3 in the buffer this hook actually receives - NOT index 12.
 * Index 12 in the transposed buffer is upload[3][0] = WVP[0][3], an
 * unrelated element that (in a standard row-vector-convention perspective
 * pipeline) contributes to the homogeneous W output scaled by the vertex's
 * OWN object-space X coordinate, not a uniform per-vertex clip-space shift -
 * i.e. the prior code was perturbing the perspective-divide term as a
 * function of each vertex's local X position, not translating the camera.
 * This plausibly explains notes/14's "texture/pattern changes with
 * magnitude" character (a real, reproducible, magnitude-scaling effect, but
 * the wrong effect) rather than clean lateral parallax. Fixed by patching
 * flat index 3, not 12. Re-verified per notes/18's controlled 0/3.25
 * screenshot comparison.
 *
 * notes/24 UPGRADE (off-axis/asymmetric frustum, replacing the symmetric-
 * frustum-reused-for-both-eyes approach above): the single-entry patch
 * above is exact ONLY for a pure rigid translation (no re-aim) - it is kept
 * conceptually (the math below reduces to exactly this when the new shear
 * term is zero) but is now computed as part of a small 4-entry patch
 * instead of one line, because a real off-axis frustum needs a SHEAR term
 * too (zero disparity at a chosen finite convergence distance, instead of
 * only at infinity), and a shear cannot be expressed as a single safe
 * matrix entry the way the translation can (see the long comment in the
 * Milestone 7 header at the top of this file for exactly why, and
 * notes/24 Sec2 for the full derivation and the bug this session's own
 * verification script caught before it ever reached the game).
 *
 * Derivation summary: model the per-eye adjustment as a single combined
 * matrix X (translate by -d, then shear x by k*z) inserted between the
 * (unknown, untraced) World*View matrix M and the known Proj:
 *   WVP_new = M * X * Proj = (M*Proj) * (Proj^-1 * X * Proj) = WVP_recv * Y
 * Y depends only on Proj (known: xScale, zn, zf) and X (known: d, k) - NOT
 * on M - and works out to differ from the identity only in column 0, so:
 *   WVP_new[r][0] = WVP_recv[r][0] + WVP_recv[r][2]*Y20 + WVP_recv[r][3]*Y30
 *   WVP_new[r][c] = WVP_recv[r][c]                        for c != 0
 * with (A = zf/(zn-zf), B = zn*zf/(zn-zf), both from the known projection):
 *   Y20 = (-d) * xScale / B
 *   Y30 = (-k - A*d/B) * xScale
 * k itself is chosen so a point on the ORIGINAL (un-offset) camera axis at
 * the convergence distance `focus` gets exactly zero disparity: k = -d/focus
 * (derived by requiring x_clip == 0 at eye-space X=0, Z=-focus - RH eye
 * space has negative Z in front of the camera). With k=0 (focus->infinity)
 * this reduces EXACTLY to the old translation-only patch - verified both
 * algebraically and numerically (debug_Y.py in the session's scratchpad).
 *
 * In the TRANSPOSED buffer this hook actually receives (upload[c][r] =
 * WVP[r][c], same convention as notes/18), WVP's column 0 (r=0..3) maps to
 * upload's ENTIRE ROW 0 - flat indices 0,1,2,3 - and WVP_recv[r][2]/[r][3]
 * map to upload[2][r]/upload[3][r] - flat indices 8+r/12+r. */
#define STEREO_WVP_REGISTER 6

/* notes/36 UI depth: the wanted per-eye shift for the CURRENT phase, in clip units - a point at
 * PSYVR_UI_DEPTH world units projects with exactly this offset (shift = -d*xScale/D, the same d
 * the stereo correction uses). Zero when disabled, no UI shader bound, or outside eye phases. */
static float UIShift_Wanted(void)
{
    float d;
    float depth;
    int eyeIdx;
    /* notes/67: g_uiDepthOverride lets one class of UI draw sit at a different
     * virtual depth from the rest (dialogue nearer than the persistent HUD).
     * Set per-draw by the Draw* hooks and cleared straight after. */
    depth = (g_uiDepthOverride > 0.0f) ? g_uiDepthOverride : g_uiDepthWorld;
    if (!g_curShaderIsUI || depth <= 0.0f || !g_projXScaleValid) return 0.0f;
    if (g_stereoPhase == STEREO_PHASE_EYE1) eyeIdx = 0;
    else if (g_stereoPhase == STEREO_PHASE_EYE2) eyeIdx = 1;
    else return 0.0f;
    d = g_vrGeomValid ? g_realHalfIPD[eyeIdx] : STEREO_HALF_IPD * (eyeIdx == 0 ? -1.0f : 1.0f);
    /* notes/42: the shift is applied in the UI shader's clip space, which the UI viewport
     * shrink (UIVp_Reconcile) then maps onto only a g_uiVpScale fraction of the frame - divide
     * by the shrink so the rendered parallax (and therefore the perceived UI depth) stays the
     * same as at FOV scale 1. g_uiVpScale is 1 when no shrink is active. */
    return (-d) * (g_projXScale / g_uiVpScale) / depth;
}

static float g_uiShiftApplied = 0.0f; /* delta currently baked into the device's c50.x */
static float g_lastC50[2] = {0.0f, 0.0f}; /* notes/43: the game's own last c50.xy upload (pre-shift) -
                                             per-draw UI element screen offset, diagnostic + classifier input */

static HRESULT STDMETHODCALLTYPE Hook_SetVertexShaderConstantF(
    IDirect3DDevice9 *This,
    UINT StartRegister,
    CONST float *pConstantData,
    UINT Vector4fCount)
{
    /* notes/36 recon: per-register upload histogram during eye phases (see g_regHisto). */
    if (g_regHisto && g_stereoPhase != STEREO_PHASE_IDLE && StartRegister < 256) {
        InterlockedIncrement(&g_regHistoCount[StartRegister]);
        if (Vector4fCount > g_regHistoVecMax[StartRegister] && Vector4fCount < 256)
            g_regHistoVecMax[StartRegister] = (BYTE)Vector4fCount;
        switch (StartRegister) {
        case 6:  InterlockedOr(&g_regComboMask, REGBIT_R6); break;
        case 10: InterlockedOr(&g_regComboMask, REGBIT_R10); break;
        case 13: InterlockedOr(&g_regComboMask, REGBIT_R13); break;
        case 16: InterlockedOr(&g_regComboMask, REGBIT_R16); break;
        case 64: InterlockedOr(&g_regComboMask, REGBIT_R64); break;
        case 96: InterlockedOr(&g_regComboMask, REGBIT_R96); break;
        default: break;
        }
    }

    /* notes/44 recon: bone-palette uploads (c64.., per-bone-count shader permutations - see
     * notes/36). Log the first two bone matrices of the largest upload once per second: enough
     * to characterize rows-per-bone and watch translations animate, proving live skeleton data
     * flows through this hook (foundation for render-level hand IK / body rig). */
    if (g_boneProbe && StartRegister >= 64 && StartRegister < 192 && Vector4fCount >= 6 &&
        pConstantData != NULL) {
        static DWORD s_lastBoneLog = 0;
        DWORD now = GetTickCount();
        if (s_lastBoneLog == 0 || (DWORD)(now - s_lastBoneLog) >= 1000) {
            const float *p = pConstantData;
            s_lastBoneLog = now;
            LogLine("BONEPROBE: c%u n=%u phase=%ld", StartRegister, Vector4fCount, g_stereoPhase);
            LogLine("BONEPROBE:  r0=[% .3f % .3f % .3f % .3f] r1=[% .3f % .3f % .3f % .3f] r2=[% .3f % .3f % .3f % .3f]",
                    p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11]);
            LogLine("BONEPROBE:  r3=[% .3f % .3f % .3f % .3f] r4=[% .3f % .3f % .3f % .3f] r5=[% .3f % .3f % .3f % .3f]",
                    p[12], p[13], p[14], p[15], p[16], p[17], p[18], p[19], p[20], p[21], p[22], p[23]);
        }
    }

    /* notes/52: PSYVR_SHADER_DUMP=1 - playbook Phase 3.3 verification. On the first c96 (32-bone
     * skinned) draw, identify which PSYVR_REG_HISTO-dumped psyvr_vs_NN.bin is the currently-bound
     * vertex shader, so it can be disassembled OFFLINE (D3DXDisassembleShader, no live game needed)
     * to read the REAL row/column-vector convention register 6 is consumed with - instead of the
     * assumed convention the whole render-level camera model has rested on since notes/24. */
    if (g_shaderDump && !g_shaderDumpLogged && StartRegister == 96 && Vector4fCount >= 96) {
        int i, found = -1;
        for (i = 0; i < g_vsDumpMapCount; i++) {
            if (g_vsDumpPtr[i] == g_currentVSPtr) { found = g_vsDumpMapIdx[i]; break; }
        }
        if (found >= 0) {
            g_shaderDumpLogged = TRUE;
            LogLine("SHADERDUMP: skinned (c96) draw's bound vertex shader = dump idx %d -> psyvr_vs_%02d.bin (ptr=%p)",
                    found, found, (void *)g_currentVSPtr);
        }
    }

    /* notes/51: PSYVR_BONE_DUMP=1 - the shoulder-anchor bone hunt. Once/sec, dump a burst of the
     * next ~16 c96 (32-bone) skinned draws: each draw's recovered entity World origin + eye
     * distance, so we can SEE whether all of Raz's draws share ONE stable origin (=> the fix is
     * "lock to a single origin + a bigger vertical offset for the shoulders", no per-bone
     * extraction) or scatter (=> stale-c6 mispairing). For the burst's first draw, also dump all
     * 32 bone model-space translations (row.w columns) - groundwork for hiding Raz's head mesh and
     * later hand IK. Uses g_lastC6 (the c6 uploaded just before this bone palette) + g_pinvVinv,
     * both valid on the monitor path (no poses / SteamVR needed). */
    if (g_boneDump && StartRegister == 96 && Vector4fCount >= 96 && pConstantData != NULL &&
        g_pinvVinvValid && g_lastC6Valid) {
        static DWORD s_bdLast = 0; static int s_bdBurst = 0; static int s_bdFirst = 0;
        DWORD now = GetTickCount();
        if (s_bdBurst == 0 && (s_bdLast == 0 || (DWORD)(now - s_bdLast) >= 1000)) {
            s_bdBurst = 16; s_bdFirst = 1; s_bdLast = now;
            LogLine("BONEDUMP: === burst phase=%ld eye=(%.1f,%.1f,%.1f) at=(%.1f,%.1f,%.1f) focus=%.1f ===",
                    g_stereoPhase, g_baseEye.x, g_baseEye.y, g_baseEye.z,
                    g_baseAt.x, g_baseAt.y, g_baseAt.z, g_focusDistance);
        }
        if (s_bdBurst > 0) {
            float wr3[4], o[4]; int c, k;
            s_bdBurst--;
            wr3[0] = g_lastC6[3]; wr3[1] = g_lastC6[7]; wr3[2] = g_lastC6[11]; wr3[3] = g_lastC6[15];
            for (c = 0; c < 4; c++) { float s = 0.0f; for (k = 0; k < 4; k++) s += wr3[k] * g_pinvVinv[k * 4 + c]; o[c] = s; }
            if (o[3] > 1e-6f || o[3] < -1e-6f) {
                float ox = o[0] / o[3], oy = o[1] / o[3], oz = o[2] / o[3];
                float ex = ox - g_baseEye.x, ey = oy - g_baseEye.y, ez = oz - g_baseEye.z;
                LogLine("BONEDUMP:  origin=(%.1f,%.1f,%.1f) eyeDist=%.1f n=%u",
                        ox, oy, oz, sqrtf(ex * ex + ey * ey + ez * ez), Vector4fCount);
            }
            if (s_bdFirst) {
                int b;
                s_bdFirst = 0;
                for (b = 0; b < 32; b++) {
                    const float *bp = pConstantData + b * 12;   /* bone b = 3 float4 rows */
                    LogLine("BONEDUMP:   b%02d t=(%7.2f %7.2f %7.2f)", b, bp[3], bp[7], bp[11]);
                }
            }
        }
    }

    /* notes/51: on a c96 (32-bone) skinned draw during an eye pass, recover this entity's world
     * origin = translation of World = WVP * (P^-1 * V^-1). WVP's row 3 (the translation row) comes
     * from the transposed upload as (u[3],u[7],u[11],u[15]); origin = (WVP_row3 * pinvVinv) / w.
     * The bone-dump session (notes/51) proved Raz is the c96 entity whose origin is NEAREST the
     * camera eye (rigid chase-cam offset, ~11.6wu in this build), rock-stable, while other 32-bone
     * NPCs sit 100s of wu away. So track the SINGLE nearest-to-eye origin this frame - no centroid
     * (which mixed Raz with NPCs -> the jitter), no near-eye rejection (which rejected Raz). */
    if (g_firstPerson && g_fpActive && g_pinvVinvValid && g_lastC6Valid &&
        g_stereoPhase != STEREO_PHASE_IDLE &&
        StartRegister == 96 && Vector4fCount >= 96) {
        float wr3[4], o[4];
        int c, k;
        wr3[0] = g_lastC6[3]; wr3[1] = g_lastC6[7]; wr3[2] = g_lastC6[11]; wr3[3] = g_lastC6[15];
        for (c = 0; c < 4; c++) {
            float s = 0.0f;
            for (k = 0; k < 4; k++) s += wr3[k] * g_pinvVinv[k * 4 + c];
            o[c] = s;
        }
        if (o[3] > 1e-6f || o[3] < -1e-6f) {
            float ox = o[0] / o[3], oy = o[1] / o[3], oz = o[2] / o[3];
            float ex = ox - g_baseEye.x, ey = oy - g_baseEye.y, ez = oz - g_baseEye.z;
            float dEye2 = ex * ex + ey * ey + ez * ez;
            /* Cap so that when only distant NPCs are on-screen (Raz occluded/off-frame) we report
             * "no candidate" and hold the last lock instead of snapping to an NPC. Raz (~11.6wu)
             * sits far under this; NPCs (100s of wu) are excluded. Floored so tiny-focus frames
             * (very close cameras) still admit Raz. */
            float capDist = 0.75f * g_focusDistance;
            if (capDist < 60.0f) capDist = 60.0f;
            if (dEye2 < capDist * capDist && (!g_razNearValid || dEye2 < g_razNearDist2)) {
                g_razNearOrigin.x = ox; g_razNearOrigin.y = oy; g_razNearOrigin.z = oz;
                g_razNearDist2 = dEye2;
                g_razNearValid = TRUE;
                memcpy(g_razNearC6, g_lastC6, sizeof(g_razNearC6)); /* notes/51: keep Raz's WVP^T for full-World recovery */
            }
        }
    }

    /* notes/51: monitor-verifiable anchor probe. The real FP PROBE runs inside the pose-gated
     * head-track path (needs SteamVR), so log the chosen nearest-to-eye Raz origin here on the
     * monitor path (once/sec) to confirm the anchor is stable standing + tracks Raz walking,
     * before any headset pass. Throttled; converges to the frame's true nearest. */
    if ((g_fpProbe || g_boneDump) && StartRegister == 96 && Vector4fCount >= 96 && g_razNearValid) {
        static DWORD s_apLast = 0;
        DWORD now = GetTickCount();
        if (s_apLast == 0 || (DWORD)(now - s_apLast) >= 1000) {
            s_apLast = now;
            LogLine("ANCHOR: raz=(%.1f,%.1f,%.1f) eyeDist=%.1f eye=(%.1f,%.1f,%.1f)",
                    g_razNearOrigin.x, g_razNearOrigin.y, g_razNearOrigin.z,
                    sqrtf(g_razNearDist2), g_baseEye.x, g_baseEye.y, g_baseEye.z);
        }
    }

    /* notes/36 UI depth: a game upload of c50 replaces any shift we baked in - re-apply it on
     * the way through when a UI shader is bound during an eye phase (see UIShift_Wanted). */
    if (StartRegister == 50 && Vector4fCount >= 1 && pConstantData != NULL) {
        float want = UIShift_Wanted();
        g_lastC50[0] = pConstantData[0];
        g_lastC50[1] = pConstantData[1];
        g_uiShiftApplied = want;
        if (want != 0.0f) {
            float first[4];
            memcpy(first, pConstantData, sizeof(first));
            first[0] += want;
            if (Vector4fCount == 1)
                return g_pRealSetVSConstF(This, 50, first, 1);
            g_pRealSetVSConstF(This, 50, first, 1);
            return g_pRealSetVSConstF(This, 51, pConstantData + 4, Vector4fCount - 1);
        }
    }

    /* notes/49: cache the RAW register-6 upload (WVP transposed, pre-patch) for first-person world
     * recovery - the very next skinned draw's bone upload pairs with it. */
    if (g_firstPerson && StartRegister == STEREO_WVP_REGISTER && Vector4fCount == 4 &&
        pConstantData != NULL) {
        memcpy(g_lastC6, pConstantData, 16 * sizeof(float));
        g_lastC6Valid = TRUE;
    }

    if (g_stereoPhase != STEREO_PHASE_IDLE &&
        StartRegister == STEREO_WVP_REGISTER && Vector4fCount == 4 &&
        pConstantData != NULL && g_projXScaleValid) {

        float patched[16];
        float xScale = g_projXScale;
        float zn = g_projZNear;
        float zf = g_projZFar;
        float sign = (g_stereoPhase == STEREO_PHASE_EYE1) ? -1.0f : 1.0f;
        int eyeIdx = (g_stereoPhase == STEREO_PHASE_EYE1) ? 0 : 1;
        float focus = g_focusDistance;
        float d, k;
        BOOL usingRealIPD = g_vrGeomValid;
        BOOL usingRealShear;
        float A = zf / (zn - zf);
        float B = zn * zf / (zn - zf);
        float Y20, Y30;
        int r;

        /* notes/32 (Task 1): use real OpenVR-sourced per-eye geometry when available
         * (g_vrGeomValid, set by VRBridge_QueryRealGeometry once IVRSystem is ready), falling back
         * to the pre-existing hardcoded STEREO_HALF_IPD/focus-distance estimate otherwise - so this
         * path behaves identically to before this session whenever OpenVR isn't initialized (the
         * VR-submit flag is off, or SteamVR isn't installed at all). */
        if (usingRealIPD) {
            d = g_realHalfIPD[eyeIdx];
        } else {
            d = STEREO_HALF_IPD * sign;
        }
        /* notes/24: k = -d/focus - the eye-space shear that puts zero disparity exactly at the
         * convergence distance, see the derivation comment above STEREO_WVP_REGISTER. notes/32:
         * when OpenVR reports a genuinely asymmetric frustum (g_realShearValid[eyeIdx]), use its
         * real k = (l+r)/2 directly instead - this driver reports an exactly symmetric frustum
         * (confirmed, notes/32 Sec1), so this branch is exercised only once real headset data with
         * real per-eye asymmetry is available; until then this reduces to the line below. */
        usingRealShear = usingRealIPD && g_realShearValid[eyeIdx];
        if (usingRealShear) {
            k = g_realShearK[eyeIdx];
        } else {
            k = (-d) / focus;
        }
        Y20 = (-d) * xScale / B;
        Y30 = (-k - A * d / B) * xScale;

        /* notes/34 head tracking: premultiply the transposed upload by Y_track^T, i.e.
         * WVP -> WVP*Y_track, BEFORE the per-eye patch below - the combined order
         * WVP*Y_track*Y_eye rotates the head first, then offsets the eye inside the rotated
         * head frame. Skipped entirely (src stays pConstantData) when no valid pose has
         * arrived, preserving the exact pre-notes/34 behavior. */
        const float *src = pConstantData;
        float tracked[16];
        if (g_trackYValid) {
            Mat4MulRow(tracked, g_trackYt, pConstantData);
            src = tracked;

            /* notes/52: HTDEBUG - recover the world-space origin from BOTH the pre-correction
             * upload (pConstantData) and the post-correction one (tracked), using the SAME
             * empirically-validated row-vector extraction the notes/49-51 entity-origin recovery
             * uses (wr3 . g_pinvVinv). If the head-tracking correction is composing correctly, the
             * two recovered origins should differ by ~PSYVR_HT_TEST_SHIFT world units; if the
             * composition order/transpose is wrong, expect near-zero or a wildly different delta. */
            if (g_htDebug && g_pinvVinvValid) {
                static DWORD s_htLast = 0;
                DWORD now = GetTickCount();
                if (s_htLast == 0 || (DWORD)(now - s_htLast) >= 1000) {
                    float wrOld[4], wrNew[4], oOld[4], oNew[4];
                    int c, k;
                    s_htLast = now;
                    wrOld[0]=pConstantData[3]; wrOld[1]=pConstantData[7]; wrOld[2]=pConstantData[11]; wrOld[3]=pConstantData[15];
                    wrNew[0]=tracked[3];       wrNew[1]=tracked[7];       wrNew[2]=tracked[11];       wrNew[3]=tracked[15];
                    for (c = 0; c < 4; c++) {
                        float sOld = 0.0f, sNew = 0.0f;
                        for (k = 0; k < 4; k++) { sOld += wrOld[k]*g_pinvVinv[k*4+c]; sNew += wrNew[k]*g_pinvVinv[k*4+c]; }
                        oOld[c] = sOld; oNew[c] = sNew;
                    }
                    if ((oOld[3]>1e-6f||oOld[3]<-1e-6f) && (oNew[3]>1e-6f||oNew[3]<-1e-6f)) {
                        float ox0=oOld[0]/oOld[3], oy0=oOld[1]/oOld[3], oz0=oOld[2]/oOld[3];
                        float ox1=oNew[0]/oNew[3], oy1=oNew[1]/oNew[3], oz1=oNew[2]/oNew[3];
                        float dx=ox1-ox0, dy=oy1-oy0, dz=oz1-oz0;
                        LogLine("HTDEBUG: shift=%.1f old=(%.1f,%.1f,%.1f) new=(%.1f,%.1f,%.1f) delta=(%.1f,%.1f,%.1f) |delta|=%.1f",
                                g_htTestShift, ox0,oy0,oz0, ox1,oy1,oz1, dx,dy,dz, sqrtf(dx*dx+dy*dy+dz*dz));
                    }
                }
            }
        }

        memcpy(patched, src, sizeof(patched));
        /* notes/24: full off-axis column-0 patch - reduces exactly to
         * notes/18's single-entry translation patch when Y20==0 (k==0).
         * See the derivation comment above STEREO_WVP_REGISTER for the
         * flat-index mapping (WVP column 0 -> upload row 0, flat 0..3;
         * WVP[r][2]/[r][3] -> upload flat 8+r/12+r). */
        for (r = 0; r < 4; r++) {
            patched[r] = src[r] + src[8 + r] * Y20 + src[12 + r] * Y30;
        }

        /* notes/21: exact per-eye draw-call counters, see comment on
         * g_svscfCountEye1/2 above. */
        if (g_stereoPhase == STEREO_PHASE_EYE1) {
            InterlockedIncrement(&g_svscfCountEye1);
        } else {
            InterlockedIncrement(&g_svscfCountEye2);
        }

        {
            static DWORD s_lastLog = 0;
            DWORD now = GetTickCount();
            if (s_lastLog == 0 || (DWORD)(now - s_lastLog) >= 2000) {
                LogLine("SVSCF stereo-correct: reg=%u phase=%ld xScale=%.4f d=%.3f focus=%.2f k=%.6f Y20=%.6f Y30=%.4f dSrc=%s kSrc=%s",
                        StartRegister, g_stereoPhase, xScale, d, focus, k, Y20, Y30,
                        usingRealIPD ? "openvr" : "hardcoded", usingRealShear ? "openvr" : "focus-est");
                s_lastLog = now;
            }
        }

        return g_pRealSetVSConstF(This, StartRegister, patched, Vector4fCount);
    }

    return g_pRealSetVSConstF(This, StartRegister, pConstantData, Vector4fCount);
}

/* Patch IDirect3DDevice9::SetVertexShaderConstantF (vtbl slot 94). Same
 * VirtualProtect-then-restore pattern as InstallPresentHook; deliberately a
 * SEPARATE VirtualProtect call (not folded into InstallPresentHook) so the
 * two hooks stay independently diagnosable/removable. */
static void InstallSetVSConstFHook(IDirect3DDevice9 *pDevice)
{
    IDirect3DDevice9Vtbl *vtbl;
    DWORD oldProtect;

    if (g_pRealSetVSConstF != NULL || pDevice == NULL) return;

    vtbl = (IDirect3DDevice9Vtbl *)pDevice->lpVtbl;

    if (!VirtualProtect(vtbl, sizeof(*vtbl), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        LogLine("ERROR: VirtualProtect failed patching SetVertexShaderConstantF, err=%lu", GetLastError());
        return;
    }

    g_pRealSetVSConstF = vtbl->SetVertexShaderConstantF;
    vtbl->SetVertexShaderConstantF = Hook_SetVertexShaderConstantF;

    VirtualProtect(vtbl, sizeof(*vtbl), oldProtect, &oldProtect);

    LogLine("Hooked IDirect3DDevice9::SetVertexShaderConstantF (vtable slot 94), original=0x%p", (void *)g_pRealSetVSConstF);
}

/* ---- Present hook (IDirect3DDevice9 vtable slot 17) -------------------- */
/*
 * Fires once per frame. Logging every single call would spam the log file
 * and add per-frame disk I/O under the same critical section other hooks
 * use, so this throttles to roughly once per second (wall-clock, via
 * GetTickCount - robust to any framerate) while still incrementing a total
 * frame counter every call, so the log shows real elapsed-frame deltas.
 *
 * Also does the stereo composite: StretchRect both offscreen eye surfaces
 * into the left/right halves of the real backbuffer before calling through
 * to the real Present, and resets the per-frame camera cache flag so the
 * next frame's first BuildViewMatrix hit re-caches a fresh clean base.
 */

