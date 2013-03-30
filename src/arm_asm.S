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

/* Prevent the stack from becoming executable */
#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif

#ifdef __arm__

.text
.syntax unified
.fpu neon
.arch armv7a
.object_arch armv4
.arm
.altmacro
.p2align 2

/******************************************************************************/

.macro asm_function function_name
    .global \function_name
.func \function_name
\function_name:
.endm

/******************************************************************************/

/*
 * writeback_scratch_to_mem_neon(int numbytes, void *dst, void *src)
 *
 * Copy a chunk of data from a cached scratch buffer (so prefetch is not
 * really needed), to a memory buffer in forward direction. Generated from
 * pixman macro templates.
 */

asm_function writeback_scratch_to_mem_neon
	mov	ip, r1
	cmp	r0, #32
	blt	0f
	tst	ip, #15
	beq	1f
	tst	ip, #1
	beq	2f
	vld1.8	{d0[1]}, [r2]!
	add	ip, ip, #1
	sub	r0, r0, #1
2:	tst	ip, #2
	beq	3f
	vld1.8	{d0[2]}, [r2]!
	vld1.8	{d0[3]}, [r2]!
	add	ip, ip, #2
	sub	r0, r0, #2
3:	tst	ip, #4
	beq	4f
	vld1.8	{d0[4]}, [r2]!
	vld1.8	{d0[5]}, [r2]!
	vld1.8	{d0[6]}, [r2]!
	vld1.8	{d0[7]}, [r2]!
	add	ip, ip, #4
	sub	r0, r0, #4
4:	tst	ip, #8
	beq	5f
	vld1.8	{d1}, [r2]!
	add	ip, ip, #8
	sub	r0, r0, #8
5:	vld1.8	{d2-d3}, [r2]!
	add	ip, ip, #16
	sub	r0, r0, #16
	tst	r1, #1
	beq	6f
	vst1.8	{d0[1]}, [r1]!
6:	tst	r1, #2
	beq	7f
	vst1.8	{d0[2]}, [r1]!
	vst1.8	{d0[3]}, [r1]!
7:	tst	r1, #4
	beq	8f
	vst1.8	{d0[4]}, [r1]!
	vst1.8	{d0[5]}, [r1]!
	vst1.8	{d0[6]}, [r1]!
	vst1.8	{d0[7]}, [r1]!
8:	tst	r1, #8
	beq	9f
	vst1.8	{d1}, [r1, :64]!
9:	vst1.8	{d2-d3}, [r1, :128]!
1:	subs	r0, r0, #32
	blt	10f
	vld1.8	{d0-d3}, [r2]!
	subs	r0, r0, #32
	blt	11f
12:	vst1.8	{d0-d3}, [r1, :128]!
	vld1.8	{d0-d3}, [r2]!
	subs	r0, r0, #32
	bge	12b
11:	vst1.8	{d0-d3}, [r1, :128]!
10:	tst	r0, #31
	beq	13f
	tst	r0, #16
	beq	14f
	vld1.8	{d2-d3}, [r2]!
14:	tst	r0, #8
	beq	15f
	vld1.8	{d1}, [r2]!
15:	tst	r0, #4
	beq	16f
	vld1.8	{d0[4]}, [r2]!
	vld1.8	{d0[5]}, [r2]!
	vld1.8	{d0[6]}, [r2]!
	vld1.8	{d0[7]}, [r2]!
16:	tst	r0, #2
	beq	17f
	vld1.8	{d0[2]}, [r2]!
	vld1.8	{d0[3]}, [r2]!
17:	tst	r0, #1
	beq	18f
	vld1.8	{d0[1]}, [r2]!
18:	tst	r0, #16
	beq	19f
	vst1.8	{d2-d3}, [r1, :128]!
19:	tst	r0, #8
	beq	20f
	vst1.8	{d1}, [r1, :64]!
20:	tst	r0, #4
	beq	21f
	vst1.8	{d0[4]}, [r1]!
	vst1.8	{d0[5]}, [r1]!
	vst1.8	{d0[6]}, [r1]!
	vst1.8	{d0[7]}, [r1]!
