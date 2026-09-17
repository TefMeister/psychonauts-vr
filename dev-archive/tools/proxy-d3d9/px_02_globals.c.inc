/* ======================================================================
 * VR submission bridge (notes/28) - additive, gated behind PSYVR_ENABLE_SUBMIT=1.
 * See the Milestone 8 header comment at the top of this file for the full design and the two
 * real architectural findings (plain-device-cannot-open-Ex-handle; UpdateSurface needs its own
 * fence) that shaped it.
 * ====================================================================== */

/* ---- OpenVR global entry points (declared directly, not via openvr_capi.h's dead #if 0 block -
 * see tools/vr-bridge/poc_openvr_init/openvr_init_poc.c and notes/25 Sec2 for why). ---- */
extern bool VR_IsRuntimeInstalled(void);
extern bool VR_IsHmdPresent(void);
extern uint32_t VR_InitInternal2(EVRInitError *peError, EVRApplicationType eApplicationType, const char *pStartupInfo);
extern void VR_ShutdownInternal(void);
extern void *VR_GetGenericInterface(const char *pchInterfaceVersion, EVRInitError *peError);
extern const char *VR_GetVRInitErrorAsEnglishDescription(EVRInitError error);

typedef IDirect3D9 *(WINAPI *Direct3DCreate9Ex9_t)(void); /* not used - Ex creation goes through Direct3DCreate9Ex below */
typedef HRESULT (WINAPI *Direct3DCreate9Ex_t)(UINT SDKVersion, IDirect3D9Ex **ppD3D);
static Direct3DCreate9Ex_t g_pRealDirect3DCreate9Ex = NULL;

/* Real-C++-vtable dispatch fix (notes/27): this installed SteamVR build's VR_GetGenericInterface()
 * returns a genuine C++ this-ptr, not the flat FnTable struct openvr_capi.h documents - calling
 * through it as a flat table crashes. Dereference the real vtable and dispatch via __thiscall. */
static void *VRBridge_RealVtable(void *thisPtr) { return *(void **)thisPtr; }

typedef EVRCompositorError (__thiscall *VRBridge_WaitGetPoses_t)(void *pThis,
    TrackedDevicePose_t *pRenderPoseArray, uint32_t unRenderPoseArrayCount,
    TrackedDevicePose_t *pGamePoseArray, uint32_t unGamePoseArrayCount);
typedef EVRCompositorError (__thiscall *VRBridge_Submit_t)(void *pThis,
    EVREye eEye, const Texture_t *pTexture, const VRTextureBounds_t *pBounds, EVRSubmitFlags nSubmitFlags);

/* notes/32 (Task 1): IVRSystem dispatch, same vtable-deref + __thiscall fix as IVRCompositor above.
 * Vtable slot indices come directly from openvr_capi.h's struct VR_IVRSystem_FnTable field order
 * (0-based, no QueryInterface/AddRef/Release prepended - IVRSystem is a plain abstract C++ class,
 * not COM - confirmed by the already-proven slot 0 = GetRecommendedRenderTargetSize used in
 * notes/27's own POC): 0=GetRecommendedRenderTargetSize, 2=GetProjectionRaw,
 * 5=GetEyeToHeadTransform, 23=GetFloatTrackedDeviceProperty.
 *
 * GetEyeToHeadTransform returns HmdMatrix34_t (48 bytes) BY VALUE - the MSVC x86 ABI this
 * installed SteamVR's vrclient.dll was built with passes a hidden pointer to caller-allocated
 * return storage as the FIRST explicit parameter (right after `this`) for any large-struct-
 * returning method. Declared explicitly here as a void-returning thiscall taking that pointer as
 * an explicit argument, rather than trusting a compiler's own (unverified for this cross-compiler-
 * calling-MSVC-code case) struct-return code generation - the same defensive, "don't assume the
 * ABI, write it out" discipline notes/27 already established for this file's other OpenVR calls.
 * GetProjectionRaw and GetFloatTrackedDeviceProperty both avoid this issue entirely (out-params /
 * plain float return) - real evidence this pattern works: tools/vr-bridge/poc_ipd_query (notes/32)
 * ran all four calls successfully against the live null-driver runtime before this code was
 * written (see notes/32 Sec1 for the confirmed real numbers). */
typedef void (__thiscall *VRBridge_GetRecommendedRenderTargetSize_t)(void *pThis, uint32_t *pW, uint32_t *pH);
typedef void (__thiscall *VRBridge_GetProjectionRaw_t)(void *pThis, EVREye eEye, float *pL, float *pR, float *pT, float *pB);
typedef void (__thiscall *VRBridge_GetEyeToHeadTransform_t)(void *pThis, HmdMatrix34_t *pOut, EVREye eEye);
typedef float (__thiscall *VRBridge_GetFloatTrackedDeviceProperty_t)(void *pThis,
    TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, ETrackedPropertyError *pError);

/* notes/33 follow-ups: same vtable-slot dispatch discipline as above. Slots verified against the
 * vendored openvr_capi.h VR_IVRSystem_FnTable field order (the same counting already proven right
 * for slots 0/2/5/23 above): 28=GetStringTrackedDeviceProperty, 30=PollNextEvent,
 * 47=AcknowledgeQuit_Exiting. PollNextEvent's bool return is a single AL byte in the MSVC x86
 * ABI - declared as unsigned char rather than trusting a cross-compiler bool. */
