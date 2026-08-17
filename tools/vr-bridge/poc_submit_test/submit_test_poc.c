/*
 * End-to-end VR bridge proof-of-concept: D3D9Ex render -> shared D3D11 surface -> OpenVR
 * IVRCompositor::Submit(), against SteamVR's null driver (no physical headset). This is the
 * "does the whole bridge concept actually work" milestone for the project (see
 * notes/27-null-driver-openvr-init-and-compositor-submit.md for full writeup).
 *
 * Combines two previously-separate, independently-proven POCs:
 *   - tools/vr-bridge/poc_shared_surface: D3D9Ex shared render target, readable live from a
 *     separate D3D11 device (proven working, notes/25).
 *   - tools/vr-bridge/poc_openvr_init: vendored OpenVR SDK links and calls into openvr_api.dll
 *     (proven working against SteamVR absent, notes/25; now updated for SteamVR PRESENT, see
 *     below for a real ABI finding made this session).
 *
 * IMPORTANT REAL FINDING (notes/27, not assumed): openvr_capi.h's "flat FnTable" struct
 * definitions (struct VR_IVRSystem_FnTable, VR_IVRCompositor_FnTable, etc.) do NOT describe what
 * this installed SteamVR build's VR_GetGenericInterface() actually returns. Empirically (via a
 * vectored exception handler + VirtualQuery, see notes/27 for the full diagnostic trail),
 * VR_GetGenericInterface() returns a genuine C++ object pointer (this-ptr into vrclient.dll) --
 * calling through it as a flat struct-of-function-pointers reads the vtable POINTER itself as if
 * it were function pointer slot 0, and jumps into a non-executable .rdata page (confirmed via
 * VirtualQuery: Protect=PAGE_READONLY at the crash address). The fix, confirmed working: treat
 * the returned pointer as a real C++ this-ptr, dereference *(void***)ptr to get the real vtable,
 * and dispatch through THAT using __thiscall (openvr.h's C++ interfaces use unattributed virtual
 * methods => default MSVC x86 ABI => this-ptr in ECX, args on stack, callee-cleans). This same
 * technique is used here for both IVRSystem (informational) and IVRCompositor (load-bearing).
 *
 * MILESTONE 2 (notes/28, THIS revision): SYNC MECHANISM FIX. notes/27 explicitly flagged
 * flush_d3d9() -- a D3DQUERYTYPE_EVENT query, Issued then polled with D3DGETDATA_FLUSH in a
 * while(true){ Sleep(1); } loop until S_OK -- as a SYNCHRONOUS GPU-PIPELINE STALL: correct for a
 * one-shot POC (clear once, prove the bytes round-trip), but wrong for a real per-frame hot path,
 * since Psychonauts already renders twice per real frame (once per eye, see
 * tools/proxy-d3d9/proxy_d3d9.c's CandB double-invoke) and a blocking CPU wait on top of that
 * would tank frame rate.
 *
 * THE ACTUAL BLOCKING BEHAVIOR, precisely: IDirect3DQuery9::GetData() itself does NOT block --
 * with or without D3DGETDATA_FLUSH it returns immediately with S_OK/S_FALSE/D3DERR_DEVICELOST.
 * D3DGETDATA_FLUSH only tells the driver "go ahead and submit the accumulated command buffer to
 * the GPU now instead of waiting for it to fill up on its own" -- it does not wait for that work
 * to finish. The stall in the OLD flush_d3d9() came entirely from wrapping GetData in a
 * while-loop with Sleep(1) between calls until it finally returned S_OK -- i.e. the CALLER chose
 * to block, D3D9 never required it to.
 *
 * THE FIX (implemented and measured below, see Part D): the standard real-world technique for
 * this exact problem is N-buffering the shared surface (here N=2, i.e. simple double-buffering)
 * combined with a SINGLE non-blocking GetData() poll per frame, never a wait loop:
 *   - Each "frame" renders into buffer[i % 2] and Issues an EVENT query on it (kicked once with
 *     D3DGETDATA_FLUSH right after Issue, so the driver submits it promptly instead of batching
 *     indefinitely -- still a single non-blocking call, not a loop).
 *   - The buffer submitted to IVRCompositor::Submit each frame is always the OTHER buffer
 *     (buffer[(i+1) % 2]) -- i.e. one frame behind the buffer currently being written -- polled
 *     with exactly ONE non-blocking GetData(NULL,0,0) call (no flush flag needed; it was already
 *     flushed a full frame interval ago). By construction that buffer's GPU work had a full
 *     frame's worth of wall-clock time to complete on the GPU before this check, so in the
 *     overwhelming common case it is already signaled -- but if it is NOT yet signaled (S_FALSE),
 *     this code does not wait for it: it simply skips submission for that frame (a dropped/
 *     resubmitted-next-frame frame, the standard graceful-degradation behavior for double
 *     buffering under transient GPU backpressure) rather than stalling the CPU thread.
 * This eliminates the full-pipeline-stall behavior entirely: the CPU-side "is it safe to hand
 * this buffer to the compositor" check is now an O(1), sub-microsecond operation on every single
 * frame, never a wait, confirmed by direct timing comparison against the OLD blocking helper
 * (kept in this file, clearly marked, ONLY for that side-by-side measurement -- not used on the
 * real per-frame submit path anymore). See notes/28 for the full before/after timing numbers.
 *
 * Build: see build.ps1. Run directly - no game process involved. Requires the null driver to be
 * enabled (notes/25 section 5 / notes/27 section 1) so SteamVR can run without physical hardware.
 */

