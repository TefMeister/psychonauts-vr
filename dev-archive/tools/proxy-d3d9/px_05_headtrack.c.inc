
/* ======================================================================
 * Stereo prototype: inline hooks into Psychonauts.exe's own code
 * ====================================================================== */

typedef struct { float x, y, z; } Vec3;

/* Fixed addresses, confirmed stable (no ASLR on the main module) across
 * every prior session - see notes/07, notes/10, notes/11, notes/12, and
 * this session's own raw-file byte dump (notes/13) which re-confirmed the
 * exact prologue bytes used below to size each inline patch. */
#define ADDR_BUILDVIEWMATRIX ((BYTE *)0x00692480u)
#define ADDR_BUILDPROJMATRIX ((BYTE *)0x006924D0u)
#define ADDR_CANDB           ((BYTE *)0x004FEDA0u)

/* Both prologues are a clean, fully-relocatable 6-byte instruction run
 * (push ebp; mov ebp,esp; <one more 3-byte instruction>) - confirmed via a
 * raw file byte dump this session (notes/13):
 *   BuildViewMatrix: 55 8B EC 83 E4 F0  (push ebp; mov ebp,esp; and esp,0xFFFFFFF0)
 *   CandB:           55 8B EC 83 EC 20  (push ebp; mov ebp,esp; sub esp,0x20)
 * 6 bytes is enough room for a 5-byte E9 rel32 jump + 1 NOP pad, and neither
 * instruction is position-dependent (no relative jmp/call inside), so both
 * can be safely copied byte-for-byte into a trampoline. */
#define INLINE_PATCH_LEN 6

/* Half interpupillary-distance offset applied along the camera's right
 * vector for each eye (full separation = 2x this value). Reasoning: the
 * title screen's live camera eye->at distance was measured at ~190-200
 * world units (notes/08, notes/09). Scaling a real human IPD (~6.3cm)
 * proportionally against a plausible ~2m real-world viewing distance for a
 * similarly-framed shot gives a ratio of ~0.0315; applied to ~195 world
 * units that is ~6.1 units full separation, i.e. ~3.05 half-offset - very
 * close to the task's own suggested "~6-7 world units" ballpark. 3.25 (6.5
 * full) was chosen as a round number in that range.
 *
 * FALLBACK ONLY as of notes/32 (Task 1): this fixed constant is now used
 * only when real OpenVR data isn't available (g_vrGeomValid == FALSE - i.e.
 * PSYVR_ENABLE_SUBMIT isn't set, or SteamVR/OpenVR init failed). When it IS
 * available, Hook_SetVertexShaderConstantF uses g_realHalfIPD[] instead -
 * the REAL per-eye IVRSystem::GetEyeToHeadTransform offset, converted to
 * world units via WORLD_UNITS_PER_METER. Confirmed against the live
 * null-driver runtime (notes/32 Sec1): real IPD = 63.0mm exactly (the
 * standard OpenVR default), real half-IPD = 3.15cm = 3.15 world units - only
 * ~3% below this hardcoded 3.25 guess, a genuine (if modest) cross-
 * validation of the original notes/15 estimate. */
#define STEREO_HALF_IPD 3.25f

/* notes/24: convergence/focal distance for the off-axis projection below -
 * the one depth at which both eyes' projected image of the same point has
 * ZERO disparity (see the derivation above Hook_SetVertexShaderConstantF).
 * Deliberately NOT a fixed constant: g_focusDistance is recomputed every
 * real frame from the camera's own live eye->at distance (BVM_OnEntry_asm
 * already caches eye/at for the disabled CPU-side rewrite; this session
 * reuses that same cached data) - a cheap, well-motivated choice for a
 * third-person camera that already follows a target, so the convergence
 * point naturally tracks wherever the game itself is already aiming each
 * frame instead of needing a scene-specific guessed constant.
 *
 * FALLBACK ONLY as of notes/32 (Task 1): notes/24's original TODO here
 * asked for IVRSystem::GetProjectionMatrix (a full asymmetric 4x4, which
 * would need its own struct-return ABI handling and would still need
 * decomposing back into this file's k/Y20/Y30 terms). This session used
 * IVRSystem::GetProjectionRaw instead (real per-eye tangent bounds, no
 * struct-return issue, no decomposition needed - see g_realShearK/
 * VRBridge_QueryRealGeometry) precisely because the task called it out as
 * the more direct API for this. When GetProjectionRaw reports a genuinely
 * asymmetric frustum (g_realShearValid[eyeIdx]), its real k = (l+r)/2
 * REPLACES this focus-distance estimate entirely for the shear term. This
 * null-driver runtime reports an exactly symmetric frustum for both eyes
 * (confirmed, not assumed - notes/32 Sec1), so g_focusDistance's estimate
 * remains in active use in every configuration actually tested this
 * session; it will be automatically superseded the moment a real headset
 * (or any driver reporting real per-eye asymmetry) is connected, with no
 * further code changes needed. */
#define STEREO_FOCUS_DISTANCE_MIN 25.0f /* world units - clamp floor so a
                                            close `at` point never produces
                                            a near-zero/negative convergence
                                            distance and an extreme shear */
#define STEREO_FOCUS_DISTANCE_DEFAULT 200.0f /* fallback before the first
                                                 real BVM cache of the
                                                 session - matches the title
                                                 screen's own observed
                                                 ~190-200 unit eye->at
                                                 framing (notes/08/09) */

static BOOL g_frameCamCached = FALSE;
static Vec3 g_baseEye, g_baseAt, g_baseUp, g_rightVec;
static void *g_camPOutMatrix = NULL;

