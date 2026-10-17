/* hxr_format.c - swapchain format choice and pixel fix-up. See hxr_format.h. */
#include <stddef.h>
#include "hxr_format.h"

/* most wanted first: same byte order + sRGB, then sRGB with a swap, then the UNORM ones */
static const int64_t g_wanted[] = {
    HXR_DXGI_B8G8R8A8_UNORM_SRGB, HXR_DXGI_R8G8B8A8_UNORM_SRGB, HXR_DXGI_B8G8R8A8_UNORM, HXR_DXGI_R8G8B8A8_UNORM,
};
#define WANTED_COUNT (sizeof g_wanted / sizeof g_wanted[0])

int64_t hxr_pick_format(const int64_t *offered, uint32_t n) {
    size_t k;
    uint32_t i;
    for (k = 0; k < WANTED_COUNT; ++k)
        for (i = 0; i < n; ++i)
            if (offered[i] == g_wanted[k]) return offered[i];
    return 0;
}

int hxr_needs_swizzle(int64_t fmt) {
    return fmt == HXR_DXGI_R8G8B8A8_UNORM || fmt == HXR_DXGI_R8G8B8A8_UNORM_SRGB;
}

void hxr_fix_pixels(uint32_t *px, size_t pixels, int swizzle) {
    size_t i;
    if (swizzle) {
        for (i = 0; i < pixels; ++i) {
            uint32_t v = px[i];   /* 0xAARRGGBB as a little-endian word -> 0xAABBGGRR */
            px[i] = 0xFF000000u | ((v & 0xFFu) << 16) | (v & 0xFF00u) | ((v >> 16) & 0xFFu);
        }
    } else {
        for (i = 0; i < pixels; ++i) px[i] |= 0xFF000000u;
    }
}
