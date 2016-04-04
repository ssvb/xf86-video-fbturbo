/*
 * Copyright © 2016 Siarhei Siamashka <siarhei.siamashka@gmail.com>
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

/* Prevent the stack from becoming executable */
#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif

#ifdef __aarch64__

    .cpu cortex-a53+fp+simd
    .text
    .align 2

/******************************************************************************/

.macro asm_function function_name
    .global \function_name
#ifdef __ELF__
    .hidden \function_name
    .type \function_name, %function
#endif
.func \function_name
\function_name:
.endm

/******************************************************************************/

asm_function aligned_fetch_fbmem_to_scratch_neon
    SIZE        .req x0
    DST         .req x1
    SRC         .req x2

    subs        SIZE, SIZE, #128
    blt         1f
0:
    ldp         q0,  q1, [SRC, #(0 * 32)]
    ldp         q2,  q3, [SRC, #(1 * 32)]
    stp         q0,  q1, [DST, #(0 * 32)]
    stp         q2,  q3, [DST, #(1 * 32)]
    ldp         q0,  q1, [SRC, #(2 * 32)]
    ldp         q2,  q3, [SRC, #(3 * 32)]
    add         SRC, SRC, #128
    stp         q0,  q1, [DST, #(2 * 32)]
    stp         q2,  q3, [DST, #(3 * 32)]
    add         DST, DST, #128
    subs        SIZE, SIZE, #128
    bge         0b
1:
    tst         SIZE, #64
    beq         1f
    ldp         q0,  q1, [SRC, #(0 * 32)]
    ldp         q2,  q3, [SRC, #(1 * 32)]
    add         SRC, SRC, #64
    stp         q0,  q1, [DST, #(0 * 32)]
    stp         q2,  q3, [DST, #(1 * 32)]
    add         DST, DST, #64
1:
    tst         SIZE, #32
    beq         1f
    ldp         q0,  q1, [SRC, #(0 * 32)]
    add         SRC, SRC, #32
    stp         q0,  q1, [DST, #(0 * 32)]
    add         DST, DST, #32
1:
    tst         SIZE, #31
    beq         1f
    ldp         q0, q1, [SRC]
    stp         q0, q1, [DST]
1:
    ret

    .unreq      SIZE
    .unreq      DST
    .unreq      SRC
.endfunc

#endif