typedef uint32_t (__thiscall *VRBridge_GetStringTrackedDeviceProperty_t)(void *pThis,
    TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, char *pchValue,
    uint32_t unBufferSize, ETrackedPropertyError *pError);
typedef unsigned char (__thiscall *VRBridge_PollNextEvent_t)(void *pThis,
    struct VREvent_t *pEvent, uint32_t uncbVREvent);
typedef void (__thiscall *VRBridge_AcknowledgeQuit_t)(void *pThis);

/* World-unit <-> real-world-meters conversion factor. Cross-validated across two prior sessions,
 * not a new guess: notes/15's independent zNear/zFar-plausibility estimate ("1 world unit ~= 1cm")
 * and notes/18's finding that the shipped STEREO_HALF_IPD=3.25 constant maps to ~6.5cm real-world
 * separation - within 1mm of average adult human IPD (63mm) - both converge on the same ~1:1cm
 * ratio. Used below (notes/32, Task 1) to convert OpenVR's meters-based IPD/eye-transform values
 * into this game's world-unit scale. */
#define WORLD_UNITS_PER_METER 100.0f

/* notes/31: per-span QueryPerformanceCounter timing, added to settle notes/30's open question
 * (does WaitGetPoses or the GetRenderTargetData/memcpy/UpdateSurface readback chain dominate the
 * measured ~8-9ms/frame VR-bridge cost?) with real microsecond-level data instead of guessing.
 * Zero cost when g_vrSubmitEnabled is FALSE (this code only ever runs inside VRBridge_* functions,
 * all of which early-out on that flag). Accumulates min/avg/max over a throttled ~1sec window,
 * matching this file's existing log-throttle convention, then resets for the next window. */
typedef struct {
    LONGLONG sumTicks;
    LONGLONG minTicks;
    LONGLONG maxTicks;
    int count;
    DWORD lastLogTick;
} VRBridgeTimingStat;

static LARGE_INTEGER g_vrPerfFreq;
static BOOL g_vrPerfFreqInit = FALSE;

static double VRBridge_TicksToMs(LONGLONG ticks)
{
    if (!g_vrPerfFreqInit) { QueryPerformanceFrequency(&g_vrPerfFreq); g_vrPerfFreqInit = TRUE; }
    if (g_vrPerfFreq.QuadPart == 0) return 0.0;
    return (double)ticks * 1000.0 / (double)g_vrPerfFreq.QuadPart;
}

static void VRBridge_RecordSpan(VRBridgeTimingStat *stat, LONGLONG deltaTicks, const char *label)
{
    DWORD now;

    if (stat->count == 0) {
        stat->minTicks = deltaTicks;
        stat->maxTicks = deltaTicks;
    } else {
        if (deltaTicks < stat->minTicks) stat->minTicks = deltaTicks;
        if (deltaTicks > stat->maxTicks) stat->maxTicks = deltaTicks;
    }
    stat->sumTicks += deltaTicks;
    stat->count++;

    now = GetTickCount();
    if (stat->lastLogTick == 0 || (DWORD)(now - stat->lastLogTick) >= 1000) {
        double avgMs = VRBridge_TicksToMs(stat->sumTicks / stat->count);
        double minMs = VRBridge_TicksToMs(stat->minTicks);
        double maxMs = VRBridge_TicksToMs(stat->maxTicks);
        LogLine("VRBridge_Timing: %-22s n=%-4d avg=%.4fms min=%.4fms max=%.4fms",
                label, stat->count, avgMs, minMs, maxMs);
        stat->sumTicks = 0;
        stat->count = 0;
        stat->lastLogTick = now;
    }
}

static VRBridgeTimingStat g_statWaitGetPoses;

/* notes/31 diagnostic-only bypass flags: runtime env vars (read once alongside
 * PSYVR_ENABLE_SUBMIT) letting an A/B comparison of WaitGetPoses vs. the per-eye pump be done by
 * relaunching with a different env var, not a rebuild - much faster iteration while isolating which
 * candidate actually drives the measured Present() cost. Both default OFF (normal behavior
 * unchanged) - purely diagnostic, not meant to ship enabled. */
static BOOL g_vrSkipWaitPoses = FALSE;
static BOOL g_vrSkipPumpEye = FALSE;

/* Head tracking (notes/34): on by default whenever the VR bridge is live and WaitGetPoses returns
 * a valid HMD pose. PSYVR_DISABLE_TRACKING=1 turns it off (view stays fixed like notes/33's first
 * headset test). PSYVR_FAKE_POSE=1 replaces the real pose with a synthesized slow head sway so the
 * whole tracking path can be verified visually on a monitor with the null driver (whose real pose
 * never moves). */
static BOOL g_trackingDisabled = FALSE;
static BOOL g_fakePose = FALSE;
/* notes/59: PSYVR_FAKE_POSE_YAW_DEG override for the fake-pose sway's yaw amplitude - see the
 * comment at its use site. Default matches the original hardcoded 0.44rad (~25.2deg). */
