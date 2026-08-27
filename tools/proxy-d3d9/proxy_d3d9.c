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

/* Reads PSYVR_ENABLE_SUBMIT once. Default OFF - the whole VR path is inert unless explicitly
 * requested, per this session's explicit "don't risk destabilizing the working monitor path"
 * requirement. Set the env var to "1" (e.g. before launching the game) to enable. */
static void VRBridge_ReadEnableFlag(void)
{
    char buf[8];
    DWORD len = GetEnvironmentVariableA("PSYVR_ENABLE_SUBMIT", buf, sizeof(buf));
    g_vrSubmitEnabled = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    LogLine("VRBridge: PSYVR_ENABLE_SUBMIT=%s -> g_vrSubmitEnabled=%d",
            (len > 0 && len < sizeof(buf)) ? buf : "(unset)", g_vrSubmitEnabled ? 1 : 0);

    /* notes/31 diagnostic-only A/B toggles - see the comment on g_vrSkipWaitPoses above. */
    len = GetEnvironmentVariableA("PSYVR_SKIP_WAITPOSES", buf, sizeof(buf));
    g_vrSkipWaitPoses = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    len = GetEnvironmentVariableA("PSYVR_SKIP_PUMPEYE", buf, sizeof(buf));
    g_vrSkipPumpEye = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    len = GetEnvironmentVariableA("PSYVR_DISABLE_TRACKING", buf, sizeof(buf));
    g_trackingDisabled = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    len = GetEnvironmentVariableA("PSYVR_FAKE_POSE", buf, sizeof(buf));
    g_fakePose = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    /* notes/59: fake-pose yaw amplitude override, degrees, 0..175 (stay short of 180 - the sin/cos
     * construction is well-defined there but a full flip is not a meaningful "look behind" test). */
    len = GetEnvironmentVariableA("PSYVR_FAKE_POSE_YAW_DEG", buf, sizeof(buf));
    if (len > 0 && len < sizeof(buf)) {
        float degv = 0.0f;
        if (sscanf(buf, "%f", &degv) == 1 && degv >= 0.0f && degv <= 175.0f)
            g_fakePoseYawAmpRad = degv * 0.017453293f;
    }
    if (g_fakePose)
        LogLine("VRBridge: fake-pose yaw amplitude = %.1f deg (PSYVR_FAKE_POSE_YAW_DEG, default 25.2)",
                g_fakePoseYawAmpRad * 57.29578f);

    /* notes/59 part 2: F12 level-jump opt-in + optional level-code override. */
    len = GetEnvironmentVariableA("PSYVR_LEVEL_JUMP_KEY", buf, sizeof(buf));
    g_levelJumpKeyEnabled = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_levelJumpKeyEnabled) {
        char codeBuf[32];
        len = GetEnvironmentVariableA("PSYVR_LEVEL_JUMP_CODE", codeBuf, sizeof(codeBuf));
        if (len > 0 && len < sizeof(codeBuf)) {
            _snprintf(g_levelJumpCode, sizeof(g_levelJumpCode), "workresource\\levels\\%s.plb", codeBuf);
            g_levelJumpCode[sizeof(g_levelJumpCode) - 1] = '\0';
        }
        LogLine("VRBridge: PSYVR_LEVEL_JUMP_KEY=1 - F12 will call SetPendingLevel(\"%s\")", g_levelJumpCode);
    }

    /* notes/67: external automation harness opt-in. Commands arrive in
     * psyvr_automation_cmds.txt next to Psychonauts.exe; results and camera
     * telemetry go to the normal proxy log. Default off - this drives real
     * engine state and is not something to leave live by accident. */
    len = GetEnvironmentVariableA("PSYVR_AUTOMATION", buf, sizeof(buf));
    g_automationEnabled = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_automationEnabled) {
        char exePath[MAX_PATH];
        char *slash;
        if (GetModuleFileNameA(NULL, exePath, sizeof(exePath))) {
            slash = strrchr(exePath, '\\');
            if (slash) {
                *(slash + 1) = '\0';
                _snprintf(g_autoCmdPath, sizeof(g_autoCmdPath), "%spsyvr_automation_cmds.txt",
                          exePath);
                g_autoCmdPath[sizeof(g_autoCmdPath) - 1] = '\0';
            }
        }
        if (g_autoCmdPath[0]) {
            FILE *cf = fopen(g_autoCmdPath, "wb");   /* clear stale commands */
            if (cf) fclose(cf);
            /* sscanf, not atoi: stdlib.h is not among this file's includes. */
            len = GetEnvironmentVariableA("PSYVR_AUTOMATION_POLL_MS", buf, sizeof(buf));
            if (len > 0 && len < sizeof(buf)) {
                int v = 0;
                if (sscanf(buf, "%d", &v) == 1 && v >= 0) g_autoPollMs = (DWORD)v;
            }
            len = GetEnvironmentVariableA("PSYVR_AUTOMATION_TELEM_MS", buf, sizeof(buf));
            if (len > 0 && len < sizeof(buf)) {
                int v = 0;
                if (sscanf(buf, "%d", &v) == 1 && v >= 0) g_autoTelemetryMs = (DWORD)v;
            }
            LogLine("VRBridge: PSYVR_AUTOMATION=1 - commands from \"%s\" (poll=%lums telem=%lums). "
                    "Try: status | level CAJA | campos <x> <y> <z> | cammove <dx> <dy> <dz> | "
                    "camhold 0|1 | flag <id> <0|1>",
                    g_autoCmdPath, g_autoPollMs, g_autoTelemetryMs);
        } else {
            g_automationEnabled = FALSE;
            LogLine("VRBridge: PSYVR_AUTOMATION=1 but the command-file path could not be built - "
                    "automation inert");
        }
    }

    /* notes/62: NUMPAD9 Visibility Tree Culling flag toggle opt-in. */
    len = GetEnvironmentVariableA("PSYVR_CULL_TOGGLE_KEY", buf, sizeof(buf));
    g_cullToggleKeyEnabled = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_cullToggleKeyEnabled)
        LogLine("VRBridge: PSYVR_CULL_TOGGLE_KEY=1 - NUMPAD9 will flip the Visibility Tree "
                "Culling flag (engine+%d+%d)", DEBUG_FLAGS_ARRAY_OFFSET, VISTREE_CULLING_ITEM_ID);

    /* notes/64: NUMPAD8 Render Wireframe flag toggle opt-in. */
    len = GetEnvironmentVariableA("PSYVR_WIREFRAME_TOGGLE_KEY", buf, sizeof(buf));
    g_wireframeToggleKeyEnabled = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_wireframeToggleKeyEnabled)
        LogLine("VRBridge: PSYVR_WIREFRAME_TOGGLE_KEY=1 - NUMPAD8 will flip the Render "
                "Wireframe flag (engine+%d+%d)", DEBUG_FLAGS_ARRAY_OFFSET, RENDER_WIREFRAME_ITEM_ID);

    /* notes/65: NUMPAD7 Collision Wireframe flag toggle opt-in. */
    len = GetEnvironmentVariableA("PSYVR_COLLISION_WIREFRAME_TOGGLE_KEY", buf, sizeof(buf));
    g_collisionWireframeToggleKeyEnabled = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_collisionWireframeToggleKeyEnabled)
        LogLine("VRBridge: PSYVR_COLLISION_WIREFRAME_TOGGLE_KEY=1 - NUMPAD7 will flip the "
                "Collision Wireframe flag (engine+%d+%d)", DEBUG_FLAGS_ARRAY_OFFSET, COLLISION_WIREFRAME_ITEM_ID);

    /* notes/66: NUMPAD6 Collision Spheres flag toggle opt-in (positive-control tool). */
    len = GetEnvironmentVariableA("PSYVR_COLLISION_SPHERES_TOGGLE_KEY", buf, sizeof(buf));
    g_collisionSpheresToggleKeyEnabled = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_collisionSpheresToggleKeyEnabled)
        LogLine("VRBridge: PSYVR_COLLISION_SPHERES_TOGGLE_KEY=1 - NUMPAD6 will flip the "
                "Collision Spheres flag (engine+%d+%d)", DEBUG_FLAGS_ARRAY_OFFSET, COLLISION_SPHERES_ITEM_ID);

    /* notes/65: opt-in auto-pause suppression - see Hook_GameWndProc/PatchIATEntry comments. Does
     * BOTH the WndProc message-swallow (cheap, harmless, catches whatever DOES come through as a
     * message) AND the GetForegroundWindow IAT patch (the fix that actually matters, per this
     * session's live evidence that the pause survived with only one spurious message ever
     * swallowed - so it's very likely a per-tick poll, not message-driven). */
    len = GetEnvironmentVariableA("PSYVR_SUPPRESS_AUTOPAUSE", buf, sizeof(buf));
    g_suppressAutoPause = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_suppressAutoPause) {
        LogLine("VRBridge: PSYVR_SUPPRESS_AUTOPAUSE=1 - will subclass the game window to swallow "
                "focus-loss messages once CreateDevice provides a window handle");
        BOOL iatOk = PatchIATEntry("user32.dll", "GetForegroundWindow", (void *)Hook_GetForegroundWindow);
        LogLine("AutoPauseSuppress: GetForegroundWindow IAT patch %s", iatOk ? "OK" : "FAILED (entry not found)");
    }

    len = GetEnvironmentVariableA("PSYVR_HT_TEST_SHIFT", buf, sizeof(buf));
    if (len > 0 && len < sizeof(buf)) g_htTestShift = (float)atof(buf);
    len = GetEnvironmentVariableA("PSYVR_HT_DEBUG", buf, sizeof(buf));
    g_htDebug = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_htTestShift != 0.0f || g_htDebug)
        LogLine("VRBridge: notes/52 composition-order test - PSYVR_HT_TEST_SHIFT=%.1f PSYVR_HT_DEBUG=%d",
                g_htTestShift, g_htDebug ? 1 : 0);
    len = GetEnvironmentVariableA("PSYVR_FP_FORCE_ACTIVE", buf, sizeof(buf));
    g_fpForceActive = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_fpForceActive) LogLine("VRBridge: PSYVR_FP_FORCE_ACTIVE=1 - FP active from startup (notes/52 X1 isolation test)");
    len = GetEnvironmentVariableA("PSYVR_FP_FORCE_RAZ", buf, sizeof(buf));
    if (len > 0 && len < sizeof(buf)) {
        float dx = 0, dy = 0, dz = 0;
        if (sscanf(buf, "%f,%f,%f", &dx, &dy, &dz) == 3) {
            g_razForceValid = TRUE;
            g_razForceOffX = dx; g_razForceOffY = dy; g_razForceOffZ = dz;
            LogLine("VRBridge: PSYVR_FP_FORCE_RAZ=%.1f,%.1f,%.1f - razWorld forced to baseEye+offset each frame (notes/52)", dx, dy, dz);
        }
    }
    len = GetEnvironmentVariableA("PSYVR_RAZLOCK_STATS", buf, sizeof(buf));
    g_razLockStats = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_razLockStats) LogLine("VRBridge: PSYVR_RAZLOCK_STATS=1 - logging Raz-lock hit rate / loss events each second (notes/53)");

    /* notes/47: first-person prototype. */
    len = GetEnvironmentVariableA("PSYVR_FIRST_PERSON", buf, sizeof(buf));
    g_firstPerson = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    {
        char fb[16];
        len = GetEnvironmentVariableA("PSYVR_FP_FORWARD", fb, sizeof(fb));
        if (len > 0 && len < sizeof(fb)) {
            float v = 0.0f;
            if (sscanf(fb, "%f", &v) == 1 && v >= 0.0f && v <= 4.0f) g_fpForward = v;
        }
        len = GetEnvironmentVariableA("PSYVR_FP_HEIGHT", fb, sizeof(fb));
        if (len > 0 && len < sizeof(fb)) {
            float v = 0.0f;
            if (sscanf(fb, "%f", &v) == 1 && v > -500.0f && v < 500.0f) g_fpHeight = v;
        }
        len = GetEnvironmentVariableA("PSYVR_FP_SMOOTH", fb, sizeof(fb));
        if (len > 0 && len < sizeof(fb)) {
            float v = 0.0f;
            if (sscanf(fb, "%f", &v) == 1 && v >= 0.02f && v <= 1.0f) g_fpSmooth = v;
        }
    }
    len = GetEnvironmentVariableA("PSYVR_FP_PROBE", buf, sizeof(buf));
    g_fpProbe = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_fpProbe) LogLine("VRBridge: PSYVR_FP_PROBE=1 - logging recovered Raz world origin vs at/eye (notes/49)");
    if (g_firstPerson)
        LogLine("VRBridge: FIRST PERSON on (PSYVR_FIRST_PERSON) - forward=%.2f x eye->at, height=%.1fwu; Raz not hidden yet (notes/47)",
                g_fpForward, g_fpHeight);
    if (g_trackingDisabled || g_fakePose) {
        LogLine("VRBridge: tracking flags - g_trackingDisabled=%d g_fakePose=%d",
                g_trackingDisabled ? 1 : 0, g_fakePose ? 1 : 0);
    }

    len = GetEnvironmentVariableA("PSYVR_DUMP_EYES", buf, sizeof(buf));
    g_dumpEyes = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_dumpEyes) LogLine("VRBridge: PSYVR_DUMP_EYES=1 - eye/backbuffer BMP dumps every ~5s to %%TEMP%%");
    len = GetEnvironmentVariableA("PSYVR_TRACE_FRAME", buf, sizeof(buf));
    g_traceFrames = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_traceFrames) LogLine("VRBridge: PSYVR_TRACE_FRAME=1 - one-frame RT/Clear/draw traces every ~5s");
    len = GetEnvironmentVariableA("PSYVR_REG_HISTO", buf, sizeof(buf));
    g_regHisto = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    len = GetEnvironmentVariableA("PSYVR_SHADER_DUMP", buf, sizeof(buf));
    if (len > 0 && len < sizeof(buf) && buf[0] == '1') g_regHisto = TRUE; /* notes/52: needs the dump-to-disk machinery on */
    len = GetEnvironmentVariableA("PSYVR_BONE_PROBE", buf, sizeof(buf));
    g_boneProbe = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_boneProbe) LogLine("VRBridge: PSYVR_BONE_PROBE=1 - throttled bone-palette upload logging (notes/44 recon)");
    len = GetEnvironmentVariableA("PSYVR_BONE_DUMP", buf, sizeof(buf));
    g_boneDump = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_boneDump) LogLine("VRBridge: PSYVR_BONE_DUMP=1 - shoulder-anchor bone hunt: per-draw origins + 32-bone translations (notes/51)");
    len = GetEnvironmentVariableA("PSYVR_SHADER_DUMP", buf, sizeof(buf));
    g_shaderDump = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_shaderDump) LogLine("VRBridge: PSYVR_SHADER_DUMP=1 - will identify which PSYVR_REG_HISTO .bin dump is the skinned (c96) world shader, for offline disassembly (notes/52, playbook Phase 3.3)");
    if (g_regHisto) LogLine("VRBridge: PSYVR_REG_HISTO=1 - vertex-shader-constant register histogram every ~5s");

    {
        char dbuf[16];
        len = GetEnvironmentVariableA("PSYVR_UI_DEPTH", dbuf, sizeof(dbuf));
        if (len > 0 && len < sizeof(dbuf)) {
            float v = 0.0f;
            if (sscanf(dbuf, "%f", &v) == 1 && v >= 0.0f && v < 100000.0f)
                g_uiDepthWorld = v;
        }
        LogLine("VRBridge: UI depth = %.0f world units (PSYVR_UI_DEPTH; 0 = UI stays at infinity)", g_uiDepthWorld);

        /* notes/37: FOV multiplier, clamped to a sane range. 1.0 = untouched. */
        /* notes/59: ceiling temporarily raised 2.5->4.0 to test candidate-3 (widen the cull/render
         * margin further) against the void-behind-player bug, per notes/40's mitigation #3 - this
         * is exploratory headroom for that specific test, not a claim that values this high are a
         * good shipping default (see notes/59 for the GPU-cost tradeoff and actual measured result). */
        len = GetEnvironmentVariableA("PSYVR_FOV_SCALE", dbuf, sizeof(dbuf));
        if (len > 0 && len < sizeof(dbuf)) {
            float v = 0.0f;
            if (sscanf(dbuf, "%f", &v) == 1 && v >= 0.5f && v <= 4.0f)
                g_fovScaleAsm = v;
        }
        LogLine("VRBridge: FOV scale = %.2f (PSYVR_FOV_SCALE, 0.5..4.0 - ceiling raised from 2.5 "
                "for notes/59's void-test headroom; 1.0 = game default)", g_fovScaleAsm);

        /* notes/43: EXPERIMENTAL per-draw UI viewport shrink, OFF by default (1.0). The notes/42
         * auto-shrink idea failed live: the game's fullscreen overlays (fades, pause/menu
         * backdrops, brightness passes) share the exact UI shader signature, so shrinking every
         * UI draw crushed them into the frame center ("everything dark"). Left as an opt-in
         * absolute factor until a per-draw fullscreen-vs-element classifier exists. */
        len = GetEnvironmentVariableA("PSYVR_UI_SCALE", dbuf, sizeof(dbuf));
        if (len > 0 && len < sizeof(dbuf)) {
            float v = 0.0f;
            if (sscanf(dbuf, "%f", &v) == 1 && v >= 0.25f && v <= 1.0f)
                g_uiVpScale = v;
        }
        if (g_uiVpScale != 1.0f)
            LogLine("VRBridge: EXPERIMENTAL UI viewport shrink = %.2f (PSYVR_UI_SCALE; known to break fullscreen overlays)", g_uiVpScale);

        /* notes/43: tangent-matched submit crop, ON by default whenever real HMD geometry is
         * available (see VRBridge_SubmitBounds). PSYVR_SUBMIT_BOUNDS=0 restores the old
         * full-texture submit (compositor stretches the frame onto the lens frustum = zoom). */
        len = GetEnvironmentVariableA("PSYVR_SUBMIT_BOUNDS", dbuf, sizeof(dbuf));
        if (len > 0 && len < sizeof(dbuf) && dbuf[0] == '0')
            g_submitBoundsEnabled = FALSE;
        LogLine("VRBridge: submit bounds = %s (PSYVR_SUBMIT_BOUNDS; tangent-matched crop kills the FOV-scale zoom)",
                g_submitBoundsEnabled ? "ON" : "OFF");
    }

    /* notes/35: eye render scale. Default 2x when the VR path is on (the headset wants far more
     * than the game's native resolution), 1x otherwise (monitor-only users keep byte-identical
     * behavior). PSYVR_RENDER_SCALE=1..4 overrides either way. */
    g_eyeScale = g_vrSubmitEnabled ? 2 : 1;
    len = GetEnvironmentVariableA("PSYVR_RENDER_SCALE", buf, sizeof(buf));
    if (len > 0 && len < sizeof(buf) && buf[0] >= '1' && buf[0] <= '4') {
        g_eyeScale = (UINT)(buf[0] - '0');
    }
    LogLine("VRBridge: eye render scale = %ux (PSYVR_RENDER_SCALE%s)", g_eyeScale,
            (len > 0) ? " set" : " defaulted");
    if (g_vrSkipWaitPoses || g_vrSkipPumpEye) {
        LogLine("VRBridge: diagnostic bypass flags - g_vrSkipWaitPoses=%d g_vrSkipPumpEye=%d",
                g_vrSkipWaitPoses ? 1 : 0, g_vrSkipPumpEye ? 1 : 0);
    }
}

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
        g_trackRefValid = TRUE;
        LogLine("HeadTrack: reference captured - pos=(%.3f,%.3f,%.3f)m yaw=%.1fdeg%s",
                pose[12], pose[13], pose[14], yaw * 57.29578f, g_fakePose ? " (FAKE POSE MODE)" : "");
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

