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
 * Milestone 8 (this revision, notes/28): VR SUBMISSION PATH (additive, off by default).
 * notes/27 proved the full D3D9Ex-shared-surface -> D3D11 -> IVRCompositor::Submit bridge works
 * end-to-end via standalone POCs, but explicitly flagged its sync mechanism (a synchronous
 * GPU-flush stall) as unfit for a real per-frame hot path. This session (notes/28) first replaced
 * that with a proven non-blocking double-buffered technique (tools/vr-bridge/poc_submit_test),
 * then discovered - empirically, via a second standalone POC
 * (tools/vr-bridge/poc_dual_device_shared) - a real architectural constraint that changes the
 * whole integration design: Psychonauts' own D3D9 device (the one this file hooks) is a PLAIN
 * (non-Ex) device, created via Direct3DCreate9 -> IDirect3D9::CreateDevice (never CreateDeviceEx -
 * confirmed by this file's own hooks, which only ever patch IDirect3D9::CreateDevice). Only a
 * D3D9Ex device's CreateTexture can produce a shared handle that ID3D11Device::OpenSharedResource
 * can open. A plain device CANNOT open a handle an Ex device originated (confirmed empirically:
 * hr=0x8876086C/D3DERR_INVALIDCALL, tried both as a direct render-target and as a StretchRect
 * destination) - so this file's own device can never render directly into anything D3D11/OpenVR
 * can see.
 *
 * The working design (proven in poc_dual_device_shared, 3/3 clean runs): a SEPARATE, private
 * D3D9Ex device ("Device A" below) is created by this DLL, matched to the same physical adapter
 * as the game's device via LUID. Each real frame, for each eye: the game's already-rendered
 * private eye surface (g_pEye1Surf/g_pEye2Surf - UNCHANGED, still used for the existing monitor
 * composite) is copied to the CPU via GetRenderTargetData (on the game's own device), then
 * memcpy'd and UpdateSurface'd into Device A's own D3DPOOL_DEFAULT shared texture (both hops
 * individually required to stay on one device each - a real MSDN constraint, not a design choice).
 * D3D11 opens Device A's shared texture and hands it to IVRCompositor::Submit.
 *
 * This CPU round trip turned out to need TWO independent non-blocking completion fences, not one:
 * GetRenderTargetData's own readback (checked via IDirect3DSurface9::LockRect with
 * D3DLOCK_DONOTWAIT) AND Device A's subsequent UpdateSurface, which is itself an async GPU upload
 * needing its own fence (an IDirect3DQuery9 D3DQUERYTYPE_EVENT on Device A, checked non-blockingly)
 * before a downstream reader touches it - discovered by a real, reproducible mismatch (a stale/
 * torn frame reaching D3D11) when only the first fence was implemented, root-caused and fixed with
 * a diagnostic blocking-flush test that confirmed the hypothesis before the real fix was written.
 * Both hops are double-buffered exactly like notes/28 Part 1's proven single-hop design, so the
 * steady-state per-frame cost is a small, fixed number of non-blocking Lock/GetData calls - never
 * a wait - see the full derivation and real timing numbers in notes/28.
 *
 * ADDITIVE BY DESIGN, OFF BY DEFAULT: every new symbol below is prefixed VRBridge_/g_vr, touches
 * NONE of the existing eye-surface/depth-stencil/composite code, and the entire path is gated
 * behind a runtime environment-variable flag (PSYVR_ENABLE_SUBMIT=1) read once at DllMain and
 * checked before any VR-specific work happens anywhere - with the flag unset (the default), this
 * file's behavior is byte-for-byte identical to before this milestone. Even with the flag set, any
 * failure at any VR bridge init/per-frame step (SteamVR absent, OpenVR call failure, etc.) simply
 * leaves g_vrBridgeReady FALSE and the existing monitor-composite path continues completely
 * unaffected - the VR path can never take down or degrade the proven working fallback.
 *
 * NOT YET LIVE-TESTED against the real game (needs SteamVR's null driver + a relaunch to pick up
 * this DLL - out of scope for this session per the task's own safety rule about the user's already-
 * running game session). See notes/28 for exactly what's proven standalone vs. what's still open.
 *
 * Milestone 9 (this revision, notes/32): REAL OPENVR-QUERIED IPD/PROJECTION DATA, replacing the
 * notes/24 hardcoded STEREO_HALF_IPD constant and estimated g_focusDistance shear term wherever
 * real data is actually available. Confirmed via a standalone POC (tools/vr-bridge/poc_ipd_query,
 * run against the live null-driver runtime BEFORE this code was written) that IVRSystem returns
 * real, plausible values even with no physical HMD: IPD = 63.0mm exactly (the standard OpenVR
 * default - only ~3% below the notes/15 hardcoded 3.25-unit guess), GetEyeToHeadTransform exactly
 * +/-31.5mm (perfectly symmetric under this driver), GetRecommendedRenderTargetSize = 1656x1840/
 * eye, and GetProjectionRaw = [-1,1,-1,1] for both eyes (i.e. this driver reports NO real off-axis
 * asymmetry - confirmed by measurement, not assumed).
 *
 * VRBridge_QueryRealGeometry() (called once from VRBridge_Init, right after IVRSystem is ready)
 * converts the meters-based IPD/eye-offset values into this game's world-unit scale via
 * WORLD_UNITS_PER_METER=100 (the notes/15/18-established "1 world unit ~= 1cm" calibration) and
 * caches them in g_realHalfIPD[]/g_realShearK[]/g_realShearValid[]. Hook_SetVertexShaderConstantF
 * now uses these directly in place of STEREO_HALF_IPD/the focus-distance-derived k whenever
 * g_vrGeomValid is TRUE - and transparently falls back to the pre-existing hardcoded behavior
 * otherwise (OpenVR not initialized, i.e. PSYVR_ENABLE_SUBMIT unset or SteamVR absent - the
 * default case for most users of this mod, who don't have or need SteamVR just for the monitor
 * side-by-side stereo path). This can never make the stereo correction worse than before this
 * session, and the real per-eye offset (g_realHalfIPD, from GetEyeToHeadTransform) need not be
 * exactly symmetric the way STEREO_HALF_IPD*sign always was, once a real headset supplies it.
 *
 * GetEyeToHeadTransform returns a struct (HmdMatrix34_t) BY VALUE - a new ABI wrinkle none of the
 * prior IVRCompositor calls hit (WaitGetPoses/Submit both take/return plain values or take output
 * pointers). Handled the same defensive way notes/27 established for this whole file: don't trust
 * a compiler's own struct-return code generation against MSVC-compiled code, declare the hidden
 * return-pointer parameter explicitly instead. See the comment above VRBridge_GetEyeToHeadTransform_t.
 *
 * GetRecommendedRenderTargetSize's real 1656x1840/eye (vs. this file's current 640x480 VR-submit
 * eye buffers, sized to match the game's own native backbuffer) was investigated for feasibility
 * and found to be a genuinely separate, larger undertaking - not attempted this session, documented
 * in notes/32 Sec4 instead of risked as a rushed mid-pipeline change.
 *
 * Build target: 32-bit (i686), matching the 32-bit Psychonauts.exe.
 */

