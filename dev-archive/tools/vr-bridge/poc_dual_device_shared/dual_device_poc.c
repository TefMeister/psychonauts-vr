/*
 * Verification POC for notes/28's real-game integration design: can a PLAIN (non-Ex) D3D9 device
 * open a shared handle that was ORIGINATED by a separate D3D9Ex device, in the same process, on
 * the same adapter -- and have that content correctly visible from a THIRD, separate D3D11 device
 * via OpenSharedResource?
 *
 * Why this matters: Psychonauts.exe's own D3D9 device (the one tools/proxy-d3d9/proxy_d3d9.c
 * hooks) is a PLAIN IDirect3DDevice9, created via the game's own call chain
 * Direct3DCreate9 -> IDirect3D9::CreateDevice (confirmed: proxy_d3d9.c only ever hooks
 * IDirect3D9::CreateDevice, never CreateDeviceEx / IDirect3D9Ex). Only a D3D9Ex device's
 * CreateTexture can produce a shared handle that ID3D11Device::OpenSharedResource can open
 * (documented D3D9Ex requirement for cross-API sharing) -- so the real proxy_d3d9.c integration
 * needs a SECOND, private D3D9Ex device (created by our own code, alongside the game's device)
 * to originate the shareable surfaces, with the game's own plain device then writing content
 * into them (either by opening the same handle directly, or by rendering into its own private
 * surface and StretchRect-copying into a same-device-opened view of the shared surface).
 *
 * This POC tests the DIRECT approach first (device B opens device A's handle by passing it as
 * a non-NULL INPUT to its own CreateTexture call) since if that works, it's zero-copy (device B
 * -- standing in for the game's device -- can render straight into the shared surface, no extra
 * StretchRect needed). If it does NOT work, proxy_d3d9.c's design falls back to the StretchRect-
 * copy approach (device B keeps its own private surface, then StretchRect's into a surface it
 * opened via ITS OWN device from device A's handle -- same-device StretchRect is always legal).
 *
 * Result of THIS run determines which of those two designs notes/28's real integration uses.
 */

#define INITGUID
#include <windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>

#define TEX_W 256
#define TEX_H 256

#define CHECK_HR(hr, msg) \
    do { \
        if (FAILED(hr)) { \
            printf("FAIL: %s (hr=0x%08lX)\n", msg, (unsigned long)(hr)); \
            return 1; \
        } \
    } while (0)

