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

#include <inttypes.h>
#include <linux/fb.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "sunxi_disp.h"
#include "sunxi_disp_ioctl.h"

/*****************************************************************************/

sunxi_disp_t *sunxi_disp_init(const char *device)
{
    sunxi_disp_t *ctx = calloc(sizeof(sunxi_disp_t), 1);
    struct fb_var_screeninfo fb_var;
    struct fb_fix_screeninfo fb_fix;

    int tmp, version;
    int gfx_layer_size;
    int ovl_layer_size;

    if (strcmp(device, "/dev/fb0") == 0) {
        ctx->fb_id = 0;
    }
    else if (strcmp(device, "/dev/fb1") == 0) {
        ctx->fb_id = 1;
    }
    else
    {
        free(ctx);
        return NULL;
    }

    ctx->fd_disp = open("/dev/disp", O_RDWR);

    /* maybe it's even not a sunxi hardware */
    if (ctx->fd_disp < 0) {
        free(ctx);
        return NULL;
    }

    /* version check */
    tmp = SUNXI_DISP_VERSION;
    version = ioctl(ctx->fd_disp, DISP_CMD_VERSION, &tmp);
    if (version < 0) {
        close(ctx->fd_disp);
        free(ctx);
        return NULL;
    }

    ctx->fd_fb = open(device, O_RDWR);
    if (ctx->fd_fb < 0) {
        close(ctx->fd_disp);
        free(ctx);
        return NULL;
    }

    if (ioctl(ctx->fd_fb, FBIOGET_VSCREENINFO, &fb_var) < 0 ||
        ioctl(ctx->fd_fb, FBIOGET_FSCREENINFO, &fb_fix) < 0)
    {
        close(ctx->fd_fb);
        close(ctx->fd_disp);
        free(ctx);
        return NULL;
    }

    ctx->xres = fb_var.xres;
    ctx->yres = fb_var.yres;
    ctx->bits_per_pixel = fb_var.bits_per_pixel;
    ctx->framebuffer_paddr = fb_fix.smem_start;
    ctx->framebuffer_size = fb_fix.smem_len;

    ctx->gfx_layer_size = ctx->xres * ctx->yres * fb_var.bits_per_pixel / 8;

    if (ctx->framebuffer_size < ctx->gfx_layer_size) {
        close(ctx->fd_fb);
        close(ctx->fd_disp);
        free(ctx);
        return NULL;
    }

    /* mmap framebuffer memory */
    ctx->framebuffer_addr = (uint8_t *)mmap(0, ctx->framebuffer_size,
                                            PROT_READ | PROT_WRITE,
                                            MAP_SHARED, ctx->fd_fb, 0);
    if (ctx->framebuffer_addr == MAP_FAILED) {
        close(ctx->fd_fb);
        close(ctx->fd_disp);
        free(ctx);
        return NULL;
    }

    ctx->cursor_enabled = 0;
    ctx->cursor_x = -1;
    ctx->cursor_y = -1;

    if (sunxi_layer_reserve(ctx) < 0)
    {
        close(ctx->fd_fb);
        close(ctx->fd_disp);
        free(ctx);
        return NULL;
    }

    return ctx;
}

int sunxi_disp_close(sunxi_disp_t *ctx)
{
    if (ctx->fd_disp >= 0) {
        /* release layer */
        sunxi_layer_release(ctx);
        /* disable cursor */
        if (ctx->cursor_enabled)
            sunxi_hw_cursor_hide(ctx);
        /* close descriptors */
        munmap(ctx->framebuffer_addr, ctx->framebuffer_size);
        close(ctx->fd_fb);
        close(ctx->fd_disp);
        ctx->fd_disp = -1;
        free(ctx);
    }
    return 0;
}

/*****************************************************************************
 * Support for hardware cursor, which has 64x64 size, 2 bits per pixel,      *
 * four 32-bit ARGB entries in the palette.                                  *
 *****************************************************************************/

int sunxi_hw_cursor_load_64x64x2bpp(sunxi_disp_t *ctx, uint8_t pixeldata[1024])
{
    uint32_t tmp[4];
    __disp_hwc_pattern_t hwc;
    hwc.addr = (uintptr_t)&pixeldata[0];
    hwc.pat_mode = DISP_HWC_MOD_H64_V64_2BPP;
    tmp[0] = ctx->fb_id;
    tmp[1] = (uintptr_t)&hwc;
    return ioctl(ctx->fd_disp, DISP_CMD_HWC_SET_FB, &tmp);
}

int sunxi_hw_cursor_load_32x32x8bpp(sunxi_disp_t *ctx, uint8_t pixeldata[1024])
{
    uint32_t tmp[4];
    __disp_hwc_pattern_t hwc;
    hwc.addr = (uintptr_t)&pixeldata[0];
    hwc.pat_mode = DISP_HWC_MOD_H32_V32_8BPP;
    tmp[0] = ctx->fb_id;
    tmp[1] = (uintptr_t)&hwc;
    return ioctl(ctx->fd_disp, DISP_CMD_HWC_SET_FB, &tmp);
}

int sunxi_hw_cursor_load_palette(sunxi_disp_t *ctx, uint32_t *palette, int n)
{
    uint32_t tmp[4];
    tmp[0] = ctx->fb_id;
    tmp[1] = (uintptr_t)palette;
    tmp[2] = 0;
    tmp[3] = n * sizeof(uint32_t);
    return ioctl(ctx->fd_disp, DISP_CMD_HWC_SET_PALETTE_TABLE, &tmp);
}