static float g_fakePoseYawAmpRad = 0.44f;
/* notes/59 part 2: F12 one-shot level jump (see CandB_AfterBoth_asm). g_levelJumpCode defaults to
 * CAJA (Sasha's Lab) - the one notes/55 confirmed via a literal loading-screen string match
 * ("CAJA_sashalab_load.dds"), the safest bet for actually being a real loadable level. Override
 * with PSYVR_LEVEL_JUMP_CODE for a different one (e.g. an outdoor CA* Campgrounds sub-area, once
 * this mechanism itself is proven). */
static BOOL g_levelJumpKeyEnabled = FALSE;
static char g_levelJumpCode[64] = "workresource\\levels\\CAJA.plb";
/* notes/62: NUMPAD9 one-shot "Visibility Tree Culling" flag toggle (see CandB_AfterBoth_asm).
 * Direct memory write, zero menu/UI interaction - traced live via decompile of the debug menu's
 * shared toggle-sync handler (sub_629490 @ exe+0x629490): for any item registered through
 * sub_629410 (the generic-ID path, as "Visibility Tree Culling" is, with id=117), the handler reads
 * a single byte at engine_ptr+44+id to sync the menu checkbox's display state - i.e. the real,
 * live flag storage is exactly *(BYTE*)(*(void**)0x78BC20 + 44 + 117). This hotkey flips that byte
 * directly; the game's own click handler was never traced (not needed - direct write is simpler
 * and doesn't require the debug menu to be open/navigable at all). */
static BOOL g_cullToggleKeyEnabled = FALSE;
#define VISTREE_CULLING_ITEM_ID 117
#define DEBUG_FLAGS_ARRAY_OFFSET 44

/* notes/67: external automation harness state. Declared here (with the other
 * globals) rather than beside its code near CandB_AfterBoth_asm, because the
 * env-var config function below runs earlier in the file. Camera offsets come
 * from recon/2026-08-27-camera-control-without-lua. */
#define PSY_CAM_POS_OFFSET    0x08
#define PSY_CAM_DIRTY_OFFSET  0x530
/* notes/67: DirectInput mouse-delta injection lives with the other hooks far
 * below, but is installed from the env-var config function above it. */
static void InstallMouseInject(void);

/* notes/67: input-path probe state. Declared HERE rather than beside the probe
 * code because the WndProc hook - which counts mouse window messages - sits
 * earlier in this file than that code does. */
static volatile LONG g_diProbeActive    = 0;
static volatile LONG g_cntGetDeviceData = 0;
static volatile LONG g_cntWmMouseMove   = 0;
static volatile LONG g_cntWmInput       = 0;
static BOOL  g_automationEnabled = FALSE;
static DWORD g_autoPollMs        = 200;
static DWORD g_autoTelemetryMs   = 1000;
static DWORD g_autoLastPoll      = 0;
static DWORD g_autoLastTelemetry = 0;
static char  g_autoCmdPath[MAX_PATH] = "";
static LONG  g_autoInCommand     = 0;     /* re-entrancy guard */
static BOOL  g_camHold           = FALSE; /* re-apply target every frame */
static float g_camHoldPos[3]     = { 0.0f, 0.0f, 0.0f };
/* Facing has its own hold: the game re-aims the camera every frame during
 * gameplay, so a one-shot direction write is overwritten before it is seen. */
static BOOL  g_lookHold          = FALSE;
static float g_lookHoldDir[3]    = { 0.0f, 0.0f, 1.0f };
/* notes/67: hold a single float in the camera object every frame - for testing
 * whether a scalar the engine recomputes can be pinned (see the pokef command). */
static int          g_pokeHoldOn  = 0;
static unsigned int g_pokeHoldOff = 0;
static float        g_pokeHoldVal = 0.0f;

/* notes/67: dialogue/menu UI at its own virtual depth, so conversation options
 * stand out in front of the persistent HUD instead of sharing one plane.
 *
 * How dialogue draws are told apart, measured live 2026-08-27 by tracing UI
 * draws with a conversation on screen and again with it dismissed: EVERY draw
 * that disappeared was a TRIANGLELIST with more than one primitive, and every
 * draw that remained was a single-quad TRIANGLESTRIP. So the test is the draw's
 * SHAPE, deliberately not the shader index or texture pointer - those are
 * registration order and heap addresses, which need not be stable between runs.
 * 0 = off; dialogue then shares g_uiDepthWorld like everything else. */
static float g_dlgDepthWorld     = 0.0f;
static float g_uiDepthOverride   = 0.0f;  /* per-draw, set by the Draw* hooks */
/* notes/64: NUMPAD8 one-shot "Render Wireframe" flag toggle - same direct-byte-write pattern as
 * notes/62's Visibility Tree Culling toggle above, same generic-ID registration path
 * (sub_628e60/sub_629410("Render Wireframe", "Display wireframe for rendered geometry", 21)
 * confirmed via a fresh decompile of sub_627590). Built to distinguish "real geometry, just dark/
 * unlit" from "genuinely nothing there" at the same void test spot notes/63 used for the culling
 * A/B - wireframe draws edges regardless of lighting, so if lines appear in the void region the
 * world is loaded there and it's a lighting/visibility read, not a missing-geometry gap. */