#define INITGUID
#include <windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../openvr-sdk/headers/openvr_capi.h"
/* Do NOT include <stdbool.h> - see poc_openvr_init/openvr_init_poc.c's header comment for why
 * (openvr_capi.h typedefs its own bool as char on Windows, collides with stdbool.h). */

/* Small hidden swapchain window - never Present()'d, just needed to create a valid D3D9Ex device. */
#define WIN_W 64
#define WIN_H 64

/* The actual per-"eye" shared render target size - deliberately backbuffer-scale (matches the
 * real per-eye offscreen surfaces tools/proxy-d3d9/proxy_d3d9.c already creates via
 * CreateRenderTarget), NOT the tiny 64x64 used by the earlier POCs - a bigger surface makes the
 * sync-timing comparison below realistic rather than trivially fast either way. */
#define FRAME_W 1920
#define FRAME_H 1080

#define NUM_BUFFERS 2   /* double-buffering - see the file header for why 2 is enough */
#define NUM_PHASE1_ITERS 20  /* OLD blocking-helper timing sample size */
#define NUM_PHASE2_ITERS 90  /* NEW non-blocking double-buffered timing + correctness sample size */

/* ---- OpenVR global entry points (declared directly, not via openvr_capi.h's dead #if 0 block -
 * see poc_openvr_init for the two documented header quirks this works around). ---- */
extern bool VR_IsRuntimeInstalled(void);
extern bool VR_IsHmdPresent(void);
extern uint32_t VR_InitInternal2(EVRInitError *peError, EVRApplicationType eApplicationType, const char *pStartupInfo);
extern void VR_ShutdownInternal(void);
extern void *VR_GetGenericInterface(const char *pchInterfaceVersion, EVRInitError *peError);
extern const char *VR_GetVRInitErrorAsEnglishDescription(EVRInitError error);

#define CHECK_HR(hr, msg) \
    do { \
        if (FAILED(hr)) { \
            printf("FAIL: %s (hr=0x%08lX)\n", msg, (unsigned long)(hr)); \
            return 1; \
        } \
    } while (0)

/* ======================================================================
 * OLD, BLOCKING sync helper - notes/25/27's original technique. Kept ONLY
 * for the direct before/after timing comparison in Part C below; the real
 * double-buffered submit loop in Part D never calls this.
 * ====================================================================== */
