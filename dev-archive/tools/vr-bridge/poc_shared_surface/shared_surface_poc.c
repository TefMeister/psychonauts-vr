/*
 * D3D9Ex -> D3D11 shared-surface interop proof-of-concept.
 *
 * Purpose (see notes/25-vr-runtime-bridge-scaffolding.md for full writeup): this project's
 * eventual VR bridge needs to hand Psychonauts' own D3D9-rendered eye surfaces to OpenVR's
 * IVRCompositor::Submit(), which only accepts D3D11 (or D3D12/Vulkan/GL) textures - never plain
 * D3D9. The standard workaround is a D3D9Ex "shared surface": a render target created with a
 * Win32 shared HANDLE via IDirect3DDevice9Ex::CreateTexture, which a completely separate D3D11
 * device can open with ID3D11Device::OpenSharedResource and read as its own texture.
 *
 * This is a genuinely open technical question for THIS project, not a settled fact - does that
 * interop actually work correctly (right pixels, right timing, no silent corruption) in this
 * environment? This program answers that empirically, standalone, with NO dependency on
 * Psychonauts, SteamVR, or OpenVR at all:
 *
 *   1. Create a real IDirect3DDevice9Ex device (offscreen, hidden window, never Present()s).
 *   2. Create one render-target texture on it with a shared HANDLE.
 *   3. Clear that surface to a known, distinctive solid color (color A) and force a full GPU
 *      flush (event query, D3DGETDATA_FLUSH) so the write is guaranteed complete, not just
 *      queued.
 *   4. Create a SEPARATE ID3D11Device, matched to the SAME physical adapter as the D3D9Ex device
 *      via DXGI adapter LUID (required for cross-API sharing to be valid at all on multi-GPU
 *      systems).
 *   5. Open the D3D9Ex surface's shared handle from the D3D11 device via OpenSharedResource,
 *      copy it into a CPU-readable staging texture, and read back real pixel bytes - compare
 *      against color A.
 *   6. Re-clear the SAME D3D9Ex surface to a second, different color (color B), flush again, and
 *      read back through the SAME already-open D3D11 texture handle a second time - if this also
 *      matches, that proves genuine live/shared GPU memory (the same underlying surface, updated
 *      in place), not a one-shot copy-on-open semantic that would only happen to work once.
 *
 * Build: see build.ps1 in this directory (32-bit, matching the eventual game-hooked DLL's
 * architecture, though nothing here is architecture-specific). Run the resulting .exe directly -
 * no game, no SteamVR, no debugger needed. Prints a clear PASS/FAIL banner plus the actual
 * sampled RGBA bytes at each stage.
 */

#define INITGUID
#include <windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>
#include <string.h>

#define TEX_W 64
#define TEX_H 64

#define CHECK_HR(hr, msg) \
    do { \
        if (FAILED(hr)) { \
            printf("FAIL: %s (hr=0x%08lX)\n", msg, (unsigned long)(hr)); \
            return 1; \
        } \
    } while (0)

/* Force all queued D3D9 GPU work to actually complete before returning - required before a
 * cross-API reader can trust the surface contents. D3DGETDATA_FLUSH + poll-until-S_OK on an
 * EVENT query is the standard D3D9 technique for this (there is no explicit "Flush" method on
 * IDirect3DDevice9). */
static HRESULT flush_d3d9(IDirect3DDevice9Ex *pDevice)
{
    IDirect3DQuery9 *pQuery = NULL;
    HRESULT hr = pDevice->lpVtbl->CreateQuery(pDevice, D3DQUERYTYPE_EVENT, &pQuery);
    if (FAILED(hr)) {
        return hr;
    }

    hr = pQuery->lpVtbl->Issue(pQuery, D3DISSUE_END);
    if (FAILED(hr)) {
        pQuery->lpVtbl->Release(pQuery);
        return hr;
    }

    DWORD start = GetTickCount();
    for (;;) {
        hr = pQuery->lpVtbl->GetData(pQuery, NULL, 0, D3DGETDATA_FLUSH);
        if (hr == S_OK) {
            break;
        }
        if (hr != S_FALSE) {
            pQuery->lpVtbl->Release(pQuery);
            return hr;
        }
        if (GetTickCount() - start > 5000) {
            pQuery->lpVtbl->Release(pQuery);
            return E_FAIL; /* timed out waiting for GPU flush */
        }
        Sleep(1);
    }

    pQuery->lpVtbl->Release(pQuery);
    return S_OK;
}

static HRESULT clear_to_color(IDirect3DDevice9Ex *pDevice, IDirect3DSurface9 *pSurf, D3DCOLOR color)
{
    HRESULT hr = pDevice->lpVtbl->SetRenderTarget(pDevice, 0, pSurf);
    if (FAILED(hr)) {
        return hr;
    }
    hr = pDevice->lpVtbl->BeginScene(pDevice);
    if (FAILED(hr)) {
        return hr;
    }
    hr = pDevice->lpVtbl->Clear(pDevice, 0, NULL, D3DCLEAR_TARGET, color, 1.0f, 0);
    pDevice->lpVtbl->EndScene(pDevice);
    if (FAILED(hr)) {
        return hr;
    }
    return flush_d3d9(pDevice);
}