21:	tst	r0, #2
	beq	22f
	vst1.8	{d0[2]}, [r1]!
	vst1.8	{d0[3]}, [r1]!
22:	tst	r0, #1
	beq	13f
	vst1.8	{d0[1]}, [r1]!
13:	bx	lr
0:	tst	r0, #31
	beq	23f
	tst	r0, #16
	beq	24f
	vld1.8	{d2-d3}, [r2]!
24:	tst	r0, #8
	beq	25f
	vld1.8	{d1}, [r2]!
25:	tst	r0, #4
	beq	26f
	vld1.8	{d0[4]}, [r2]!
	vld1.8	{d0[5]}, [r2]!
	vld1.8	{d0[6]}, [r2]!
	vld1.8	{d0[7]}, [r2]!
26:	tst	r0, #2
	beq	27f
	vld1.8	{d0[2]}, [r2]!
	vld1.8	{d0[3]}, [r2]!
27:	tst	r0, #1
	beq	28f
	vld1.8	{d0[1]}, [r2]!
28:	tst	r0, #16
	beq	29f
	vst1.8	{d2-d3}, [r1]!
29:	tst	r0, #8
	beq	30f
	vst1.8	{d1}, [r1]!
30:	tst	r0, #4
	beq	31f
	vst1.8	{d0[4]}, [r1]!
	vst1.8	{d0[5]}, [r1]!
	vst1.8	{d0[6]}, [r1]!
	vst1.8	{d0[7]}, [r1]!
31:	tst	r0, #2
	beq	32f
	vst1.8	{d0[2]}, [r1]!
	vst1.8	{d0[3]}, [r1]!
32:	tst	r0, #1
	beq	23f
	vst1.8	{d0[1]}, [r1]!
23:	bx	lr
.endfunc

/******************************************************************************/

/*
 * aligned_fetch_fbmem_to_scratch_neon(int numbytes, void *scratch, void *fbmem)
 *
 * Both 'scratch' and 'fbmem' pointers must be 32 bytes aligned.
 * The value in 'numbytes' is also rounded up to a multiple of 32 bytes.

 * The only purpose of this code is to attempt minimizing penalty incured
 * by doing uncached reads from memory (for example framebuffer). We are
 * trying to do the largest possible perfectly aligned reads to fetch
 * data into a temporary scratch buffer in L1 cache.
 */

asm_function aligned_fetch_fbmem_to_scratch_neon
    SIZE        .req r0
    DST         .req r1
    SRC         .req r2

    subs        SIZE, #128
    blt         1f
0:
    /* aligned load from the source (framebuffer) */
    vld1.64     {q0, q1}, [SRC, :256]!
    vld1.64     {q2, q3}, [SRC, :256]!
    vld1.64     {q8, q9}, [SRC, :256]!
    vld1.64     {q10, q11}, [SRC, :256]!
    /* fetch destination (scratch buffer) into L1 cache */
    ldr         r3, [DST]
    ldr         ip, [DST, #64]
    /* aligned store to the scratch buffer */
    vst1.64     {q0, q1}, [DST, :256]!
    vst1.64     {q2, q3}, [DST, :256]!
    vst1.64     {q8, q9}, [DST, :256]!
    vst1.64     {q10, q11}, [DST, :256]!
    subs        SIZE, SIZE, #128
    bge         0b
1:
    tst         SIZE, #64
    beq         1f
    vld1.64     {q0, q1}, [SRC, :256]!
    vld1.64     {q2, q3}, [SRC, :256]!
    ldr         r3, [DST]
    vst1.64     {q0, q1}, [DST, :256]!
    vst1.64     {q2, q3}, [DST, :256]!
1:
    tst         SIZE, #32
    beq         1f
    vld1.64     {q0, q1}, [SRC, :256]!
    vst1.64     {q0, q1}, [DST, :256]!
1:
    tst         SIZE, #31
    beq         1f
    vld1.64     {q0, q1}, [SRC, :256]!
    vst1.64     {q0, q1}, [DST, :256]!
1:
    bx          lr

    .unreq      SIZE
    .unreq      DST
    .unreq      SRC
.endfunc

#endif