/* notes/47: first-person camera-position smoothing. The chase camera bobs/springs as Raz walks,
 * and our forward offset is a multiple of the springy eye->at distance, so both ride straight into
 * the VR view as nauseating bounce. HEAD ROTATION stays crisp (it comes from the HMD, not this) -
 * only the game-camera-driven POSITION is low-passed, which is locomotion, not head motion, so
 * smoothing it is comfortable, not laggy-feeling. EMA of the eye position + focus distance, with a
 * hard snap on big jumps (teleports/level loads/camera cuts) so it doesn't fly across the level. */
static Vec3 g_fpSmoothEye = {0.0f, 0.0f, 0.0f};
static float g_fpSmoothFocus = 0.0f;
static BOOL g_fpSmoothInit = FALSE;
/* (g_fpSmooth scalar is declared much earlier with the other FP knobs - the env parser needs it) */
static Vec3 Vec3Sub(Vec3 a, Vec3 b);       /* fwd decls - defined below VRBridge_UpdateHeadTracking */
static Vec3 Vec3Cross(Vec3 a, Vec3 b);
static Vec3 Vec3Normalize(Vec3 v);

/* notes/49: render-level bone/entity extraction for head-locked first person. Raz's skeleton
 * already flows through our SetVertexShaderConstantF hook: every 3D draw uploads Transpose(W*V*P)
 * to register 6, and skinned (character) draws also upload a bone palette (r64..r191, notes/36).
 * We recover the per-draw WORLD matrix (World = WVP * P^-1 * V^-1) and take its translation as the
 * entity's world origin. The skinned draw whose origin is nearest the chase camera's look-at point
 * is Raz (the camera always frames him). That origin moves smoothly with Raz (no chase-camera
 * spring), so locking the FP eye to it removes the position bounce; head rotation still comes from
 * the HMD. All inputs are already cached: WVP (the r6 upload), V (from the BVM eye/at/up cache), P
 * (from the BPM xScale/yScale/zn/zf cache). g_pinvVinv = P^-1 * V^-1 is rebuilt once per frame. */
static float g_pinvVinv[16];               /* P^-1 * V^-1 (view+proj undo), per frame */
static BOOL  g_pinvVinvValid = FALSE;
static float g_lastC6[16];                 /* most recent register-6 upload (WVP transposed, as received) */
static BOOL  g_lastC6Valid = FALSE;
static float g_razSumX = 0.0f, g_razSumY = 0.0f, g_razSumZ = 0.0f; /* centroid accumulator THIS frame */
static int   g_razCount = 0;               /* # skinned origins near the lock this frame */
static Vec3  g_razWorld = {0,0,0};         /* promoted + smoothed Raz origin used by the FP camera */
static BOOL  g_razValid = FALSE;
static int   g_razMissFrames = 0;          /* consecutive frames with no candidate -> re-lock */
/* notes/51: the bone-dump session proved Raz is the c96 entity whose recovered origin is NEAREST
 * the camera eye (rigid chase-cam offset ~11.6wu), rock-stable, while other 32-bone NPCs sit far
 * (100s of wu). The old notes/49 logic did the opposite - rejected near-eye origins (0.35*focus ~
 * 67wu) as "camera-attached" (so it rejected Raz) and centroid-averaged whatever remained (mixing
 * Raz with NPCs -> the jitter). Replaced with: track the SINGLE nearest-to-eye c96 origin this
 * frame (that's Raz), no averaging. */
static Vec3  g_razNearOrigin = {0,0,0};    /* nearest-to-eye c96 origin THIS frame (= Raz) */
static float g_razNearDist2 = 0.0f;        /* its squared eye-distance (only meaningful if valid) */
static BOOL  g_razNearValid = FALSE;       /* a candidate was found this frame */
static float g_razNearC6[16];              /* notes/51: the winning (=Raz) draw's r6 upload (WVP^T), for full-World recovery */
/* notes/51: Raz's full world orientation, recovered from his World = WVP*pinvVinv (not just the
 * translation). Used to derive a STABLE first-person view from Raz's own facing instead of the
 * swaying chase camera. World's upper-3x3 rows are the model X/Y/Z axes expressed in world space. */
static float g_razAxisX[3] = {1,0,0};      /* model +X axis in world */
static float g_razAxisY[3] = {0,1,0};      /* model +Y axis in world */
static float g_razAxisZ[3] = {0,0,1};      /* model +Z axis in world */
static BOOL  g_razBasisValid = FALSE;
/* notes/51: F4 runtime toggle. FP engages only when TRUE, so it never molests the title/menu/brain
 * screens (which have decorative skinned characters our nearest-to-eye detector would latch as a
 * phantom "Raz"). Starts FALSE; the user presses F4 once in real gameplay. */
static BOOL  g_fpActive = FALSE;
/* (g_fpProbe scalar is declared much earlier with the other FP knobs - the env parser needs it) */
/* notes/24: off-axis convergence/focal distance, recomputed each real frame
 * from the live eye->at distance - see the comment on
 * STEREO_FOCUS_DISTANCE_MIN/DEFAULT above for the reasoning. Starts at the
 * DEFAULT fallback so the very first correction (before BVM_OnEntry_asm's
 * first hit of the session) uses a sane value rather than 0. */
static float g_focusDistance = STEREO_FOCUS_DISTANCE_DEFAULT;