static BOOL g_wireframeToggleKeyEnabled = FALSE;
#define RENDER_WIREFRAME_ITEM_ID 21
/* notes/65: NUMPAD7 one-shot "Collision Wireframe" flag toggle - same direct-byte-write pattern,
 * item id 22 confirmed in the same sub_627590 decompile as item 21 above (notes/64). This is the
 * theoretically CORRECT tool for the real-gap-vs-dark-terrain question (notes/59's original
 * recommendation): collision queries typically run independent of render-time visibility culling,
 * so collision wireframe can reveal geometry the renderer decided not to submit. */
static BOOL g_collisionWireframeToggleKeyEnabled = FALSE;
#define COLLISION_WIREFRAME_ITEM_ID 22
/* notes/66: NUMPAD6 one-shot "Collision Spheres" flag toggle - a fresh full decompile of
 * sub_627590 (angr, this session) recovered the COMPLETE debug-menu item list and settled an
 * open question: TCRF's "Show Collision" is NOT a real registered item in this exe at all - no
 * such string/id exists anywhere in the function. The real, closest equivalent is "Collision
 * Spheres" (id 0x11=17, "Display collision spheres"), registered via the same sub_629410
 * generic-ID checkbox path as items 117/21/22 (all already proven direct-byte-toggleable) - used
 * here as the POSITIVE CONTROL notes/65 flagged as needed before trusting Collision Wireframe's
 * negative void result (that sanity check showed nothing changed even on definitely-present
 * nearby geometry, so it couldn't be trusted). */
static BOOL g_collisionSpheresToggleKeyEnabled = FALSE;
#define COLLISION_SPHERES_ITEM_ID 0x11
/* notes/65: opt-in WndProc subclass that swallows focus-loss notifications (WM_ACTIVATE/
 * WM_ACTIVATEAPP/WM_NCACTIVATE/WM_KILLFOCUS) before the game's own window procedure sees them -
 * built specifically to defeat the "while you were away, your game was automatically paused"
 * dialog that has now blocked live testing in notes/60 and notes/64 (five prior focus-recovery
 * techniques all failed: SetForegroundWindow retry, AttachThreadInput, a coordinate-verified mouse
 * click, minimize/restore, SystemParametersInfo(SPI_SETFOREGROUNDLOCKTIMEOUT) - ACCESS_DENIED).
 * Different approach: rather than fighting to regain real OS focus, prevent the game from ever
 * finding out it lost focus in the first place - our own GetAsyncKeyState-based hotkeys (F12/
 * NUMPAD7/8/9) already don't need real focus (they poll global key state), so if the game itself
 * never sees a deactivate message, its own auto-pause logic (whatever internal check triggers the
 * dialog) should simply never fire. */
static BOOL g_suppressAutoPause = FALSE;
static HWND g_gameHwnd = NULL;
static WNDPROC g_origWndProc = NULL;

/* notes/65 part 2: the WndProc message-swallow above only ever caught ONE spurious message across
 * a full failing run (see the live log) - i.e. the "while you were away" auto-pause is NOT purely
 * reactive to window messages. Far more likely: the game polls GetForegroundWindow() (or an
 * equivalent) directly, once per tick, and compares it against its own hwnd - a common "auto-pause
 * when not the real foreground app" pattern that no message-suppression can catch. Real fix: IAT-
 * patch user32.dll!GetForegroundWindow in the game exe's own import table so every call the game
 * makes to it returns the game's own hwnd unconditionally, regardless of real OS focus state. */
static HWND WINAPI Hook_GetForegroundWindow(void)
{
    return g_gameHwnd ? g_gameHwnd : GetForegroundWindow();
}

/* Walks the running exe's PE import directory, finds the IAT slot for
 * <dllNameLower>!<funcName>, and overwrites it with hookFunc. Returns TRUE on success. General-
 * purpose (unlike MakeTrampoline/PatchJump, which byte-patch a function's own prologue and assume
 * this project's known x86 codegen patterns) - this only ever touches one pointer in a table we
 * already know the exact layout of, so it works regardless of the target function's own code. */
