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

#ifndef CPUINFO_H
#define CPUINFO_H

/* The information about CPU features */
typedef struct {
    /* The values originating from MIDR register */
    int arm_implementer;
    int arm_architecture;
    int arm_variant;
    int arm_part;
    int arm_revision;
    /* CPU features */
    int has_arm_edsp;
    int has_arm_vfp;
    int has_arm_neon;
    int has_arm_wmmx;
    /* The user-friendly CPU description string (usable for logs, etc.) */
    char *processor_name;
} cpuinfo_t;

cpuinfo_t *cpuinfo_init();
void cpuinfo_close(cpuinfo_t *cpuinfo);

#endif