/* notes/14: the raw view-matrix rewrite from notes/13 never reached the GPU
 * because it happens after that frame's ONE shader-constant upload already
 * ran. This session found the actual per-draw matrix upload:
 * SetVertexShaderConstantF(StartRegister=6, Vector4fCount=4) - a per-draw
 * clip-space composite matrix (its caller, exe+0x51D2xx, computes it via two
 * chained matrix-helper calls, exe+0x433E50 and exe+0x42E2A0, before the
 * upload). The correction math below needs the live projection matrix's
 * xScale (Proj[0][0]) - an EARLIER attempt this session cached
 * BuildProjectionMatrix's pOutMatrix POINTER (mirroring how
 * g_camPOutMatrix works for the view matrix) and read *pOutMatrix from the
 * SetVertexShaderConstantF hook, but that produced wildly inconsistent
 * values (1.0, 0.0, 0.0849 across consecutive reads) - live evidence that,
 * unlike BuildViewMatrix's pEye/pAt/pUp (confirmed persistent object fields
 * in notes/09), BuildProjectionMatrix's pOutMatrix is frequently a
 * SHORT-LIVED caller stack temp: valid only while that caller's own frame is
 * still on the stack, and BuildProjectionMatrix fires only once or twice
 * total per session (notes/07), so by the time a later frame's
 * SetVertexShaderConstantF hook reads it, that stack region has long since
 * been reused by unrelated code. Fixed by computing xScale directly from
 * BuildProjectionMatrix's ENTRY ARGUMENTS (rawFov, Aspect) instead, using
 * the exact conversion formula already disassembled in notes/07 -
 * eliminates any dependency on the output buffer's lifetime. */
#define ADDR_FOV_DIV_CONST ((double *)0x00703698u) /* rawFov -> fovy: divide by this... */
#define ADDR_FOV_MUL_CONST ((float  *)0x00793444u) /* ...then multiply by this (notes/07 disasm) */
static float g_projXScale = 0.0f;
static BOOL g_projXScaleValid = FALSE;
/* notes/24: zn/zf, needed (alongside xScale) for the off-axis correction's
 * A=zf/(zn-zf) and B=zn*zf/(zn-zf) terms - see the derivation above
 * Hook_SetVertexShaderConstantF. BuildProjectionMatrix's signature
 * (pOutMatrix, rawFov, Aspect, zn, zf) was already established in notes/07;
 * only rawFov/Aspect were read before this session. Captured the same way
 * (from entry ARGUMENTS, not the output buffer - see the large comment
 * block above g_projXScale for why the output buffer is unsafe to cache). */
static float g_projZNear = 10.0f;   /* live-confirmed default (notes/06) */
static float g_projZFar = 50000.0f; /* live-confirmed default (notes/06) */

/* notes/43: tangent-matched submit crop (prototype above VRBridge_PumpEye's call site).
 * The compositor maps the submitted texture's bounds rectangle linearly-in-tangent-space onto
 * the headset's per-eye lens frustum. We historically submitted the FULL texture (NULL bounds),
 * so whatever FOV the game rendered was stretched onto the lens frustum - the entire reason the
 * picture looked zoomed-in and PSYVR_FOV_SCALE needed per-headset hand-tuning (notes/40). This
 * computes, per eye, the sub-rectangle of our frame whose tangent extents equal the headset's
 * real frustum (GetProjectionRaw), making the angular mapping exactly 1:1 at ANY FOV scale.
 * PSYVR_FOV_SCALE then only controls how much rendered frame exists OUTSIDE the visible window
 * (culling margin + HUD legroom) - not the zoom.
 *   Frame tangent extents: x in [k - 1/xScale, k + 1/xScale] (k = the same per-eye shear the
 *   WVP patch applies), y symmetric +/-1/yScale (no vertical shear yet - hence the vMin clamp
 *   on headsets whose frustum reaches higher than it does low, like the Quest 3).
 *   Texture orientation: u=0 = clip x=-1 (left), v=0 = clip y=+1 (top; D3D row 0).
 * A clamped bound means that axis lacks coverage and the compositor mildly stretches it; the
 * log suggests the PSYVR_FOV_SCALE that reaches full coverage. */