void __cdecl CandB_BeforeEye1_asm(void) asm("CandB_BeforeEye1_asm");
void __cdecl CandB_BeforeEye1_asm(void)
{
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
}

/* ===================================================================== *
 * notes/67: external automation harness
 *
 * Lets commands be delivered from OUTSIDE the process: append a line to
 * psyvr_automation_cmds.txt next to Psychonauts.exe, and this reads it,
 * runs it, and truncates the file. No synthetic input, no hotkey, no
 * rebuild to change a level code - so an unattended session can drive the
 * game after the user has launched it.
 *
 * This is the XIII harness pattern ported over, including the two lessons
 * that cost that project a crash and a debugging round:
 *
 *  1. NEVER dispatch a real engine COMMAND from a render-path hook. XIII
 *     GPF'd doing exactly that (its queue drained from a camera hook that
 *     turned out to be called from inside the engine's Draw). We are on
 *     the render path here too - CandB_AfterBoth runs mid-frame - so this
 *     harness deliberately only does work that is already proven safe from
 *     this exact site: SetPendingLevel (stages an async request; the F12
 *     path has done this since notes/59), debug-flag byte pokes (notes/62,
 *     64), and plain field writes on the camera object. It does NOT call
 *     into the Lua interpreter or any general command dispatcher. If that
 *     is ever wanted, it needs a game-logic tick site found first.
 *  2. Log BEFORE the call, not after. XIII's first version logged on
 *     completion, so its crash left no record of which command died and it
 *     had to be inferred. LogLine already opens/appends/closes per call,
 *     so every line is on disk before the next one runs.
 *
 * Camera access is the recon/2026-08-27-camera-control-without-lua finding:
 * the Lua bindings are a marshalling shim over plain engine work, so the
 * camera is reachable with a guard + an accessor + three float stores.
 * ===================================================================== */
