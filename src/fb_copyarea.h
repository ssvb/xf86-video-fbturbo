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

#ifndef FB_COPYAREA_H
#define FB_COPYAREA_H

#include "interfaces.h"

typedef struct {
    /* framebuffer descriptor */
    int fd;

    int                 xres, yres, bits_per_pixel;
    uint8_t            *framebuffer_addr;  /* mmapped address */
    uintptr_t           framebuffer_paddr; /* physical address */
    uint32_t            framebuffer_size;  /* total size of the framebuffer */
    int                 framebuffer_height;/* virtual vertical resolution */
    int                 framebuffer_stride;
    uint32_t            gfx_layer_size;    /* the size of the primary layer */

    uint8_t            *xserver_fbmem; /* framebuffer mapping done by xserver */

    /* fb_copyarea accelerated implementation of blt2d_i interface */
    blt2d_i             blt2d;
    /* Optional fallback interface to handle unsupported operations */
    blt2d_i            *fallback_blt2d;
} fb_copyarea_t;

fb_copyarea_t *fb_copyarea_init(const char *fb_device, void *xserver_fbmem);
void fb_copyarea_close(fb_copyarea_t *fb_copyarea);

int fb_copyarea_blt(void               *self,
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
                    int                 h);

#endif
