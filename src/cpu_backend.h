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

#ifndef CPU_BACKEND_H
#define CPU_BACKEND_H

#include <inttypes.h>

#include "cpuinfo.h"
#include "interfaces.h"

/*
 * A set of CPU specific optimizations for different operations.
 * Supports a single memory area, where reads are uncached and may
 * need special treatment.
 */
typedef struct {
    /* The information about CPU features */
    cpuinfo_t *cpuinfo;
    /* The range of addresses for uncached area */
    uint8_t   *uncached_area_begin;
    uint8_t   *uncached_area_end;
    /* An accelerated implementation of blt2d_i interface */
    blt2d_i    blt2d;
} cpu_backend_t;

cpu_backend_t *cpu_backend_init(uint8_t *uncached_buffer, size_t uncached_buffer_size);
void cpu_backend_close(cpu_backend_t *cpu_backend);

#endif