#define INITGUID
#include <windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <ctype.h>
#include "../vr-bridge/openvr-sdk/headers/openvr_capi.h"
/* Do NOT include <stdbool.h> - openvr_capi.h typedefs its own bool as char on Windows, which
 * collides with <stdbool.h>'s #define bool _Bool if both are included in the same translation
 * unit (documented in tools/vr-bridge/poc_openvr_init/openvr_init_poc.c, notes/25 Sec2). */

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

/* Moved up from the "Stereo prototype" section below (notes/28) so the VR bridge code that
 * immediately follows - which reads/writes these same globals (g_pDevice, g_pEye1Surf/2Surf,
 * g_bbWidth/Height) - can reference them without a forward-declaration dance. No functional
 * change: same globals, same lifecycle, just declared earlier in the file. */
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
/* notes/35: eye render targets can now be LARGER than the backbuffer (the VR submit path was
 * shipping the game's 800x600/640x480 upscaled to a headset wanting ~2496x2688/eye - notes/33
 * Sec5). g_eyeScale multiplies the backbuffer dims (same aspect - the projection math is
 * untouched); the monitor composite StretchRects back DOWN to the backbuffer halves and the VR
 * buffers are sized to the eye surfaces. Scale=1 keeps every path byte-identical to before. */
static UINT g_eyeScale = 1;   /* PSYVR_RENDER_SCALE (1-4); defaults to 2 when PSYVR_ENABLE_SUBMIT=1, else 1 */
static UINT g_eyeWidth = 0;   /* actual eye render-target dims = bb dims * g_eyeScale */
static UINT g_eyeHeight = 0;
static BOOL g_stereoReady = FALSE; /* device + both offscreen color+depth surfaces created OK */

