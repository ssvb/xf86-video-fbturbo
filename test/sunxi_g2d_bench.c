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
#include <sys/time.h>
#include <signal.h>

#include <pixman.h>

#include "../src/sunxi_disp.h"

#define NTESTS  200
/*
 * We don't want to benchmark perfectly aligned source and destination buffers,
 * so we are benchmarking the rectangles which have width "xres - PADDING" and
 * are randomly shifted by "rand() % PADDING" on X axis.
 */
#define PADDING 16

static sunxi_disp_t *disp;

double gettime(void)
{
    struct timeval tv;
    gettimeofday (&tv, NULL);
    return (double)((int64_t)tv.tv_sec * 1000000 + tv.tv_usec) / 1000000.;
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
    double t1, t2;
    int x1, x2;
    int i, pos = 0, framenum = 0, yoffs, color;
    void *addr;

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

    /***************************************************************************/

    printf("\nRunning G2D benchmarks for framebuffer (typically writecombine mapped)\n");

    srand(0);
    t1 = gettime();
    for (i = 0; i < NTESTS; i++) {
        x1 = rand() % PADDING;
        x2 = rand() % PADDING;
        sunxi_g2d_blit_a8r8g8b8(disp, x1, 0, x2, disp->yres, disp->xres - PADDING, disp->yres);
    }
    t2 = gettime();
    printf("G2D blit performance: %.2f MPix/s (%.2f MB/s)\n",
           (double)(disp->xres - PADDING) * disp->yres * NTESTS / (t2 - t1) / 1000000.,
           (double)(disp->xres - PADDING) * disp->yres * NTESTS / (t2 - t1) / 1000000. * 4);

    srand(0);
    t1 = gettime();
    for (i = 0; i < NTESTS; i++) {
        x1 = rand() % PADDING;
        x2 = rand() % PADDING;
        sunxi_g2d_fill_a8r8g8b8(disp, x1, 0, disp->xres - PADDING, disp->yres, 0xFFFF0000);
    }
    t2 = gettime();
    printf("G2D fill performance: %.2f MPix/s (%.2f MB/s)\n",
           (double)(disp->xres - PADDING) * disp->yres * NTESTS / (t2 - t1) / 1000000.,
           (double)(disp->xres - PADDING) * disp->yres * NTESTS / (t2 - t1) / 1000000. * 4);

    /***************************************************************************/

    printf("\nRunning pixman benchmarks for framebuffer (typically writecombine mapped)\n");

    addr = disp->framebuffer_addr;

    srand(0);
    t1 = gettime();
    for (i = 0; i < NTESTS; i++) {
        x1 = rand() % PADDING;
        x2 = rand() % PADDING;
        pixman_blt(addr, /* src_bits */
                   addr, /* dst_bits */
                   disp->xres,
                   disp->xres,
                   32,
                   32,
                   x1, disp->yres,
                   x2, 0,
                   disp->xres - PADDING, disp->yres);
    }
    t2 = gettime();
    printf("pixman blit performance: %.2f MPix/s (%.2f MB/s)\n",
           (double)(disp->xres - PADDING) * disp->yres * NTESTS / (t2 - t1) / 1000000.,
           (double)(disp->xres - PADDING) * disp->yres * NTESTS / (t2 - t1) / 1000000. * 4);

    srand(0);
    t1 = gettime();
    for (i = 0; i < NTESTS; i++) {
        x1 = rand() % PADDING;
        pixman_fill(addr, /* dst_bits */
                   disp->xres,
                   32,
                   x1, 0,
                   disp->xres - PADDING, disp->yres,
                   0xFF0000FF);
    }
    t2 = gettime();
    printf("pixman fill performance: %.2f MPix/s (%.2f MB/s)\n",
           (double)(disp->xres - PADDING) * disp->yres * NTESTS / (t2 - t1) / 1000000.,
           (double)(disp->xres - PADDING) * disp->yres * NTESTS / (t2 - t1) / 1000000. * 4);

    /***************************************************************************/

    printf("\nRunning pixman benchmarks for normal RAM (typically mapped as WB cached)\n");

    addr = malloc(disp->xres * disp->yres * 4 * 2);
    memset(addr, 0xCC, disp->xres * disp->yres * 4 * 2);

    srand(0);
    t1 = gettime();
    for (i = 0; i < NTESTS; i++) {
        x1 = rand() % PADDING;
        x2 = rand() % PADDING;
        pixman_blt(addr, /* src_bits */
                   addr, /* dst_bits */
                   disp->xres,
                   disp->xres,
                   32,
                   32,
                   x1, disp->yres,
                   x2, 0,
                   disp->xres - PADDING, disp->yres);
    }
    t2 = gettime();
    printf("pixman blit performance: %.2f MPix/s (%.2f MB/s)\n",
           (double)(disp->xres - PADDING) * disp->yres * NTESTS / (t2 - t1) / 1000000.,
           (double)(disp->xres - PADDING) * disp->yres * NTESTS / (t2 - t1) / 1000000. * 4);

    srand(0);
    t1 = gettime();
    for (i = 0; i < NTESTS; i++) {
        x1 = rand() % PADDING;
        pixman_fill(addr, /* dst_bits */
                   disp->xres,
                   32,
                   x1, 0,
                   disp->xres - PADDING, disp->yres,
                   0xFF0000FF);
    }
    t2 = gettime();
    printf("pixman fill performance: %.2f MPix/s (%.2f MB/s)\n",
           (double)(disp->xres - PADDING) * disp->yres * NTESTS / (t2 - t1) / 1000000.,
           (double)(disp->xres - PADDING) * disp->yres * NTESTS / (t2 - t1) / 1000000. * 4);

    free(addr);

    sunxi_disp_close(disp);

    return 0;
}