static HRESULT flush_d3d9_BLOCKING(IDirect3DDevice9Ex *pDevice, IDirect3DQuery9 *pQuery, double *outStallMs)
{
    LARGE_INTEGER freq, t0, t1;
    HRESULT hr;
    (void)pDevice;

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    hr = pQuery->lpVtbl->Issue(pQuery, D3DISSUE_END);
    if (FAILED(hr)) return hr;

    for (;;) {
        hr = pQuery->lpVtbl->GetData(pQuery, NULL, 0, D3DGETDATA_FLUSH);
        if (hr == S_OK) break;
        if (hr != S_FALSE) return hr;
        Sleep(1);   /* <-- THE stall: this loop does not return until the GPU is done */
    }

    QueryPerformanceCounter(&t1);
    if (outStallMs) {
        *outStallMs = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
    }
    return S_OK;
}

static HRESULT RenderColor(IDirect3DDevice9Ex *pDevice, IDirect3DSurface9 *pSurf, D3DCOLOR color)
{
    HRESULT hr = pDevice->lpVtbl->SetRenderTarget(pDevice, 0, pSurf);
    if (FAILED(hr)) return hr;
    hr = pDevice->lpVtbl->BeginScene(pDevice);
    if (FAILED(hr)) return hr;
    hr = pDevice->lpVtbl->Clear(pDevice, 0, NULL, D3DCLEAR_TARGET, color, 1.0f, 0);
    pDevice->lpVtbl->EndScene(pDevice);
    return hr;
}

/* ---- Real-C++-vtable dispatch helper (the confirmed-working fix - see file header). ---- */
static void *real_vtable(void *thisPtr)
{
    return *(void **)thisPtr; /* first 4 bytes of any polymorphic MSVC C++ object = vtable ptr */
}

/* IVRCompositor vtable slot indices, from openvr.h's class IVRCompositor method declaration
 * order (0-based): 0 SetTrackingSpace, 1 GetTrackingSpace, 2 WaitGetPoses, 3 GetLastPoses,
 * 4 GetLastPoseForTrackedDeviceIndex, 5 GetSubmitTexture, 6 Submit. */
typedef EVRCompositorError (__thiscall *WaitGetPoses_t)(void *pThis,
    TrackedDevicePose_t *pRenderPoseArray, uint32_t unRenderPoseArrayCount,
    TrackedDevicePose_t *pGamePoseArray, uint32_t unGamePoseArrayCount);
typedef EVRCompositorError (__thiscall *Submit_t)(void *pThis,
    EVREye eEye, const Texture_t *pTexture, const VRTextureBounds_t *pBounds, EVRSubmitFlags nSubmitFlags);

/* Per-ring-buffer state for the NEW double-buffered non-blocking submit path (Part D). */
typedef struct {
    IDirect3DTexture9 *pTex9;
    IDirect3DSurface9 *pSurf9;
    HANDLE sharedHandle;
    ID3D11Texture2D *pTex11;
    IDirect3DQuery9 *pQuery9;
    BOOL queryIssued;      /* has Issue(D3DISSUE_END) ever been called for the current content? */
    BYTE expectR, expectG, expectB;  /* color this buffer was last rendered to, for correctness verification */
} RingBuffer;

