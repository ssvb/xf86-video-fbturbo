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
#include "g2d_driver.h"

/*****************************************************************************/

sunxi_disp_t *sunxi_disp_init(const char *device, void *xserver_fbmem)
{
    sunxi_disp_t *ctx = calloc(sizeof(sunxi_disp_t), 1);
    struct fb_var_screeninfo fb_var;
    struct fb_fix_screeninfo fb_fix;

    int tmp, version;
    int gfx_layer_size;
    int ovl_layer_size;

    /* use /dev/fb0 by default */
    if (!device)
        device = "/dev/fb0";

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

    /* store the already existing mapping done by xserver */
    ctx->xserver_fbmem = xserver_fbmem;

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
    ctx->framebuffer_height = ctx->framebuffer_size /
                              (ctx->xres * ctx->bits_per_pixel / 8);
    ctx->gfx_layer_size = ctx->xres * ctx->yres * fb_var.bits_per_pixel / 8;

    if (ctx->framebuffer_size < ctx->gfx_layer_size) {
        close(ctx->fd_fb);
        close(ctx->fd_disp);
        free(ctx);
        return NULL;
    }

    if (ctx->xserver_fbmem) {
        /* use already existing mapping */
        ctx->framebuffer_addr = ctx->xserver_fbmem;
    }
    else {
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
    }

    ctx->cursor_enabled = 0;
    ctx->cursor_x = -1;
    ctx->cursor_y = -1;

    /* Get the id of the screen layer */
    if (ioctl(ctx->fd_fb,
              ctx->fb_id == 0 ? FBIOGET_LAYER_HDL_0 : FBIOGET_LAYER_HDL_1,
              &ctx->gfx_layer_id))
    {
        /* Some boards use an inverted screen layer configuration */
        if (ioctl(ctx->fd_fb,
                  ctx->fb_id == 0 ? FBIOGET_LAYER_HDL_1 : FBIOGET_LAYER_HDL_0,
                  &ctx->gfx_layer_id))
        {
            close(ctx->fd_fb);
            close(ctx->fd_disp);
            free(ctx);
            return NULL;
        }
    }

    if (sunxi_layer_reserve(ctx) < 0)
    {
        close(ctx->fd_fb);
        close(ctx->fd_disp);
        free(ctx);
        return NULL;
    }

    ctx->fd_g2d = open("/dev/g2d", O_RDWR);

    ctx->blt2d.self = ctx;
    ctx->blt2d.overlapped_blt = sunxi_g2d_blt;

    return ctx;
}

