/*
 * Minimal logging proxy d3d9.dll for Psychonauts VR reverse-engineering.
 *
 * Purpose: validate that a DLL dropped into the game directory as "d3d9.dll"
 * gets loaded by the game and that its single imported entry point,
 * Direct3DCreate9, gets called - WITHOUT changing any game behavior. It loads
 * the real system d3d9.dll and forwards the call unmodified.
 *
 * Milestone 2: also vtable-hooks IDirect3D9::CreateDevice and
 * IDirect3DDevice9::Present, purely to observe (log) D3DPRESENT_PARAMETERS and
 * confirm Present fires every frame. Still no behavior changes there - both
 * COM hooks call straight through to the real implementation.
 *
 * Milestone 3 (this revision): first real stereo-rendering prototype. Adds
 * TWO inline (byte-patch/trampoline) hooks directly into Psychonauts.exe's
 * own code, at fixed addresses proven stable across every prior session
 * (no ASLR on the main module):
 *
 *   - BuildViewMatrix   (exe+0x292480 / 0x00692480) - notes/07, notes/09
 *   - CandB             (exe+0xFEDA0  / 0x004FEDA0) - notes/10, notes/11,
 *                         notes/12 ("draw one eye's worth of the scene",
 *                         confirmed safe to invoke twice per frame)
 *
 * On every real per-frame BuildViewMatrix call, the FIRST call of the frame
 * has its clean (unmodified) eye/at/up cached and a camera right-vector
 * computed - the real call is then let through completely unmodified (no
 * write-in-place, sidestepping the "*pEye is a persistent pointer, writes
 * carry forward frame-to-frame" compounding gotcha documented in notes/09).
 *
 * On every real per-frame CandB call, the hook invokes the real CandB body
 * TWICE (proven safe in notes/12): once with the cached camera nudged left
 * along its right-vector into an offscreen "eye 1" render target, once
 * nudged right into an offscreen "eye 2" render target. The per-eye view
 * matrix is applied by directly re-invoking BuildViewMatrix's own real,
 * unmodified body (through its trampoline) with a fresh eye position
 * computed from the cached clean base each time - never a read-back of an
 * already-offset value.
 *
 * Present is extended to composite both offscreen eye surfaces into the left
 * and right halves of the real backbuffer (two StretchRect calls) before
 * calling through to the real Present - a side-by-side stereo image.
 *
 * See notes/13-first-stereo-prototype.md for the full writeup, exact
 * addresses/byte dumps used to design the inline hooks, and empirical
 * results.
 *
 * Milestone 4 (this revision, notes/14): notes/13 found the CPU-side
 * BuildViewMatrix rewrite above never reached the GPU. This session located
 * the actual per-draw shader-constant upload responsible
 * (IDirect3DDevice9::SetVertexShaderConstantF, StartRegister=6,
 * Vector4fCount=4, one consistent call site exe+0x51D33E) and hooks it (new
 * COM vtable hook, slot 94) to apply a closed-form clip-space correction -
 * adding (-d*xScale) to the matrix's row3.x - equivalent to inserting a
 * view-space translation of d along the camera's local right axis between
 * View and Proj in a row-vector v*World*View*Proj pipeline, regardless of
 * what World/View individually were. Also adds a BuildProjectionMatrix
 * inline hook purely to compute the live xScale (replicating the exact
 * rawFov->fovy conversion notes/07 disassembled) needed by that correction.
 * See notes/14-shader-constant-stereo-hook.md for the full derivation,
 * the register-identification methodology (including a live matrix-
 * decomposition test that ruled out several other candidate registers), and
 * empirical results.
 *
 * Milestone 5 (this revision, notes/21): first real-gameplay live-log read
 * after the notes/20 fix was deployed and played. Confirmed the notes/20
 * fix itself works exactly as designed (no more premature/duplicate internal
 * Present hits during eye 2's pass), but the user's live report of the two
 * original symptoms persisting (right eye tracks but is dark; left eye still
 * freezes, beyond what focus-loss auto-pause explains) meant a SECOND cause
 * for each was still needed. Three independent, low-risk fixes added:
 *   1. Hook_Present now forces the real hardware Present to a full-
 *      backbuffer blit (NULL src/dest/dirty rect) instead of passing the
 *      game's own Present args through - our composite always redraws the
 *      whole backbuffer, so any partial-rect optimization the game applies
 *      based on its own (non-stereo) dirty-region tracking could otherwise
 *      leave stale pixels on screen, which looks exactly like "one half
 *      frozen." Also logs the game's original rect args for confirmation.
 *   2. A new IDirect3DDevice9::Reset hook (vtable slot 16, right before
 *      Present's slot 17) releases and recreates all three D3DPOOL_DEFAULT
 *      surfaces (backbuffer ref, both eye render targets) around Reset -
 *      previously unhandled entirely, a real correctness gap for any device
 *      parameter change/recovery, newly relevant because this is the first
 *      session where the user has been alt-tabbing during live testing.
 *   3. Exact (not throttle-race-sampled) per-real-frame eye1/eye2 register-6
 *      draw-call counters (g_svscfCountEye1/2), logged each real composite -
 *      notes/20's 167:13 phase skew persisted essentially unchanged (109:15)
 *      AFTER that session's fix, proving the skew was never caused by the
 *      bug notes/20 fixed; this instrumentation turns "probably a real
 *      asymmetry" into an exact number for the next live-log read, in place
 *      of further speculation about the root cause of the dark-eye asymmetry
 *      itself (not yet found - see notes/21 for the full disposition).
 * See notes/21-<name>.md for the full live-log evidence and derivation.
 *
 * Milestone 6 (this revision, notes/22): read the LIVE log of the user's own already-
 * running real-gameplay session (real level, not the title screen - camera traveled from
 * (-371,457,17) to (52198,-32867,-3980) over the session) with notes/21's exact per-frame
 * g_svscfCountEye1/g_svscfCountEye2 counters actually active for the first time. Found
 * PERFECT parity: 206/206 real-frame samples showed eye1 == eye2 exactly (sum 16960:16960,
 * ratio 1.000) - the long-chased "eye1:eye2 draw-call asymmetry" (167:13, then 109:15) was
 * NEVER REAL. It was an artifact of the OLD "SVSCF stereo-correct: phase=X" throttled log
 * line sharing one `static DWORD s_lastLog` across BOTH phases inside
 * Hook_SetVertexShaderConstantF - since eye1's burst of corrections always fires first each
 * frame (CandB_BeforeEye1 runs before CandB_BeforeEye2), the 2-second throttle reopening
 * almost always lands during eye1's burst and gets claimed by phase=1, systematically
 * starving phase=2's log line regardless of real relative work. This retroactively explains
 * why two full sessions of fixes (notes/20, notes/21) never moved that ratio: it was never
 * measuring real draw-call volume in the first place. This RULES OUT the frustum-culling-
 * cache-reuse hypothesis (nothing to explain - draw counts are equal) and redirects the
 * whole investigation.
 *
 * With no real draw-call asymmetry to explain, re-examined the code for a structural
 * asymmetry instead and found one that was there the whole time: both g_pEye1Surf and
 * g_pEye2Surf have ALWAYS rendered against the device's single shared auto depth-stencil
 * surface (SetDepthStencilSurface was never called anywhere in this file), and only eye2
 * ever explicitly Cleared it (eye1 never cleared at all). This exactly matches a clue
 * notes/14 already recorded but didn't fully connect: "clearing BOTH eyes flipped which
 * eye's background was missing" - the textbook signature of two passes sharing one
 * physical depth buffer, where whichever eye's Clear() runs last each frame "wins" a
 * properly-reset depth buffer and the other inherits stale depth data (causing depth-test
 * rejections that read as missing/stale content). Fixed by giving each eye its own private
 * depth-stencil surface (created/released alongside the existing color render targets, same
 * Reset-hook lifecycle) and Clearing both eyes unconditionally every frame - now safe with
 * no shared resource left to contaminate. See notes/22-<name>.md for the full derivation
 * and live-log evidence.
 *
 * Milestone 7 (this revision, notes/24): OFF-AXIS PROJECTION UPGRADE. Prior
 * sessions' GPU-side correction (notes/14/18) already produced a genuinely
 * PARALLEL (non-toe-in) per-eye camera - it inserts a rigid view-space
 * translation between View and Proj, never re-aims either eye's look
 * direction - but it reused the SAME symmetric projection frustum for both
 * eyes, which is only correct for a stereo pair converged at INFINITE
 * distance (zero disparity only for infinitely-far points; every finite-
 * distance point shows disparity that only shrinks asymptotically, never
 * reaching zero - see the analytic check in notes/24). Real VR SDKs
 * (OpenVR/OpenXR) instead use an ASYMMETRIC/off-axis frustum per eye so
 * that a chosen finite convergence/focal distance gets exactly zero
 * disparity, with natural "crossed" parallax nearer than that and
 * "uncrossed" parallax farther - this session adds that missing piece.
 *
 * The naive way to do this - patch the received per-draw WVP matrix's
 * row2/col0 entry the same way the existing code patches row3/col0 for the
 * translation - turns out to be WRONG in general (a real bug this session's
 * own verification script caught before it ever reached the game): the
 * translation-only patch is safe as a SINGLE matrix entry only because
 * inserting a translation between the (unknown, untraced - see notes/14
 * Sec1b) World*View matrix M and Proj can only ever perturb M's own row 3,
 * and M's row 3 is the one row an AFFINE M is guaranteed to interact with
 * Proj in a way that lands on exactly one WVP entry. A shear (needed for
 * the off-axis frustum) has no such guarantee - it lives in M's row 2,
 * which is NOT affine-constrained, so a single-entry patch silently assumes
 * the camera happens to be axis-aligned at the world origin, and produces a
 * WRONG (non-zero-at-convergence-distance) result for any other camera pose
 * (confirmed both analytically and by a standalone Python check - see
 * notes/24 Sec2 for the full derivation, the bug, and the fix). The
 * general, camera-pose-agnostic fix derives the correction as a small
 * matrix Y = Proj^-1 * (translate-then-shear) * Proj and applies
 * WVP_new = WVP_received * Y - Y works out to differ from identity only in
 * column 0, so the fix only ever touches 4 of the 16 received floats
 * (row0..row3, column 0), each computed from the RECEIVED matrix's own
 * row2/row3 values plus known projection constants (xScale, zn, zf) -
 * still no need to trace/decompose M. Verified to reduce EXACTLY to the
 * existing (already-shipped, working) translation-only patch when the new
 * shear term is zero, and to produce exactly zero eye-to-eye disparity at
 * the chosen convergence distance for arbitrary (tilted, off-origin) camera
 * poses in the standalone verification script - see notes/24.
 *
 * The convergence/focal distance uses the camera's own live per-frame
 * eye->at distance (already cached for the disabled CPU-side rewrite) - it
 * naturally tracks wherever the game's own camera is looking each frame
 * rather than a fixed guessed constant. The IPD half-offset remains the
 * fixed STEREO_HALF_IPD constant, same as before (still no real headset to
 * source real per-frame values from) - see the TODO comments below for
 * exactly what a real OpenVR/OpenXR runtime would replace.
 *
 * Vtable indices used (0-based, standard COM: slot 0/1/2 are always
 * QueryInterface/AddRef/Release):
 *   IDirect3D9::CreateDevice                    = slot 16
 *   IDirect3DDevice9::Reset                     = slot 16 (different vtable)
 *   IDirect3DDevice9::Present                   = slot 17
 *   IDirect3DDevice9::SetVertexShaderConstantF  = slot 94
 * These are NOT guessed - they come from two independent, cross-checked
 * sources: (1) counting fields in the IDirect3D9Vtbl / IDirect3DDevice9Vtbl
 * struct definitions in mingw-w64's own d3d9.h (bundled with the LLVM-MinGW
 * toolchain used to build this DLL - see the STDMETHOD() ordering in that
 * header), and (2) a prior live x64dbg session that read the *actual* vtable
 * out of process memory and breakpointed both slots successfully (see
 * notes/04-live-debug-findings.md). Rather than hardcode raw slot numbers
 * into pointer arithmetic, the hooks below patch the named function-pointer
 * fields of the real d3d9.h vtbl structs directly (This->lpVtbl->CreateDevice
 * = ..., ->lpVtbl->Present = ...) so the compiler - not manual offset math -
 * guarantees the correct slot is patched.
 *
 * Build target: 32-bit (i686), matching the 32-bit Psychonauts.exe.
 */