int sunxi_hw_cursor_set_position(sunxi_disp_t *ctx, int x, int y)
{
    int result;
    uint32_t tmp[4];
    __disp_pos_t pos = { x, y };
    tmp[0] = ctx->fb_id;
    tmp[1] = (uintptr_t)&pos;
    if (pos.x < 0)
        pos.x = 0;
    if (pos.y < 0)
        pos.y = 0;
    result = ioctl(ctx->fd_disp, DISP_CMD_HWC_SET_POS, &tmp);
    if (result >= 0) {
        ctx->cursor_x = pos.x;
        ctx->cursor_y = pos.y;
    }
    return result;
}

int sunxi_hw_cursor_show(sunxi_disp_t *ctx)
{
    int result;
    uint32_t tmp[4];
    tmp[0] = ctx->fb_id;
    result = ioctl(ctx->fd_disp, DISP_CMD_HWC_OPEN, &tmp);
    if (result >= 0)
        ctx->cursor_enabled = 1;
    return result;
}

int sunxi_hw_cursor_hide(sunxi_disp_t *ctx)
{
    int result;
    uint32_t tmp[4];
    tmp[0] = ctx->fb_id;
    result = ioctl(ctx->fd_disp, DISP_CMD_HWC_CLOSE, &tmp);
    if (result >= 0)
        ctx->cursor_enabled = 0;
    return result;
}

/*****************************************************************************
 * Support for scaled layers                                                 *
 *****************************************************************************/

int sunxi_layer_reserve(sunxi_disp_t *ctx)
{
    __disp_layer_info_t layer_info;
    uint32_t tmp[4];

    /* try to allocate a layer */

    tmp[0] = ctx->fb_id;
    tmp[1] = DISP_LAYER_WORK_MODE_NORMAL;
    ctx->layer_id = ioctl(ctx->fd_disp, DISP_CMD_LAYER_REQUEST, &tmp);
    if (ctx->layer_id < 0)
        return -1;

    /* also try to enable scaler for this layer */

    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->layer_id;
    tmp[2] = (uintptr_t)&layer_info;
    if (ioctl(ctx->fd_disp, DISP_CMD_LAYER_GET_PARA, tmp) < 0)
        return ctx->layer_id;

    layer_info.mode = DISP_LAYER_WORK_MODE_SCALER;

    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->layer_id;
    tmp[2] = (uintptr_t)&layer_info;
    if (ioctl(ctx->fd_disp, DISP_CMD_LAYER_SET_PARA, tmp) >= 0)
        ctx->layer_has_scaler = 1;

    return ctx->layer_id;
}


int sunxi_layer_release(sunxi_disp_t *ctx)
{
    int result;
    uint32_t tmp[4];

    if (ctx->layer_id < 0)
        return -1;

    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->layer_id;
    ioctl(ctx->fd_disp, DISP_CMD_LAYER_RELEASE, &tmp);

    ctx->layer_id = -1;
    ctx->layer_has_scaler = 0;
    return 0;
}

int sunxi_layer_set_x8r8g8b8_input_buffer(sunxi_disp_t *ctx,
                                          uint32_t      offset_in_framebuffer,
                                          int           width,
                                          int           height,
                                          int           stride)
{
    __disp_fb_t fb;
    __disp_rect_t rect = { 0, 0, width, height };
    uint32_t tmp[4];
    memset(&fb, 0, sizeof(fb));

    if (ctx->layer_id < 0)
        return -1;

    fb.addr[0] = ctx->framebuffer_paddr + offset_in_framebuffer;
    fb.size.width = stride;
    fb.size.height = height;
    fb.format = DISP_FORMAT_ARGB8888;
    fb.seq = DISP_SEQ_ARGB;
    fb.mode = DISP_MOD_INTERLEAVED;

    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->layer_id;
    tmp[2] = (uintptr_t)&fb;
    if (ioctl(ctx->fd_disp, DISP_CMD_LAYER_SET_FB, &tmp) < 0)
        return -1;

    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->layer_id;
    tmp[2] = (uintptr_t)&rect;
    return ioctl(ctx->fd_disp, DISP_CMD_LAYER_SET_SRC_WINDOW, &tmp);
}

int sunxi_layer_set_output_window(sunxi_disp_t *ctx, int x, int y, int w, int h)
{
    __disp_rect_t rect = { x, y, w, h };
    uint32_t tmp[4];

    if (ctx->layer_id < 0)
        return -1;

    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->layer_id;
    tmp[2] = (uintptr_t)&rect;
    return ioctl(ctx->fd_disp, DISP_CMD_LAYER_SET_SCN_WINDOW, &tmp);
}

int sunxi_layer_show(sunxi_disp_t *ctx)
{
    uint32_t tmp[4];

    if (ctx->layer_id < 0)
        return -1;

    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->layer_id;
    return ioctl(ctx->fd_disp, DISP_CMD_LAYER_OPEN, &tmp);
}

int sunxi_layer_hide(sunxi_disp_t *ctx)
{
    int result;
    uint32_t tmp[4];

    if (ctx->layer_id < 0)
        return -1;

    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->layer_id;
    return ioctl(ctx->fd_disp, DISP_CMD_LAYER_CLOSE, &tmp);
}

/*****************************************************************************/

int sunxi_wait_for_vsync(sunxi_disp_t *ctx)
{
    return ioctl(ctx->fd_fb, FBIO_WAITFORVSYNC, 0);
}