int sunxi_disp_close(sunxi_disp_t *ctx)
{
    if (ctx->fd_disp >= 0) {
        if (ctx->fd_g2d >= 0) {
            close(ctx->fd_g2d);
        }
        /* release layer */
        sunxi_layer_release(ctx);
        /* disable cursor */
        if (ctx->cursor_enabled)
            sunxi_hw_cursor_hide(ctx);
        /* close descriptors */
        if (!ctx->xserver_fbmem)
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

static int sunxi_layer_change_work_mode(sunxi_disp_t *ctx, int new_mode)
{
    __disp_layer_info_t layer_info;
    uint32_t tmp[4];

    if (ctx->layer_id < 0)
        return -1;

    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->layer_id;
    tmp[2] = (uintptr_t)&layer_info;
    if (ioctl(ctx->fd_disp, DISP_CMD_LAYER_GET_PARA, tmp) < 0)
        return -1;

    layer_info.mode = new_mode;

    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->layer_id;
    tmp[2] = (uintptr_t)&layer_info;
    return ioctl(ctx->fd_disp, DISP_CMD_LAYER_SET_PARA, tmp);
}

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

    /* Initially set the layer configuration to something reasonable */

    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->layer_id;
    tmp[2] = (uintptr_t)&layer_info;
    if (ioctl(ctx->fd_disp, DISP_CMD_LAYER_GET_PARA, tmp) < 0)
        return -1;

    /* the screen and overlay layers need to be in different pipes */
    layer_info.pipe      = 1;
    layer_info.alpha_en  = 1;
    layer_info.alpha_val = 255;

    layer_info.fb.addr[0] = ctx->framebuffer_paddr;
    layer_info.fb.size.width = 1;
    layer_info.fb.size.height = 1;
    layer_info.fb.format = DISP_FORMAT_ARGB8888;
    layer_info.fb.seq = DISP_SEQ_ARGB;
    layer_info.fb.mode = DISP_MOD_INTERLEAVED;

    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->layer_id;
    tmp[2] = (uintptr_t)&layer_info;
    if (ioctl(ctx->fd_disp, DISP_CMD_LAYER_SET_PARA, tmp) < 0)
        return -1;

    /* Now probe the scaler mode to see if there is a free scaler available */
    if (sunxi_layer_change_work_mode(ctx, DISP_LAYER_WORK_MODE_SCALER) == 0)
        ctx->layer_has_scaler = 1;

    /* Revert back to normal mode */
    sunxi_layer_change_work_mode(ctx, DISP_LAYER_WORK_MODE_NORMAL);
    ctx->layer_scaler_is_enabled = 0;
    ctx->layer_format = DISP_FORMAT_ARGB8888;

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

int sunxi_layer_set_rgb_input_buffer(sunxi_disp_t *ctx,
                                     int           bpp,
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

    if (ctx->layer_scaler_is_enabled) {
        if (sunxi_layer_change_work_mode(ctx, DISP_LAYER_WORK_MODE_NORMAL) == 0)
            ctx->layer_scaler_is_enabled = 0;
        else
            return -1;
    }

    fb.addr[0] = ctx->framebuffer_paddr + offset_in_framebuffer;
    fb.size.height = height;
    if (bpp == 32) {
        fb.format = DISP_FORMAT_ARGB8888;
        fb.seq = DISP_SEQ_ARGB;
        fb.mode = DISP_MOD_INTERLEAVED;
        fb.size.width = stride;
    } else if (bpp == 16) {
        fb.format = DISP_FORMAT_RGB565;
        fb.seq = DISP_SEQ_P10;
        fb.mode = DISP_MOD_INTERLEAVED;
        fb.size.width = stride * 2;
    } else {
        return -1;
    }

    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->layer_id;
    tmp[2] = (uintptr_t)&fb;
    if (ioctl(ctx->fd_disp, DISP_CMD_LAYER_SET_FB, &tmp) < 0)
        return -1;

    ctx->layer_buf_x = rect.x;
    ctx->layer_buf_y = rect.y;
    ctx->layer_buf_w = rect.width;
    ctx->layer_buf_h = rect.height;
    ctx->layer_format = fb.format;

    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->layer_id;
    tmp[2] = (uintptr_t)&rect;
    return ioctl(ctx->fd_disp, DISP_CMD_LAYER_SET_SRC_WINDOW, &tmp);
}

int sunxi_layer_set_yuv420_input_buffer(sunxi_disp_t *ctx,
                                        uint32_t      y_offset_in_framebuffer,
                                        uint32_t      u_offset_in_framebuffer,
                                        uint32_t      v_offset_in_framebuffer,
                                        int           width,
                                        int           height,
                                        int           stride,
                                        int           x_pixel_offset,
                                        int           y_pixel_offset)
{
    __disp_fb_t fb;
    __disp_rect_t rect = { x_pixel_offset, y_pixel_offset, width, height };
    uint32_t tmp[4];
    memset(&fb, 0, sizeof(fb));

    if (ctx->layer_id < 0)
        return -1;

    if (!ctx->layer_scaler_is_enabled) {
        if (sunxi_layer_change_work_mode(ctx, DISP_LAYER_WORK_MODE_SCALER) == 0)
            ctx->layer_scaler_is_enabled = 1;
        else
            return -1;
    }

    fb.addr[0] = ctx->framebuffer_paddr + y_offset_in_framebuffer;
    fb.addr[1] = ctx->framebuffer_paddr + u_offset_in_framebuffer;
    fb.addr[2] = ctx->framebuffer_paddr + v_offset_in_framebuffer;
    fb.size.width = stride;
    fb.size.height = height;
    fb.format = DISP_FORMAT_YUV420;
    fb.seq = DISP_SEQ_P3210;
    fb.mode = DISP_MOD_NON_MB_PLANAR;

    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->layer_id;
    tmp[2] = (uintptr_t)&fb;
    if (ioctl(ctx->fd_disp, DISP_CMD_LAYER_SET_FB, &tmp) < 0)
        return -1;

    ctx->layer_buf_x = rect.x;
    ctx->layer_buf_y = rect.y;
    ctx->layer_buf_w = rect.width;
    ctx->layer_buf_h = rect.height;
    ctx->layer_format = fb.format;

    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->layer_id;
    tmp[2] = (uintptr_t)&rect;
    return ioctl(ctx->fd_disp, DISP_CMD_LAYER_SET_SRC_WINDOW, &tmp);
}

int sunxi_layer_set_output_window(sunxi_disp_t *ctx, int x, int y, int w, int h)
{
    __disp_rect_t buf_rect = {
        ctx->layer_buf_x, ctx->layer_buf_y,
        ctx->layer_buf_w, ctx->layer_buf_h
    };
    __disp_rect_t win_rect = { x, y, w, h };
    uint32_t tmp[4];
    int err;

    if (ctx->layer_id < 0 || w <= 0 || h <= 0)
        return -1;

    /*
     * Handle negative window Y coordinates (workaround a bug).
     * The Allwinner A10/A13 display controller hardware is expected to
     * support negative coordinates of the top left corners of the layers.
     * But there is some bug either in the kernel driver or in the hardware,
     * which messes up the picture on screen when the Y coordinate is negative
     * for YUV layer. Negative X coordinates are not affected. RGB formats
     * are not affected too.
     *
     * We fix this by just recalculating which part of the buffer in memory
     * corresponds to Y=0 on screen and adjust the input buffer settings.
     */
    if (ctx->layer_format == DISP_FORMAT_YUV420 &&
                                  (y < 0 || ctx->layer_win_y < 0)) {
        if (win_rect.y < 0) {
            int y_shift = -(double)y * buf_rect.height / win_rect.height;
            buf_rect.y      += y_shift;
            buf_rect.height -= y_shift;
            win_rect.height += win_rect.y;
            win_rect.y       = 0;
        }

        if (buf_rect.height <= 0 || win_rect.height <= 0) {
            /* No part of the window is visible. Just construct a fake rectangle
             * outside the screen as a window placement (but with a non-negative Y
             * coordinate). Do this to avoid passing bogus negative heights to
             * the kernel driver (who knows how it would react?) */
            win_rect.x = -1;
            win_rect.y = 0;
            win_rect.width = 1;
            win_rect.height = 1;
            tmp[0] = ctx->fb_id;
            tmp[1] = ctx->layer_id;
            tmp[2] = (uintptr_t)&win_rect;
            return ioctl(ctx->fd_disp, DISP_CMD_LAYER_SET_SCN_WINDOW, &tmp);
        }

        tmp[0] = ctx->fb_id;
        tmp[1] = ctx->layer_id;
        tmp[2] = (uintptr_t)&buf_rect;
        if ((err = ioctl(ctx->fd_disp, DISP_CMD_LAYER_SET_SRC_WINDOW, &tmp)))
            return err;
    }
    /* Save the new non-adjusted window position */
    ctx->layer_win_x = x;
    ctx->layer_win_y = y;

    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->layer_id;
    tmp[2] = (uintptr_t)&win_rect;
    return ioctl(ctx->fd_disp, DISP_CMD_LAYER_SET_SCN_WINDOW, &tmp);
}

int sunxi_layer_show(sunxi_disp_t *ctx)
{
    uint32_t tmp[4];

    if (ctx->layer_id < 0)
        return -1;

    /* YUV formats need to use a scaler */
    if (ctx->layer_format == DISP_FORMAT_YUV420 && !ctx->layer_scaler_is_enabled) {
        if (sunxi_layer_change_work_mode(ctx, DISP_LAYER_WORK_MODE_SCALER) == 0)
            ctx->layer_scaler_is_enabled = 1;
    }

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

    /* If the layer is hidden, there is no need to keep the scaler occupied */
    if (ctx->layer_scaler_is_enabled) {
        if (sunxi_layer_change_work_mode(ctx, DISP_LAYER_WORK_MODE_NORMAL) == 0)
            ctx->layer_scaler_is_enabled = 0;
    }

    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->layer_id;
    return ioctl(ctx->fd_disp, DISP_CMD_LAYER_CLOSE, &tmp);
}

int sunxi_layer_set_colorkey(sunxi_disp_t *ctx, uint32_t color)
{
    uint32_t tmp[4];
    __disp_colorkey_t colorkey;
    __disp_color_t disp_color;

    disp_color.alpha = (color >> 24) & 0xFF;
    disp_color.red   = (color >> 16) & 0xFF;
    disp_color.green = (color >> 8)  & 0xFF;
    disp_color.blue  = (color >> 0)  & 0xFF;

    colorkey.ck_min = disp_color;
    colorkey.ck_max = disp_color;
    colorkey.red_match_rule   = 2;
    colorkey.green_match_rule = 2;
    colorkey.blue_match_rule  = 2;

    tmp[0] = ctx->fb_id;
    tmp[1] = (uintptr_t)&colorkey;
    if (ioctl(ctx->fd_disp, DISP_CMD_SET_COLORKEY, &tmp))
        return -1;

    /* Set the overlay layer below the screen layer */
    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->gfx_layer_id;
    if (ioctl(ctx->fd_disp, DISP_CMD_LAYER_BOTTOM, &tmp) < 0)
        return -1;

    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->layer_id;
    if (ioctl(ctx->fd_disp, DISP_CMD_LAYER_BOTTOM, &tmp) < 0)
        return -1;

    /* Enable color key for the overlay layer */
    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->layer_id;
    if (ioctl(ctx->fd_disp, DISP_CMD_LAYER_CK_ON, &tmp) < 0)
        return -1;

    /* Disable color key and enable global alpha for the screen layer */
    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->gfx_layer_id;
    if (ioctl(ctx->fd_disp, DISP_CMD_LAYER_CK_OFF, &tmp) < 0)
        return -1;

    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->gfx_layer_id;
    if (ioctl(ctx->fd_disp, DISP_CMD_LAYER_ALPHA_ON, &tmp) < 0)
        return -1;

    return 0;
}

int sunxi_layer_disable_colorkey(sunxi_disp_t *ctx)
{
    uint32_t tmp[4];

    /* Disable color key for the overlay layer */
    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->layer_id;
    if (ioctl(ctx->fd_disp, DISP_CMD_LAYER_CK_OFF, &tmp) < 0)
        return -1;

    /* Set the overlay layer above the screen layer */
    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->layer_id;
    if (ioctl(ctx->fd_disp, DISP_CMD_LAYER_BOTTOM, &tmp) < 0)
        return -1;

    tmp[0] = ctx->fb_id;
    tmp[1] = ctx->gfx_layer_id;
    if (ioctl(ctx->fd_disp, DISP_CMD_LAYER_BOTTOM, &tmp) < 0)
        return -1;

    return 0;
}

/*****************************************************************************/

int sunxi_wait_for_vsync(sunxi_disp_t *ctx)
{
    return ioctl(ctx->fd_fb, FBIO_WAITFORVSYNC, 0);
}

/*****************************************************************************/

int sunxi_g2d_fill_a8r8g8b8(sunxi_disp_t *disp,
                            int           x,
                            int           y,
                            int           w,
                            int           h,
                            uint32_t      color)
{
    g2d_fillrect tmp;

    if (disp->fd_g2d < 0)
        return -1;

    if (w <= 0 || h <= 0)
        return 0;

    tmp.flag                = G2D_FIL_NONE;
    tmp.dst_image.addr[0]   = disp->framebuffer_paddr;
    tmp.dst_image.w         = disp->xres;
    tmp.dst_image.h         = disp->framebuffer_height;
    tmp.dst_image.format    = G2D_FMT_ARGB_AYUV8888;
    tmp.dst_image.pixel_seq = G2D_SEQ_NORMAL;
    tmp.dst_rect.x          = x;
    tmp.dst_rect.y          = y;
    tmp.dst_rect.w          = w;
    tmp.dst_rect.h          = h;
    tmp.color               = color;
    tmp.alpha               = 0;

    return ioctl(disp->fd_g2d, G2D_CMD_FILLRECT, &tmp);
}

int sunxi_g2d_blit_a8r8g8b8(sunxi_disp_t *disp,
                            int           dst_x,
                            int           dst_y,
                            int           src_x,
                            int           src_y,
                            int           w,
                            int           h)
{
    g2d_blt tmp;

    if (disp->fd_g2d < 0)
        return -1;

    if (w <= 0 || h <= 0)
        return 0;

    tmp.flag                = G2D_BLT_NONE;
    tmp.src_image.addr[0]   = disp->framebuffer_paddr;
    tmp.src_image.w         = disp->xres;
    tmp.src_image.h         = disp->framebuffer_height;
    tmp.src_image.format    = G2D_FMT_ARGB_AYUV8888;
    tmp.src_image.pixel_seq = G2D_SEQ_NORMAL;
    tmp.src_rect.x          = src_x;
    tmp.src_rect.y          = src_y;
    tmp.src_rect.w          = w;
    tmp.src_rect.h          = h;
    tmp.dst_image.addr[0]   = disp->framebuffer_paddr;
    tmp.dst_image.w         = disp->xres;
    tmp.dst_image.h         = disp->framebuffer_height;
    tmp.dst_image.format    = G2D_FMT_ARGB_AYUV8888;
    tmp.dst_image.pixel_seq = G2D_SEQ_NORMAL;
    tmp.dst_x               = dst_x;
    tmp.dst_y               = dst_y;
    tmp.color               = 0;
    tmp.alpha               = 0;

    return ioctl(disp->fd_g2d, G2D_CMD_BITBLT, &tmp);
}

/*
 * The following function implements a 16bpp blit using 32bpp mode by
 * splitting the area into an aligned middle part (which is blit using
 * 32bpp mode) and left and right edges if required.
 *
 * It assumes the parameters have already been validated by the caller.
 * This includes the condition (src_x & 1) == (dst_x & 1), which is
 * necessary to be able to use 32bpp mode.
 */

int sunxi_g2d_blit_r5g6b5_in_three(sunxi_disp_t *disp, uint8_t *src_bits,
    uint8_t *dst_bits, int src_stride, int dst_stride, int src_x, int src_y,
    int dst_x, int dst_y, int w, int h)
{
    g2d_blt tmp;
    /* Set up the invariant blit parameters. */
    tmp.flag                = G2D_BLT_NONE;
    tmp.src_image.h         = src_y + h;
    tmp.src_rect.y          = src_y;
    tmp.src_rect.h          = h;
    tmp.dst_image.h         = dst_y + h;
    tmp.dst_y               = dst_y;
    tmp.color               = 0;
    tmp.alpha               = 0;

    if (src_x & 1) {
        tmp.src_image.addr[0]   = disp->framebuffer_paddr +
                                  (src_bits - disp->framebuffer_addr);
        tmp.src_image.format    = G2D_FMT_RGB565;
        tmp.src_image.pixel_seq = G2D_SEQ_P10;
        tmp.src_image.w         = src_stride * 2;
        tmp.src_rect.x          = src_x;
        tmp.src_rect.w          = 1;
        tmp.dst_image.addr[0]   = disp->framebuffer_paddr +
                                  (dst_bits - disp->framebuffer_addr);
        tmp.dst_image.format    = G2D_FMT_RGB565;
        tmp.dst_image.pixel_seq = G2D_SEQ_P10;
        tmp.dst_image.w         = dst_stride * 2;
        tmp.dst_x               = dst_x;
        if (ioctl(disp->fd_g2d, G2D_CMD_BITBLT, &tmp))
            return 0;
        src_x++;
        dst_x++;
        w--;
    }
    if (w >= 2) {
        int w2;
        tmp.src_image.addr[0]   = disp->framebuffer_paddr +
                                  (src_bits - disp->framebuffer_addr);
        tmp.src_image.format    = G2D_FMT_ARGB_AYUV8888;
        tmp.src_image.pixel_seq = G2D_SEQ_NORMAL;
        tmp.src_image.w         = src_stride;
        tmp.src_rect.x          = src_x >> 1;
        tmp.src_rect.w          = w >> 1;
        tmp.dst_image.addr[0]   = disp->framebuffer_paddr +
                                  (dst_bits - disp->framebuffer_addr);
        tmp.dst_image.format    = G2D_FMT_ARGB_AYUV8888;
        tmp.dst_image.pixel_seq = G2D_SEQ_NORMAL;
        tmp.dst_image.w         = dst_stride;
        tmp.dst_x               = dst_x >> 1;
        if (ioctl(disp->fd_g2d, G2D_CMD_BITBLT, &tmp))
            return 0;
        w2 = (w >> 1) * 2;
        src_x += w2;
        dst_x += w2;
        w &= 1;
    }
    if (w) {
        tmp.src_image.addr[0]   = disp->framebuffer_paddr +
                                  (src_bits - disp->framebuffer_addr);
        tmp.src_image.format    = G2D_FMT_RGB565;
        tmp.src_image.pixel_seq = G2D_SEQ_P10;
        tmp.src_image.w         = src_stride * 2;
        tmp.src_rect.x          = src_x;
        tmp.src_rect.w          = 1;
        tmp.dst_image.addr[0]   = disp->framebuffer_paddr +
                                  (dst_bits - disp->framebuffer_addr);

        tmp.dst_image.format    = G2D_FMT_RGB565;
        tmp.dst_image.pixel_seq = G2D_SEQ_P10;
        tmp.dst_image.w         = dst_stride * 2;
        tmp.dst_x               = dst_x;
        if (ioctl(disp->fd_g2d, G2D_CMD_BITBLT, &tmp))
            return 0;
    }
    return 1;
}

static inline int sunxi_g2d_try_fallback_blt(void               *self,
                                             uint32_t           *src_bits,
                                             uint32_t           *dst_bits,
                                             int                 src_stride,
                                             int                 dst_stride,
                                             int                 src_bpp,
                                             int                 dst_bpp,
                                             int                 src_x,
                                             int                 src_y,
                                             int                 dst_x,
                                             int                 dst_y,
                                             int                 w,
                                             int                 h)
{
    sunxi_disp_t *disp = (sunxi_disp_t *)self;
    if (disp->fallback_blt2d)
        return disp->fallback_blt2d->overlapped_blt(disp->fallback_blt2d->self,
                                                    src_bits, dst_bits,
                                                    src_stride, dst_stride,
                                                    src_bpp, dst_bpp,
                                                    src_x, src_y,
                                                    dst_x, dst_y, w, h);
    return 0;

}

#define FALLBACK_BLT() sunxi_g2d_try_fallback_blt(self, src_bits,        \
                                                  dst_bits, src_stride,  \
                                                  dst_stride, src_bpp,   \
                                                  dst_bpp, src_x, src_y, \
                                                  dst_x, dst_y, w, h);

/*
 * G2D counterpart for pixman_blt (function arguments are the same with
 * only sunxi_disp_t extra argument added). Supports 16bpp (r5g6b5) and
 * 32bpp (a8r8g8b8) formats and also conversion between them.
 *
 * Can do G2D accelerated blits only if both source and destination
 * buffers are inside framebuffer. Returns FALSE (0) otherwise.
 */
int sunxi_g2d_blt(void               *self,
                  uint32_t           *src_bits,
                  uint32_t           *dst_bits,
                  int                 src_stride,
                  int                 dst_stride,
                  int                 src_bpp,
                  int                 dst_bpp,
                  int                 src_x,
                  int                 src_y,
                  int                 dst_x,
                  int                 dst_y,
                  int                 w,
                  int                 h)
{
    sunxi_disp_t *disp = (sunxi_disp_t *)self;
    int blt_size_threshold;
    g2d_blt tmp;

    /* Zero size blit, nothing to do */
    if (w <= 0 || h <= 0)
        return 1;

    /*
     * Very minimal validation here. We just assume that if the begginging
     * of both source and destination images belongs to the framebuffer,
     * then these images are entirely residing inside the framebuffer
     * without crossing its borders. Any other checks are supposed
     * to be done by the caller.
     */
    if ((uint8_t *)src_bits < disp->framebuffer_addr ||
        (uint8_t *)src_bits >= disp->framebuffer_addr + disp->framebuffer_size ||
        (uint8_t *)dst_bits < disp->framebuffer_addr ||
        (uint8_t *)dst_bits >= disp->framebuffer_addr + disp->framebuffer_size)
    {
        return FALLBACK_BLT();
    }

    /*
     * If the area is smaller than G2D_BLT_SIZE_THRESHOLD, prefer to avoid the
     * overhead of G2D and do a CPU blit instead. There is a special threshold
     * for 16bpp to 16bpp copy.
     */
    if (src_bpp == 16 && dst_bpp == 16)
        blt_size_threshold = G2D_BLT_SIZE_THRESHOLD_16BPP;
    else
        blt_size_threshold = G2D_BLT_SIZE_THRESHOLD;
    if (w * h < blt_size_threshold)
        return FALLBACK_BLT();

    /* Unsupported overlapping type */
    if (src_bits == dst_bits && src_y == dst_y && src_x + 1 < dst_x)
        return FALLBACK_BLT();

    if (disp->fd_g2d < 0)
        return FALLBACK_BLT();

    /* Do a 16-bit using 32-bit mode if possible. */
    if (src_bpp == 16 && dst_bpp == 16 && (src_x & 1) == (dst_x & 1))
        /* Check whether the overlapping type is supported, the condition */
        /* is slightly different compared to the regular blit. */
        if (!(src_bits == dst_bits && src_y == dst_y && src_x < dst_x))
            return sunxi_g2d_blit_r5g6b5_in_three(disp, (uint8_t *)src_bits,
                (uint8_t *)dst_bits, src_stride, dst_stride, src_x, src_y,
                dst_x, dst_y, w, h);

    if ((src_bpp != 16 && src_bpp != 32) || (dst_bpp != 16 && dst_bpp != 32))
        return FALLBACK_BLT();

    tmp.flag                    = G2D_BLT_NONE;
    tmp.src_image.addr[0]       = disp->framebuffer_paddr +
                                  ((uint8_t *)src_bits - disp->framebuffer_addr);
    tmp.src_rect.x              = src_x;
    tmp.src_rect.y              = src_y;
    tmp.src_rect.w              = w;
    tmp.src_rect.h              = h;
    tmp.src_image.h             = src_y + h;
    if (src_bpp == 32) {
        tmp.src_image.w         = src_stride;
        tmp.src_image.format    = G2D_FMT_ARGB_AYUV8888;
        tmp.src_image.pixel_seq = G2D_SEQ_NORMAL;
    }
    else if (src_bpp == 16) {
        tmp.src_image.w         = src_stride * 2;
        tmp.src_image.format    = G2D_FMT_RGB565;
        tmp.src_image.pixel_seq = G2D_SEQ_P10;
    }

    tmp.dst_image.addr[0]       = disp->framebuffer_paddr +
                                  ((uint8_t *)dst_bits - disp->framebuffer_addr);
    tmp.dst_x                   = dst_x;
    tmp.dst_y                   = dst_y;
    tmp.color                   = 0;
    tmp.alpha                   = 0;
    tmp.dst_image.h             = dst_y + h;
    if (dst_bpp == 32) {
        tmp.dst_image.w         = dst_stride;
        tmp.dst_image.format    = G2D_FMT_ARGB_AYUV8888;
        tmp.dst_image.pixel_seq = G2D_SEQ_NORMAL;
    }
    else if (dst_bpp == 16) {
        tmp.dst_image.w         = dst_stride * 2;
        tmp.dst_image.format    = G2D_FMT_RGB565;
        tmp.dst_image.pixel_seq = G2D_SEQ_P10;
    }

    return ioctl(disp->fd_g2d, G2D_CMD_BITBLT, &tmp) == 0;
}