#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

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
typedef HRESULT (STDMETHODCALLTYPE *Reset_t)(
    IDirect3DDevice9 *This,
    D3DPRESENT_PARAMETERS *pPresentationParameters);
typedef HRESULT (STDMETHODCALLTYPE *SetVertexShaderConstantF_t)(
    IDirect3DDevice9 *This,
    UINT StartRegister,
    CONST float *pConstantData,
    UINT Vector4fCount);

static HMODULE g_hRealD3D9 = NULL;
static Direct3DCreate9_t g_pRealDirect3DCreate9 = NULL;
static CreateDevice_t g_pRealCreateDevice = NULL;
static Present_t g_pRealPresent = NULL;
static Reset_t g_pRealReset = NULL;
static SetVertexShaderConstantF_t g_pRealSetVSConstF = NULL;
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
 * TODO(real headset): this is a placeholder for real per-eye IPD data. Once
 * an OpenVR/OpenXR runtime is available, replace this fixed constant with
 * the runtime's own reported IPD (e.g. OpenVR's
 * IVRSystem::GetFloatTrackedDeviceProperty(..., Prop_UserIpdMeters_Float),
 * converted into this game's world-unit scale via the same ~1 world unit =
 * ~1cm calibration notes/15 cross-validated) - ideally sampled live each
 * frame rather than cached once, since real headsets allow runtime IPD
 * adjustment. */
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
 * TODO(real headset): a real OpenVR/OpenXR runtime does not need this
 * estimation step at all - it supplies each eye's already-correct
 * asymmetric projection matrix directly (IVRSystem::GetProjectionMatrix /
 * xrLocateViews), computed from the actual physical display geometry, not
 * a convergence-distance guess. This whole g_focusDistance mechanism only
 * exists to approximate that in the no-headset case; once real per-eye
 * projection matrices are available this file should consume THOSE
 * directly instead of re-deriving an off-axis frustum from a fixed FOV +
 * an estimated focal distance. */
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

