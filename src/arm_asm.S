/*
 * Copyright © 2006-2008, 2013 Siarhei Siamashka <siarhei.siamashka@gmail.com>
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
#ifdef __ELF__
    .hidden \function_name
    .type \function_name, %function
#endif
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
 * Helper macro for memcpy function, it can copy data from source (r1) to 
 * destination (r0) buffers fixing alignment in the process. Destination
 * buffer should be aligned already (4 bytes alignment is required.
 * Size of the block to copy is in r2 register
 */
.macro  UNALIGNED_MEMCPY shift
    sub     r1, #(\shift)
    ldr     ip, [r1], #4

    tst     r0, #4
    movne   r3, ip, lsr #(\shift * 8)
    ldrne   ip, [r1], #4
    subne   r2, r2, #4
    orrne   r3, r3, ip, asl #(32 - \shift * 8)
    strne   r3, [r0], #4

    tst     r0, #8
    movne   r3, ip, lsr #(\shift * 8)
    ldmiane r1!, {r4, ip}
    subne   r2, r2, #8
    orrne   r3, r3, r4, asl #(32 - \shift * 8)
    movne   r4, r4, lsr #(\shift * 8)
    orrne   r4, r4, ip, asl #(32 - \shift * 8)
    stmiane r0!, {r3-r4}
    cmp     r2, #32
    blt     3f
    pld     [r1, #48]
    stmfd   sp!, {r7, r8, r9, r10, r11}
    add     r3, r1, #128
    bic     r3, r3, #31
    sub     r9, r3, r1
1:
    pld     [r1, r9]
    subs    r2, r2, #32
    movge   r3, ip, lsr #(\shift * 8)
    ldmiage r1!, {r4-r6, r7, r8, r10, r11, ip}
    orrge   r3, r3, r4, asl #(32 - \shift * 8)
    movge   r4, r4, lsr #(\shift * 8)
    orrge   r4, r4, r5, asl #(32 - \shift * 8)
    movge   r5, r5, lsr #(\shift * 8)
    orrge   r5, r5, r6, asl #(32 - \shift * 8)
    movge   r6, r6, lsr #(\shift * 8)
    orrge   r6, r6, r7, asl #(32 - \shift * 8)
    stmiage r0!, {r3-r6}
    movge   r7, r7, lsr #(\shift * 8)
    orrge   r7, r7, r8, asl #(32 - \shift * 8)
    movge   r8, r8, lsr #(\shift * 8)
    orrge   r8, r8, r10, asl #(32 - \shift * 8)
    movge   r10, r10, lsr #(\shift * 8)
    orrge   r10, r10, r11, asl #(32 - \shift * 8)
    movge   r11, r11, lsr #(\shift * 8)
    orrge   r11, r11, ip, asl #(32 - \shift * 8)
    stmiage r0!, {r7, r8, r10, r11}
    bgt     1b
2:
    ldmfd   sp!, {r7, r8, r9, r10, r11}
3:  /* copy remaining data */
    tst     r2, #16
    movne   r3, ip, lsr #(\shift * 8)
    ldmiane r1!, {r4-r6, ip}
    orrne   r3, r3, r4, asl #(32 - \shift * 8)
    movne   r4, r4, lsr #(\shift * 8)
    orrne   r4, r4, r5, asl #(32 - \shift * 8)
    movge   r5, r5, lsr #(\shift * 8)
    orrge   r5, r5, r6, asl #(32 - \shift * 8)
    movge   r6, r6, lsr #(\shift * 8)
    orrge   r6, r6, ip, asl #(32 - \shift * 8)
    stmiane r0!, {r3-r6}

    tst     r2, #8
    movne   r3, ip, lsr #(\shift * 8)
    ldmiane r1!, {r4, ip}
    orrne   r3, r3, r4, asl #(32 - \shift * 8)
    movne   r4, r4, lsr #(\shift * 8)
    orrne   r4, r4, ip, asl #(32 - \shift * 8)
    stmiane r0!, {r3-r4}

    tst     r2, #4
    movne   r3, ip, lsr #(\shift * 8)
    ldrne   ip, [r1], #4
    sub     r1, r1, #(4 - \shift)
    orrne   r3, r3, ip, asl #(32 - \shift * 8)
    strne   r3, [r0], #4

    tst     r2, #2
    ldrbne  r3, [r1], #1
    ldrbne  r4, [r1], #1
    ldr     r5, [sp], #4
    strbne  r3, [r0], #1
    strbne  r4, [r0], #1

    tst     r2, #1
    ldrbne  r3, [r1], #1
    ldr     r6, [sp], #4
    strbne  r3, [r0], #1

    ldmfd   sp!, {r0, r4}

    bx      lr
.endm

/*
 * Memcpy function with Raspberry Pi specific aligned prefetch, based on
 * https://garage.maemo.org/plugins/scmsvn/viewcvs.php/mplayer/trunk/fastmem-arm9/fastmem-arm9.S
 */
asm_function memcpy_armv5te
    cmp     r2, #20
    blt     9f
    /* copy data until destination address is 4 bytes aligned */
    tst     r0, #1
    ldrbne  r3, [r1], #1
    stmfd   sp!, {r0, r4}
    subne   r2, r2, #1
    strbne  r3, [r0], #1
    tst     r0, #2
    ldrbne  r3, [r1], #1
    ldrbne  r4, [r1], #1
    stmfd   sp!, {r5, r6}
    subne   r2, r2, #2
    orrne   r3, r3, r4, asl #8
    strhne  r3, [r0], #2
    /* destination address is 4 bytes aligned */
    /* now we should handle 4 cases of source address alignment */
    tst     r1, #1
    bne     6f
    tst     r1, #2
    bne     7f

    /* both source and destination are 4 bytes aligned */
    stmfd   sp!, {r7, r8, r9, r10, r11}
    tst     r0, #4
    ldrne   r4, [r1], #4
    subne   r2, r2, #4
    strne   r4, [r0], #4
    tst     r0, #8
    ldmiane r1!, {r3-r4}
    add     r9, r1, #96
    subne   r2, r2, #8
    bic     r9, r9, #31
    stmiane r0!, {r3-r4}
    sub     r9, r9, r1
1:
    subs    r2, r2, #32
    ldmiage r1!, {r3-r6, r7, r8, r10, r11}
    pld     [r1, r9]
    stmiage r0!, {r3-r6}
    stmiage r0!, {r7, r8, r10, r11}
    bgt     1b
2:
    ldmfd   sp!, {r7, r8, r9, r10, r11}
    tst     r2, #16
    ldmiane r1!, {r3-r6}
    stmiane r0!, {r3-r6}
    tst     r2, #8
    ldmiane r1!, {r3-r4}
    stmiane r0!, {r3-r4}
    tst     r2, #4
    ldrne   r3, [r1], #4
    mov     ip, r0
    strne   r3, [ip], #4
    tst     r2, #2
    ldrhne  r3, [r1], #2
    ldmfd   sp!, {r5, r6}
    strhne  r3, [ip], #2
    tst     r2, #1
    ldrbne  r3, [r1], #1
    ldmfd   sp!, {r0, r4}
    strbne  r3, [ip], #1

    bx      lr

6:
    tst    r1, #2
    bne    8f
    UNALIGNED_MEMCPY 1
7:
    UNALIGNED_MEMCPY 2
8:
    UNALIGNED_MEMCPY 3
9:
    stmfd  sp!, {r0, r4}
1:  subs   r2, r2, #3
    ldrbge ip, [r0]
    ldrbge r3, [r1], #1
    ldrbge r4, [r1], #1
    ldrbge ip, [r1], #1
    strbge r3, [r0], #1
    strbge r4, [r0], #1
    strbge ip, [r0], #1
    bge    1b
    adds   r2, r2, #2
    ldrbge r3, [r1], #1
    mov    ip, r0
    ldr    r0, [sp], #4
    strbge r3, [ip], #1
    ldrbgt r3, [r1], #1
    ldr    r4, [sp], #4
    strbgt r3, [ip], #1
    bx     lr
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

asm_function aligned_fetch_fbmem_to_scratch_vfp
    SIZE        .req r0
    DST         .req r1
    SRC         .req r2

    vpush       {d8-d15}
    subs        SIZE, #128
    blt         1f
0:
    /* aligned load from the source (framebuffer) */
    vldm        SRC!, {d0, d1, d2, d3, d4, d5, d6, d7, d8, d9, d10, d11, d12, d13, d14, d15}
    /* aligned store to the scratch buffer */
    vstm        DST!, {d0, d1, d2, d3, d4, d5, d6, d7, d8, d9, d10, d11, d12, d13, d14, d15}
    subs        SIZE, SIZE, #128
    bge         0b
1:
    tst         SIZE, #64
    beq         1f
    vldm        SRC!, {d0, d1, d2, d3, d4, d5, d6, d7}
    vstm        DST!, {d0, d1, d2, d3, d4, d5, d6, d7}
1:
    tst         SIZE, #32
    beq         1f
    vldm        SRC!, {d0, d1, d2, d3}
    vstm        DST!, {d0, d1, d2, d3}
1:
    tst         SIZE, #31
    beq         1f
    vldm        SRC!, {d0, d1, d2, d3}
    vstm        DST!, {d0, d1, d2, d3}
1:
    vpop        {d8-d15}
    bx          lr

    .unreq      SIZE
    .unreq      DST
    .unreq      SRC
.endfunc

asm_function aligned_fetch_fbmem_to_scratch_arm
    SIZE        .req r0
    DST         .req r1
    SRC         .req r2

    push        {r4-r11, lr}
    subs        SIZE, #128
    blt         1f
0:
    ldmia       SRC!, {r4-r11}
    stmia       DST!, {r4-r11}
    ldmia       SRC!, {r4-r11}
    stmia       DST!, {r4-r11}
    ldmia       SRC!, {r4-r11}
    stmia       DST!, {r4-r11}
    ldmia       SRC!, {r4-r11}
    stmia       DST!, {r4-r11}
    subs        SIZE, SIZE, #128
    bge         0b
1:
    tst         SIZE, #64
    beq         1f
    ldmia       SRC!, {r4-r11}
    stmia       DST!, {r4-r11}
    ldmia       SRC!, {r4-r11}
    stmia       DST!, {r4-r11}
1:
    tst         SIZE, #32
    beq         1f
    ldmia       SRC!, {r4-r11}
    stmia       DST!, {r4-r11}
1:
    tst         SIZE, #31
    beq         1f
    ldmia       SRC!, {r4-r11}
    stmia       DST!, {r4-r11}
1:
    pop         {r4-r11, pc}

    .unreq      SIZE
    .unreq      DST
    .unreq      SRC
.endfunc

#endif