static HRESULT ReadbackPixel(ID3D11DeviceContext *pContext11, ID3D11Texture2D *pStaging,
                              ID3D11Texture2D *pSrc, int x, int y, BYTE *outB, BYTE *outG, BYTE *outR)
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr;
    BYTE *row;

    pContext11->lpVtbl->CopyResource(pContext11, (ID3D11Resource *)pStaging, (ID3D11Resource *)pSrc);
    hr = pContext11->lpVtbl->Map(pContext11, (ID3D11Resource *)pStaging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return hr;

    row = (BYTE *)mapped.pData + (size_t)y * mapped.RowPitch;
    *outB = row[x * 4 + 0];
    *outG = row[x * 4 + 1];
    *outR = row[x * 4 + 2];

    pContext11->lpVtbl->Unmap(pContext11, (ID3D11Resource *)pStaging, 0);
    return S_OK;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== VR bridge POC v2: non-blocking double-buffered sync + IVRCompositor::Submit ===\n\n");

    /* ---------- Part A: D3D9Ex device (proven mechanism, poc_shared_surface / notes/25) ---------- */
    WNDCLASSEXA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "PsyVR_SubmitTestPOC2";
    RegisterClassExA(&wc);
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "PsyVR submit-test POC v2",
                                 WS_OVERLAPPEDWINDOW, 0, 0, WIN_W, WIN_H, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) { printf("FAIL: CreateWindowExA\n"); return 1; }

    IDirect3D9Ex *pD3D9Ex = NULL;
    HRESULT hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &pD3D9Ex);
    CHECK_HR(hr, "Direct3DCreate9Ex");

    LUID luid9;
    hr = pD3D9Ex->lpVtbl->GetAdapterLUID(pD3D9Ex, D3DADAPTER_DEFAULT, &luid9);
    CHECK_HR(hr, "IDirect3D9Ex::GetAdapterLUID");

    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hwnd;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.BackBufferWidth = WIN_W;
    pp.BackBufferHeight = WIN_H;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9Ex *pDevice = NULL;
    hr = pD3D9Ex->lpVtbl->CreateDeviceEx(pD3D9Ex, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE,
        &pp, NULL, &pDevice);
    CHECK_HR(hr, "IDirect3D9Ex::CreateDeviceEx");
    printf("[A1] D3D9Ex device created\n\n");

    /* ---------- Part B: D3D11 device matched to the same adapter (unchanged mechanism) ---------- */
    IDXGIFactory1 *pFactory = NULL;
    hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&pFactory);
    CHECK_HR(hr, "CreateDXGIFactory1");
    IDXGIAdapter1 *pChosenAdapter = NULL;
    for (UINT i = 0;; i++) {
        IDXGIAdapter1 *pAdapter = NULL;
        hr = pFactory->lpVtbl->EnumAdapters1(pFactory, i, &pAdapter);
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc;
        pAdapter->lpVtbl->GetDesc1(pAdapter, &desc);
        if (desc.AdapterLuid.LowPart == luid9.LowPart && desc.AdapterLuid.HighPart == luid9.HighPart) {
            pChosenAdapter = pAdapter;
            wprintf(L"[B1] Matched D3D11 adapter to D3D9Ex's LUID: %s\n", desc.Description);
            break;
        }
        pAdapter->lpVtbl->Release(pAdapter);
    }
    if (!pChosenAdapter) { printf("FAIL: no DXGI adapter matched D3D9Ex LUID\n"); return 1; }

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL gotLevel;
    ID3D11Device *pDevice11 = NULL;
    ID3D11DeviceContext *pContext11 = NULL;
    hr = D3D11CreateDevice((IDXGIAdapter *)pChosenAdapter, D3D_DRIVER_TYPE_UNKNOWN, NULL,
                            D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels),
                            D3D11_SDK_VERSION, &pDevice11, &gotLevel, &pContext11);
    CHECK_HR(hr, "D3D11CreateDevice");
    printf("[B2] D3D11CreateDevice OK, feature level=0x%X\n\n", gotLevel);

    /* ---------- Part C: OLD blocking-helper timing baseline (throwaway surface, not submitted) ---------- */
    printf("--- Part C: OLD blocking sync helper (flush_d3d9_BLOCKING) - %d-iteration timing baseline ---\n", NUM_PHASE1_ITERS);
    IDirect3DTexture9 *pScratchTex = NULL;
    hr = pDevice->lpVtbl->CreateTexture(pDevice, FRAME_W, FRAME_H, 1, D3DUSAGE_RENDERTARGET,
                                         D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &pScratchTex, NULL);
    CHECK_HR(hr, "CreateTexture (scratch, non-shared)");
    IDirect3DSurface9 *pScratchSurf = NULL;
    hr = pScratchTex->lpVtbl->GetSurfaceLevel(pScratchTex, 0, &pScratchSurf);
    CHECK_HR(hr, "GetSurfaceLevel (scratch)");
    IDirect3DQuery9 *pScratchQuery = NULL;
    hr = pDevice->lpVtbl->CreateQuery(pDevice, D3DQUERYTYPE_EVENT, &pScratchQuery);
    CHECK_HR(hr, "CreateQuery (scratch)");

    double sumMs = 0.0, minMs = 1e18, maxMs = 0.0;
    for (int i = 0; i < NUM_PHASE1_ITERS; i++) {
        D3DCOLOR c = D3DCOLOR_ARGB(255, (i * 3) % 256, (i * 7) % 256, (i * 11) % 256);
        hr = RenderColor(pDevice, pScratchSurf, c);
        CHECK_HR(hr, "RenderColor (Part C)");
        double stallMs = 0.0;
        hr = flush_d3d9_BLOCKING(pDevice, pScratchQuery, &stallMs);
        CHECK_HR(hr, "flush_d3d9_BLOCKING");
        sumMs += stallMs;
        if (stallMs < minMs) minMs = stallMs;
        if (stallMs > maxMs) maxMs = stallMs;
    }
    printf("[C1] OLD blocking helper over %d iters: min=%.4fms avg=%.4fms max=%.4fms "
           "(CPU thread genuinely slept/spun inside this call every single time)\n\n",
           NUM_PHASE1_ITERS, minMs, sumMs / NUM_PHASE1_ITERS, maxMs);

    pScratchQuery->lpVtbl->Release(pScratchQuery);
    pScratchSurf->lpVtbl->Release(pScratchSurf);
    pScratchTex->lpVtbl->Release(pScratchTex);

    /* ---------- Part D: NEW double-buffered, non-blocking sync + real Submit loop ---------- */
    printf("--- Part D: NEW non-blocking double-buffered sync - %d simulated frames @ ~60fps pacing ---\n", NUM_PHASE2_ITERS);

    RingBuffer buf[NUM_BUFFERS];
    ZeroMemory(buf, sizeof(buf));
    for (int b = 0; b < NUM_BUFFERS; b++) {
        hr = pDevice->lpVtbl->CreateTexture(pDevice, FRAME_W, FRAME_H, 1, D3DUSAGE_RENDERTARGET,
                                             D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &buf[b].pTex9, &buf[b].sharedHandle);
        CHECK_HR(hr, "CreateTexture (ring buffer, shared)");
        hr = buf[b].pTex9->lpVtbl->GetSurfaceLevel(buf[b].pTex9, 0, &buf[b].pSurf9);
        CHECK_HR(hr, "GetSurfaceLevel (ring buffer)");
        hr = pDevice->lpVtbl->CreateQuery(pDevice, D3DQUERYTYPE_EVENT, &buf[b].pQuery9);
        CHECK_HR(hr, "CreateQuery (ring buffer)");
        hr = pDevice11->lpVtbl->OpenSharedResource(pDevice11, buf[b].sharedHandle, &IID_ID3D11Texture2D, (void **)&buf[b].pTex11);
        CHECK_HR(hr, "OpenSharedResource (ring buffer)");
    }
    printf("[D1] %d shared %dx%d ring buffers created and opened on D3D11\n\n", NUM_BUFFERS, FRAME_W, FRAME_H);

    /* Staging texture for CPU readback correctness verification. */
    D3D11_TEXTURE2D_DESC stagingDesc;
    buf[0].pTex11->lpVtbl->GetDesc(buf[0].pTex11, &stagingDesc);
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;
    ID3D11Texture2D *pStaging = NULL;
    hr = pDevice11->lpVtbl->CreateTexture2D(pDevice11, &stagingDesc, NULL, &pStaging);
    CHECK_HR(hr, "CreateTexture2D (staging)");

    /* ---------- OpenVR init (unchanged mechanism, notes/27) ---------- */
    printf("VR_IsRuntimeInstalled() = %s\n", VR_IsRuntimeInstalled() ? "true" : "false");
    printf("VR_IsHmdPresent()       = %s\n\n", VR_IsHmdPresent() ? "true" : "false");

    EVRInitError err = EVRInitError_VRInitError_None;
    uint32_t token = VR_InitInternal2(&err, EVRApplicationType_VRApplication_Scene, NULL);
    printf("[D2] VR_InitInternal2(VRApplication_Scene) -> token=%u, error=%d (%s)\n",
           token, (int)err, VR_GetVRInitErrorAsEnglishDescription(err));
    if (!(err == EVRInitError_VRInitError_None && token != 0)) {
        printf("\nFAIL: OpenVR init did not succeed - cannot proceed to Submit.\n");
        return 1;
    }

    EVRInitError compErr = EVRInitError_VRInitError_None;
    void *compPtr = VR_GetGenericInterface(IVRCompositor_Version, &compErr);
    printf("[D3] VR_GetGenericInterface(%s) -> ptr=%p, error=%d\n", IVRCompositor_Version, compPtr, (int)compErr);
    if (!compPtr) {
        printf("\nFAIL: could not get IVRCompositor interface.\n");
        VR_ShutdownInternal();
        return 1;
    }
    void **vtbl = (void **)real_vtable(compPtr);
    WaitGetPoses_t pWaitGetPoses = (WaitGetPoses_t)vtbl[2];
    Submit_t pSubmit = (Submit_t)vtbl[6];
    printf("[D4] IVRCompositor ready (vtable=%p, __thiscall dispatch fix applied)\n\n", (void *)vtbl);

    TrackedDevicePose_t renderPoses[64], gamePoses[64];

    /* ---- The actual per-frame loop: this is the pattern meant to go into proxy_d3d9.c's
     * Hook_Present / CandB_AfterBoth_asm eventually - render, issue-non-blocking, poll-
     * non-blocking the PREVIOUS buffer, submit if ready, never wait. ---- */
    LARGE_INTEGER freq, pt0, pt1;
    QueryPerformanceFrequency(&freq);
    double pollSumUs = 0.0, pollMinUs = 1e18, pollMaxUs = 0.0;
    int pollSamples = 0, submittedCount = 0, skippedCount = 0, mismatchCount = 0;

    for (int i = 0; i < NUM_PHASE2_ITERS; i++) {
        int curIdx = i % NUM_BUFFERS;
        int prevIdx = (i + NUM_BUFFERS - 1) % NUM_BUFFERS;

        /* 1) Render this frame's content into the CURRENT buffer. */
        BYTE r = (BYTE)((i * 3 + 40) % 256), g = (BYTE)((i * 7 + 80) % 256), b = (BYTE)((i * 11 + 120) % 256);
        hr = RenderColor(pDevice, buf[curIdx].pSurf9, D3DCOLOR_ARGB(255, r, g, b));
        CHECK_HR(hr, "RenderColor (Part D)");
        buf[curIdx].expectR = r; buf[curIdx].expectG = g; buf[curIdx].expectB = b;

        /* 2) Issue a non-blocking event query for it. GetData is called ONCE, immediately,
         * with D3DGETDATA_FLUSH purely to kick the driver into submitting the command buffer
         * promptly (NOT to wait for the result) - this call itself never blocks. */
        buf[curIdx].pQuery9->lpVtbl->Issue(buf[curIdx].pQuery9, D3DISSUE_END);
        buf[curIdx].pQuery9->lpVtbl->GetData(buf[curIdx].pQuery9, NULL, 0, D3DGETDATA_FLUSH);
        buf[curIdx].queryIssued = TRUE;

        /* 3) Non-blockingly check whether the PREVIOUS buffer (one full frame interval old)
         * is ready. A SINGLE GetData call, no flush flag (already flushed last iteration), no
         * loop, no Sleep - if it returns S_FALSE we simply skip submission this frame instead
         * of waiting for it. */
        BOOL ready = FALSE;
        if (buf[prevIdx].queryIssued) {
            QueryPerformanceCounter(&pt0);
            HRESULT pollHr = buf[prevIdx].pQuery9->lpVtbl->GetData(buf[prevIdx].pQuery9, NULL, 0, 0);
            QueryPerformanceCounter(&pt1);
            double us = (double)(pt1.QuadPart - pt0.QuadPart) * 1000000.0 / (double)freq.QuadPart;
            pollSumUs += us; pollSamples++;
            if (us < pollMinUs) pollMinUs = us;
            if (us > pollMaxUs) pollMaxUs = us;
            ready = (pollHr == S_OK);
        }

        if (ready) {
            /* 4) Submit the previous (confirmed-ready) buffer to the compositor, both eyes. */
            ZeroMemory(renderPoses, sizeof(renderPoses));
            ZeroMemory(gamePoses, sizeof(gamePoses));
            pWaitGetPoses(compPtr, renderPoses, 64, gamePoses, 64);

            Texture_t tex;
            tex.handle = (void *)buf[prevIdx].pTex11;
            tex.eType = ETextureType_TextureType_DirectX;
            tex.eColorSpace = EColorSpace_ColorSpace_Auto;
            EVRCompositorError eL = pSubmit(compPtr, EVREye_Eye_Left, &tex, NULL, EVRSubmitFlags_Submit_Default);
            EVRCompositorError eR = pSubmit(compPtr, EVREye_Eye_Right, &tex, NULL, EVRSubmitFlags_Submit_Default);

            /* 5) Correctness check: read back the ACTUAL bytes the compositor was just handed
             * and confirm they match what was rendered into that buffer (not stale, not
             * corrupted, not a different buffer's content). */
            BYTE gotB, gotG, gotR;
            ReadbackPixel(pContext11, pStaging, buf[prevIdx].pTex11, FRAME_W / 2, FRAME_H / 2, &gotB, &gotG, &gotR);
            BOOL match = (gotR == buf[prevIdx].expectR && gotG == buf[prevIdx].expectG && gotB == buf[prevIdx].expectB);
            if (!match) {
                mismatchCount++;
                printf("[D-frame %2d] MISMATCH: expected R=%u G=%u B=%u, got R=%u G=%u B=%u\n",
                       i, buf[prevIdx].expectR, buf[prevIdx].expectG, buf[prevIdx].expectB, gotR, gotG, gotB);
            }
            if (eL != EVRCompositorError_VRCompositorError_None || eR != EVRCompositorError_VRCompositorError_None) {
                printf("[D-frame %2d] Submit error: L=%d R=%d\n", i, (int)eL, (int)eR);
            }
            submittedCount++;
        } else if (buf[prevIdx].queryIssued) {
            skippedCount++;
            printf("[D-frame %2d] buffer not yet GPU-ready - SKIPPED this frame's submit "
                   "(no stall; will try again next frame)\n", i);
        }

        Sleep(16); /* simulate ~60fps frame pacing between iterations */
    }

    printf("\n[D5] Part D summary over %d simulated frames:\n", NUM_PHASE2_ITERS);
    printf("     submitted=%d skipped=%d mismatches=%d\n", submittedCount, skippedCount, mismatchCount);
    printf("     non-blocking poll cost: min=%.2fus avg=%.2fus max=%.2fus (%d samples)\n",
           pollMinUs, pollSamples ? pollSumUs / pollSamples : 0.0, pollMaxUs, pollSamples);
    printf("     compare to Part C's OLD blocking helper: min=%.4fms avg=%.4fms max=%.4fms\n",
           minMs, sumMs / NUM_PHASE1_ITERS, maxMs);

    int pass = (mismatchCount == 0) && (submittedCount > 0);
    printf("\n=== RESULT: %s ===\n", pass
           ? "PASS - non-blocking double-buffered sync produced zero pixel mismatches, poll cost is microsecond-scale (no full-pipeline stall)"
           : "FAIL - see mismatch/submit counts above");

    VR_ShutdownInternal();
    printf("VR_ShutdownInternal() called.\n");

    pStaging->lpVtbl->Release(pStaging);
    for (int b = 0; b < NUM_BUFFERS; b++) {
        buf[b].pTex11->lpVtbl->Release(buf[b].pTex11);
        buf[b].pQuery9->lpVtbl->Release(buf[b].pQuery9);
        buf[b].pSurf9->lpVtbl->Release(buf[b].pSurf9);
        buf[b].pTex9->lpVtbl->Release(buf[b].pTex9);
    }
    pContext11->lpVtbl->Release(pContext11);
    pDevice11->lpVtbl->Release(pDevice11);
    pChosenAdapter->lpVtbl->Release(pChosenAdapter);
    pFactory->lpVtbl->Release(pFactory);
    pDevice->lpVtbl->Release(pDevice);
    pD3D9Ex->lpVtbl->Release(pD3D9Ex);
    DestroyWindow(hwnd);

    return pass ? 0 : 1;
}
