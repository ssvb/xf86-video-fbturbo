/*
 * Copyright © 2013 Siarhei Siamashka <siarhei.siamashka@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef SUNXI_DISP_H
#define SUNXI_DISP_H

#include <inttypes.h>

/*
 * Support for Allwinner A10 display controller features such as layers
 * and hardware cursor
 */
typedef struct {
    int                 fd_fb;
    int                 fd_disp;
    int                 fd_g2d;
    int                 fb_id;             /* /dev/fb0 = 0, /dev/fb1 = 1 */

    int                 xres, yres, bits_per_pixel;
    uint8_t            *framebuffer_addr;  /* mmapped address */
    uintptr_t           framebuffer_paddr; /* physical address */
    uint32_t            framebuffer_size;  /* total size of the framebuffer */
    int                 framebuffer_height;/* virtual vertical resolution */
    uint32_t            gfx_layer_size;    /* the size of the primary layer */

    uint8_t            *xserver_fbmem; /* framebuffer mapping done by xserver */

    /* Hardware cursor support */
    int                 cursor_enabled;
    int                 cursor_x, cursor_y;

    /* Layers support */
    int                 layer_id;
    int                 layer_has_scaler;
} sunxi_disp_t;

sunxi_disp_t *sunxi_disp_init(const char *fb_device, void *xserver_fbmem);
int sunxi_disp_close(sunxi_disp_t *ctx);

/*
 * Support for hardware cursor, which has 64x64 size, 2 bits per pixel,
 * four 32-bit ARGB entries in the palette.
 */
int sunxi_hw_cursor_load_64x64x2bpp(sunxi_disp_t *ctx, uint8_t pixeldata[1024]);
int sunxi_hw_cursor_load_32x32x8bpp(sunxi_disp_t *ctx, uint8_t pixeldata[1024]);
int sunxi_hw_cursor_load_palette(sunxi_disp_t *ctx, uint32_t *palette, int n);
int sunxi_hw_cursor_set_position(sunxi_disp_t *ctx, int x, int y);
int sunxi_hw_cursor_show(sunxi_disp_t *ctx);
int sunxi_hw_cursor_hide(sunxi_disp_t *ctx);

/*
 * Support for one sunxi disp layer (even though there are more than
 * one available) in the offscreen part of framebuffer, which may be
 * useful for DRI2 vsync aware frame flipping and implementing XV
 * extension (video overlay).
 */

int sunxi_layer_reserve(sunxi_disp_t *ctx);
int sunxi_layer_release(sunxi_disp_t *ctx);

int sunxi_layer_set_x8r8g8b8_input_buffer(sunxi_disp_t  *ctx,
                                          uint32_t      offset_in_framebuffer,
                                          int           width,
                                          int           height,
                                          int           stride);

int sunxi_layer_set_output_window(sunxi_disp_t *ctx, int x, int y, int w, int h);

int sunxi_layer_show(sunxi_disp_t *ctx);
int sunxi_layer_hide(sunxi_disp_t *ctx);

/*
 * Wait for vsync
 */
int sunxi_wait_for_vsync(sunxi_disp_t *ctx);

/*
 * Simple G2D fill and blit operations
 */

int sunxi_g2d_fill_a8r8g8b8(sunxi_disp_t *disp,
                            int           x,
                            int           y,
                            int           w,
                            int           h,
                            uint32_t      color);

int sunxi_g2d_blit_a8r8g8b8(sunxi_disp_t *disp,
                            int           dst_x,
                            int           dst_y,
                            int           src_x,
                            int           src_y,
                            int           w,
                            int           h);

#endif