static const VRTextureBounds_t *VRBridge_SubmitBounds(int e)
{
    static VRTextureBounds_t s_bounds[2];
    static DWORD s_lastLog[2] = { 0, 0 };
    float xHalf, yHalf, k, l, r, t, b, uMin, uMax, vMin, vMax;
    BOOL clamped = FALSE;

    if (!g_submitBoundsEnabled || !g_realProjRawValid || !g_projXScaleValid ||
        g_projXScale <= 0.0f || g_projYScale <= 0.0f || e < 0 || e > 1)
        return NULL;

    xHalf = 1.0f / g_projXScale;
    yHalf = 1.0f / g_projYScale;
    k = g_realShearValid[e] ? g_realShearK[e]
                            : (-g_realHalfIPD[e]) / ((g_focusDistance > 1.0f) ? g_focusDistance : 1.0f);
    l = g_realProjRaw[e][0]; r = g_realProjRaw[e][1];
    t = g_realProjRaw[e][2]; b = g_realProjRaw[e][3];

    uMin = (l - (k - xHalf)) / (2.0f * xHalf);
    uMax = (r - (k - xHalf)) / (2.0f * xHalf);
    vMin = (yHalf - fabsf(t)) / (2.0f * yHalf); /* t is the UPWARD tangent, negative in OpenVR's convention */
    vMax = (yHalf + b) / (2.0f * yHalf);        /* b = downward tangent, positive */

    if (uMin < 0.0f) { uMin = 0.0f; clamped = TRUE; }
    if (vMin < 0.0f) { vMin = 0.0f; clamped = TRUE; }
    if (uMax > 1.0f) { uMax = 1.0f; clamped = TRUE; }
    if (vMax > 1.0f) { vMax = 1.0f; clamped = TRUE; }
    if (uMax - uMin < 0.05f || vMax - vMin < 0.05f)
        return NULL; /* degenerate - something's off, full-texture submit is the safer fallback */

    s_bounds[e].uMin = uMin; s_bounds[e].uMax = uMax;
    s_bounds[e].vMin = vMin; s_bounds[e].vMax = vMax;

    {
        DWORD now = GetTickCount();
        if (s_lastLog[e] == 0 || (DWORD)(now - s_lastLog[e]) >= 5000) {
            /* full-coverage suggestion: the FOV scale at which no bound needs clamping.
             * rawFov (and therefore fovy) is linear in the scale, so scale_needed =
             * fovy_needed / fovy_base per axis; horizontal converts through the aspect. */
            float fovyCur = 2.0f * atanf(yHalf);
            float fovScale = (g_fovScaleAsm > 0.01f) ? g_fovScaleAsm : 1.0f;
            float fovyBase = fovyCur / fovScale;
            float aspect = xHalf / yHalf;
            float needY = (fabsf(t) > b) ? fabsf(t) : b;
            float needXl = fabsf(l - k), needXr = fabsf(r - k);
            float needX = (needXl > needXr) ? needXl : needXr;
            float scaleY = 2.0f * atanf(needY) / fovyBase;
            float scaleX = 2.0f * atanf(needX / aspect) / fovyBase;
            float suggested = (scaleX > scaleY) ? scaleX : scaleY;
            LogLine("VRBridge: submit bounds eye=%d u=[%.3f,%.3f] v=[%.3f,%.3f]%s - angular mapping 1:1"
                    " (full lens coverage needs PSYVR_FOV_SCALE>=%.2f, current %.2f)",
                    e, uMin, uMax, vMin, vMax, clamped ? " (CLAMPED: frame smaller than lens frustum on an axis)" : "",
                    suggested + 0.005f, fovScale);
            s_lastLog[e] = now;
        }
    }
    return &s_bounds[e];
}

/* ======================================================================
 * Head tracking (notes/34)
 * ======================================================================
 *
 * Reuses the exact insertion point the per-eye stereo correction already proved live: the game
 * uploads Transpose(M*P) to register 6 per draw (M = unknown World*View, P = known projection),
 * and any rigid transform X inserted between M and P becomes a right-multiplication
 * WVP_new = WVP * Y with Y = P^-1 * X * P (see the derivation above STEREO_WVP_REGISTER). The
 * per-eye X (translate d, shear k) is special-cased to a cheap column-0 patch; head tracking
 * needs a full rotation, so its Y is a dense 4x4 computed numerically ONCE per frame here and
 * applied as one 4x4 multiply per register-6 upload (~80/frame - negligible).
 *
 * Spaces: OpenVR tracking space and the game's eye space are both right-handed x-right/y-up/
 * -z-forward, so the axis mapping is identity and only the translation needs the established
 * WORLD_UNITS_PER_METER scale. The view correction T is the INVERSE of the head's motion
 * relative to a reference captured at first valid pose (position + yaw only - pitch/roll stay
 * absolute so the game's horizon is level no matter how the head was tilted at init).
 * Combined order per draw: WVP * Y_track * Y_eye (rotate the whole head first, then offset the
 * eye within the rotated head frame - matching how real VR SDKs compose eye poses).
 *
 * All matrices below are row-vector convention (p' = p * M), flat row-major [r*4+c], matching
 * the existing derivation; OpenVR's HmdMatrix34_t is column-vector, transposed on ingest. */

/* (g_projYScale is declared much earlier, next to g_fovScaleAsm - VRBridge_QueryRealGeometry
 * needs it for the notes/37 suggested-FOV-scale log) */

static float g_trackRefInv[16];    /* undoes the reference pose's position+yaw (tracking space, meters) */
static float g_trackYt[16];        /* Transpose(P^-1 * T * P) for this frame - premultiplies the transposed upload */
/* (g_trackRefValid/g_trackYValid are declared much earlier, next to the env flags - see there) */

static void Mat4MulRow(float out[16], const float a[16], const float b[16]) /* out = a*b; out may alias a or b */
{
    float tmp[16];
    int r, c, k;
    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++) {
            float s = 0.0f;
            for (k = 0; k < 4; k++) s += a[r * 4 + k] * b[k * 4 + c];
            tmp[r * 4 + c] = s;
        }
    memcpy(out, tmp, 16 * sizeof(float));
}