/* Guard + accessor on the engine singleton, both __thiscall. Returns NULL
 * whenever the engine says there is no usable camera right now (loading,
 * menus), so every caller fails soft rather than writing into nothing. */
static void *AutoGetCamera(void)
{
    void *engine = *(void **)0x78BC20;
    if (!engine) return NULL;
    if (!((BOOL (__thiscall *)(void *))0x504220)(engine)) return NULL;
    return ((void *(__thiscall *)(void *))0x4FA5A0)(engine);
}

static BOOL AutoReadCameraPos(float out[3])
{
    void *cam = AutoGetCamera();
    if (!cam) return FALSE;
    memcpy(out, (char *)cam + PSY_CAM_POS_OFFSET, sizeof(float) * 3);
    return TRUE;
}

static BOOL AutoWriteCameraPos(const float p[3])
{
    void *cam = AutoGetCamera();
    if (!cam) return FALSE;
    memcpy((char *)cam + PSY_CAM_POS_OFFSET, p, sizeof(float) * 3);
    *((unsigned char *)cam + PSY_CAM_DIRTY_OFFSET) |= 1;
    return TRUE;
}

static void AutoRunCommand(const char *cmd)
{
    float pos[3];
    char  codeBuf[64];
    float a = 0.0f, b = 0.0f, c = 0.0f;
    int   id = 0, val = 0;

    LogLine("Auto: BEGIN \"%s\"", cmd);

    if (_strnicmp(cmd, "level ", 6) == 0) {
        void *engine = *(void **)0x78BC20;
        if (sscanf(cmd + 6, "%31s", codeBuf) == 1 && engine) {
            char path[128];
            _snprintf(path, sizeof(path), "workresource\\levels\\%s.plb", codeBuf);
            path[sizeof(path) - 1] = '\0';
            ((void (__thiscall *)(void *, const char *, BOOL))0x4FFA40)(engine, path, TRUE);
            LogLine("Auto: END   \"%s\" -> SetPendingLevel(\"%s\")", cmd, path);
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (bad code or engine NULL)", cmd);
        }

    } else if (_strnicmp(cmd, "campos ", 7) == 0) {
        if (sscanf(cmd + 7, "%f %f %f", &a, &b, &c) == 3) {
            pos[0] = a; pos[1] = b; pos[2] = c;
            g_camHoldPos[0] = a; g_camHoldPos[1] = b; g_camHoldPos[2] = c;
            LogLine("Auto: END   \"%s\" -> %s", cmd,
                    AutoWriteCameraPos(pos) ? "written" : "FAILED (no camera)");
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: campos <x> <y> <z>)", cmd);
        }

    } else if (_strnicmp(cmd, "cammove ", 8) == 0) {
        if (sscanf(cmd + 8, "%f %f %f", &a, &b, &c) == 3 && AutoReadCameraPos(pos)) {
            pos[0] += a; pos[1] += b; pos[2] += c;
            g_camHoldPos[0] = pos[0]; g_camHoldPos[1] = pos[1]; g_camHoldPos[2] = pos[2];
            LogLine("Auto: END   \"%s\" -> %s, now %.1f %.1f %.1f", cmd,
                    AutoWriteCameraPos(pos) ? "written" : "FAILED",
                    pos[0], pos[1], pos[2]);
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (bad args or no camera)", cmd);
        }

    } else if (_strnicmp(cmd, "lookhold ", 9) == 0) {
        g_lookHold = (cmd[9] == '1');
        LogLine("Auto: END   \"%s\" -> lookhold=%d dir %.4f %.4f %.4f", cmd, (int)g_lookHold,
                g_lookHoldDir[0], g_lookHoldDir[1], g_lookHoldDir[2]);

    } else if (_strnicmp(cmd, "camhold ", 8) == 0) {
        g_camHold = (cmd[8] == '1');
        if (g_camHold && !AutoReadCameraPos(g_camHoldPos))
            LogLine("Auto: camhold on, but no camera yet - target stays as last set");
        LogLine("Auto: END   \"%s\" -> camhold=%d target %.1f %.1f %.1f", cmd, (int)g_camHold,
                g_camHoldPos[0], g_camHoldPos[1], g_camHoldPos[2]);

    } else if (_strnicmp(cmd, "flag ", 5) == 0) {
        void *engine = *(void **)0x78BC20;
        if (sscanf(cmd + 5, "%d %d", &id, &val) == 2 && engine && id >= 0 && id < 256) {
            unsigned char *flag = (unsigned char *)engine + DEBUG_FLAGS_ARRAY_OFFSET + id;
            *flag = (unsigned char)(val ? 1 : 0);
            LogLine("Auto: END   \"%s\" -> debug flag %d = %d", cmd, id, (int)*flag);
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: flag <id 0-255> <0|1>)", cmd);
        }

    /* notes/67: live-tunable UI knobs. These were env-var-only, so every
     * experiment cost a relaunch (and a relaunch costs the user, since only
     * they may start the game). They are plain floats consumed by existing
     * per-draw code, so changing them mid-session is safe and takes effect on
     * the next frame. */
    } else if (_strnicmp(cmd, "uidepth ", 8) == 0) {
        if (sscanf(cmd + 8, "%f", &a) == 1 && a >= 0.0f) {
            g_uiDepthWorld = a;
            LogLine("Auto: END   \"%s\" -> UI depth = %.0f world units (0 = infinity)", cmd, a);
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: uidepth <world units, 0 = off>)", cmd);
        }

    } else if (_strnicmp(cmd, "dlgdepth ", 9) == 0) {
        if (sscanf(cmd + 9, "%f", &a) == 1 && a >= 0.0f) {
            g_dlgDepthWorld = a;
            LogLine("Auto: END   \"%s\" -> dialogue UI depth = %.0f world units "
                    "(0 = share the normal UI depth of %.0f)", cmd, a, g_uiDepthWorld);
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: dlgdepth <world units, 0 = off>)", cmd);
        }

    } else if (_strnicmp(cmd, "uiscale ", 8) == 0) {
        if (sscanf(cmd + 8, "%f", &a) == 1 && a > 0.0f) {
            g_uiVpScale = a;
            LogLine("Auto: END   \"%s\" -> UI viewport scale = %.2f", cmd, a);
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: uiscale <factor > 0>)", cmd);
        }

    } else if (_strnicmp(cmd, "trace ", 6) == 0) {
        /* Arms the notes/35 one-frame trace, which includes the TRACE-UI lines
         * carrying each UI draw's shader index and vertex positions - the
         * per-draw identity needed to tell one HUD element from another. */
        g_traceFrames = (cmd[6] == '1');
        LogLine("Auto: END   \"%s\" -> one-frame trace %s", cmd,
                g_traceFrames ? "ARMED (one frame every ~5s)" : "off");

    /* notes/67: FOV scale, live. g_fovScaleAsm is read per call by the injected
     * stub, so a change lands on the next frame. Runtime control turns the
     * void A/B that 00-status lists as the top open item from "one relaunch per
     * value" (and only the user can relaunch) into one session. */
    } else if (_strnicmp(cmd, "fov ", 4) == 0) {
        if (sscanf(cmd + 4, "%f", &a) == 1 && a >= 0.5f && a <= 4.0f) {
            g_fovScaleAsm = a;
            LogLine("Auto: END   \"%s\" -> FOV scale = %.2f", cmd, a);
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: fov <0.5..4.0>)", cmd);
        }

    /* notes/67: READ-ONLY dump of the camera's orientation matrix at
     * camera+0x20, decoded statically in recon/2026-08-27 but never yet
     * exercised. Reading first: the matrix convention (row vs column major,
     * axis order, handedness) is NOT known, and writing a rotation built on a
     * guessed convention would just produce a confusing wrong result. Sampling
     * it while the game turns the camera is how the convention gets settled. */
    /* notes/67: WRITE the camera's facing direction. camera+0x20 is three
     * floats, not the 3x3 matrix the static read first assumed - the live
     * dump settled that (m[0..2] came back unit-length, m[3..8] were
     * unrelated neighbouring fields including a stray 104.0). So turning the
     * camera is writing a normalized direction; there is no matrix
     * convention, handedness or axis order left to guess.
     *
     * Why this matters beyond a free camera: 00-status lists "feed HMD yaw
     * into the game camera" as Candidate 1 for the black void, marked
     * unimplementable - which looks like it was judged when no way to set the
     * camera's facing was known. Pulling the camera 4000 units back showed the
     * engine culls and renders relative to the camera object we write, so if
     * facing is writable too, the game should cull toward wherever the head is
     * pointing and leave no unrendered region to fall into. Untested. */
    } else if (_strnicmp(cmd, "camlook ", 8) == 0) {
        if (sscanf(cmd + 8, "%f %f %f", &a, &b, &c) == 3) {
            float len = (float)sqrt(a*a + b*b + c*c);
            if (len < 1e-6f) {
                LogLine("Auto: END   \"%s\" -> FAILED (zero-length direction)", cmd);
            } else {
                void *cam = AutoGetCamera();
                if (cam) {
                    float *d = (float *)((char *)cam + 0x20);
                    d[0] = a / len; d[1] = b / len; d[2] = c / len;
                    *((unsigned char *)cam + PSY_CAM_DIRTY_OFFSET) |= 1;
                    g_lookHoldDir[0] = d[0]; g_lookHoldDir[1] = d[1]; g_lookHoldDir[2] = d[2];
                    LogLine("Auto: END   \"%s\" -> dir = %.4f %.4f %.4f", cmd, d[0], d[1], d[2]);
                } else {
                    LogLine("Auto: END   \"%s\" -> FAILED (no camera)", cmd);
                }
            }
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: camlook <x> <y> <z>)", cmd);
        }

    /* notes/67: dump raw floats from the camera object, to FIND the field the
     * renderer actually uses for view direction.
     *
     * Measured 2026-08-27, and it rules out the obvious candidates:
     *  - camera+0x08 (position) IS used: writes visibly move the view.
     *  - camera+0x20 is NOT: a write survives in memory unread and unchanged
     *    by the game, yet the rendered view does not move at all. It is what
     *    SetCameraOrientation writes, but not what the renderer consumes.
     *  - the camera is NOT a look-at rig: sliding it 2500 units sideways
     *    translated the view and let Raz leave the frame entirely, instead of
     *    rotating to keep a target centred.
     * So facing lives in some other field. This dumps a window of the object
     * as float and hex together, to spot another unit-length vec3 or a
     * rotation matrix. Read-only.
     *
     * Usage: dump 0x0 64   (offset is bytes from the camera pointer) */
    } else if (_strnicmp(cmd, "dump ", 5) == 0) {
        unsigned int off = 0; int cnt = 0;
        if (sscanf(cmd + 5, "%x %d", &off, &cnt) == 2 && cnt > 0 && cnt <= 64 &&
            off < 0x2000) {
            void *cam = AutoGetCamera();
            if (cam) {
                int i;
                char line[512];
                for (i = 0; i < cnt; i += 4) {
                    const unsigned char *base = (const unsigned char *)cam + off + i * 4;
                    const float *f = (const float *)base;
                    const unsigned int *u = (const unsigned int *)base;
                    int n = (cnt - i) < 4 ? (cnt - i) : 4;
                    if (n == 4)
                        _snprintf(line, sizeof(line),
                                  "+0x%03X: %12.4f %12.4f %12.4f %12.4f | %08X %08X %08X %08X",
                                  off + i * 4, f[0], f[1], f[2], f[3], u[0], u[1], u[2], u[3]);
                    else if (n == 3)
                        _snprintf(line, sizeof(line), "+0x%03X: %12.4f %12.4f %12.4f | %08X %08X %08X",
                                  off + i * 4, f[0], f[1], f[2], u[0], u[1], u[2]);
                    else if (n == 2)
                        _snprintf(line, sizeof(line), "+0x%03X: %12.4f %12.4f | %08X %08X",
                                  off + i * 4, f[0], f[1], u[0], u[1]);
                    else
                        _snprintf(line, sizeof(line), "+0x%03X: %12.4f | %08X", off + i * 4, f[0], u[0]);
                    line[sizeof(line) - 1] = '\0';
                    LogLine("Auto: dump %s", line);
                }
                LogLine("Auto: END   \"%s\" -> dumped %d floats from camera+0x%X", cmd, cnt, off);
            } else {
                LogLine("Auto: END   \"%s\" -> FAILED (no camera)", cmd);
            }
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: dump <hexOffset < 0x2000> <count 1..64>)", cmd);
        }

    } else if (_stricmp(cmd, "orient") == 0) {
        void *cam = AutoGetCamera();
        if (cam) {
            const float *m = (const float *)((char *)cam + 0x20);
            LogLine("Auto: END   \"orient\" -> m[0..8] = "
                    "%.4f %.4f %.4f | %.4f %.4f %.4f | %.4f %.4f %.4f",
                    m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8]);
        } else {
            LogLine("Auto: END   \"orient\" -> no camera available right now");
        }

    } else if (_stricmp(cmd, "status") == 0) {
        if (AutoReadCameraPos(pos))
            LogLine("Auto: END   \"status\" -> campos %.2f %.2f %.2f camhold=%d uidepth=%.0f "
                    "uiscale=%.2f trace=%d",
                    pos[0], pos[1], pos[2], (int)g_camHold, g_uiDepthWorld, g_uiVpScale,
                    (int)g_traceFrames);
        else
            LogLine("Auto: END   \"status\" -> no camera available right now");

    } else {
        LogLine("Auto: END   \"%s\" -> UNKNOWN command", cmd);
    }
}

