#pragma once
/* hxr_format - which headset swapchain format takes the game's 8-bit picture, and the byte fix-up it needs
 * (2026-10-07, /lm hard-reset-vr reader). Pure C: test/hxr_format_test.c runs it alone.
 *
 * D3D9's X8R8G8B8 / A8R8G8B8 sit in memory as bytes B,G,R,X/A - the same layout as DXGI B8G8R8A8. The game's picture
 * is already gamma-encoded, so an _SRGB swapchain format is preferred (the runtime then shows it at the right
 * brightness); a plain UNORM one is the fallback. If the runtime only offers R8G8B8A8, red and blue are swapped. */
#include <stddef.h>
#include <stdint.h>

#define HXR_DXGI_R8G8B8A8_UNORM       28
#define HXR_DXGI_R8G8B8A8_UNORM_SRGB  29
#define HXR_DXGI_B8G8R8A8_UNORM       87
#define HXR_DXGI_B8G8R8A8_UNORM_SRGB  91

/* The best of the runtime's offered formats, in OUR order of preference (g_wanted), or 0 = none usable. */
int64_t hxr_pick_format(const int64_t *offered, uint32_t n);

/* 1 if this swapchain format needs red and blue swapped from the D3D9 byte order. */
int hxr_needs_swizzle(int64_t fmt);

/* In place, over `pixels` 32-bit pixels: alpha set to opaque (X8 bytes are undefined), red/blue swapped if asked. */
void hxr_fix_pixels(uint32_t *px, size_t pixels, int swizzle);