static BOOL PatchIATEntry(const char *dllNameLower, const char *funcName, void *hookFunc)
{
    HMODULE exe = GetModuleHandleA(NULL);
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)exe;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return FALSE;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((BYTE *)exe + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return FALSE;
    IMAGE_DATA_DIRECTORY impDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (impDir.VirtualAddress == 0) return FALSE;
    PIMAGE_IMPORT_DESCRIPTOR imp = (PIMAGE_IMPORT_DESCRIPTOR)((BYTE *)exe + impDir.VirtualAddress);
    for (; imp->Name != 0; imp++) {
        const char *modName = (const char *)((BYTE *)exe + imp->Name);
        char modNameLower[64]; int i;
        for (i = 0; modName[i] && i < 63; i++) modNameLower[i] = (char)tolower((unsigned char)modName[i]);
        modNameLower[i] = '\0';
        if (strcmp(modNameLower, dllNameLower) != 0) continue;

        PIMAGE_THUNK_DATA thunkOrig = (PIMAGE_THUNK_DATA)((BYTE *)exe + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        PIMAGE_THUNK_DATA thunkIAT  = (PIMAGE_THUNK_DATA)((BYTE *)exe + imp->FirstThunk);
        for (; thunkOrig->u1.AddressOfData != 0; thunkOrig++, thunkIAT++) {
            if (IMAGE_SNAP_BY_ORDINAL(thunkOrig->u1.Ordinal)) continue;
            PIMAGE_IMPORT_BY_NAME byName = (PIMAGE_IMPORT_BY_NAME)((BYTE *)exe + thunkOrig->u1.AddressOfData);
            if (strcmp((const char *)byName->Name, funcName) != 0) continue;

            DWORD oldProtect;
            if (!VirtualProtect(&thunkIAT->u1.Function, sizeof(void *), PAGE_READWRITE, &oldProtect))
                return FALSE;
            thunkIAT->u1.Function = (ULONG_PTR)hookFunc;
            VirtualProtect(&thunkIAT->u1.Function, sizeof(void *), oldProtect, &oldProtect);
            return TRUE;
        }
    }
    return FALSE;
}

static LRESULT CALLBACK Hook_GameWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    /* notes/67: count mouse-bearing window messages while the input probe is
     * open, to find which path this game actually reads the mouse through. */
    if (g_diProbeActive) {
        if (msg == 0x0200) InterlockedIncrement(&g_cntWmMouseMove);  /* WM_MOUSEMOVE */
        else if (msg == 0x00FF) InterlockedIncrement(&g_cntWmInput); /* WM_INPUT (Raw Input) */
    }
    if (msg == WM_ACTIVATEAPP || msg == WM_ACTIVATE || msg == WM_NCACTIVATE || msg == WM_KILLFOCUS) {
        static DWORD s_lastLog = 0;
        DWORD now = GetTickCount();
        if (now - s_lastLog > 2000) {
            LogLine("AutoPauseSuppress: swallowed msg=0x%X wParam=0x%p (not forwarded to game WndProc)",
                    msg, (void *)wParam);
            s_lastLog = now;
        }
        /* WM_NCACTIVATE's return value affects the non-client area's drawn active/inactive state;
         * returning TRUE keeps it drawn as active regardless of real focus. The others are
         * notifications, not queries - any return value is fine, the point is not forwarding them. */
        return (msg == WM_NCACTIVATE) ? TRUE : 0;
    }
    return CallWindowProcA(g_origWndProc, hWnd, msg, wParam, lParam);
}
/* notes/52: isolated empirical test for the head-tracking composition order (playbook-style
 * instrument-before-assuming, after shader disassembly ruled out a WVP-convention bug). */
static float g_htTestShift = 0.0f;   /* PSYVR_HT_TEST_SHIFT: raw world-unit Z shift injected into T */
static BOOL  g_htDebug = FALSE;      /* PSYVR_HT_DEBUG: log origin-before vs origin-after the correction */
/* notes/52: force-override knobs to test X1's construction (the FP-specific "move eye to Raz"
 * matrix) in total isolation - no gameplay, no F4, no real Raz detection needed. Sets g_fpActive
 * and a fixed g_razWorld directly, so X1 = f(known razWorld, known g_baseEye) is fully controlled
 * and repeatable, letting HTDEBUG measure whether X1's dot-product construction + its composition
 * into T (Mat4MulRow(T,X1,T)) produces the expected world-space shift - same method as the T-only
 * test that already proved the downstream pipeline correct. */
static BOOL  g_fpForceActive = FALSE;   /* PSYVR_FP_FORCE_ACTIVE=1 - checked alongside g_fpActive (declared later) */
static BOOL  g_razForceValid = FALSE;   /* PSYVR_FP_FORCE_RAZ="dx,dy,dz" set -> razWorld = baseEye + (dx,dy,dz) each frame */
static float g_razForceOffX = 0.0f, g_razForceOffY = 0.0f, g_razForceOffZ = 0.0f;
/* notes/53: real-gameplay Raz-lock reliability diagnostic (the new leading suspect after the
 * composition math was proven correct). Counts, per frame, whether the raw nearest-c96 detector
 * found a candidate (g_razNearValid) and whether the smoothed/held lock stayed up (g_razValid),
 * plus real lock-LOSS events (g_razValid true->false transitions) - the single most diagnostic
 * number, since each one is a frame where the anchor snaps from razWorld-relative to the totally
 * different chase-cam-relative fallback shift. */
static BOOL g_razLockStats = FALSE;   /* PSYVR_RAZLOCK_STATS=1 */
static LONG g_rlTotalFrames = 0, g_rlNearHit = 0, g_rlValidHit = 0, g_rlLossEvents = 0;
static BOOL g_rlPrevValid = FALSE;

/* notes/47: EXPERIMENTAL first-person prototype (PSYVR_FIRST_PERSON=1, default off). The game's
 * chase camera looks from g_baseEye toward g_baseAt; the look-at point tracks Raz, at distance
 * g_focusDistance forward. First person = slide the eye forward onto that point, then let head
 * tracking rotate the view about it. Implemented as a view-space forward translation X1 composed
 * into the SAME P^-1*X*P premultiply the head-tracking path already applies to register 6, so it
 * inherits stereo, per-eye offset, and head rotation for free. Does NOT hide Raz's model yet
 * (that needs per-draw entity identification - next step); this prototype is about proving the
 * viewpoint. FP_FORWARD is the fraction of the eye->at distance to travel (1.0 = exactly onto the
 * look-at point; <1 backs off so we sit behind Raz's head rather than inside it). FP_HEIGHT lifts
 * the eye vertically in world units after the forward move. */