static IDirect3DDevice9 *g_pDevice = NULL;
static IDirect3DSurface9 *g_pRealBackBuffer = NULL;
static IDirect3DSurface9 *g_pEye1Surf = NULL;
static IDirect3DSurface9 *g_pEye2Surf = NULL;
/* notes/22: the device's single auto depth-stencil surface (EnableAutoDepthStencil=1,
 * live-confirmed AutoDepthStencilFormat=75/D3DFMT_D24S8) was NEVER reassigned per eye -
 * both g_pEye1Surf/g_pEye2Surf rendered against the SAME physical depth buffer the whole
 * time, and SetDepthStencilSurface was never called anywhere in this file. Each eye now
 * gets its own private depth-stencil surface, captured/restored exactly like the color
 * render targets. See the comment on SetEyeAndTarget() for the full mechanism this fixes. */
static IDirect3DSurface9 *g_pRealDepthStencil = NULL;
static IDirect3DSurface9 *g_pEye1DepthStencil = NULL;
static IDirect3DSurface9 *g_pEye2DepthStencil = NULL;
static UINT g_bbWidth = 0;
static UINT g_bbHeight = 0;
static BOOL g_stereoReady = FALSE; /* device + both offscreen color+depth surfaces created OK */

static BOOL g_frameCamCached = FALSE;
static Vec3 g_baseEye, g_baseAt, g_baseUp, g_rightVec;
static void *g_camPOutMatrix = NULL;
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
    g_projXScaleValid = TRUE;

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

