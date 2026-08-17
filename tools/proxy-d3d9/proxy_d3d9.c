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
 * Vtable indices used (0-based, standard COM: slot 0/1/2 are always
 * QueryInterface/AddRef/Release):
 *   IDirect3D9::CreateDevice                    = slot 16
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
typedef HRESULT (STDMETHODCALLTYPE *SetVertexShaderConstantF_t)(
    IDirect3DDevice9 *This,
    UINT StartRegister,
    CONST float *pConstantData,
    UINT Vector4fCount);

static HMODULE g_hRealD3D9 = NULL;
static Direct3DCreate9_t g_pRealDirect3DCreate9 = NULL;
static CreateDevice_t g_pRealCreateDevice = NULL;
static Present_t g_pRealPresent = NULL;
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
 * full) was chosen as a round number in that range. */
#define STEREO_HALF_IPD 3.25f

static IDirect3DDevice9 *g_pDevice = NULL;
static IDirect3DSurface9 *g_pRealBackBuffer = NULL;
static IDirect3DSurface9 *g_pEye1Surf = NULL;
static IDirect3DSurface9 *g_pEye2Surf = NULL;
static UINT g_bbWidth = 0;
static UINT g_bbHeight = 0;
static BOOL g_stereoReady = FALSE; /* device + both offscreen surfaces created OK */

static BOOL g_frameCamCached = FALSE;
static Vec3 g_baseEye, g_baseAt, g_baseUp, g_rightVec;
static void *g_camPOutMatrix = NULL;

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
void __cdecl BPM_OnEntry_asm(float rawFov, float aspect) asm("BPM_OnEntry_asm");
void __cdecl BPM_OnEntry_asm(float rawFov, float aspect)
{
    double divConst = *ADDR_FOV_DIV_CONST;
    float mulConst = *ADDR_FOV_MUL_CONST;
    float fovy;

    if (aspect <= 0.0f || divConst == 0.0) return;

    fovy = (float)((double)rawFov / divConst) * mulConst;
    if (fovy <= 0.0f || fovy >= 3.14159265f) return; /* sanity guard */

    g_projXScale = 1.0f / (tanf(fovy * 0.5f) * aspect);
    g_projXScaleValid = TRUE;

    {
        static DWORD s_lastLog = 0;
        DWORD now = GetTickCount();
        if (s_lastLog == 0 || (DWORD)(now - s_lastLog) >= 2000) {
            LogLine("BPM cache SET: rawFov=%.3f aspect=%.4f fovy=%.4f xScale=%.4f",
                    rawFov, aspect, fovy, g_projXScale);
            s_lastLog = now;
        }
    }
}

/* Switches the active render target to the given eye's dedicated offscreen
 * surface and overwrites the cached camera's view matrix in place by
 * directly re-invoking BuildViewMatrix's real, unmodified body (through its
 * trampoline) with a fresh eye position computed from the cached clean base
 * - never from a read-back already-offset value. */