static BOOL g_firstPerson = FALSE;
static float g_fpForward = 0.90f;  /* PSYVR_FP_FORWARD 0..4 */
static float g_fpHeight = 0.0f;    /* PSYVR_FP_HEIGHT world units (100 = 1m) */
static float g_fpSmooth = 0.15f;   /* PSYVR_FP_SMOOTH 0.02..1: lower = smoother/more lag, 1 = off
                                      (declared here - the env parser below runs before the Vec3
                                      smoothing state defined near the camera-cache globals) */
static BOOL g_fpProbe = FALSE;     /* PSYVR_FP_PROBE=1: log recovered Raz origin (declared here for
                                      the same reason - env parser runs before the notes/49 globals) */

/* notes/37: PSYVR_FOV_SCALE - multiplies the game's rawFov argument IN PLACE on the stack at
 * BuildProjectionMatrix entry (fovy is linear in rawFov, notes/07), widening/narrowing the
 * rendered field of view. The compositor maps the submitted eye textures onto the HEADSET's
 * frustum (~80-90deg vertical on a Quest 3) regardless of what FOV the game rendered - a
 * narrower game FOV therefore shows up magnified/zoomed in the headset. Default 1.0 is an
 * exact no-op (IEEE x*1.0 == x); VRBridge_QueryRealGeometry logs a suggested value computed
 * from the real HMD tangents. Everything downstream (xScale cache, stereo shear, culling via
 * this matrix) follows automatically because the scaled value is what the game itself consumes.
 * Non-static with an explicit asm label so Hook_BuildProjectionMatrix's stub can fmuls it. */
__attribute__((used)) float g_fovScaleAsm asm("g_fovScaleAsm") = 1.0f;
static float g_projYScale = 0.0f;  /* cot(fovy/2) = xScale*aspect, cached in BPM_OnEntry;
                                      declared here (early) for QueryRealGeometry's FOV log */

static BOOL g_dumpEyes = FALSE; /* PSYVR_DUMP_EYES=1: periodic BMP dumps of eye1/eye2/backbuffer
                                   (diagnostic for the notes/23 black-left-eye bug - notes/35) */
/* notes/35: PSYVR_TRACE_FRAME=1 - every ~5s, log ONE full frame's exact sequence of
 * SetRenderTarget/Clear/draw batches with the stereo phase attached, to see where the
 * black-left-eye screen's content actually goes. Armed at BeforeEye1, disarmed at composite. */
static BOOL g_traceFrames = FALSE;
static volatile LONG g_traceActive = 0;
static volatile LONG g_traceDrawCount = 0;

/* notes/36: PSYVR_REG_HISTO=1 - accumulate a histogram of SetVertexShaderConstantF
 * (StartRegister -> upload count, last/max Vector4fCount) during eye phases, flushed to the log
 * every ~5s. Recon for the skinned-geometry (bone matrix) constant range - the one register
 * family the stereo/tracking correction doesn't cover yet. */
static BOOL g_regHisto = FALSE;
static BOOL g_boneProbe = FALSE; /* notes/44: PSYVR_BONE_PROBE=1 - throttled logging of bone-palette
                                    uploads (c64.., notes/36) - feasibility recon for hand IK / body rig */
/* notes/52: playbook Phase 3.3 verification - correlate which PSYVR_REG_HISTO-dumped shader .bin
 * is the skinned (c96) world shader, so it can be disassembled OFFLINE (no live capture needed
 * beyond this one mapping). See Hook_CreateVertexShader/Hook_SetVertexShader (notes/36, further
 * down this file) for the existing dump-every-shader-to-disk machinery this reuses. */
#define VS_DUMP_MAP_MAX 512
static IDirect3DVertexShader9 *g_vsDumpPtr[VS_DUMP_MAP_MAX];
static int g_vsDumpMapIdx[VS_DUMP_MAP_MAX];
static int g_vsDumpMapCount = 0;
static IDirect3DVertexShader9 *g_currentVSPtr = NULL;
static BOOL g_shaderDump = FALSE;       /* PSYVR_SHADER_DUMP=1 */
static BOOL g_shaderDumpLogged = FALSE; /* latch - log at most once per run */

static BOOL g_boneDump = FALSE;  /* notes/51: PSYVR_BONE_DUMP=1 - shoulder-anchor bone hunt. Once/sec
                                    logs a burst of the next ~16 c96 draws (each recovered entity origin
                                    + eye-dist, to see if Raz's draws cluster or scatter) plus all 32
                                    bone model-space translations for the burst's first draw. Runs on the
                                    monitor path (no poses/SteamVR needed). */
static volatile LONG g_regHistoCount[256];
static BYTE g_regHistoVecMax[256];
/* notes/36: per-draw register-combination tracking. Bits set in SVSCF for interesting registers,
 * consumed at each draw call: combo[mask]++ tells us which register sets arrive together per
 * draw (e.g. "r96 bones WITHOUT a fresh r6" = skinned draws rely on an earlier r6 upload, or
 * don't use r6 at all - the shader bytecode dumps settle which). */
#define REGBIT_R6   0x01
#define REGBIT_R10  0x02
#define REGBIT_R13  0x04
#define REGBIT_R16  0x08
#define REGBIT_R64  0x10
#define REGBIT_R96  0x20
static volatile LONG g_regComboMask = 0;
static volatile LONG g_regComboCount[64];
static volatile LONG g_vsDumpIndex = 0; /* CreateVertexShader bytecode dump counter */