void __cdecl CandB_BeforeEye1_asm(void) asm("CandB_BeforeEye1_asm");
void __cdecl CandB_BeforeEye1_asm(void)
{
    g_stereoPhase = STEREO_PHASE_EYE1;
    g_eye2Presented = 0;
    SetEyeAndTarget(-1.0f, g_pEye1Surf, g_pEye1DepthStencil);
}

void __cdecl CandB_BeforeEye2_asm(void) asm("CandB_BeforeEye2_asm");
void __cdecl CandB_BeforeEye2_asm(void)
{
    g_stereoPhase = STEREO_PHASE_EYE2;
    SetEyeAndTarget(+1.0f, g_pEye2Surf, g_pEye2DepthStencil);
}

void __cdecl CandB_AfterBoth_asm(void) asm("CandB_AfterBoth_asm");
void __cdecl CandB_AfterBoth_asm(void)
{
    g_stereoPhase = STEREO_PHASE_IDLE;
    if (!g_stereoReady || !g_pDevice || !g_pRealBackBuffer) return;
    g_pDevice->lpVtbl->SetRenderTarget(g_pDevice, 0, g_pRealBackBuffer);
    if (g_pRealDepthStencil) {
        g_pDevice->lpVtbl->SetDepthStencilSurface(g_pDevice, g_pRealDepthStencil);
    }
}