static void Mat4Identity(float m[16])
{
    int i;
    for (i = 0; i < 16; i++) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void VRBridge_UpdateHeadTracking(const HmdMatrix34_t *pose34)
{
    float pose[16]; /* device -> tracking space, row-vector */
    float T[16];    /* the view correction: inverse head motion, translation in world units */
    float P[16], Pinv[16], Y[16];
    int r, c;

    if (g_trackingDisabled && !g_firstPerson) return; /* notes/47: FP-only still needs this path */
    if (!g_projXScaleValid || g_projYScale == 0.0f) return;

    /* notes/47: live first-person tuning (edge-detected) so the sweet spot can be found in one
     * running session with no relaunch. F7/F8 = forward -/+ (0.05 step), F9/F10 = height -/+
     * (10wu step). Logged so the final values can be baked into the launcher. */
    if (g_firstPerson) {
        static SHORT p4;
        SHORT k4 = GetAsyncKeyState(VK_F4);
        if ((k4 & 0x8000) && !(p4 & 0x8000)) {   /* notes/51: F4 toggles FP on/off (starts off - press in gameplay) */
            g_fpActive = !g_fpActive;
            LogLine("FP: %s (F4) - engages only in gameplay to avoid menu/title phantom-Raz", g_fpActive ? "ON" : "OFF");
        }
        p4 = k4;
    }
    if (g_firstPerson) {
        static SHORT p5, p6, p7, p8, p9, p10;
        SHORT k5 = GetAsyncKeyState(VK_F5), k6 = GetAsyncKeyState(VK_F6);
        SHORT k7 = GetAsyncKeyState(VK_F7), k8 = GetAsyncKeyState(VK_F8);
        SHORT k9 = GetAsyncKeyState(VK_F9), k10 = GetAsyncKeyState(VK_F10);
        BOOL changed = FALSE;
        if ((k5 & 0x8000) && !(p5 & 0x8000)) { g_fpSmooth -= 0.03f; changed = TRUE; }  /* smoother */
        if ((k6 & 0x8000) && !(p6 & 0x8000)) { g_fpSmooth += 0.03f; changed = TRUE; }  /* snappier */
        if ((k7 & 0x8000) && !(p7 & 0x8000)) { g_fpForward -= 0.25f; changed = TRUE; }
        if ((k8 & 0x8000) && !(p8 & 0x8000)) { g_fpForward += 0.25f; changed = TRUE; }
        if ((k9 & 0x8000) && !(p9 & 0x8000)) { g_fpHeight -= 5.0f; changed = TRUE; }
        if ((k10 & 0x8000) && !(p10 & 0x8000)) { g_fpHeight += 5.0f; changed = TRUE; }
        if (g_fpForward < -2.0f) g_fpForward = -2.0f;
        if (g_fpForward > 10.0f) g_fpForward = 10.0f; /* notes/51: with the razWorld-locked anchor the eye
                                                       * starts AT Raz's origin; a forward push reaches his head */
        if (g_fpSmooth < 0.02f) g_fpSmooth = 0.02f;
        if (g_fpSmooth > 1.0f) g_fpSmooth = 1.0f;
        if (changed)
            /* notes/51: report the ACTUAL forward push in world units (the nudge is g_fpForward*20wu -
             * the old log multiplied by focus (~192) and was ~10x wrong, which hid why the eye stalled
             * behind Raz). */
            LogLine("FP tune: forward=%.2f (%.1fwu), height=%.1fwu, smooth=%.2f  [F5/F6 smooth, F7/F8 fwd, F9/F10 height]",
                    g_fpForward, g_fpForward * 20.0f, g_fpHeight, g_fpSmooth);
        p5 = k5; p6 = k6; p7 = k7; p8 = k8; p9 = k9; p10 = k10;
    }

    /* notes/47: first-person with head tracking OFF - use an identity head transform and jump
     * straight to the forward-translation + Y build below. notes/52: g_fakePose takes priority -
     * it's an explicit "synthesize a pose for testing" request, needed to run a real (non-identity)
     * T through this pipeline on the monitor path (no SteamVR) for the isolated translation test. */
    if ((g_trackingDisabled || g_fpPreviewMode) && !g_fakePose) {
        Mat4Identity(T);   /* notes/51: monitor preview also uses a frozen (identity) head */
        goto fp_and_build;
    }

    /* notes/35: F11 recenters - drops the reference so the next valid pose re-captures it
     * (position + yaw, same as startup). Edge-detected; costs one GetAsyncKeyState per frame. */
    {
        static SHORT s_prevF11 = 0;
        SHORT f11 = GetAsyncKeyState(VK_F11);
        if ((f11 & 0x8000) && !(s_prevF11 & 0x8000) && g_trackRefValid) {
            g_trackRefValid = FALSE;
            LogLine("HeadTrack: F11 pressed - recentering (reference re-captures on next valid pose)");
        }
        s_prevF11 = f11;
    }

    if (g_fakePose) {
        /* Synthesized gentle sway (~±25° yaw by default, ~±8° pitch, ±6cm lateral) - makes the
         * tracking path visible on a monitor even though the null driver's real pose never moves.
         * notes/59: yaw amplitude is now overridable (PSYVR_FAKE_POSE_YAW_DEG, read once at
         * startup into g_fakePoseYawAmpRad) so an off-axis void test can sweep well past the
         * default gentle sway without a separate test harness - default behavior (0.44rad~=25deg)
         * is unchanged when unset. */
        float t = (float)GetTickCount() * 0.001f;
        float yaw = g_fakePoseYawAmpRad * sinf(t * 0.5f), pitch = 0.14f * sinf(t * 0.33f);
        float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
        float ry[16], rx[16];
        Mat4Identity(ry); ry[0] = cy; ry[2] = -sy; ry[8] = sy; ry[10] = cy;      /* RotY, row-vector */
        Mat4Identity(rx); rx[5] = cp; rx[6] = sp; rx[9] = -sp; rx[10] = cp;      /* RotX, row-vector */
        Mat4MulRow(pose, rx, ry); /* pitch about local x, then yaw about tracking y */
        pose[12] = 0.06f * sinf(t * 0.7f); pose[13] = 0.0f; pose[14] = 0.0f;
    } else if (pose34) {
        for (r = 0; r < 3; r++)
            for (c = 0; c < 3; c++)
                pose[c * 4 + r] = pose34->m[r][c]; /* transpose: column-vector 3x4 -> row-vector rotation */
        pose[3] = pose[7] = pose[11] = 0.0f;
        pose[12] = pose34->m[0][3]; pose[13] = pose34->m[1][3]; pose[14] = pose34->m[2][3];
        pose[15] = 1.0f;
    } else {
        return; /* invalid pose this frame - keep last good correction rather than snapping back */
    }

    if (!g_trackRefValid) {
        /* forward = -(row 2) of the row-vector rotation; yaw chosen so RotYRow(yaw) has the same
         * horizontal forward. RefInv = Trans(-pos) * RotY(-yaw). */
        float yaw = atan2f(pose[8], pose[10]);
        float cyi = cosf(-yaw), syi = sinf(-yaw);
        float trn[16], ryi[16];
        Mat4Identity(trn); trn[12] = -pose[12]; trn[13] = -pose[13]; trn[14] = -pose[14];
        Mat4Identity(ryi); ryi[0] = cyi; ryi[2] = -syi; ryi[8] = syi; ryi[10] = cyi;
        Mat4MulRow(g_trackRefInv, trn, ryi);
        g_trackRefYaw = yaw;
        g_trackRefValid = TRUE;
        LogLine("HeadTrack: reference captured - pos=(%.3f,%.3f,%.3f)m yaw=%.1fdeg%s",
                pose[12], pose[13], pose[14], yaw * 57.29578f, g_fakePose ? " (FAKE POSE MODE)" : "");
    }

    {
        /* notes/68 part 3: publish head yaw RELATIVE to the reference, wrapped to
         * +/-180, for the camera-transform follow mode. Computed here because
         * this is where a validated pose exists. */
        float yawNow = atan2f(pose[8], pose[10]) - g_trackRefYaw;
        while (yawNow >  3.14159265f) yawNow -= 6.28318531f;
        while (yawNow < -3.14159265f) yawNow += 6.28318531f;
        g_headYawDeg = yawNow * 57.29578f;

        /* ⚠️ AVOID DOUBLE ROTATION. The register-6 correction built below already
         * rotates the RENDERED IMAGE by head yaw. When camfollow is on, the
         * engine's own camera is being turned by the same yaw - so leaving yaw in
         * here too would turn the view TWICE, which in a headset reads as the
         * world swinging at double head speed. That is not a cosmetic bug; it is
         * a fast route to motion sickness, so it must not reach a wearer.
         *
         * Split the work instead: camfollow owns YAW (it is the half that fixes
         * culling, and it has no free-look clamp), and the register-6 path keeps
         * pitch, roll and positional head motion. Cancel yaw out of the pose by
         * pre-rotating it by -yawNow about the tracking up axis. */
        if (g_camFollowHead) {
            float cy = cosf(-yawNow), sy = sinf(-yawNow);
            float ryi[16], tmp[16];
            Mat4Identity(ryi); ryi[0] = cy; ryi[2] = -sy; ryi[8] = sy; ryi[10] = cy;
            Mat4MulRow(tmp, ryi, pose);
            memcpy(pose, tmp, sizeof(tmp));
        }
    }

    {
        /* Motion = pose * RefInv (head motion relative to reference), then T = Motion^-1 with the
         * translation converted to world units. Rigid inverse: R^T, t' = -t*R^T. */
        float motion[16];
        Mat4MulRow(motion, pose, g_trackRefInv);
        for (r = 0; r < 3; r++)
            for (c = 0; c < 3; c++)
                T[r * 4 + c] = motion[c * 4 + r];
        T[3] = T[7] = T[11] = 0.0f;
        for (c = 0; c < 3; c++)
            T[12 + c] = -(motion[12] * T[c] + motion[13] * T[4 + c] + motion[14] * T[8 + c])
                        * WORLD_UNITS_PER_METER;
        T[15] = 1.0f;
    }

    /* notes/52: isolated empirical test - inject a KNOWN, raw translation directly into T,
     * bypassing all pose/reference-capture/FP-anchor complexity, to directly measure (via
     * HTDEBUG below) whether a known shift here produces the expected world-space displacement
     * once composed into g_trackYt and applied to a register-6 upload. Settles the composition-
     * order question with real data instead of more hand-derived matrix algebra. */
    if (g_htTestShift != 0.0f) T[14] += g_htTestShift;

fp_and_build:
    /* notes/47: first-person - slide the eye forward onto Raz (the look-at point sits
     * g_focusDistance ahead along view -Z, so the world shifts +Z by that much to bring it to
     * the camera), then apply the head transform about that new origin. Row-vector: T := X1 * T
     * so points are re-origined first, then head-rotated/leaned. Off by default (byte-identical). */
    /* notes/47 FIX: do NOT gate on g_frameCamCached - Hook_Present resets it to FALSE BEFORE it
     * calls VRBridge_PumpPoses (which runs us), so the guard was always false and FP never
     * reached the render (user saw "keys did nothing visually"). g_focusDistance persists across
     * frames (set each BVM, clamped, default otherwise), so it's valid with or without the flag. */
    if (g_firstPerson && (g_fpActive || g_fpForceActive)) {
        float X1[16];
        Vec3 fwd = Vec3Normalize(Vec3Sub(g_baseAt, g_baseEye));
        Vec3 right = g_rightVec;
        Vec3 up = Vec3Normalize(Vec3Cross(right, fwd));

        /* notes/52: X1-isolation test override - force a KNOWN razWorld (baseEye + fixed offset),
         * bypassing the nearest-c96 detector entirely, so X1's construction can be tested with a
         * fully controlled, repeatable input (no gameplay/Raz needed). */
        if (g_razForceValid) {
            g_razWorld.x = g_baseEye.x + g_razForceOffX;
            g_razWorld.y = g_baseEye.y + g_razForceOffY;
            g_razWorld.z = g_baseEye.z + g_razForceOffZ;
            g_razValid = TRUE;
            g_razMissFrames = 0;
        } else
        /* notes/51: promote this frame's nearest-to-eye c96 origin (= Raz, see the recovery block
         * in Hook_SetVertexShaderConstantF) into the smoothed g_razWorld the camera locks to. A
         * single stable origin, not a centroid - so no inter-entity averaging jitter. If no
         * candidate this frame (Raz occluded / cutscene / scene change), hold the last lock for a
         * short grace period, then drop it so the next frame re-seeds. */
        if (g_razNearValid) {
            Vec3 cand = g_razNearOrigin;
            g_razMissFrames = 0;
            if (!g_razValid) { g_razWorld = cand; g_razValid = TRUE; }
            else {
                Vec3 de = Vec3Sub(cand, g_razWorld);
                float a = g_fpSmooth;
                /* snap (don't smooth) across big jumps - level load / camera cut - so the eye
                 * doesn't translate wildly between the old and new anchor. */
                if (de.x*de.x + de.y*de.y + de.z*de.z > 500.0f*500.0f) g_razWorld = cand;
                else { g_razWorld.x += de.x * a; g_razWorld.y += de.y * a; g_razWorld.z += de.z * a; }
            }
        } else if (g_razValid) {
            if (++g_razMissFrames > 8) { g_razValid = FALSE; g_razMissFrames = 0; }
        }

        /* notes/53: Raz-lock reliability stats - once per frame, count whether the raw detector
         * found a candidate this frame (g_razNearValid) and whether the smoothed lock is up
         * (g_razValid), and count real lock-LOSS events (g_razValid true->false transitions,
         * i.e. frames where the anchor snaps to the different chase-cam-relative fallback). */
        if (g_razLockStats) {
            g_rlTotalFrames++;
            if (g_razNearValid) g_rlNearHit++;
            if (g_razValid) g_rlValidHit++;
            if (g_rlPrevValid && !g_razValid) g_rlLossEvents++;
            g_rlPrevValid = g_razValid;
            {
                static DWORD s_rlLast = 0;
                DWORD now = GetTickCount();
                if (s_rlLast == 0 || (DWORD)(now - s_rlLast) >= 1000) {
                    s_rlLast = now;
                    LogLine("RAZLOCK: total=%ld nearHit=%ld(%.0f%%) validHit=%ld(%.0f%%) lossEvents=%ld",
                            g_rlTotalFrames, g_rlNearHit,
                            g_rlTotalFrames ? 100.0f * g_rlNearHit / g_rlTotalFrames : 0.0f,
                            g_rlValidHit,
                            g_rlTotalFrames ? 100.0f * g_rlValidHit / g_rlTotalFrames : 0.0f,
                            g_rlLossEvents);
                }
            }
        }

        /* notes/51: recover Raz's FULL World matrix (World = WVP * pinvVinv) from the winning draw's
         * r6 upload, to get his model orientation in world space. g_razNearC6 = Raz's WVP transposed,
         * so WVP row i = column i of g_razNearC6 = (c6[i], c6[4+i], c6[8+i], c6[12+i]); World row i =
         * (that WVP row) * pinvVinv. World's upper-3x3 rows are the model X/Y/Z axes in world - the
         * basis a stable Raz-facing view will be built from (identifying which axis is forward/up is
         * the point of the probe below). */
        if (g_razNearValid) {
            int i, cc, kk;
            float world[16];
            for (i = 0; i < 4; i++) {
                float row[4];
                row[0] = g_razNearC6[i]; row[1] = g_razNearC6[4 + i];
                row[2] = g_razNearC6[8 + i]; row[3] = g_razNearC6[12 + i];
                for (cc = 0; cc < 4; cc++) {
                    float s = 0.0f;
                    for (kk = 0; kk < 4; kk++) s += row[kk] * g_pinvVinv[kk * 4 + cc];
                    world[i * 4 + cc] = s;
                }
            }
            /* normalize each 3-axis (rows 0,1,2) - discard the homogeneous 4th component */
            for (i = 0; i < 3; i++) {
                float *dst = (i == 0) ? g_razAxisX : (i == 1) ? g_razAxisY : g_razAxisZ;
                float x = world[i * 4 + 0], y = world[i * 4 + 1], z = world[i * 4 + 2];
                float len = sqrtf(x * x + y * y + z * z);
                if (len > 1e-6f) { dst[0] = x / len; dst[1] = y / len; dst[2] = z / len; }
            }
            g_razBasisValid = TRUE;

            if (g_fpProbe) {
                static DWORD s_ax = 0; DWORD now = GetTickCount();
                if (s_ax == 0 || (DWORD)(now - s_ax) >= 1000) {
                    s_ax = now;
                    LogLine("RAZAXIS: X=(%.3f,%.3f,%.3f) Y=(%.3f,%.3f,%.3f) Z=(%.3f,%.3f,%.3f) baseUp=(%.3f,%.3f,%.3f) pos=(%.1f,%.1f,%.1f)",
                            g_razAxisX[0], g_razAxisX[1], g_razAxisX[2],
                            g_razAxisY[0], g_razAxisY[1], g_razAxisY[2],
                            g_razAxisZ[0], g_razAxisZ[1], g_razAxisZ[2],
                            g_baseUp.x, g_baseUp.y, g_baseUp.z,
                            g_razWorld.x, g_razWorld.y, g_razWorld.z);
                }
            }
        }

        Mat4Identity(X1);
        if (g_razValid) {
            /* Lock the eye to Raz's world origin + a vertical lift (his origin is at his feet/root)
             * and an optional small forward nudge. Camera world-move d = targetEye - g_baseEye
             * becomes the view-space translation (-d.right, -d.up, +d.fwd). */
            Vec3 targetEye, d;
            /* notes/49: lift along TRUE world up (+Y in this engine - the BuildViewMatrix up input
             * came back as (0,0.888,-0.46), i.e. the camera up already tilted by the chase pitch,
             * which sank the lift). Raz's centroid is his body center, so a fixed +Y offset puts the
             * eye at head height and keeps it there whatever the camera does. */
            /* notes/51: lift along the CAMERA's up vector (per frame), NOT a hardcoded world axis.
             * Psychonauts levels use different up-axes (the start area is +Z-up, later areas +Y-up
             * - confirmed live from g_baseUp), so a fixed +Y lift moves the eye SIDEWAYS in +Z-up
             * areas, dropping it to ground level beside Raz (looked 3rd-person + "pulled down").
             * `up` here = normalize(cross(right, fwd)) computed above = the current camera up. */
            Vec3 wUp = up;
            float fnudge = g_fpForward * 20.0f;   /* wu along facing (F7/F8; ~2wu per press) */
            targetEye.x = g_razWorld.x + g_fpHeight * wUp.x + fnudge * fwd.x;
            targetEye.y = g_razWorld.y + g_fpHeight * wUp.y + fnudge * fwd.y;
            targetEye.z = g_razWorld.z + g_fpHeight * wUp.z + fnudge * fwd.z;
            d = Vec3Sub(targetEye, g_baseEye);
            X1[12] = -(d.x * right.x + d.y * right.y + d.z * right.z);
            X1[13] = -(d.x * up.x + d.y * up.y + d.z * up.z);
            X1[14] = (d.x * fwd.x + d.y * fwd.y + d.z * fwd.z);
        } else {
            /* Fallback (no Raz found yet): the notes/47 forward-fraction with position smoothing. */
            Vec3 d;
            float useFocus;
            if (!g_fpSmoothInit) {
                g_fpSmoothEye = g_baseEye; g_fpSmoothFocus = g_focusDistance; g_fpSmoothInit = TRUE;
            } else {
                Vec3 de = Vec3Sub(g_baseEye, g_fpSmoothEye);
                float dist2 = de.x * de.x + de.y * de.y + de.z * de.z;
                if (dist2 > 500.0f * 500.0f) {
                    g_fpSmoothEye = g_baseEye; g_fpSmoothFocus = g_focusDistance;
                } else {
                    float a = g_fpSmooth;
                    g_fpSmoothEye.x += de.x * a; g_fpSmoothEye.y += de.y * a; g_fpSmoothEye.z += de.z * a;
                    g_fpSmoothFocus += (g_focusDistance - g_fpSmoothFocus) * a;
                }
            }
            useFocus = (g_fpSmoothFocus > 1.0f) ? g_fpSmoothFocus : g_focusDistance;
            X1[14] = g_fpForward * useFocus;
            X1[13] = -g_fpHeight;
            d = Vec3Sub(g_fpSmoothEye, g_baseEye);
            X1[12] += -(d.x * right.x + d.y * right.y + d.z * right.z);
            X1[13] += -(d.x * up.x + d.y * up.y + d.z * up.z);
            X1[14] += (d.x * fwd.x + d.y * fwd.y + d.z * fwd.z);
        }
        Mat4MulRow(T, X1, T);

        if (g_fpProbe) {
            static DWORD s_lp = 0; DWORD now = GetTickCount();
            if (s_lp == 0 || (DWORD)(now - s_lp) >= 1000) {
                s_lp = now;
                LogLine("FP PROBE: razValid=%d n=%d raz=(%.1f,%.1f,%.1f) at=(%.1f,%.1f,%.1f) eye=(%.1f,%.1f,%.1f) baseUp=(%.3f,%.3f,%.3f)",
                        g_razValid ? 1 : 0, g_razCount, g_razWorld.x, g_razWorld.y, g_razWorld.z,
                        g_baseAt.x, g_baseAt.y, g_baseAt.z, g_baseEye.x, g_baseEye.y, g_baseEye.z,
                        g_baseUp.x, g_baseUp.y, g_baseUp.z);
            }
        }
    }

    {
        float xS = g_projXScale, yS = g_projYScale;
        float A = g_projZFar / (g_projZNear - g_projZFar);
        float B = g_projZNear * g_projZFar / (g_projZNear - g_projZFar);
        int i;
        for (i = 0; i < 16; i++) { P[i] = 0.0f; Pinv[i] = 0.0f; }
        P[0] = xS; P[5] = yS; P[10] = A; P[11] = -1.0f; P[14] = B;
        Pinv[0] = 1.0f / xS; Pinv[5] = 1.0f / yS; Pinv[11] = 1.0f / B; Pinv[14] = -1.0f; Pinv[15] = A / B;
        Mat4MulRow(Y, Pinv, T);
        Mat4MulRow(Y, Y, P);
        for (r = 0; r < 4; r++)
            for (c = 0; c < 4; c++)
                g_trackYt[r * 4 + c] = Y[c * 4 + r];
        g_trackYValid = TRUE;
    }

    {
        static DWORD s_lastLog = 0;
        DWORD now = GetTickCount();
        if (s_lastLog == 0 || (DWORD)(now - s_lastLog) >= 2000) {
            LogLine("HeadTrack: T fwd=(%.3f,%.3f,%.3f) t=(%.2f,%.2f,%.2f)wu src=%s",
                    -T[8], -T[9], -T[10], T[12], T[13], T[14],
                    g_fakePose ? "fake" : "openvr");
            s_lastLog = now;
        }
    }
}