/* notes/36: UI depth. Bytecode analysis of ALL 455 of the game's vertex shaders found exactly two
 * position paths: 445 shaders (all rigid + all 403 skinned/bone-palette ones) transform through
 * the corrected c6 matrix - already stereo/tracking-correct - and 10 pure screen-space UI shaders
 * (oPos = input + c50, never reading c6..c9) which bypass the correction entirely, so the HUD
 * renders with ZERO parallax and would sit at infinity in the headset. Fix: identify those
 * shaders at CreateVertexShader time (signature: no const read in c6..c9), track when one is
 * bound, and shift its per-draw c50.x upload per eye to place the UI at a comfortable virtual
 * depth: shift = -d * xScale / PSYVR_UI_DEPTH (world units, default 200 ~= 2m; 0 disables). */
static float g_uiDepthWorld = 200.0f;
/* notes/42+43: UI viewport shrink - shrinks the viewport about its center while a UI-signature
 * shader is bound during an eye phase (the UI shaders are purely additive, oPos = input + c50,
 * so a constant can't rescale positions; the viewport transform rescales the whole draw
 * regardless of shader internals). Live-tested 2026-08-19 and DEMOTED to an experimental
 * opt-in: the game draws fullscreen overlays (fades, pause/menu backdrops) through the same
 * shaders, and shrinking those crushes the whole presented scene into the frame center. OFF
 * (1.0) unless PSYVR_UI_SCALE sets an absolute factor. The shipping fix for HUD visibility at
 * FOV scale > 1 is the tangent-matched submit crop instead - see VRBridge_SubmitBounds. */
static float g_uiVpScale = 1.0f;      /* per-draw viewport shrink for UI draws (1 = none) */
static float g_hmdFovyRad = 0.0f;     /* real HMD vertical FOV (radians), stashed by QueryRealGeometry
                                         for the deferred suggested-FOV log in BPM_OnEntry (notes/40 Issue 1) */
static volatile LONG g_fovSuggestLogged = 0;
#define UI_SHADER_MAX 64
static void *g_uiShaders[UI_SHADER_MAX];
static volatile LONG g_uiShaderCount = 0;
static volatile LONG g_curShaderIsUI = 0;
static volatile LONG g_curUIShaderIdx = -1; /* notes/43: which registered UI shader is bound (-1 = none) */
/* Head-tracking per-frame state - the matrices themselves live with the tracking module further
 * down (they need the projection-cache globals); these two flags are up here because
 * VRBridge_Shutdown (defined earlier) clears them. */
static BOOL g_trackRefValid = FALSE;
static float g_trackRefYaw = 0.0f;   /* notes/68: reference yaw, for the head-follow camera */
static float g_headYawDeg  = 0.0f;   /* notes/68: head yaw relative to that reference, degrees */
static int   g_camFollowHead  = 0;   /* notes/68: camera transform follows head yaw */
static float g_camFollowScale = 1.0f;/* sign/gain, tunable live in the headset */
static BOOL g_trackYValid = FALSE;
/* notes/51: monitor-only first-person preview. When SteamVR is absent the pose pump is inert and
 * first person never renders. Setting this forces VRBridge_UpdateHeadTracking down its identity-head
 * path (like g_trackingDisabled) so the FP eye-move builds with a frozen head orientation and shows
 * on the flat monitor - the anchor can then be seen/tuned without a headset. Driven from Hook_Present
 * only when the VR bridge is NOT active, so the real VR path is untouched. */
static BOOL g_fpPreviewMode = FALSE;

/* Per-eye double-buffered two-hop pipeline state (notes/28, proven in
 * tools/vr-bridge/poc_dual_device_shared/dual_device_poc.c). "Device B" in that POC's terms is
 * always g_pDevice (the game's own plain device, already tracked); "Device A" is g_pVRDeviceA
 * below (shared across both eyes - one private D3D9Ex device backs both eyes' buffers). */
typedef struct {
    /* Hop 1: g_pDevice's rendered eye surface -> GetRenderTargetData -> sysmemB (game device, sysmem) */
    IDirect3DSurface9 *sysmemB[2];
    BOOL pendingB[2];
    /* Hop 2: memcpy -> UpdateSurface -> Device A's own shared D3DPOOL_DEFAULT texture, fenced per-slot */
    IDirect3DTexture9 *texA[2];
    IDirect3DSurface9 *surfA[2];
    HANDLE handleA[2];
    IDirect3DQuery9 *queryA[2];
    BOOL pendingA[2];
    ID3D11Texture2D *tex11[2];
    int hop1Count; /* selects which Device-A slot to write next / which to consume (hop1Count-2) */
    int frameCount; /* per-real-frame counter, ALWAYS increments once per VRBridge_PumpEye call
                        regardless of whether a promotion happened this frame - selects which
                        sysmemB[] slot to write next (bCur = frameCount % 2). Must be independent
                        of hop1Count: hop1Count only advances when a promotion actually succeeds,
                        so using it to pick bCur/bPrev (an earlier bug) meant bCur was stuck at 0
                        forever (see notes/29), sysmemB[1] was never written, pendingB[bPrev] was
                        never TRUE, no promotion could ever happen, and Submit was never reached. */
    EVREye vrEye;
    /* notes/31: per-eye timing stats for the two candidate hot-path costs. */
    VRBridgeTimingStat statGRTD;      /* GetRenderTargetData call alone (hop 1a) */
    VRBridgeTimingStat statReadback;  /* LockRect+memcpy+UpdateSurface promotion chain (hop 1b), only when it actually runs */
} VRBridgeEyeState;