/* Read the whole drop-file, truncate it, then run each line. Truncation
 * happens BEFORE execution deliberately: a line still in the file proves it
 * never ran, which is how XIII's crash was pinned down. */
static void AutoDrainCommands(void)
{
    char  contents[4096];
    size_t n = 0;
    FILE *f;
    char *line, *next;

    f = fopen(g_autoCmdPath, "rb");
    if (!f) return;
    n = fread(contents, 1, sizeof(contents) - 1, f);
    fclose(f);
    if (n == 0) return;
    contents[n] = '\0';

    f = fopen(g_autoCmdPath, "wb");
    if (f) fclose(f);

    line = contents;
    while (line && *line) {
        char *end;
        next = strchr(line, '\n');
        if (next) *next++ = '\0';
        /* trim */
        while (*line == ' ' || *line == '\t' || *line == '\r') line++;
        end = line + strlen(line);
        while (end > line && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) *--end = '\0';
        if (*line && *line != '#' && !(line[0] == '/' && line[1] == '/'))
            AutoRunCommand(line);
        line = next;
    }
}

static void AutomationTick(void)
{
    DWORD now;
    float pos[3];

    if (!g_automationEnabled) return;
    now = GetTickCount();

    /* Re-apply the held position every frame so the game's own camera update
     * cannot walk it back - this is what makes a free camera actually hold. */
    if (g_camHold) AutoWriteCameraPos(g_camHoldPos);
    if (g_lookHold) {
        void *cam = AutoGetCamera();
        if (cam) {
            float *d = (float *)((char *)cam + 0x20);
            d[0] = g_lookHoldDir[0]; d[1] = g_lookHoldDir[1]; d[2] = g_lookHoldDir[2];
            *((unsigned char *)cam + PSY_CAM_DIRTY_OFFSET) |= 1;
        }
    }

    if (now - g_autoLastPoll >= g_autoPollMs) {
        g_autoLastPoll = now;
        if (InterlockedCompareExchange(&g_autoInCommand, 1, 0) == 0) {
            AutoDrainCommands();
            InterlockedExchange(&g_autoInCommand, 0);
        }
    }

    if (g_autoTelemetryMs && now - g_autoLastTelemetry >= g_autoTelemetryMs) {
        g_autoLastTelemetry = now;
        if (AutoReadCameraPos(pos))
            LogLine("AutoTelem: campos %.2f %.2f %.2f camhold=%d", pos[0], pos[1], pos[2],
                    (int)g_camHold);
        else
            LogLine("AutoTelem: no camera (loading/menu?)");
    }
}