/* ---- naked inline-hook entry points -----------------------------------
 *
 * Both are entered via a JMP planted at the target function's own first
 * INLINE_PATCH_LEN bytes (see PatchJump), so at entry the CPU state is
 * EXACTLY what it would be at the real function's own entry point: no
 * prologue has run yet, ESP points at the return address the real caller's
 * `call` instruction pushed, and any incoming registers the real function
 * itself reads (BuildViewMatrix reads none; CandB reads ECX as a "this"-like
 * context pointer - confirmed by its own first instruction, `mov
 * [ebp-0x20],ecx`, in the raw byte dump in notes/13) are exactly as the game
 * set them up.
 *
 * Hook_BuildViewMatrix: a plain "observe via a C helper, then tail-JMP into
 * the trampoline" hook. Since it ends in a JMP (not a CALL) to the
 * trampoline, the trampoline's own eventual `ret` pops the REAL original
 * return address and resumes the game's own caller directly - transparent,
 * single real invocation, our C helper never mutates the stack region past
 * reading it.
 *
 * Hook_CandB: a "call the real body twice" hook. It uses CALL (not JMP) to
 * invoke the trampoline both times, which means each invocation pushes our
 * own return address on top of whatever's already on the stack; the real
 * function's own standard epilogue (mov esp,ebp; pop ebp; ret) pops exactly
 * that top return address and hands control straight back to us - the real
 * original return address (pushed by the game's own `call CandB`) sits
 * untouched further down the stack the entire time, and our own final `ret`
 * pops it, returning control to the game exactly once, after both eye
 * passes have completed. This two-tier "call resolves inward" trick is what
 * lets the game's own render-dispatch function be invoked twice per frame
 * from a single interception point.
 */
__attribute__((naked)) void Hook_BuildViewMatrix(void)
{
    __asm__ __volatile__(
        "movl 4(%esp), %eax\n\t"   /* pOutMatrix */
        "movl 8(%esp), %ecx\n\t"   /* pEye */
        "movl 12(%esp), %edx\n\t"  /* pAt */
        "pushl 16(%esp)\n\t"       /* pUp (esp unchanged so far, offset still valid) */
        "pushl %edx\n\t"
        "pushl %ecx\n\t"
        "pushl %eax\n\t"
        "call BVM_OnEntry_asm\n\t"
        "addl $16, %esp\n\t"
        "jmp *BVM_Trampoline_asm\n\t"
    );
}

/* Same tail-jmp shape as Hook_BuildViewMatrix, adapted for
 * BuildProjectionMatrix(pOutMatrix, rawFov, Aspect, zn, zf) - all four raw
 * 4-byte float/stack args are read exactly as __cdecl already represents
 * them on the stack. notes/24 extends this to also read zn/zf (previously
 * only rawFov/Aspect were needed); the stack layout itself was already
 * established in notes/07 and is unchanged here - only which fields this
 * hook happens to read is new. */
__attribute__((naked)) void Hook_BuildProjectionMatrix(void)
{
    __asm__ __volatile__(
        "movl 8(%esp), %eax\n\t"   /* rawFov */
        "movl 12(%esp), %ecx\n\t"  /* Aspect */
        "movl 16(%esp), %edx\n\t"  /* zn */
        "pushl 20(%esp)\n\t"       /* zf (esp unchanged so far, offset still valid) */
        "pushl %edx\n\t"
        "pushl %ecx\n\t"
        "pushl %eax\n\t"
        "call BPM_OnEntry_asm\n\t"
        "addl $16, %esp\n\t"
        "jmp *BPM_Trampoline_asm\n\t"
    );
}

