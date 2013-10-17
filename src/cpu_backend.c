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

#include <stdlib.h>
#include <string.h>

#include "cpuinfo.h"
#include "cpu_backend.h"

#ifdef __arm__

#ifdef __GNUC__
#define always_inline inline __attribute__((always_inline))
#else
#define always_inline inline
#endif

void memcpy_armv5te(void *dst, const void *src, int size);
void writeback_scratch_to_mem_neon(int size, void *dst, const void *src);
void aligned_fetch_fbmem_to_scratch_neon(int size, void *dst, const void *src);
void aligned_fetch_fbmem_to_scratch_vfp(int size, void *dst, const void *src);
void aligned_fetch_fbmem_to_scratch_arm(int size, void *dst, const void *src);

static always_inline void
writeback_scratch_to_mem_arm(int size, void *dst, const void *src)
{
    memcpy_armv5te(dst, src, size);
}

#define SCRATCHSIZE 2048

/*
 * This is a function similar to memmove, which tries to minimize uncached read
 * penalty for the source buffer (for example if the source is a framebuffer).
 *
 * Note: because this implementation fetches data as 32 byte aligned chunks
 * valgrind is going to scream about read accesses outside the source buffer.
 * (even if an aligned 32 byte chunk contains only a single byte belonging
 * to the source buffer, the whole chunk is going to be read).
 */
static always_inline void
twopass_memmove(void *dst_, const void *src_, size_t size,
                void (*aligned_fetch_fbmem_to_scratch)(int, void *, const void *),
                void (*writeback_scratch_to_mem)(int, void *, const void *))
{
    uint8_t tmpbuf[SCRATCHSIZE + 32 + 31];
    uint8_t *scratchbuf = (uint8_t *)((uintptr_t)(&tmpbuf[0] + 31) & ~31);
    uint8_t *dst = (uint8_t *)dst_;
    const uint8_t *src = (const uint8_t *)src_;
    uintptr_t alignshift = (uintptr_t)src & 31;
    uintptr_t extrasize = (alignshift == 0) ? 0 : 32;

    if (src > dst) {
        while (size >= SCRATCHSIZE) {
            aligned_fetch_fbmem_to_scratch(SCRATCHSIZE + extrasize,
                                           scratchbuf, src - alignshift);
            writeback_scratch_to_mem(SCRATCHSIZE, dst, scratchbuf + alignshift);
            size -= SCRATCHSIZE;
            dst += SCRATCHSIZE;
            src += SCRATCHSIZE;
        }
        if (size > 0) {
            aligned_fetch_fbmem_to_scratch(size + extrasize,
                                           scratchbuf, src - alignshift);
            writeback_scratch_to_mem(size, dst, scratchbuf + alignshift);
        }
    }
    else {
        uintptr_t remainder = size % SCRATCHSIZE;
        dst += size - remainder;
        src += size - remainder;
        size -= remainder;
        if (remainder) {
            aligned_fetch_fbmem_to_scratch(remainder + extrasize,
                                           scratchbuf, src - alignshift);
            writeback_scratch_to_mem(remainder, dst, scratchbuf + alignshift);
        }
        while (size > 0) {
            dst -= SCRATCHSIZE;
            src -= SCRATCHSIZE;
            size -= SCRATCHSIZE;
            aligned_fetch_fbmem_to_scratch(SCRATCHSIZE + extrasize,
                                           scratchbuf, src - alignshift);
            writeback_scratch_to_mem(SCRATCHSIZE, dst, scratchbuf + alignshift);
        }
    }
}

static void
twopass_memmove_neon(void *dst, const void *src, size_t size)
{
    twopass_memmove(dst, src, size,
                    aligned_fetch_fbmem_to_scratch_neon,
                    writeback_scratch_to_mem_neon);
}

static void
twopass_memmove_vfp(void *dst, const void *src, size_t size)
{
    twopass_memmove(dst, src, size,
                    aligned_fetch_fbmem_to_scratch_vfp,
                    writeback_scratch_to_mem_arm);
}

static void
twopass_memmove_arm(void *dst, const void *src, size_t size)
{
    twopass_memmove(dst, src, size,
                    aligned_fetch_fbmem_to_scratch_arm,
                    writeback_scratch_to_mem_arm);
}

static void
twopass_blt_8bpp(int        width,
                 int        height,
                 uint8_t   *dst_bytes,
                 uintptr_t  dst_stride,
                 uint8_t   *src_bytes,
                 uintptr_t  src_stride,
                 void (*twopass_memmove)(void *, const void *, size_t))
{
    if (src_bytes < dst_bytes + width &&
        src_bytes + src_stride * height > dst_bytes)
    {
        src_bytes += src_stride * height - src_stride;
        dst_bytes += dst_stride * height - dst_stride;
        dst_stride = -dst_stride;
        src_stride = -src_stride;
        if (src_bytes + width > dst_bytes)
        {
            while (--height >= 0)
            {
                twopass_memmove(dst_bytes, src_bytes, width);
                dst_bytes += dst_stride;
                src_bytes += src_stride;
            }
            return;
        }
    }
    while (--height >= 0)
    {
        twopass_memmove(dst_bytes, src_bytes, width);
        dst_bytes += dst_stride;
        src_bytes += src_stride;
    }
}