/* Reads one BGRA pixel from a D3D11 texture via a CPU-readable staging copy. out[] receives
 * bytes in the texture's own memory order (B,G,R,A for DXGI_FORMAT_B8G8R8A8_UNORM, which is
 * what a D3D9 D3DFMT_A8R8G8B8 shared surface opens as on the D3D11 side). */
static HRESULT read_pixel(ID3D11Device *dev, ID3D11DeviceContext *ctx, ID3D11Texture2D *tex,
                           UINT x, UINT y, BYTE out[4])
{
    D3D11_TEXTURE2D_DESC desc;
    tex->lpVtbl->GetDesc(tex, &desc);

    D3D11_TEXTURE2D_DESC sdesc = desc;
    sdesc.Usage = D3D11_USAGE_STAGING;
    sdesc.BindFlags = 0;
    sdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sdesc.MiscFlags = 0;

    ID3D11Texture2D *staging = NULL;
    HRESULT hr = dev->lpVtbl->CreateTexture2D(dev, &sdesc, NULL, &staging);
    if (FAILED(hr)) {
        return hr;
    }

    ctx->lpVtbl->CopyResource(ctx, (ID3D11Resource *)staging, (ID3D11Resource *)tex);

    D3D11_MAPPED_SUBRESOURCE map;
    hr = ctx->lpVtbl->Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map);
    if (FAILED(hr)) {
        staging->lpVtbl->Release(staging);
        return hr;
    }

    BYTE *row = (BYTE *)map.pData + (size_t)y * map.RowPitch;
    memcpy(out, row + (size_t)x * 4, 4);

    ctx->lpVtbl->Unmap(ctx, (ID3D11Resource *)staging, 0);
    staging->lpVtbl->Release(staging);
    return S_OK;
}