static HRESULT flush_and_wait_once(IDirect3DDevice9 *pDevice)
{
    /* One-shot blocking wait is fine HERE - this is a correctness verification POC, not the
     * real per-frame path (which uses the non-blocking mechanism proven in submit_test_poc.c /
     * notes/28 Part D). Kept deliberately simple so this POC tests ONE thing (does cross-device
     * sharing work at all) without also re-proving the sync mechanism. */
    IDirect3DQuery9 *pQuery = NULL;
    HRESULT hr = pDevice->lpVtbl->CreateQuery(pDevice, D3DQUERYTYPE_EVENT, &pQuery);
    if (FAILED(hr)) return hr;
    hr = pQuery->lpVtbl->Issue(pQuery, D3DISSUE_END);
    if (FAILED(hr)) { pQuery->lpVtbl->Release(pQuery); return hr; }
    DWORD start = GetTickCount();
    for (;;) {
        hr = pQuery->lpVtbl->GetData(pQuery, NULL, 0, D3DGETDATA_FLUSH);
        if (hr == S_OK) break;
        if (hr != S_FALSE) { pQuery->lpVtbl->Release(pQuery); return hr; }
        if (GetTickCount() - start > 5000) { pQuery->lpVtbl->Release(pQuery); return E_FAIL; }
        Sleep(1);
    }
    pQuery->lpVtbl->Release(pQuery);
    return S_OK;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== Dual-device shared-surface verification POC ===\n\n");

    WNDCLASSEXA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "PsyVR_DualDevicePOC";
    RegisterClassExA(&wc);
    HWND hwndA = CreateWindowExA(0, wc.lpszClassName, "DevA", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, NULL, NULL, wc.hInstance, NULL);
    HWND hwndB = CreateWindowExA(0, wc.lpszClassName, "DevB", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, NULL, NULL, wc.hInstance, NULL);
    if (!hwndA || !hwndB) { printf("FAIL: CreateWindowExA\n"); return 1; }

    /* ---- Device A: D3D9Ex, originates the shared handle (stands in for our new private VR-bridge device) ---- */
    IDirect3D9Ex *pD3D9Ex = NULL;
    HRESULT hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &pD3D9Ex);
    CHECK_HR(hr, "Direct3DCreate9Ex");

    D3DPRESENT_PARAMETERS ppA;
    ZeroMemory(&ppA, sizeof(ppA));
    ppA.Windowed = TRUE;
    ppA.SwapEffect = D3DSWAPEFFECT_DISCARD;
    ppA.hDeviceWindow = hwndA;
    ppA.BackBufferFormat = D3DFMT_UNKNOWN;
    ppA.BackBufferWidth = 64;
    ppA.BackBufferHeight = 64;
    ppA.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9Ex *pDeviceA = NULL;
    hr = pD3D9Ex->lpVtbl->CreateDeviceEx(pD3D9Ex, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwndA,
        D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE,
        &ppA, NULL, &pDeviceA);
    CHECK_HR(hr, "IDirect3D9Ex::CreateDeviceEx (Device A)");
    printf("[1] Device A (D3D9Ex) created\n");

    IDirect3DTexture9 *pTexA = NULL;
    HANDLE sharedHandle = NULL;
    hr = pDeviceA->lpVtbl->CreateTexture(pDeviceA, TEX_W, TEX_H, 1, D3DUSAGE_RENDERTARGET,
                                          D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &pTexA, &sharedHandle);
    CHECK_HR(hr, "CreateTexture (Device A, shared, handle OUT)");
    IDirect3DSurface9 *pSurfA = NULL;
    hr = pTexA->lpVtbl->GetSurfaceLevel(pTexA, 0, &pSurfA);
    CHECK_HR(hr, "GetSurfaceLevel (Device A)");
    printf("[2] Device A originated shared handle=0x%p\n\n", sharedHandle);

    /* ---- Device B: PLAIN (non-Ex) D3D9, stands in for the GAME's own real device ---- */
    IDirect3D9 *pD3D9Plain = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D9Plain) { printf("FAIL: Direct3DCreate9 (plain)\n"); return 1; }

    D3DPRESENT_PARAMETERS ppB;
    ZeroMemory(&ppB, sizeof(ppB));
    ppB.Windowed = TRUE;
    ppB.SwapEffect = D3DSWAPEFFECT_DISCARD;
    ppB.hDeviceWindow = hwndB;
    ppB.BackBufferFormat = D3DFMT_UNKNOWN;
    ppB.BackBufferWidth = 64;
    ppB.BackBufferHeight = 64;
    ppB.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9 *pDeviceB = NULL;
    hr = pD3D9Plain->lpVtbl->CreateDevice(pD3D9Plain, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwndB,
        D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
        &ppB, &pDeviceB);
    CHECK_HR(hr, "IDirect3D9::CreateDevice (Device B, PLAIN non-Ex)");
    printf("[3] Device B (PLAIN, non-Ex) created - this is what Psychonauts.exe's real device is\n\n");

    /* ---- THE TEST: Device B opens Device A's shared handle directly (as a non-NULL INPUT) ---- */
    HANDLE handleForB = sharedHandle;  /* CreateTexture treats a non-NULL *pSharedHandle as an OPEN request */
    IDirect3DTexture9 *pTexB_opened = NULL;
    hr = pDeviceB->lpVtbl->CreateTexture(pDeviceB, TEX_W, TEX_H, 1, D3DUSAGE_RENDERTARGET,
                                          D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &pTexB_opened, &handleForB);
    printf("[4] Device B CreateTexture(pSharedHandle=DeviceA's handle) -> hr=0x%08lX %s\n",
           (unsigned long)hr, SUCCEEDED(hr) ? "SUCCESS - direct zero-copy open WORKS" : "FAILED - falling back to StretchRect-copy design");

    int directOpenWorks = SUCCEEDED(hr) && pTexB_opened != NULL;

    IDirect3DSurface9 *pSurfB_target = NULL;   /* the surface Device B actually renders into for this test */
    IDirect3DTexture9 *pTexB_private = NULL;   /* fallback: Device B's own private (non-shared) surface */
    IDirect3DTexture9 *pTexB_viaOwnHandle = NULL; /* fallback: Device B opens Device A's handle for the StretchRect DEST */

    if (directOpenWorks) {
        hr = pTexB_opened->lpVtbl->GetSurfaceLevel(pTexB_opened, 0, &pSurfB_target);
        CHECK_HR(hr, "GetSurfaceLevel (Device B opened texture)");
        printf("    -> Device B will render DIRECTLY into the shared surface (zero-copy design confirmed usable)\n\n");
    } else {
        /* Fallback path: Device B renders into its own private surface, then needs to StretchRect
         * into a surface IT ALSO opened via its own device from Device A's handle (StretchRect
         * requires both surfaces to belong to the SAME device). Test that sub-path too, so we know
         * for certain which design proxy_d3d9.c should use. */
        printf("    -> Testing fallback: Device B renders privately, then StretchRect's into its own\n"
               "       same-device-opened view of Device A's shared handle...\n");
        hr = pDeviceB->lpVtbl->CreateTexture(pDeviceB, TEX_W, TEX_H, 1, D3DUSAGE_RENDERTARGET,
                                              D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &pTexB_private, NULL);
        CHECK_HR(hr, "CreateTexture (Device B, private)");
        IDirect3DSurface9 *pSurfB_private = NULL;
        hr = pTexB_private->lpVtbl->GetSurfaceLevel(pTexB_private, 0, &pSurfB_private);
        CHECK_HR(hr, "GetSurfaceLevel (Device B private)");

        HANDLE handleForB2 = sharedHandle;
        hr = pDeviceB->lpVtbl->CreateTexture(pDeviceB, TEX_W, TEX_H, 1, D3DUSAGE_RENDERTARGET,
                                              D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &pTexB_viaOwnHandle, &handleForB2);
        printf("    Device B CreateTexture(pSharedHandle=DeviceA's handle) [2nd attempt] -> hr=0x%08lX\n", (unsigned long)hr);
        if (FAILED(hr) || !pTexB_viaOwnHandle) {
            printf("    -> Device B (plain D3D9) cannot open Device A's (D3D9Ex) shared handle AT ALL, by any\n"
                   "       method tried. Skipping the StretchRect sub-test; proceeding straight to Part 2's\n"
                   "       CPU-round-trip fallback design (the only remaining option).\n");
            pSurfB_target = NULL; /* signal: neither same-device design works, Part 2 is mandatory */
        } else {
            IDirect3DSurface9 *pSurfB_viaOwnHandle = NULL;
            hr = pTexB_viaOwnHandle->lpVtbl->GetSurfaceLevel(pTexB_viaOwnHandle, 0, &pSurfB_viaOwnHandle);
            CHECK_HR(hr, "GetSurfaceLevel (Device B via-own-handle)");

            /* Render into the PRIVATE surface first (simulating the game's existing eye render target). */
            hr = pDeviceB->lpVtbl->SetRenderTarget(pDeviceB, 0, pSurfB_private);
            CHECK_HR(hr, "SetRenderTarget (private)");
            pDeviceB->lpVtbl->BeginScene(pDeviceB);
            pDeviceB->lpVtbl->Clear(pDeviceB, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 77, 88, 99), 1.0f, 0);
            pDeviceB->lpVtbl->EndScene(pDeviceB);

            /* Then StretchRect from the private surface into Device B's own view of the shared surface -
             * same-device StretchRect, always legal, this is the extra copy step this design would add. */
            RECT full = { 0, 0, TEX_W, TEX_H };
            hr = pDeviceB->lpVtbl->StretchRect(pDeviceB, pSurfB_private, &full, pSurfB_viaOwnHandle, &full, D3DTEXF_NONE);
            printf("    StretchRect (private -> Device B's own view of Device A's shared surface) -> hr=0x%08lX\n", (unsigned long)hr);
            if (FAILED(hr)) {
                printf("    -> StretchRect into the shared surface also failed. Proceeding to Part 2.\n");
                pSurfB_target = NULL;
            } else {
                pSurfB_target = pSurfB_viaOwnHandle;
            }
        }
    }

    int sameDeviceDesignWorks = (pSurfB_target != NULL);

    if (sameDeviceDesignWorks) {
        if (directOpenWorks) {
            /* Direct-open path: render straight into the shared surface. */
            hr = pDeviceB->lpVtbl->SetRenderTarget(pDeviceB, 0, pSurfB_target);
            CHECK_HR(hr, "SetRenderTarget (Device B, shared surface, direct)");
            pDeviceB->lpVtbl->BeginScene(pDeviceB);
            pDeviceB->lpVtbl->Clear(pDeviceB, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 77, 88, 99), 1.0f, 0);
            pDeviceB->lpVtbl->EndScene(pDeviceB);
        }
        hr = flush_and_wait_once(pDeviceB);
        CHECK_HR(hr, "flush_and_wait_once (Device B)");
        printf("[5] Device B wrote R=77,G=88,B=99 into the shared surface (via %s), flushed\n\n",
               directOpenWorks ? "direct zero-copy render" : "StretchRect copy");
    } else {
        printf("[5] Skipping same-device write/readback check - neither same-device design worked "
               "on this system (confirmed above). Proceeding straight to Part 2.\n\n");
    }

    /* ---- Now confirm a THIRD, separate D3D11 device (standing in for the real VR-bridge code)
     * sees Device B's write, by opening Device A's ORIGINAL handle (not Device B's view of it). ---- */
    LUID luid9;
    hr = pD3D9Ex->lpVtbl->GetAdapterLUID(pD3D9Ex, D3DADAPTER_DEFAULT, &luid9);
    CHECK_HR(hr, "GetAdapterLUID");

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
            break;
        }
        pAdapter->lpVtbl->Release(pAdapter);
    }
    if (!pChosenAdapter) { printf("FAIL: no matching DXGI adapter\n"); return 1; }

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL gotLevel;
    ID3D11Device *pDevice11 = NULL;
    ID3D11DeviceContext *pContext11 = NULL;
    hr = D3D11CreateDevice((IDXGIAdapter *)pChosenAdapter, D3D_DRIVER_TYPE_UNKNOWN, NULL,
                            D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels),
                            D3D11_SDK_VERSION, &pDevice11, &gotLevel, &pContext11);
    CHECK_HR(hr, "D3D11CreateDevice");

    ID3D11Texture2D *pTex11 = NULL;
    int pass = 1; /* trivially true when this check is skipped - overall result then rests on Part 2 */

    if (sameDeviceDesignWorks) {
        hr = pDevice11->lpVtbl->OpenSharedResource(pDevice11, sharedHandle, &IID_ID3D11Texture2D, (void **)&pTex11);
        CHECK_HR(hr, "ID3D11Device::OpenSharedResource (Device A's ORIGINAL handle)");
        printf("[6] D3D11 opened Device A's original handle OK\n");

        D3D11_TEXTURE2D_DESC desc11;
        pTex11->lpVtbl->GetDesc(pTex11, &desc11);
        desc11.Usage = D3D11_USAGE_STAGING;
        desc11.BindFlags = 0;
        desc11.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc11.MiscFlags = 0;
        ID3D11Texture2D *pStagingReal = NULL;
        hr = pDevice11->lpVtbl->CreateTexture2D(pDevice11, &desc11, NULL, &pStagingReal);
        CHECK_HR(hr, "CreateTexture2D (staging)");
        pContext11->lpVtbl->CopyResource(pContext11, (ID3D11Resource *)pStagingReal, (ID3D11Resource *)pTex11);

        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = pContext11->lpVtbl->Map(pContext11, (ID3D11Resource *)pStagingReal, 0, D3D11_MAP_READ, 0, &mapped);
        CHECK_HR(hr, "Map (staging)");
        BYTE *row = (BYTE *)mapped.pData + 10 * mapped.RowPitch;
        BYTE gotB = row[10 * 4 + 0], gotG = row[10 * 4 + 1], gotR = row[10 * 4 + 2];
        pContext11->lpVtbl->Unmap(pContext11, (ID3D11Resource *)pStagingReal, 0);

        printf("[7] D3D11 readback @ (10,10): R=%u G=%u B=%u (expected R=77 G=88 B=99)\n\n", gotR, gotG, gotB);

        pass = (gotR == 77 && gotG == 88 && gotB == 99);
        printf("=== SAME-DEVICE-DESIGN RESULT: %s ===\n", pass
               ? (directOpenWorks
                  ? "PASS (DIRECT ZERO-COPY DESIGN CONFIRMED)"
                  : "PASS (STRETCHRECT-COPY DESIGN CONFIRMED)")
               : "FAIL - see readback values above");

        pStagingReal->lpVtbl->Release(pStagingReal);
    } else {
        printf("=== SAME-DEVICE-DESIGN RESULT: N/A (neither design usable on this system - see Part 2) ===\n");
    }

    /* ---------------------------------------------------------------------------------------
     * Part 2 (only reached if the direct/StretchRect design above failed, as it did this run):
     * since Device B (plain, same as the real game) cannot touch Device A's (Ex) shared surface
     * at all, the only remaining path is a CPU-visible round trip: GetRenderTargetData copies
     * Device B's rendered content to a SYSTEMMEM surface Device B itself owns; that gets memcpy'd
     * into a SYSTEMMEM surface Device A owns (GetRenderTargetData/UpdateSurface both require
     * source+dest to belong to the SAME device - confirmed via MSDN, not assumed); Device A's
     * UpdateSurface then pushes that into its own D3DPOOL_DEFAULT shared texture, which D3D11
     * already knows how to open (proven above and in notes/25/27).
     *
     * The open question THIS section answers empirically: is GetRenderTargetData itself a
     * blocking/stalling call (a well-known historical D3D9 gotcha), and can the whole chain be
     * kept non-blocking via the same N-buffering + one-shot-non-blocking-poll pattern proven in
     * notes/28 Part 1 (submit_test_poc.c)? Tested with Lock(D3DLOCK_DONOTWAIT) as the
     * non-blocking readiness check on the SYSTEMMEM surface, exactly mirroring the event-query
     * poll used there.
     * --------------------------------------------------------------------------------------- */
    printf("\n--- Part 2: CPU-round-trip fallback, FULLY non-blocking TWO-HOP pipeline ---\n");
    printf("(First attempt used only ONE fence (Device B's readback) and got final-content-match=NO -\n"
           " a real finding: Device A's UpdateSurface is itself an async GPU upload that also needs its\n"
           " OWN completion fence before a downstream reader touches it, confirmed by the diagnostic\n"
           " blocking-flush-fixes-it test. This version adds that SECOND fence, double-buffered, and\n"
           " uses ONLY non-blocking checks throughout - no waits anywhere in the steady-state loop.)\n\n");

    /* ---- Stage 1 (Device B): render -> GetRenderTargetData -> sysmem, double-buffered ---- */
    IDirect3DSurface9 *sysmemB[2] = {0};
    BOOL pendingB[2] = { FALSE, FALSE };
    BYTE expectB_R[2] = {0}, expectB_G[2] = {0}, expectB_Bc[2] = {0};

    /* ---- Stage 2 (Device A): sysmem (scratch, single) -> UpdateSurface -> shared tex, double-buffered,
     * each slot with its OWN event query fencing "is this slot's last upload GPU-complete yet". ---- */
    IDirect3DSurface9 *sysmemA_scratch = NULL;
    IDirect3DTexture9 *texA[2] = {0};
    IDirect3DSurface9 *surfA[2] = {0};
    HANDLE handleA[2] = {0};
    IDirect3DQuery9 *queryA[2] = {0};
    BOOL pendingA[2] = { FALSE, FALSE };
    BYTE expectA_R[2] = {0}, expectA_G[2] = {0}, expectA_Bc[2] = {0};

    for (int s = 0; s < 2; s++) {
        hr = pDeviceB->lpVtbl->CreateOffscreenPlainSurface(pDeviceB, TEX_W, TEX_H, D3DFMT_A8R8G8B8,
                                                             D3DPOOL_SYSTEMMEM, &sysmemB[s], NULL);
        CHECK_HR(hr, "CreateOffscreenPlainSurface (Device B sysmem)");
        hr = pDeviceA->lpVtbl->CreateTexture(pDeviceA, TEX_W, TEX_H, 1, D3DUSAGE_RENDERTARGET,
                                              D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texA[s], &handleA[s]);
        CHECK_HR(hr, "CreateTexture (Device A, part2 shared dest, double-buffered)");
        hr = texA[s]->lpVtbl->GetSurfaceLevel(texA[s], 0, &surfA[s]);
        CHECK_HR(hr, "GetSurfaceLevel (Device A, part2)");
        hr = pDeviceA->lpVtbl->CreateQuery(pDeviceA, D3DQUERYTYPE_EVENT, &queryA[s]);
        CHECK_HR(hr, "CreateQuery (Device A, part2)");
    }
    hr = pDeviceA->lpVtbl->CreateOffscreenPlainSurface(pDeviceA, TEX_W, TEX_H, D3DFMT_A8R8G8B8,
                                                         D3DPOOL_SYSTEMMEM, &sysmemA_scratch, NULL);
    CHECK_HR(hr, "CreateOffscreenPlainSurface (Device A sysmem scratch)");

    IDirect3DTexture9 *pRTTex = NULL;
    IDirect3DSurface9 *pRTSurf = NULL;
    hr = pDeviceB->lpVtbl->CreateTexture(pDeviceB, TEX_W, TEX_H, 1, D3DUSAGE_RENDERTARGET,
                                          D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &pRTTex, NULL);
    CHECK_HR(hr, "CreateTexture (Device B, part2 render target)");
    hr = pRTTex->lpVtbl->GetSurfaceLevel(pRTTex, 0, &pRTSurf);
    CHECK_HR(hr, "GetSurfaceLevel (Device B, part2)");

    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    double grtdSumMs = 0.0, grtdMinMs = 1e18, grtdMaxMs = 0.0;
    double pollSumUs = 0.0, pollMinUs = 1e18, pollMaxUs = 0.0;
    int pollSamples = 0;
    int hop1Count = 0, hop1Skipped = 0;   /* B-sysmem -> A-upload hop */
    int hop2Count = 0, hop2Skipped = 0;   /* A-upload-complete -> "ready to submit" hop */
    int mismatchCount = 0;
    BYTE lastVerifiedR = 0, lastVerifiedG = 0, lastVerifiedB = 0;
    BOOL haveVerified = FALSE;
    const int N2 = 150; /* more iterations than Part 1's stage - this pipeline has more latency to settle */

    /* D3D11 side: open BOTH of Device A's shared handles once, up front (real code would do this once too). */
    ID3D11Texture2D *tex11A[2] = {0};
    hr = pDevice11->lpVtbl->OpenSharedResource(pDevice11, handleA[0], &IID_ID3D11Texture2D, (void **)&tex11A[0]);
    CHECK_HR(hr, "OpenSharedResource (part2 slot 0)");
    hr = pDevice11->lpVtbl->OpenSharedResource(pDevice11, handleA[1], &IID_ID3D11Texture2D, (void **)&tex11A[1]);
    CHECK_HR(hr, "OpenSharedResource (part2 slot 1)");
    D3D11_TEXTURE2D_DESC desc2;
    tex11A[0]->lpVtbl->GetDesc(tex11A[0], &desc2);
    desc2.Usage = D3D11_USAGE_STAGING; desc2.BindFlags = 0; desc2.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc2.MiscFlags = 0;
    ID3D11Texture2D *pStaging2 = NULL;
    hr = pDevice11->lpVtbl->CreateTexture2D(pDevice11, &desc2, NULL, &pStaging2);
    CHECK_HR(hr, "CreateTexture2D (part2 staging)");

    for (int i = 0; i < N2; i++) {
        int bCur = i % 2, bPrev = (i + 1) % 2;

        /* --- Render + readback (Device B) --- */
        BYTE r = (BYTE)((i * 5 + 10) % 256), g = (BYTE)((i * 9 + 20) % 256), b = (BYTE)((i * 13 + 30) % 256);
        pDeviceB->lpVtbl->SetRenderTarget(pDeviceB, 0, pRTSurf);
        pDeviceB->lpVtbl->BeginScene(pDeviceB);
        pDeviceB->lpVtbl->Clear(pDeviceB, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, r, g, b), 1.0f, 0);
        pDeviceB->lpVtbl->EndScene(pDeviceB);
        expectB_R[bCur] = r; expectB_G[bCur] = g; expectB_Bc[bCur] = b;

        QueryPerformanceCounter(&t0);
        hr = pDeviceB->lpVtbl->GetRenderTargetData(pDeviceB, pRTSurf, sysmemB[bCur]);
        QueryPerformanceCounter(&t1);
        double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
        grtdSumMs += ms; if (ms < grtdMinMs) grtdMinMs = ms; if (ms > grtdMaxMs) grtdMaxMs = ms;
        if (FAILED(hr)) { printf("FAIL: GetRenderTargetData hr=0x%08lX\n", (unsigned long)hr); return 1; }
        pendingB[bCur] = TRUE;

        /* --- Hop 1: non-blocking check Device B's PREVIOUS sysmem slot; if ready, promote to Device A --- */
        if (pendingB[bPrev]) {
            D3DLOCKED_RECT lockedB;
            QueryPerformanceCounter(&t0);
            HRESULT lockHr = sysmemB[bPrev]->lpVtbl->LockRect(sysmemB[bPrev], &lockedB, NULL, D3DLOCK_READONLY | D3DLOCK_DONOTWAIT);
            QueryPerformanceCounter(&t1);
            double us = (double)(t1.QuadPart - t0.QuadPart) * 1000000.0 / (double)freq.QuadPart;
            pollSumUs += us; pollSamples++;
            if (us < pollMinUs) pollMinUs = us; if (us > pollMaxUs) pollMaxUs = us;

            if (lockHr == S_OK) {
                int aCur = hop1Count % 2;
                /* Before overwriting Device A's slot aCur, non-blockingly confirm its PREVIOUS upload
                 * (if any) already completed - otherwise skip this promotion entirely rather than wait. */
                BOOL aSlotFree = TRUE;
                if (pendingA[aCur]) {
                    HRESULT qHr = queryA[aCur]->lpVtbl->GetData(queryA[aCur], NULL, 0, 0);
                    aSlotFree = (qHr == S_OK);
                }
                if (aSlotFree) {
                    D3DLOCKED_RECT lockedA;
                    hr = sysmemA_scratch->lpVtbl->LockRect(sysmemA_scratch, &lockedA, NULL, 0);
                    CHECK_HR(hr, "LockRect (Device A sysmem scratch)");
                    for (int y = 0; y < TEX_H; y++) {
                        memcpy((BYTE *)lockedA.pBits + y * lockedA.Pitch,
                               (BYTE *)lockedB.pBits + y * lockedB.Pitch, TEX_W * 4);
                    }
                    sysmemA_scratch->lpVtbl->UnlockRect(sysmemA_scratch);

                    RECT full = { 0, 0, TEX_W, TEX_H };
                    hr = pDeviceA->lpVtbl->UpdateSurface(pDeviceA, sysmemA_scratch, &full, surfA[aCur], NULL);
                    if (FAILED(hr)) { printf("FAIL: UpdateSurface hr=0x%08lX\n", (unsigned long)hr); return 1; }

                    /* Issue+kick (non-blocking) the fence for THIS upload into slot aCur. */
                    queryA[aCur]->lpVtbl->Issue(queryA[aCur], D3DISSUE_END);
                    queryA[aCur]->lpVtbl->GetData(queryA[aCur], NULL, 0, D3DGETDATA_FLUSH);
                    pendingA[aCur] = TRUE;
                    expectA_R[aCur] = expectB_R[bPrev]; expectA_G[aCur] = expectB_G[bPrev]; expectA_Bc[aCur] = expectB_Bc[bPrev];
                    hop1Count++;
                } else {
                    hop1Skipped++; /* Device A backpressure - drop this frame's promotion, no wait */
                }
                sysmemB[bPrev]->lpVtbl->UnlockRect(sysmemB[bPrev]);
            }
            /* if lockHr != S_OK: Device B's readback for this slot isn't done yet - just move on, no wait */
        }

        /* --- Hop 2: non-blocking check whichever Device A slot is NOT the one most recently written -
         * i.e. one hop1 "generation" behind - and if its fence is signaled, that's the frame safe to
         * hand to the compositor (here: verify via D3D11 readback instead, since there's no real
         * IVRCompositor::Submit target in THIS particular POC - that part is already proven in
         * submit_test_poc.c / Part 1). --- */
        if (hop1Count >= 2) {
            int aConsume = (hop1Count - 2) % 2;
            if (pendingA[aConsume]) {
                HRESULT qHr = queryA[aConsume]->lpVtbl->GetData(queryA[aConsume], NULL, 0, 0);
                if (qHr == S_OK) {
                    pContext11->lpVtbl->CopyResource(pContext11, (ID3D11Resource *)pStaging2, (ID3D11Resource *)tex11A[aConsume]);
                    D3D11_MAPPED_SUBRESOURCE mp;
                    hr = pContext11->lpVtbl->Map(pContext11, (ID3D11Resource *)pStaging2, 0, D3D11_MAP_READ, 0, &mp);
                    CHECK_HR(hr, "Map (part2 staging, steady-state)");
                    BYTE *row = (BYTE *)mp.pData + 10 * mp.RowPitch;
                    BYTE gB = row[10*4+0], gG = row[10*4+1], gR = row[10*4+2];
                    pContext11->lpVtbl->Unmap(pContext11, (ID3D11Resource *)pStaging2, 0);

                    if (gR != expectA_R[aConsume] || gG != expectA_G[aConsume] || gB != expectA_Bc[aConsume]) {
                        mismatchCount++;
                        printf("[P2 hop2 gen %d] MISMATCH: expected R=%u G=%u B=%u got R=%u G=%u B=%u\n",
                               hop1Count - 2, expectA_R[aConsume], expectA_G[aConsume], expectA_Bc[aConsume], gR, gG, gB);
                    }
                    lastVerifiedR = gR; lastVerifiedG = gG; lastVerifiedB = gB; haveVerified = TRUE;
                    hop2Count++;
                } else {
                    hop2Skipped++;
                }
            }
        }

        Sleep(16);
    }

    printf("[P2-1] GetRenderTargetData over %d iters: min=%.4fms avg=%.4fms max=%.4fms\n",
           N2, grtdMinMs, grtdSumMs / N2, grtdMaxMs);
    printf("[P2-2] Non-blocking poll cost (both hops' checks): min=%.2fus avg=%.2fus max=%.2fus (%d samples)\n",
           pollMinUs, pollSamples ? pollSumUs / pollSamples : 0.0, pollMaxUs, pollSamples);
    printf("[P2-3] hop1 (B-readback -> A-upload): promoted=%d skipped=%d\n", hop1Count, hop1Skipped);
    printf("[P2-4] hop2 (A-upload-complete -> verified-ready): verified=%d skipped=%d mismatches=%d\n\n",
           hop2Count, hop2Skipped, mismatchCount);

    int pass2 = haveVerified && (mismatchCount == 0) && (hop2Count > 0);
    printf("=== PART 2 RESULT: %s ===\n", pass2
           ? "PASS - fully non-blocking two-hop CPU-round-trip pipeline, zero pixel mismatches, no waits anywhere"
           : "FAIL - see mismatch/verified counts above");

    tex11A[0]->lpVtbl->Release(tex11A[0]);
    tex11A[1]->lpVtbl->Release(tex11A[1]);
    pStaging2->lpVtbl->Release(pStaging2);
    for (int s = 0; s < 2; s++) {
        sysmemB[s]->lpVtbl->Release(sysmemB[s]);
        queryA[s]->lpVtbl->Release(queryA[s]);
        surfA[s]->lpVtbl->Release(surfA[s]);
        texA[s]->lpVtbl->Release(texA[s]);
    }
    sysmemA_scratch->lpVtbl->Release(sysmemA_scratch);
    pRTSurf->lpVtbl->Release(pRTSurf);
    pRTTex->lpVtbl->Release(pRTTex);

    if (pTex11) pTex11->lpVtbl->Release(pTex11);
    pContext11->lpVtbl->Release(pContext11);
    pDevice11->lpVtbl->Release(pDevice11);
    pChosenAdapter->lpVtbl->Release(pChosenAdapter);
    pFactory->lpVtbl->Release(pFactory);
    if (pTexB_opened) pTexB_opened->lpVtbl->Release(pTexB_opened);
    if (pTexB_private) pTexB_private->lpVtbl->Release(pTexB_private);
    if (pTexB_viaOwnHandle) pTexB_viaOwnHandle->lpVtbl->Release(pTexB_viaOwnHandle);
    pDeviceB->lpVtbl->Release(pDeviceB);
    pD3D9Plain->lpVtbl->Release(pD3D9Plain);
    pSurfA->lpVtbl->Release(pSurfA);
    pTexA->lpVtbl->Release(pTexA);
    pDeviceA->lpVtbl->Release(pDeviceA);
    pD3D9Ex->lpVtbl->Release(pD3D9Ex);
    DestroyWindow(hwndA);
    DestroyWindow(hwndB);

    return (pass && pass2) ? 0 : 1;
}