static always_inline int
overlapped_blt(void     *self,
               uint32_t *src_bits,
               uint32_t *dst_bits,
               int       src_stride,
               int       dst_stride,
               int       src_bpp,
               int       dst_bpp,
               int       src_x,
               int       src_y,
               int       dst_x,
               int       dst_y,
               int       width,
               int       height,
               void (*twopass_memmove)(void *, const void *, size_t))
{
    uint8_t *dst_bytes = (uint8_t *)dst_bits;
    uint8_t *src_bytes = (uint8_t *)src_bits;
    cpu_backend_t *ctx = (cpu_backend_t *)self;
    int bpp = src_bpp >> 3;
    int uncached_source = (src_bytes >= ctx->uncached_area_begin) &&
                          (src_bytes < ctx->uncached_area_end);
    if (!uncached_source)
        return 0;

    if (src_bpp != dst_bpp || src_bpp & 7 || src_stride < 0 || dst_stride < 0)
        return 0;

    twopass_blt_8bpp((uintptr_t) width * bpp,
                     height,
                     dst_bytes + (uintptr_t) dst_y * dst_stride * 4 +
                                 (uintptr_t) dst_x * bpp,
                     (uintptr_t) dst_stride * 4,
                     src_bytes + (uintptr_t) src_y * src_stride * 4 +
                                 (uintptr_t) src_x * bpp,
                     (uintptr_t) src_stride * 4,
                     twopass_memmove);
    return 1;
}

static int
overlapped_blt_neon(void     *self,
                    uint32_t *src_bits,
                    uint32_t *dst_bits,
                    int       src_stride,
                    int       dst_stride,
                    int       src_bpp,
                    int       dst_bpp,
                    int       src_x,
                    int       src_y,
                    int       dst_x,
                    int       dst_y,
                    int       width,
                    int       height)
{
    return overlapped_blt(self, src_bits, dst_bits, src_stride, dst_stride,
                          src_bpp, dst_bpp, src_x, src_y, dst_x, dst_y,
                          width, height,
                          twopass_memmove_neon);
}

static int
overlapped_blt_vfp(void     *self,
                   uint32_t *src_bits,
                   uint32_t *dst_bits,
                   int       src_stride,
                   int       dst_stride,
                   int       src_bpp,
                   int       dst_bpp,
                   int       src_x,
                   int       src_y,
                   int       dst_x,
                   int       dst_y,
                   int       width,
                   int       height)
{
    return overlapped_blt(self, src_bits, dst_bits, src_stride, dst_stride,
                          src_bpp, dst_bpp, src_x, src_y, dst_x, dst_y,
                          width, height,
                          twopass_memmove_vfp);
}

static int
overlapped_blt_arm(void     *self,
                   uint32_t *src_bits,
                   uint32_t *dst_bits,
                   int       src_stride,
                   int       dst_stride,
                   int       src_bpp,
                   int       dst_bpp,
                   int       src_x,
                   int       src_y,
                   int       dst_x,
                   int       dst_y,
                   int       width,
                   int       height)
{
    return overlapped_blt(self, src_bits, dst_bits, src_stride, dst_stride,
                          src_bpp, dst_bpp, src_x, src_y, dst_x, dst_y,
                          width, height,
                          twopass_memmove_arm);
}

#endif

/* An empty, always failing implementation */
static int
overlapped_blt_noop(void     *self,
                    uint32_t *src_bits,
                    uint32_t *dst_bits,
                    int       src_stride,
                    int       dst_stride,
                    int       src_bpp,
                    int       dst_bpp,
                    int       src_x,
                    int       src_y,
                    int       dst_x,
                    int       dst_y,
                    int       width,
                    int       height)
{
    return 0;
}

cpu_backend_t *cpu_backend_init(uint8_t *uncached_buffer,
                                size_t   uncached_buffer_size)
{
    cpu_backend_t *ctx = calloc(sizeof(cpu_backend_t), 1);
    if (!ctx)
        return NULL;

    ctx->uncached_area_begin = uncached_buffer;
    ctx->uncached_area_end   = uncached_buffer + uncached_buffer_size;

    ctx->blt2d.self = ctx;
    ctx->blt2d.overlapped_blt = overlapped_blt_noop;

    ctx->cpuinfo = cpuinfo_init();

#ifdef __arm__
    if (ctx->cpuinfo->has_arm_neon &&
        ctx->cpuinfo->arm_implementer == 0x41 &&
        ctx->cpuinfo->arm_part == 0xC08)
    {
        /* NEON works better on Cortex-A8 */
        ctx->blt2d.overlapped_blt = overlapped_blt_neon;
    }
    else if (ctx->cpuinfo->has_arm_wmmx) {
        /* ARM LDM/STM works better than VFP/WMMX on Marvell PJ4 */
        ctx->blt2d.overlapped_blt = overlapped_blt_arm;
    }
    else if (ctx->cpuinfo->has_arm_vfp && ctx->cpuinfo->has_arm_edsp) {
        /* VFP works better on Cortex-A9, Cortex-A15 and maybe everything else */
        ctx->blt2d.overlapped_blt = overlapped_blt_vfp;
    }
#endif

    return ctx;
}

void cpu_backend_close(cpu_backend_t *ctx)
{
    if (ctx->cpuinfo)
        cpuinfo_close(ctx->cpuinfo);

    free(ctx);
}