static void SetEyeAndTarget(float sign, IDirect3DSurface9 *targetSurf, BOOL explicitClear)
{
    if (!g_stereoReady || !g_pDevice || !targetSurf) return;

    g_pDevice->lpVtbl->SetRenderTarget(g_pDevice, 0, targetSurf);

    /* notes/14 background-layer-bug investigation: both eyes' offscreen
     * targets share the device's single auto depth-stencil surface (never
     * reassigned via SetDepthStencilSurface) - if CandB's own internal
     * Clear() calls somehow don't unconditionally reset depth/stencil on
     * every invocation, eye 2's background draws could silently fail a
     * depth test against eye 1's leftover depth values. Explicitly clearing
     * color+depth+stencil here, before CandB's real body runs for THIS eye,
     * tests that cheaply regardless of whatever CandB does internally.
     * Empirically (this session): clearing BOTH eyes flipped which eye's
     * background was missing (eye1 went blank, eye2's fixed itself) rather
     * than fixing both - clearing is gated per-eye by the caller so this can
     * be isolated/compared; see notes/14 for the full experiment log. */
    if (explicitClear) {
        g_pDevice->lpVtbl->Clear(g_pDevice, 0, NULL,
                                  D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
                                  D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    }

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

void __cdecl CandB_BeforeEye1_asm(void) asm("CandB_BeforeEye1_asm");
void __cdecl CandB_BeforeEye1_asm(void)
{
    g_stereoPhase = STEREO_PHASE_EYE1;
    g_eye2Presented = 0;
    SetEyeAndTarget(-1.0f, g_pEye1Surf, FALSE);
}

void __cdecl CandB_BeforeEye2_asm(void) asm("CandB_BeforeEye2_asm");
void __cdecl CandB_BeforeEye2_asm(void)
{
    g_stereoPhase = STEREO_PHASE_EYE2;
    SetEyeAndTarget(+1.0f, g_pEye2Surf, TRUE);
}

void __cdecl CandB_AfterBoth_asm(void) asm("CandB_AfterBoth_asm");
void __cdecl CandB_AfterBoth_asm(void)
{
    g_stereoPhase = STEREO_PHASE_IDLE;
    if (!g_stereoReady || !g_pDevice || !g_pRealBackBuffer) return;
    g_pDevice->lpVtbl->SetRenderTarget(g_pDevice, 0, g_pRealBackBuffer);
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
 * BuildProjectionMatrix(pOutMatrix, rawFov, Aspect, zn, zf) - only rawFov
 * and Aspect (both raw 4-byte float stack args - pushed/read as plain
 * 32-bit values here, exactly how __cdecl already represents them on the
 * stack) are needed. */
__attribute__((naked)) void Hook_BuildProjectionMatrix(void)
{
    __asm__ __volatile__(
        "movl 8(%esp), %eax\n\t"   /* rawFov */
        "movl 12(%esp), %ecx\n\t"  /* Aspect */
        "pushl %ecx\n\t"
        "pushl %eax\n\t"
        "call BPM_OnEntry_asm\n\t"
        "addl $8, %esp\n\t"
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
    HRESULT hrBB, hrE1, hrE2;

    g_pDevice = pDevice;
    g_bbWidth = pp->BackBufferWidth;
    g_bbHeight = pp->BackBufferHeight;

    hrBB = pDevice->lpVtbl->GetBackBuffer(pDevice, 0, 0, D3DBACKBUFFER_TYPE_MONO, &g_pRealBackBuffer);
    hrE1 = pDevice->lpVtbl->CreateRenderTarget(pDevice, g_bbWidth, g_bbHeight, D3DFMT_A8R8G8B8,
                                                D3DMULTISAMPLE_NONE, 0, FALSE, &g_pEye1Surf, NULL);
    hrE2 = pDevice->lpVtbl->CreateRenderTarget(pDevice, g_bbWidth, g_bbHeight, D3DFMT_A8R8G8B8,
                                                D3DMULTISAMPLE_NONE, 0, FALSE, &g_pEye2Surf, NULL);

    LogLine("SetupStereoSurfaces: GetBackBuffer hr=0x%08lX ptr=0x%p | Eye1 hr=0x%08lX ptr=0x%p | Eye2 hr=0x%08lX ptr=0x%p (%ux%u)",
            (unsigned long)hrBB, (void *)g_pRealBackBuffer,
            (unsigned long)hrE1, (void *)g_pEye1Surf,
            (unsigned long)hrE2, (void *)g_pEye2Surf,
            g_bbWidth, g_bbHeight);

    g_stereoReady = SUCCEEDED(hrBB) && SUCCEEDED(hrE1) && SUCCEEDED(hrE2) &&
                    g_pRealBackBuffer && g_pEye1Surf && g_pEye2Surf;

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
 * screenshot comparison. */
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
        float sign = (g_stereoPhase == STEREO_PHASE_EYE1) ? -1.0f : 1.0f;
        float d = STEREO_HALF_IPD * sign;

        memcpy(patched, pConstantData, sizeof(patched));
        /* notes/18: index 3, NOT 12 - see the derivation comment above
         * STEREO_WVP_REGISTER. The uploaded buffer is Transpose(WVP)
         * (confirmed live, notes/17), so WVP's row3/col0 element (the one
         * the closed-form correction targets) lives at flat index 3 in
         * this already-transposed buffer, not index 12. */
        patched[3] += (-d) * xScale;

        {
            static DWORD s_lastLog = 0;
            DWORD now = GetTickCount();
            if (s_lastLog == 0 || (DWORD)(now - s_lastLog) >= 2000) {
                LogLine("SVSCF stereo-correct: reg=%u phase=%ld xScale=%.4f d=%.3f delta=%.4f",
                        StartRegister, g_stereoPhase, xScale, d, (-d) * xScale);
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
            LogLine("Present() composite (phase EYE2): StretchRect L=0x%08lX R=0x%08lX", (unsigned long)hrL, (unsigned long)hrR);
        }
    }

    g_frameCamCached = FALSE;

    if (doLog) {
        LogLine("Present() hit - total frame #%ld phase=%ld (throttled ~1 log/sec)", frame, g_stereoPhase);
        g_lastPresentLogTick = now;
    }

    return g_pRealPresent(This, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

/* Patch IDirect3DDevice9::Present (vtbl slot 17) to point at our hook.
 * The vtable pointed to by This->lpVtbl is a single shared, normally
 * read-only structure (typically in the real d3d9.dll's .rdata), so it must
 * be made writable with VirtualProtect before patching and restored after.
 * Guarded by g_deviceHooked so a second CreateDevice call (should one ever
 * happen) doesn't re-patch an already-patched vtable. */
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

    VirtualProtect(vtbl, sizeof(*vtbl), oldProtect, &oldProtect);

    g_deviceHooked = TRUE;
    LogLine("Hooked IDirect3DDevice9::Present (vtable slot 17), original=0x%p", (void *)g_pRealPresent);
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