void __cdecl CandB_AfterBoth_asm(void) asm("CandB_AfterBoth_asm");
void __cdecl CandB_AfterBoth_asm(void)
{
    /* notes/67: external command queue + camera telemetry. Gated behind
     * PSYVR_AUTOMATION=1 (default off). See the block above for why this is
     * safe from this render-path site and what it deliberately will not do. */
    AutomationTick();

    /* notes/59 part 2: F12 - one-shot Lua-free level jump (notes/55's SetPendingLevel finding,
     * never live-tested before now). Opt-in (PSYVR_LEVEL_JUMP_KEY=1, default off) since this calls
     * real, non-diagnostic engine code with a hardcoded level - not something to leave live by
     * accident. Runs here (unconditionally every real frame) rather than inside the VR-bridge-gated
     * head-tracking block, so it works with no headset/SteamVR involved at all. */
    if (g_levelJumpKeyEnabled) {
        static SHORT s_prevF12 = 0;
        SHORT f12 = GetAsyncKeyState(VK_F12);
        if ((f12 & 0x8000) && !(s_prevF12 & 0x8000)) {
            void *engine = *(void **)0x78BC20;
            LogLine("LevelJump: F12 pressed - engine=%p, calling SetPendingLevel(\"%s\")",
                    engine, g_levelJumpCode);
            ((void (__thiscall *)(void *, const char *, BOOL))0x4FFA40)(engine, g_levelJumpCode, TRUE);
            LogLine("LevelJump: SetPendingLevel call returned (no crash) - watch for a world-space "
                    "BVM camera-coord jump in the next few seconds' log lines");
        }
        s_prevF12 = f12;
    }

    /* notes/62: NUMPAD9 - one-shot direct toggle of the "Visibility Tree Culling" debug flag
     * (exe+0x629490 decompile: for a generic-ID item like this one, id=117, the menu's own display
     * sync reads *(BYTE*)(engine_ptr + 44 + id) - writing that same byte directly here sidesteps
     * the debug menu's UI entirely, no navigation/focus needed). Opt-in (PSYVR_CULL_TOGGLE_KEY=1,
     * default off) - this is a real gameplay-affecting engine flag, not a diagnostic no-op. */
    if (g_cullToggleKeyEnabled) {
        static SHORT s_prevNum9 = 0;
        SHORT num9 = GetAsyncKeyState(VK_NUMPAD9);
        if ((num9 & 0x8000) && !(s_prevNum9 & 0x8000)) {
            void *engine = *(void **)0x78BC20;
            if (engine) {
                unsigned char *flag = (unsigned char *)engine + DEBUG_FLAGS_ARRAY_OFFSET + VISTREE_CULLING_ITEM_ID;
                *flag = !*flag;
                LogLine("CullToggle: NUMPAD9 pressed - Visibility Tree Culling flag @ %p (engine+%d) now %d",
                        flag, DEBUG_FLAGS_ARRAY_OFFSET + VISTREE_CULLING_ITEM_ID, (int)*flag);
            } else {
                LogLine("CullToggle: NUMPAD9 pressed but engine=*(void**)0x78BC20 is NULL, skipped");
            }
        }
        s_prevNum9 = num9;
    }

    /* notes/64: NUMPAD8 - one-shot direct toggle of the "Render Wireframe" debug flag, same
     * direct-byte-write pattern as NUMPAD9 above. Opt-in (PSYVR_WIREFRAME_TOGGLE_KEY=1, default
     * off). */
    if (g_wireframeToggleKeyEnabled) {
        static SHORT s_prevNum8 = 0;
        SHORT num8 = GetAsyncKeyState(VK_NUMPAD8);
        if ((num8 & 0x8000) && !(s_prevNum8 & 0x8000)) {
            void *engine = *(void **)0x78BC20;
            if (engine) {
                unsigned char *flag = (unsigned char *)engine + DEBUG_FLAGS_ARRAY_OFFSET + RENDER_WIREFRAME_ITEM_ID;
                *flag = !*flag;
                LogLine("WireframeToggle: NUMPAD8 pressed - Render Wireframe flag @ %p (engine+%d) now %d",
                        flag, DEBUG_FLAGS_ARRAY_OFFSET + RENDER_WIREFRAME_ITEM_ID, (int)*flag);
            } else {
                LogLine("WireframeToggle: NUMPAD8 pressed but engine=*(void**)0x78BC20 is NULL, skipped");
            }
        }
        s_prevNum8 = num8;
    }

    /* notes/65: NUMPAD7 - one-shot direct toggle of the "Collision Wireframe" debug flag, same
     * direct-byte-write pattern as NUMPAD8/9 above. Opt-in (PSYVR_COLLISION_WIREFRAME_TOGGLE_KEY=1,
     * default off). This is the theoretically-correct tool for the real-gap-vs-dark-terrain test -
     * see the constant's comment. */
    if (g_collisionWireframeToggleKeyEnabled) {
        static SHORT s_prevNum7 = 0;
        SHORT num7 = GetAsyncKeyState(VK_NUMPAD7);
        if ((num7 & 0x8000) && !(s_prevNum7 & 0x8000)) {
            void *engine = *(void **)0x78BC20;
            if (engine) {
                unsigned char *flag = (unsigned char *)engine + DEBUG_FLAGS_ARRAY_OFFSET + COLLISION_WIREFRAME_ITEM_ID;
                *flag = !*flag;
                LogLine("CollisionWireframeToggle: NUMPAD7 pressed - Collision Wireframe flag @ %p (engine+%d) now %d",
                        flag, DEBUG_FLAGS_ARRAY_OFFSET + COLLISION_WIREFRAME_ITEM_ID, (int)*flag);
            } else {
                LogLine("CollisionWireframeToggle: NUMPAD7 pressed but engine=*(void**)0x78BC20 is NULL, skipped");
            }
        }
        s_prevNum7 = num7;
    }

    /* notes/66: NUMPAD6 - one-shot direct toggle of the "Collision Spheres" debug flag, same
     * direct-byte-write pattern as NUMPAD7/8/9 above. Opt-in (PSYVR_COLLISION_SPHERES_TOGGLE_KEY=1,
     * default off). Positive-control tool: proves whether debug-menu visual flags render anything
     * at all in this PC build before trusting a negative result from any of them. */
    if (g_collisionSpheresToggleKeyEnabled) {
        static SHORT s_prevNum6 = 0;
        SHORT num6 = GetAsyncKeyState(VK_NUMPAD6);
        if ((num6 & 0x8000) && !(s_prevNum6 & 0x8000)) {
            void *engine = *(void **)0x78BC20;
            if (engine) {
                unsigned char *flag = (unsigned char *)engine + DEBUG_FLAGS_ARRAY_OFFSET + COLLISION_SPHERES_ITEM_ID;
                *flag = !*flag;
                LogLine("CollisionSpheresToggle: NUMPAD6 pressed - Collision Spheres flag @ %p (engine+%d) now %d",
                        flag, DEBUG_FLAGS_ARRAY_OFFSET + COLLISION_SPHERES_ITEM_ID, (int)*flag);
            } else {
                LogLine("CollisionSpheresToggle: NUMPAD6 pressed but engine=*(void**)0x78BC20 is NULL, skipped");
            }
        }
        s_prevNum6 = num6;
    }

    if (g_traceActive) { TraceFlushDrawsFwd(); LogLine("TRACE: -- AfterBoth (restoring BACKBUF) --"); }
    g_stereoPhase = STEREO_PHASE_IDLE;
    UIShift_ReconcileFwd(); /* notes/36: phase idle - removes any applied UI shift */
    UIVp_PhaseChangedFwd(); /* notes/42: phase idle - drop any applied UI viewport shrink */
    if (!g_stereoReady || !g_pDevice || !g_pRealBackBuffer) return;

    /* notes/28: both eyes' private render targets (g_pEye1Surf/g_pEye2Surf) are now fully
     * rendered for this frame - the natural point to pump the additive VR submission path, BEFORE
     * the render target is switched back to the real backbuffer below. Inert unless
     * PSYVR_ENABLE_SUBMIT=1 (see VRBridge_OnFrameComposited's own guard); never touches
     * g_pRealBackBuffer or anything else the existing monitor composite (Hook_Present, below)
     * depends on. */
    VRBridge_OnFrameComposited();

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
        /* notes/37: scale rawFov in place BEFORE anything reads it - the observer below and the
         * real body then both see the same scaled value. x87 stack is empty at a cdecl call
         * boundary; eax/ecx/edx are caller-saved and clobbered below anyway. */
        "flds 8(%esp)\n\t"
        "fmuls g_fovScaleAsm\n\t"
        "fstps 8(%esp)\n\t"
        "movl 8(%esp), %eax\n\t"   /* rawFov (post-scale) */
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

    /* notes/35: eye surfaces at scale x backbuffer (same aspect - projection untouched). If the
     * scaled allocations fail (VRAM/32-bit address space), fall back to 1x rather than losing
     * stereo entirely. */
    g_eyeWidth = g_bbWidth * g_eyeScale;
    g_eyeHeight = g_bbHeight * g_eyeScale;

    hrBB = pDevice->lpVtbl->GetBackBuffer(pDevice, 0, 0, D3DBACKBUFFER_TYPE_MONO, &g_pRealBackBuffer);
    hrE1 = pDevice->lpVtbl->CreateRenderTarget(pDevice, g_eyeWidth, g_eyeHeight, D3DFMT_A8R8G8B8,
                                                D3DMULTISAMPLE_NONE, 0, FALSE, &g_pEye1Surf, NULL);
    hrE2 = pDevice->lpVtbl->CreateRenderTarget(pDevice, g_eyeWidth, g_eyeHeight, D3DFMT_A8R8G8B8,
                                                D3DMULTISAMPLE_NONE, 0, FALSE, &g_pEye2Surf, NULL);
    if ((FAILED(hrE1) || FAILED(hrE2)) && g_eyeScale > 1) {
        LogLine("SetupStereoSurfaces: %ux-scaled eye surfaces failed (hrE1=0x%08lX hrE2=0x%08lX) - falling back to 1x",
                g_eyeScale, (unsigned long)hrE1, (unsigned long)hrE2);
        if (g_pEye1Surf) { g_pEye1Surf->lpVtbl->Release(g_pEye1Surf); g_pEye1Surf = NULL; }
        if (g_pEye2Surf) { g_pEye2Surf->lpVtbl->Release(g_pEye2Surf); g_pEye2Surf = NULL; }
        g_eyeScale = 1;
        g_eyeWidth = g_bbWidth;
        g_eyeHeight = g_bbHeight;
        hrE1 = pDevice->lpVtbl->CreateRenderTarget(pDevice, g_eyeWidth, g_eyeHeight, D3DFMT_A8R8G8B8,
                                                    D3DMULTISAMPLE_NONE, 0, FALSE, &g_pEye1Surf, NULL);
        hrE2 = pDevice->lpVtbl->CreateRenderTarget(pDevice, g_eyeWidth, g_eyeHeight, D3DFMT_A8R8G8B8,
                                                    D3DMULTISAMPLE_NONE, 0, FALSE, &g_pEye2Surf, NULL);
    }

    /* notes/22: capture the device's original auto depth-stencil surface (so it can be
     * restored after both eye passes) and give each eye its OWN private depth-stencil
     * surface matching the real one's format - see the comment on SetEyeAndTarget() for
     * why sharing a single depth buffer between both eyes was the real root cause of the
     * dark-eye/frozen-eye symptoms. Format comes from pp->AutoDepthStencilFormat (live-
     * confirmed 75/D3DFMT_D24S8), not guessed. Discard=TRUE matches typical depth-buffer
     * usage and is safe here since every eye pass unconditionally Clears its own depth
     * surface before drawing. */
    hrDS = pDevice->lpVtbl->GetDepthStencilSurface(pDevice, &g_pRealDepthStencil);
    hrDS1 = pDevice->lpVtbl->CreateDepthStencilSurface(pDevice, g_eyeWidth, g_eyeHeight,
                                                        pp->AutoDepthStencilFormat,
                                                        D3DMULTISAMPLE_NONE, 0, TRUE,
                                                        &g_pEye1DepthStencil, NULL);
    hrDS2 = pDevice->lpVtbl->CreateDepthStencilSurface(pDevice, g_eyeWidth, g_eyeHeight,
                                                        pp->AutoDepthStencilFormat,
                                                        D3DMULTISAMPLE_NONE, 0, TRUE,
                                                        &g_pEye2DepthStencil, NULL);

    LogLine("SetupStereoSurfaces: GetBackBuffer hr=0x%08lX ptr=0x%p | Eye1 hr=0x%08lX ptr=0x%p | Eye2 hr=0x%08lX ptr=0x%p (%ux%u, scale=%ux, bb=%ux%u) | "
            "GetDS hr=0x%08lX ptr=0x%p | Eye1DS hr=0x%08lX ptr=0x%p | Eye2DS hr=0x%08lX ptr=0x%p",
            (unsigned long)hrBB, (void *)g_pRealBackBuffer,
            (unsigned long)hrE1, (void *)g_pEye1Surf,
            (unsigned long)hrE2, (void *)g_pEye2Surf,
            g_eyeWidth, g_eyeHeight, g_eyeScale, g_bbWidth, g_bbHeight,
            (unsigned long)hrDS, (void *)g_pRealDepthStencil,
            (unsigned long)hrDS1, (void *)g_pEye1DepthStencil,
            (unsigned long)hrDS2, (void *)g_pEye2DepthStencil);

    g_stereoReady = SUCCEEDED(hrBB) && SUCCEEDED(hrE1) && SUCCEEDED(hrE2) &&
                    SUCCEEDED(hrDS1) && SUCCEEDED(hrDS2) &&
                    g_pRealBackBuffer && g_pEye1Surf && g_pEye2Surf &&
                    g_pEye1DepthStencil && g_pEye2DepthStencil;

    LogLine("Stereo ready = %d", g_stereoReady ? 1 : 0);

    /* notes/28: additive VR submission path - only does anything if PSYVR_ENABLE_SUBMIT=1 was set
     * (default off). Called after the existing monitor-composite surfaces are confirmed ready so
     * the VR bridge's own eye buffers can be sized to match g_bbWidth/g_bbHeight exactly - runs on
     * both initial CreateDevice AND every Reset (this same function handles both, unchanged). */
    if (g_stereoReady) {
        VRBridge_OnStereoSurfacesReady(pDevice, g_eyeWidth, g_eyeHeight);
    }
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
        if (g_firstPerson && !(g_vrSubmitEnabled && g_vrBridgeReady)) {
            g_fpPreviewMode = TRUE;
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