static BOOL g_vrSubmitEnabled = FALSE;   /* runtime flag: env var PSYVR_ENABLE_SUBMIT=1, read once at DllMain */
static BOOL g_vrBridgeInitAttempted = FALSE;
static BOOL g_vrBridgeReady = FALSE;     /* Device A + D3D11 + OpenVR all initialized OK */
static IDirect3D9Ex *g_pVRD3D9Ex = NULL;
static IDirect3DDevice9Ex *g_pVRDeviceA = NULL;
static IDirect3DSurface9 *g_pVRSysmemAScratch = NULL; /* shared transient scratch, both eyes (used sequentially, never concurrently) */
static ID3D11Device *g_pVRDevice11 = NULL;
static ID3D11DeviceContext *g_pVRContext11 = NULL;
static void *g_pVRCompositor = NULL;
static VRBridge_WaitGetPoses_t g_pVRWaitGetPoses = NULL;
static VRBridge_Submit_t g_pVRSubmit = NULL;
static VRBridgeEyeState g_vrEye1, g_vrEye2; /* eye1=left, eye2=right, matching this file's existing eye numbering */
static UINT g_vrBufWidth = 0, g_vrBufHeight = 0; /* dimensions the eye buffers above were sized for */

/* notes/32 (Task 1): real per-eye geometry sourced from OpenVR, replacing the hardcoded
 * STEREO_HALF_IPD/focus-distance estimate wherever it's actually available. g_pVRSystem/its
 * function pointers are only non-NULL when g_vrSubmitEnabled=1 AND VRBridge_Init reached the
 * IVRSystem step successfully - the stereo correction in Hook_SetVertexShaderConstantF (which
 * runs unconditionally, VR-submit on or off) checks g_vrGeomValid and falls back to the original
 * hardcoded constants whenever it's FALSE, so users without SteamVR installed at all keep getting
 * exactly the pre-existing, already-live-tested/mod-repo-pushed monitor-stereo behavior. */
static void *g_pVRSystem = NULL;
static VRBridge_GetRecommendedRenderTargetSize_t g_pVRGetRecommendedRTSize = NULL;
static VRBridge_GetProjectionRaw_t g_pVRGetProjectionRaw = NULL;
static VRBridge_GetEyeToHeadTransform_t g_pVRGetEyeToHeadTransform = NULL;
static VRBridge_GetFloatTrackedDeviceProperty_t g_pVRGetFloatProp = NULL;
static VRBridge_GetStringTrackedDeviceProperty_t g_pVRGetStringProp = NULL;
static VRBridge_PollNextEvent_t g_pVRPollNextEvent = NULL;
static VRBridge_AcknowledgeQuit_t g_pVRAcknowledgeQuit = NULL;

/* notes/33 §4: latched TRUE when SteamVR sends a quit-class event. From that point the VR runtime
 * is never touched again (vrserver is about to vanish; SteamVR kills scene apps that linger) and
 * VRBridge_Init refuses to re-initialize for the rest of the process lifetime. */
static BOOL g_vrQuitRequested = FALSE;

static BOOL g_vrGeomValid = FALSE;
static float g_realHalfIPD[2] = { 0.0f, 0.0f };  /* world units, SIGNED per eye (index 0=left/eye1,
                                                     1=right/eye2) - directly usable as `d`, no
                                                     separate sign multiply needed (unlike the old
                                                     STEREO_HALF_IPD*sign pattern), since a real
                                                     headset's eye offsets need not be symmetric. */
static float g_realProjRaw[2][4];                /* notes/43: per-eye GetProjectionRaw l,r,t,b (raw
                                                    tangents) for the submit-time texture bounds */
static BOOL g_realProjRawValid = FALSE;
static BOOL g_submitBoundsEnabled = TRUE;        /* notes/43: PSYVR_SUBMIT_BOUNDS=0 disables the
                                                    tangent-matched submit crop (debug escape hatch) */
static const VRTextureBounds_t *VRBridge_SubmitBounds(int e); /* notes/43: defined below the
                                                    projection-cache globals it needs */
static float g_realShearK[2] = { 0.0f, 0.0f };   /* dimensionless GetProjectionRaw (l+r)/2 tangent-
                                                     space frustum-center offset per eye - directly
                                                     usable as the off-axis shear coefficient `k`
                                                     (see the derivation above STEREO_WVP_REGISTER;
                                                     k = (l+r)/2 is derived in notes/32 Sec2). */
static BOOL g_realShearValid[2] = { FALSE, FALSE }; /* FALSE when GetProjectionRaw reports an
                                                        exactly-symmetric frustum (as this null
                                                        driver does - confirmed, not assumed, see
                                                        notes/32 Sec1) - i.e. no real off-axis data
                                                        to use, so Y30 keeps the existing
                                                        focus-distance-estimated k instead. */