__attribute__((naked)) void Hook_CandB(void)
{
    __asm__ __volatile__(
        "movl %ecx, CandB_This_asm\n\t"
        "call CandB_BeforeEye1_asm\n\t"
        "movl CandB_This_asm, %ecx\n\t"
        "call *CandB_Trampoline_asm\n\t"
        "call CandB_BeforeEye2_asm\n\t"
        "movl CandB_This_asm, %ecx\n\t"
        "call *CandB_Trampoline_asm\n\t"
        "call CandB_AfterBoth_asm\n\t"
        "ret\n\t"
    );
}

/* Allocate an executable trampoline stub: a byte-for-byte copy of the
 * original function's first `patchLen` bytes, followed by a JMP back to
 * (originalFuncAddr + patchLen) to resume the function's own unpatched
 * body. Calling this stub (via `call`) behaves exactly like calling the
 * real, never-hooked function - it's the mechanism used both to let each
 * hook's own "real" invocation proceed, and to let arbitrary other code
 * (e.g. SetEyeAndTarget re-invoking BuildViewMatrix) call the pristine
 * original directly. */
static void *MakeTrampoline(BYTE *originalFuncAddr, int patchLen)
{
    BYTE *stub = (BYTE *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    DWORD relTarget;

    if (!stub) return NULL;

    memcpy(stub, originalFuncAddr, (size_t)patchLen);

    stub[patchLen] = 0xE9; /* JMP rel32 */
    relTarget = (DWORD)(originalFuncAddr + patchLen) - (DWORD)(stub + patchLen + 5);
    memcpy(stub + patchLen + 1, &relTarget, 4);

    return stub;
}

/* Overwrite the first `patchLen` (>=5) bytes of originalFuncAddr with
 * E9 <rel32 to hookFuncAddr>, padded with NOPs to patchLen bytes. */
static BOOL PatchJump(BYTE *originalFuncAddr, int patchLen, void *hookFuncAddr)
{
    DWORD oldProtect;
    DWORD relTarget;
    int i;

    if (!VirtualProtect(originalFuncAddr, (SIZE_T)patchLen, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        LogLine("ERROR: VirtualProtect failed patching 0x%p, err=%lu", (void *)originalFuncAddr, GetLastError());
        return FALSE;
    }

    originalFuncAddr[0] = 0xE9;
    relTarget = (DWORD)hookFuncAddr - (DWORD)(originalFuncAddr + 5);
    memcpy(originalFuncAddr + 1, &relTarget, 4);
    for (i = 5; i < patchLen; i++) originalFuncAddr[i] = 0x90;

    VirtualProtect(originalFuncAddr, (SIZE_T)patchLen, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), originalFuncAddr, (SIZE_T)patchLen);
    return TRUE;
}

/* Installs both inline hooks. Pure code-patching, needs no live D3D device,
 * so it's called once from DllMain(DLL_PROCESS_ATTACH) - the target
 * addresses are inside Psychonauts.exe's own already-loaded .text section
 * from the moment the process starts. */
static BOOL InstallInlineHooks(void)
{
    void *bvmTrampoline = MakeTrampoline(ADDR_BUILDVIEWMATRIX, INLINE_PATCH_LEN);
    void *bpmTrampoline = MakeTrampoline(ADDR_BUILDPROJMATRIX, INLINE_PATCH_LEN);
    void *candbTrampoline = MakeTrampoline(ADDR_CANDB, INLINE_PATCH_LEN);

    if (!bvmTrampoline || !bpmTrampoline || !candbTrampoline) {
        LogLine("ERROR: MakeTrampoline failed (bvm=%p bpm=%p candb=%p)", bvmTrampoline, bpmTrampoline, candbTrampoline);
        return FALSE;
    }

    BVM_Trampoline_asm = bvmTrampoline;
    BPM_Trampoline_asm = bpmTrampoline;
    CandB_Trampoline_asm = candbTrampoline;
    g_realBuildViewMatrix = (BuildViewMatrixFn)bvmTrampoline;

    if (!PatchJump(ADDR_BUILDVIEWMATRIX, INLINE_PATCH_LEN, (void *)Hook_BuildViewMatrix)) return FALSE;
    if (!PatchJump(ADDR_BUILDPROJMATRIX, INLINE_PATCH_LEN, (void *)Hook_BuildProjectionMatrix)) return FALSE;
    if (!PatchJump(ADDR_CANDB, INLINE_PATCH_LEN, (void *)Hook_CandB)) return FALSE;

    LogLine("Installed inline hooks: BuildViewMatrix @0x%p (trampoline=0x%p), BuildProjectionMatrix @0x%p (trampoline=0x%p), CandB @0x%p (trampoline=0x%p)",
            (void *)ADDR_BUILDVIEWMATRIX, bvmTrampoline, (void *)ADDR_BUILDPROJMATRIX, bpmTrampoline, (void *)ADDR_CANDB, candbTrampoline);
    return TRUE;
}

/* Creates the two offscreen "per-eye" render targets, matching the real
 * backbuffer's dimensions/format, and grabs+retains a reference to the real
 * backbuffer surface (needed by Hook_Present's composite step and by
 * CandB_AfterBoth_asm to restore the render target after both eye passes). */
static void SetupStereoSurfaces(IDirect3DDevice9 *pDevice, D3DPRESENT_PARAMETERS *pp)
{
    HRESULT hrBB, hrE1, hrE2, hrDS, hrDS1, hrDS2;

    g_pDevice = pDevice;
    g_bbWidth = pp->BackBufferWidth;
    g_bbHeight = pp->BackBufferHeight;

    hrBB = pDevice->lpVtbl->GetBackBuffer(pDevice, 0, 0, D3DBACKBUFFER_TYPE_MONO, &g_pRealBackBuffer);
    hrE1 = pDevice->lpVtbl->CreateRenderTarget(pDevice, g_bbWidth, g_bbHeight, D3DFMT_A8R8G8B8,
                                                D3DMULTISAMPLE_NONE, 0, FALSE, &g_pEye1Surf, NULL);
    hrE2 = pDevice->lpVtbl->CreateRenderTarget(pDevice, g_bbWidth, g_bbHeight, D3DFMT_A8R8G8B8,
                                                D3DMULTISAMPLE_NONE, 0, FALSE, &g_pEye2Surf, NULL);

    /* notes/22: capture the device's original auto depth-stencil surface (so it can be
     * restored after both eye passes) and give each eye its OWN private depth-stencil
     * surface matching the real one's format - see the comment on SetEyeAndTarget() for
     * why sharing a single depth buffer between both eyes was the real root cause of the
     * dark-eye/frozen-eye symptoms. Format comes from pp->AutoDepthStencilFormat (live-
     * confirmed 75/D3DFMT_D24S8), not guessed. Discard=TRUE matches typical depth-buffer
     * usage and is safe here since every eye pass unconditionally Clears its own depth
     * surface before drawing. */
    hrDS = pDevice->lpVtbl->GetDepthStencilSurface(pDevice, &g_pRealDepthStencil);
    hrDS1 = pDevice->lpVtbl->CreateDepthStencilSurface(pDevice, g_bbWidth, g_bbHeight,
                                                        pp->AutoDepthStencilFormat,
                                                        D3DMULTISAMPLE_NONE, 0, TRUE,
                                                        &g_pEye1DepthStencil, NULL);
    hrDS2 = pDevice->lpVtbl->CreateDepthStencilSurface(pDevice, g_bbWidth, g_bbHeight,
                                                        pp->AutoDepthStencilFormat,
                                                        D3DMULTISAMPLE_NONE, 0, TRUE,
                                                        &g_pEye2DepthStencil, NULL);

    LogLine("SetupStereoSurfaces: GetBackBuffer hr=0x%08lX ptr=0x%p | Eye1 hr=0x%08lX ptr=0x%p | Eye2 hr=0x%08lX ptr=0x%p (%ux%u) | "
            "GetDS hr=0x%08lX ptr=0x%p | Eye1DS hr=0x%08lX ptr=0x%p | Eye2DS hr=0x%08lX ptr=0x%p",
            (unsigned long)hrBB, (void *)g_pRealBackBuffer,
            (unsigned long)hrE1, (void *)g_pEye1Surf,
            (unsigned long)hrE2, (void *)g_pEye2Surf,
            g_bbWidth, g_bbHeight,
            (unsigned long)hrDS, (void *)g_pRealDepthStencil,
            (unsigned long)hrDS1, (void *)g_pEye1DepthStencil,
            (unsigned long)hrDS2, (void *)g_pEye2DepthStencil);

    g_stereoReady = SUCCEEDED(hrBB) && SUCCEEDED(hrE1) && SUCCEEDED(hrE2) &&
                    SUCCEEDED(hrDS1) && SUCCEEDED(hrDS2) &&
                    g_pRealBackBuffer && g_pEye1Surf && g_pEye2Surf &&
                    g_pEye1DepthStencil && g_pEye2DepthStencil;

    LogLine("Stereo ready = %d", g_stereoReady ? 1 : 0);
}

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

static HRESULT STDMETHODCALLTYPE Hook_SetVertexShaderConstantF(
    IDirect3DDevice9 *This,
    UINT StartRegister,
    CONST float *pConstantData,
    UINT Vector4fCount)
{
    if (g_stereoPhase != STEREO_PHASE_IDLE &&
        StartRegister == STEREO_WVP_REGISTER && Vector4fCount == 4 &&
        pConstantData != NULL && g_projXScaleValid) {

        float patched[16];
        float xScale = g_projXScale;
        float zn = g_projZNear;
        float zf = g_projZFar;
        float sign = (g_stereoPhase == STEREO_PHASE_EYE1) ? -1.0f : 1.0f;
        float d = STEREO_HALF_IPD * sign;
        float focus = g_focusDistance;
        /* notes/24: k = -d/focus - the eye-space shear that puts zero
         * disparity exactly at the convergence distance, see the
         * derivation comment above STEREO_WVP_REGISTER. */
        float k = (-d) / focus;
        float A = zf / (zn - zf);
        float B = zn * zf / (zn - zf);
        float Y20 = (-d) * xScale / B;
        float Y30 = (-k - A * d / B) * xScale;
        int r;

        memcpy(patched, pConstantData, sizeof(patched));
        /* notes/24: full off-axis column-0 patch - reduces exactly to
         * notes/18's single-entry translation patch when Y20==0 (k==0).
         * See the derivation comment above STEREO_WVP_REGISTER for the
         * flat-index mapping (WVP column 0 -> upload row 0, flat 0..3;
         * WVP[r][2]/[r][3] -> upload flat 8+r/12+r). */
        for (r = 0; r < 4; r++) {
            patched[r] = pConstantData[r] + pConstantData[8 + r] * Y20 + pConstantData[12 + r] * Y30;
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
                LogLine("SVSCF stereo-correct: reg=%u phase=%ld xScale=%.4f d=%.3f focus=%.2f k=%.6f Y20=%.6f Y30=%.4f",
                        StartRegister, g_stereoPhase, xScale, d, focus, k, Y20, Y30);
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
        RECT srcFull;
        RECT dstLeft;
        RECT dstRight;
        HRESULT hrL, hrR;

        srcFull.left = 0; srcFull.top = 0; srcFull.right = (LONG)g_bbWidth; srcFull.bottom = (LONG)g_bbHeight;
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
    return g_pRealPresent(This, NULL, NULL, hDestWindowOverride, NULL);
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

    hr = g_pRealReset(This, pPresentationParameters);

    LogLine("Real Reset returned hr=0x%08lX", (unsigned long)hr);

    if (SUCCEEDED(hr) && pPresentationParameters) {
        SetupStereoSurfaces(This, pPresentationParameters);
    }

    return hr;
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

    VirtualProtect(vtbl, sizeof(*vtbl), oldProtect, &oldProtect);

    g_deviceHooked = TRUE;
    LogLine("Hooked IDirect3DDevice9::Present (vtable slot 17), original=0x%p; ::Reset (vtable slot 16), original=0x%p",
            (void *)g_pRealPresent, (void *)g_pRealReset);
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
        InstallSetVSConstFHook(*ppReturnedDeviceInterface);
        if (pPresentationParameters) {
            SetupStereoSurfaces(*ppReturnedDeviceInterface, pPresentationParameters);
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
    (void)lpvReserved;

    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        InitializeCriticalSection(&g_logLock);
        g_logLockInit = TRUE;
        LogLine("==== psychonautsvr proxy d3d9.dll: DLL_PROCESS_ATTACH (pid=%lu) ====", GetCurrentProcessId());
        if (!InstallInlineHooks()) {
            LogLine("ERROR: InstallInlineHooks failed - stereo prototype disabled, proxy still runs as pure observer");
        }
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
