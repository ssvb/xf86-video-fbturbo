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

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <signal.h>

#include "../src/sunxi_disp.h"

static sunxi_disp_t *disp;

void memset32(uint32_t *buf, uint32_t color, int n)
{
    while (n--) {
        *buf++ = color;
    }
}

void fill_framebuffer(void *buf_, int yoffs, int width, int height,
                      int bw_split_pos, uint32_t color)
{
    uint32_t *buf = (uint32_t *)buf_;

    if (disp->fd_g2d >= 0) {
        sunxi_g2d_fill_a8r8g8b8(disp, 0, yoffs,
                                bw_split_pos, height, color);
        sunxi_g2d_fill_a8r8g8b8(disp, bw_split_pos, yoffs,
                                width - bw_split_pos, height + yoffs, 0);
    }
    else {
        buf += yoffs * width;
        while (height--) {
            memset32(buf, color, bw_split_pos);
            memset32(buf + bw_split_pos, 0x00, width - bw_split_pos);
            buf += width;
        }
    }
}


void disp_destructor(int signum)
{
    printf("\nsunxi_disp_close()\n");
    if (disp) {
        sunxi_disp_close(disp);
        disp = NULL;
    }
    exit(0);
}

int main(int argc, char *argv[])
{
    int pos = 0, framenum = 0, yoffs, color;

    disp = sunxi_disp_init("/dev/fb0", NULL);
    /*
     * setup the signal handler to catch Ctrl-C in order to prevent leaking
     * the layer on process termination (that's a kernel bug).
     */
    signal(SIGINT, disp_destructor);

    if (!disp) {
        printf("sunxi_disp_init() failed\n");
        exit(1);
    }

    printf("disp->xres=%d, disp->yres=%d, disp_bits_per_pixel=%d, g2d_accel=%s\n",
           disp->xres, disp->yres, disp->bits_per_pixel,
           (disp->fd_g2d >= 0) ? "yes" : "no");

    if (disp->bits_per_pixel != 32) {
        printf("Sorry, only 32 bits per pixel is supported for now\n");
        exit(1);
    }

    if (disp->framebuffer_size < disp->xres * disp->yres * 4 * 2) {
        printf("Sorry, framebuffer size is too small (need at least %.1f MiB)\n",
               (double)disp->xres * disp->yres * 4 * 2 / (1024 * 1024));
        exit(1);
    }

    printf("\nYou should see some tear-free animation where the left half\n");
    printf("of the screen is filled with yellow color, the right half of the\n");
    printf("screen is black and the border between yellow and black areas is\n");
    printf("moving and bouncing between left and right sides of the screen.\n\n");

    printf("In the case if vsync is broken, you may see some blue colored\n");
    printf("areas or tearing in the vertical line separating yellow and black.\n\n");

    printf("This demo can be stopped by pressing Ctrl-C.\n");

    /* setup layer window to cover the whole screen */
    sunxi_layer_set_output_window(disp, 0, 0, disp->xres, disp->yres);
    /* setup the layer scanout buffer to the first page in the framebuffer */
    sunxi_layer_set_rgb_input_buffer(disp, disp->bits_per_pixel,
                                     0, disp->xres, disp->yres, disp->xres);
    /* make the layer visible */
    sunxi_layer_show(disp);

    while (1) {
        if (framenum % 2 == 1) {
            color = 0xFFFFFF00;
            yoffs = 0;
        } else {
            color = 0xFFFFFF00;
            yoffs = disp->yres;
        }
        pos = (pos + 16) % (disp->xres * 2);

        /* paint part of the screen with blue in the offscreen buffer */
        fill_framebuffer(disp->framebuffer_addr, yoffs,
                         disp->xres, disp->yres,
                         pos < disp->xres ? pos : 2 * disp->xres - pos,
                         0xFF0000FF);

        /* paint part of the screen with yellow in the offscreen buffer */
        fill_framebuffer(disp->framebuffer_addr, yoffs,
                         disp->xres, disp->yres,
                         pos < disp->xres ? pos : 2 * disp->xres - pos,
                         color);

        /* schedule the change of layer scanout buffer on next vsync */
        sunxi_layer_set_rgb_input_buffer(disp, disp->bits_per_pixel,
                                         yoffs * disp->xres * 4,
                                         disp->xres, disp->yres, disp->xres);
        /* wait for the vsync itself */
        sunxi_wait_for_vsync(disp);

        framenum++;
    }

    return 0;
}