int main(void)
{
    printf("=== D3D9Ex -> D3D11 shared-surface interop proof-of-concept ===\n\n");

    /* --- Stage 0: hidden window (D3D9Ex CreateDeviceEx requires a real HWND; never shown,
     * never Present()'d - this whole test is purely offscreen). --- */
    WNDCLASSEXA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "PsyVR_SharedSurfacePOC";
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "PsyVR shared-surface POC",
                                 WS_OVERLAPPEDWINDOW, 0, 0, TEX_W, TEX_H,
                                 NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) {
        printf("FAIL: CreateWindowExA\n");
        return 1;
    }

    /* --- Stage 1: D3D9Ex device --- */
    IDirect3D9Ex *pD3D9Ex = NULL;
    HRESULT hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &pD3D9Ex);
    CHECK_HR(hr, "Direct3DCreate9Ex");
    printf("[1] Direct3DCreate9Ex OK\n");

    LUID luid9;
    hr = pD3D9Ex->lpVtbl->GetAdapterLUID(pD3D9Ex, D3DADAPTER_DEFAULT, &luid9);
    CHECK_HR(hr, "IDirect3D9Ex::GetAdapterLUID");
    printf("[1] D3D9Ex adapter LUID = %08lX:%08lX\n",
           (unsigned long)luid9.HighPart, (unsigned long)luid9.LowPart);

    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hwnd;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.BackBufferWidth = TEX_W;
    pp.BackBufferHeight = TEX_H;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9Ex *pDevice = NULL;
    hr = pD3D9Ex->lpVtbl->CreateDeviceEx(
        pD3D9Ex, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE,
        &pp, NULL, &pDevice);
    CHECK_HR(hr, "IDirect3D9Ex::CreateDeviceEx");
    printf("[1] CreateDeviceEx OK (D3D9Ex device created)\n\n");

    /* --- Stage 2: shared render-target texture --- */
    IDirect3DTexture9 *pTex9 = NULL;
    HANDLE sharedHandle = NULL;
    hr = pDevice->lpVtbl->CreateTexture(pDevice, TEX_W, TEX_H, 1, D3DUSAGE_RENDERTARGET,
                                         D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &pTex9, &sharedHandle);
    CHECK_HR(hr, "CreateTexture (shared)");
    printf("[2] CreateTexture (shared, %ux%u, A8R8G8B8) OK, sharedHandle=%p\n\n", TEX_W, TEX_H, sharedHandle);

    IDirect3DSurface9 *pSurf9 = NULL;
    hr = pTex9->lpVtbl->GetSurfaceLevel(pTex9, 0, &pSurf9);
    CHECK_HR(hr, "GetSurfaceLevel");

    /* --- Stage 3: separate D3D11 device, matched to the same physical adapter --- */
    IDXGIFactory1 *pFactory = NULL;
    hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&pFactory);
    CHECK_HR(hr, "CreateDXGIFactory1");

    IDXGIAdapter1 *pChosenAdapter = NULL;
    for (UINT i = 0;; i++) {
        IDXGIAdapter1 *pAdapter = NULL;
        hr = pFactory->lpVtbl->EnumAdapters1(pFactory, i, &pAdapter);
        if (hr == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_ADAPTER_DESC1 desc;
        pAdapter->lpVtbl->GetDesc1(pAdapter, &desc);
        if (desc.AdapterLuid.LowPart == luid9.LowPart && desc.AdapterLuid.HighPart == luid9.HighPart) {
            pChosenAdapter = pAdapter;
            wprintf(L"[3] Matched D3D11 adapter to D3D9Ex's LUID: %s\n", desc.Description);
            break;
        }
        pAdapter->lpVtbl->Release(pAdapter);
    }
    if (!pChosenAdapter) {
        printf("FAIL: no DXGI adapter matched the D3D9Ex device's LUID\n");
        return 1;
    }

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL gotLevel;
    ID3D11Device *pDevice11 = NULL;
    ID3D11DeviceContext *pContext11 = NULL;
    hr = D3D11CreateDevice((IDXGIAdapter *)pChosenAdapter, D3D_DRIVER_TYPE_UNKNOWN, NULL,
                            D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels),
                            D3D11_SDK_VERSION, &pDevice11, &gotLevel, &pContext11);
    CHECK_HR(hr, "D3D11CreateDevice");
    printf("[3] D3D11CreateDevice OK, feature level=0x%X\n\n", gotLevel);

    /* --- Stage 4: open the D3D9Ex shared surface from the D3D11 device --- */
    ID3D11Texture2D *pTex11 = NULL;
    hr = pDevice11->lpVtbl->OpenSharedResource(pDevice11, sharedHandle, &IID_ID3D11Texture2D, (void **)&pTex11);
    CHECK_HR(hr, "ID3D11Device::OpenSharedResource");
    printf("[4] OpenSharedResource OK - D3D11 opened the D3D9Ex-created shared surface\n");

    D3D11_TEXTURE2D_DESC desc11;
    pTex11->lpVtbl->GetDesc(pTex11, &desc11);
    printf("    D3D11 texture desc: %ux%u, DXGI format=%d\n\n", desc11.Width, desc11.Height, (int)desc11.Format);

    /* --- Stage 5: color A round-trip --- */
    D3DCOLOR colorA = D3DCOLOR_ARGB(255, 200, 64, 32);   /* R=200 G=64  B=32 */
    hr = clear_to_color(pDevice, pSurf9, colorA);
    CHECK_HR(hr, "Clear to color A + flush");
    printf("[5] D3D9Ex side cleared the shared surface to color A (R=200,G=64,B=32), flushed\n");

    BYTE px[4];
    hr = read_pixel(pDevice11, pContext11, pTex11, 10, 10, px);
    CHECK_HR(hr, "read_pixel after color A");
    printf("    D3D11 readback @ (10,10): B=%u G=%u R=%u A=%u\n", px[0], px[1], px[2], px[3]);
    int passA = (px[2] == 200 && px[1] == 64 && px[0] == 32);
    printf("    -> %s\n\n", passA ? "MATCH (color A)" : "MISMATCH");

    /* --- Stage 6: color B round-trip through the SAME already-open D3D11 texture handle,
     * proving live/shared memory rather than a one-shot copy-on-open. --- */
    D3DCOLOR colorB = D3DCOLOR_ARGB(255, 10, 220, 90);   /* R=10  G=220 B=90 */
    hr = clear_to_color(pDevice, pSurf9, colorB);
    CHECK_HR(hr, "Clear to color B + flush");
    printf("[6] D3D9Ex side re-cleared the SAME shared surface to color B (R=10,G=220,B=90), flushed\n");

    hr = read_pixel(pDevice11, pContext11, pTex11, 10, 10, px);
    CHECK_HR(hr, "read_pixel after color B");
    printf("    D3D11 readback @ (10,10): B=%u G=%u R=%u A=%u\n", px[0], px[1], px[2], px[3]);
    int passB = (px[2] == 10 && px[1] == 220 && px[0] == 90);
    printf("    -> %s\n\n", passB ? "MATCH (color B) -- proves LIVE sharing, not a one-shot snapshot" : "MISMATCH");

    printf("=== RESULT: %s ===\n",
           (passA && passB)
               ? "PASS - D3D9Ex shared surface is correctly and LIVE readable from a separate D3D11 device"
               : "FAIL - see mismatches above");

    /* Cleanup (release order: D3D11 objects, then D3D9 objects, then window). Legacy D3D9(Ex)
     * shared handles are not ordinary Win32 handles and are not CloseHandle()'d - their lifetime
     * is tied to the owning D3D9 resource. */
    pTex11->lpVtbl->Release(pTex11);
    pContext11->lpVtbl->Release(pContext11);
    pDevice11->lpVtbl->Release(pDevice11);
    pChosenAdapter->lpVtbl->Release(pChosenAdapter);
    pFactory->lpVtbl->Release(pFactory);

    pSurf9->lpVtbl->Release(pSurf9);
    pTex9->lpVtbl->Release(pTex9);
    pDevice->lpVtbl->Release(pDevice);
    pD3D9Ex->lpVtbl->Release(pD3D9Ex);

    DestroyWindow(hwnd);

    return (passA && passB) ? 0 : 1;
}
