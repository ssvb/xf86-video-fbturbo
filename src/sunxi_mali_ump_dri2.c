/*
 * Copyright (C) 2013 Siarhei Siamashka <siarhei.siamashka@gmail.com>
 *
 * Initially derived from "mali_dri2.c" which is a part of xf86-video-mali,
 * even though there is now hardly any original line of code remaining.
 *
 * Copyright (C) 2010 ARM Limited. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ump/ump.h>
#include <ump/ump_ref_drv.h>

#include <sys/ioctl.h>

#include "xorgVersion.h"
#include "xf86_OSproc.h"
#include "xf86.h"
#include "xf86drm.h"
#include "dri2.h"
#include "damage.h"
#include "fb.h"

#include "fbdev_priv.h"
#include "sunxi_disp.h"
#include "sunxi_disp_hwcursor.h"
#include "sunxi_disp_ioctl.h"
#include "sunxi_mali_ump_dri2.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a)  (sizeof((a)) / sizeof((a)[0]))
#endif

/*
 * The code below is borrowed from "xserver/dix/window.c"
 */

#define BOXES_OVERLAP(b1, b2) \
      (!( ((b1)->x2 <= (b2)->x1)  || \
	( ((b1)->x1 >= (b2)->x2)) || \
	( ((b1)->y2 <= (b2)->y1)) || \
	( ((b1)->y1 >= (b2)->y2)) ) )

static BoxPtr
WindowExtents(WindowPtr pWin, BoxPtr pBox)
{
    pBox->x1 = pWin->drawable.x - wBorderWidth(pWin);
    pBox->y1 = pWin->drawable.y - wBorderWidth(pWin);
    pBox->x2 = pWin->drawable.x + (int) pWin->drawable.width
        + wBorderWidth(pWin);
    pBox->y2 = pWin->drawable.y + (int) pWin->drawable.height
        + wBorderWidth(pWin);
    return pBox;
}

static int
FancyTraverseTree(WindowPtr pWin, VisitWindowProcPtr func, pointer data)
{
    int result;
    WindowPtr pChild;

    if (!(pChild = pWin))
        return WT_NOMATCH;
    while (1) {
        result = (*func) (pChild, data);
        if (result == WT_STOPWALKING)
            return WT_STOPWALKING;
        if ((result == WT_WALKCHILDREN) && pChild->lastChild) {
            pChild = pChild->lastChild;
            continue;
        }
        while (!pChild->prevSib && (pChild != pWin))
            pChild = pChild->parent;
        if (pChild == pWin)
            break;
        pChild = pChild->prevSib;
    }
    return WT_NOMATCH;
}

static int
WindowWalker(WindowPtr pWin, pointer value)
{
    SunxiMaliDRI2 *mali = (SunxiMaliDRI2 *)value;

    if (mali->bWalkingAboveOverlayWin) {
        if (pWin->mapped && pWin->realized && pWin->drawable.class != InputOnly) {
            BoxRec sboxrec1;
            BoxPtr sbox1 = WindowExtents(pWin, &sboxrec1);
            BoxRec sboxrec2;
            BoxPtr sbox2 = WindowExtents(mali->pOverlayWin, &sboxrec2);
            if (BOXES_OVERLAP(sbox1, sbox2)) {
                mali->bOverlayWinOverlapped = TRUE;
                DebugMsg("overlapped by %p, x=%d, y=%d, w=%d, h=%d\n", pWin,
                         pWin->drawable.x, pWin->drawable.y,
                         pWin->drawable.width, pWin->drawable.height);
                return WT_STOPWALKING;
            }
        }
    }
    else if (pWin == mali->pOverlayWin) {
        mali->bWalkingAboveOverlayWin = TRUE;
    }

    return WT_WALKCHILDREN;
}

/* Migrate pixmap to UMP buffer */
static UMPBufferInfoPtr
MigratePixmapToUMP(PixmapPtr pPixmap)
{
    ScreenPtr pScreen = pPixmap->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SunxiMaliDRI2 *mali = SUNXI_MALI_UMP_DRI2(pScrn);
    UMPBufferInfoPtr umpbuf;
    size_t pitch = ((pPixmap->devKind + 7) / 8) * 8;
    size_t size = pitch * pPixmap->drawable.height;

    HASH_FIND_PTR(mali->HashPixmapToUMP, &pPixmap, umpbuf);

    if (umpbuf) {
        DebugMsg("MigratePixmapToUMP %p, already exists = %p\n", pPixmap, umpbuf);
        return umpbuf;
    }

    /* create the UMP buffer */
    umpbuf = calloc(1, sizeof(UMPBufferInfoRec));
    if (!umpbuf) {
        ErrorF("MigratePixmapToUMP: calloc failed\n");
        return NULL;
    }
    umpbuf->refcount = 1;
    umpbuf->pPixmap = pPixmap;
    umpbuf->handle = ump_ref_drv_allocate(size, UMP_REF_DRV_CONSTRAINT_PHYSICALLY_LINEAR);
    if (umpbuf->handle == UMP_INVALID_MEMORY_HANDLE) {
        ErrorF("MigratePixmapToUMP: ump_ref_drv_allocate failed\n");
        free(umpbuf);
        return NULL;
    }
    umpbuf->size = size;
    umpbuf->addr = ump_mapped_pointer_get(umpbuf->handle);
    umpbuf->depth = pPixmap->drawable.depth;
    umpbuf->width = pPixmap->drawable.width;
    umpbuf->height = pPixmap->drawable.height;

    /* copy the pixel data to the new location */
    if (pitch == pPixmap->devKind) {
        memcpy(umpbuf->addr, pPixmap->devPrivate.ptr, size);
    } else {
        int y;
        for (y = 0; y < umpbuf->height; y++) {
            memcpy(umpbuf->addr + y * pitch, 
                   pPixmap->devPrivate.ptr + y * pPixmap->devKind,
                   pPixmap->devKind);
        }
    }

    umpbuf->BackupDevKind = pPixmap->devKind;
    umpbuf->BackupDevPrivatePtr = pPixmap->devPrivate.ptr;

    pPixmap->devKind = pitch;
    pPixmap->devPrivate.ptr = umpbuf->addr;

    HASH_ADD_PTR(mali->HashPixmapToUMP, pPixmap, umpbuf);

    DebugMsg("MigratePixmapToUMP %p, new buf = %p\n", pPixmap, umpbuf);
    return umpbuf;
}

static void UpdateOverlay(ScreenPtr pScreen);

static void unref_ump_buffer_info(UMPBufferInfoPtr umpbuf)
{
    if (--umpbuf->refcount <= 0) {
        DebugMsg("unref_ump_buffer_info(%p) [refcount=%d, handle=%p]\n",
                 umpbuf, umpbuf->refcount, umpbuf->handle);
        if (umpbuf->handle != UMP_INVALID_MEMORY_HANDLE) {
            ump_mapped_pointer_release(umpbuf->handle);
            ump_reference_release(umpbuf->handle);
        }
        free(umpbuf);
    }
    else {
        DebugMsg("Reduced ump_buffer_info %p refcount to %d\n",
                 umpbuf, umpbuf->refcount);
    }
}

static void umpbuf_add_to_queue(DRI2WindowStatePtr window_state,
                                UMPBufferInfoPtr umpbuf)
{
    if (window_state->ump_queue[window_state->ump_queue_head]) {
        ErrorF("Fatal error, UMP buffers queue overflow!\n");
        return;
    }

    window_state->ump_queue[window_state->ump_queue_head] = umpbuf;
    window_state->ump_queue_head = (window_state->ump_queue_head + 1) %
                                   ARRAY_SIZE(window_state->ump_queue);
}

static UMPBufferInfoPtr umpbuf_fetch_from_queue(DRI2WindowStatePtr window_state)
{
    UMPBufferInfoPtr umpbuf;

    /* Check if the queue is empty */
    if (window_state->ump_queue_tail == window_state->ump_queue_head)
        return NULL;

    umpbuf = window_state->ump_queue[window_state->ump_queue_tail];
    window_state->ump_queue[window_state->ump_queue_tail] = NULL;

    window_state->ump_queue_tail = (window_state->ump_queue_tail + 1) %
                                   ARRAY_SIZE(window_state->ump_queue);
    return umpbuf;
}

/* Verify and fixup the DRI2Buffer before returning it to the X server */
static DRI2Buffer2Ptr validate_dri2buf(DRI2Buffer2Ptr dri2buf)
{
    UMPBufferInfoPtr umpbuf = (UMPBufferInfoPtr)dri2buf->driverPrivate;
    umpbuf->secure_id = dri2buf->name;
    umpbuf->pitch     = dri2buf->pitch;
    umpbuf->cpp       = dri2buf->cpp;
    umpbuf->offs      = dri2buf->flags;
    return dri2buf;
}

static DRI2Buffer2Ptr MaliDRI2CreateBuffer(DrawablePtr  pDraw,
                                           unsigned int attachment,
                                           unsigned int format)
{
    ScreenPtr                pScreen  = pDraw->pScreen;
    ScrnInfoPtr              pScrn    = xf86Screens[pScreen->myNum];
    DRI2Buffer2Ptr           buffer;
    UMPBufferInfoPtr         privates;
    ump_handle               handle;
    SunxiMaliDRI2           *mali = SUNXI_MALI_UMP_DRI2(pScrn);
    sunxi_disp_t            *disp = SUNXI_DISP(pScrn);
    Bool                     can_use_overlay = TRUE;
    PixmapPtr                pWindowPixmap;
    DRI2WindowStatePtr       window_state = NULL;
    Bool                     need_window_resize_bug_workaround = FALSE;

    if (!(buffer = calloc(1, sizeof *buffer))) {
        ErrorF("MaliDRI2CreateBuffer: calloc failed\n");
        return NULL;
    }

    if (pDraw->type == DRAWABLE_WINDOW &&
        (pWindowPixmap = pScreen->GetWindowPixmap((WindowPtr)pDraw)))
    {
        DebugMsg("win=%p (w=%d, h=%d, x=%d, y=%d) has backing pix=%p (w=%d, h=%d, screen_x=%d, screen_y=%d)\n",
                 pDraw, pDraw->width, pDraw->height, pDraw->x, pDraw->y,
                 pWindowPixmap, pWindowPixmap->drawable.width, pWindowPixmap->drawable.height,
                 pWindowPixmap->screen_x, pWindowPixmap->screen_y);
    }

    /* If it is a pixmap, just migrate this pixmap to UMP buffer */
    if (pDraw->type == DRAWABLE_PIXMAP)
    {
        if (!(privates = MigratePixmapToUMP((PixmapPtr)pDraw))) {
            ErrorF("MaliDRI2CreateBuffer: MigratePixmapToUMP failed\n");
            free(buffer);
            return NULL;
        }
        privates->refcount++;
        buffer->attachment    = attachment;
        buffer->driverPrivate = privates;
        buffer->format        = format;
        buffer->flags         = 0;
        buffer->cpp           = pDraw->bitsPerPixel / 8;
        buffer->pitch         = ((PixmapPtr)pDraw)->devKind;
        buffer->name = ump_secure_id_get(privates->handle);

        DebugMsg("DRI2CreateBuffer pix=%p, buf=%p:%p, att=%d, ump=%d:%d, w=%d, h=%d, cpp=%d, depth=%d\n",
                 pDraw, buffer, privates, attachment, buffer->name, buffer->flags,
                 privates->width, privates->height, buffer->cpp, privates->depth);

        return validate_dri2buf(buffer);
    }

    if (!(privates = calloc(1, sizeof *privates))) {
        ErrorF("MaliDRI2CreateBuffer: calloc failed\n");
        free(buffer);
        return NULL;
    }
    privates->refcount = 1;

    /* The default common values */
    buffer->attachment    = attachment;
    buffer->driverPrivate = privates;
    buffer->format        = format + 1; /* hack to suppress DRI2 buffers reuse */
    buffer->flags         = 0;
    buffer->cpp           = pDraw->bitsPerPixel / 8;
    /* Stride must be 8 bytes aligned for Mali400 */
    buffer->pitch         = ((buffer->cpp * pDraw->width + 7) / 8) * 8;

    privates->size     = pDraw->height * buffer->pitch;
    privates->width    = pDraw->width;
    privates->height   = pDraw->height;
    privates->depth    = pDraw->depth;

    /* We are not interested in anything other than back buffer requests ... */
    if (attachment != DRI2BufferBackLeft || pDraw->type != DRAWABLE_WINDOW) {
        /* ... and just return some dummy UMP buffer */
        privates->handle = UMP_INVALID_MEMORY_HANDLE;
        privates->addr   = NULL;
        buffer->name     = mali->ump_null_secure_id;
        return validate_dri2buf(buffer);
    }

    /* We could not allocate disp layer or get framebuffer secure id */
    if (!disp || mali->ump_fb_secure_id == UMP_INVALID_SECURE_ID)
        can_use_overlay = FALSE;

    /* Overlay is already used by a different window */
    if (mali->pOverlayWin && mali->pOverlayWin != (void *)pDraw)
        can_use_overlay = FALSE;

    /* Don't waste overlay on some strange 1x1 window created by gnome-shell */
    if (pDraw->width == 1 && pDraw->height == 1)
        can_use_overlay = FALSE;

    /* TODO: try to support other color depths later */
    if (pDraw->bitsPerPixel != 32)
        can_use_overlay = FALSE;

    if (disp && disp->framebuffer_size - disp->gfx_layer_size < privates->size * 2) {
        DebugMsg("Not enough space in the offscreen framebuffer (wanted %d for DRI2)\n",
                 privates->size);
        can_use_overlay = FALSE;
    }

    /* Allocate the DRI2-related window bookkeeping information */
    HASH_FIND_PTR(mali->HashWindowState, &pDraw, window_state);
    if (!window_state) {
        window_state = calloc(1, sizeof(*window_state));
        window_state->pDraw = pDraw;
        HASH_ADD_PTR(mali->HashWindowState, pDraw, window_state);
        DebugMsg("Allocate DRI2 bookkeeping for window %p\n", pDraw);
        if (disp && can_use_overlay) {
            /* erase the offscreen part of the framebuffer */
            memset(disp->framebuffer_addr + disp->gfx_layer_size, 0,
                   disp->framebuffer_size - disp->gfx_layer_size);
        }
    }
    window_state->buf_request_cnt++;

    /* For odd buffer requests save the window size */
    if (window_state->buf_request_cnt & 1) {
        /* remember window size for one buffer */
        window_state->width = pDraw->width;
        window_state->height = pDraw->height;
    }

    /* For even buffer requests check if the window size is still the same */
    need_window_resize_bug_workaround =
                          !(window_state->buf_request_cnt & 1) &&
                          (pDraw->width != window_state->width ||
                           pDraw->height != window_state->height) &&
                          mali->ump_null_secure_id <= 2;

    if (can_use_overlay) {
        /* Release unneeded buffers */
        if (window_state->ump_mem_buffer_ptr)
            unref_ump_buffer_info(window_state->ump_mem_buffer_ptr);
        window_state->ump_mem_buffer_ptr = NULL;

        /* Use offscreen part of the framebuffer as an overlay */
        privates->handle = UMP_INVALID_MEMORY_HANDLE;
        privates->addr = disp->framebuffer_addr;

        buffer->name = mali->ump_fb_secure_id;

        if (window_state->buf_request_cnt & 1) {
            buffer->flags = disp->gfx_layer_size;
            privates->extra_flags |= UMPBUF_MUST_BE_ODD_FRAME;
        }
        else {
            buffer->flags = disp->gfx_layer_size + privates->size;
            privates->extra_flags |= UMPBUF_MUST_BE_EVEN_FRAME;
        }

        umpbuf_add_to_queue(window_state, privates);
        privates->refcount++;

        mali->pOverlayWin = (WindowPtr)pDraw;

        if (need_window_resize_bug_workaround) {
            DebugMsg("DRI2 buffers size mismatch detected, trying to recover\n");
            buffer->name = mali->ump_alternative_fb_secure_id;
        }
    }
    else {
        /* Release unneeded buffers */
        if (window_state->ump_back_buffer_ptr)
            unref_ump_buffer_info(window_state->ump_back_buffer_ptr);
        window_state->ump_back_buffer_ptr = NULL;
        if (window_state->ump_front_buffer_ptr)
            unref_ump_buffer_info(window_state->ump_front_buffer_ptr);
        window_state->ump_front_buffer_ptr = NULL;

        if (need_window_resize_bug_workaround) {
            DebugMsg("DRI2 buffers size mismatch detected, trying to recover\n");

            if (window_state->ump_mem_buffer_ptr)
                unref_ump_buffer_info(window_state->ump_mem_buffer_ptr);
            window_state->ump_mem_buffer_ptr = NULL;

            privates->handle = UMP_INVALID_MEMORY_HANDLE;
            privates->addr   = NULL;
            buffer->name     = mali->ump_null_secure_id;
            return validate_dri2buf(buffer);
        }

        /* Reuse the existing UMP buffer if we can */
        if (window_state->ump_mem_buffer_ptr &&
            window_state->ump_mem_buffer_ptr->size == privates->size &&
            window_state->ump_mem_buffer_ptr->depth == privates->depth &&
            window_state->ump_mem_buffer_ptr->width == privates->width &&
            window_state->ump_mem_buffer_ptr->height == privates->height) {

            free(privates);

            privates = window_state->ump_mem_buffer_ptr;
            privates->refcount++;
            buffer->driverPrivate = privates;
            buffer->name = ump_secure_id_get(privates->handle);

            DebugMsg("Reuse the already allocated UMP buffer %p, ump=%d\n",
                     privates, buffer->name);
            return validate_dri2buf(buffer);
        }

        /* Allocate UMP memory buffer */
#ifdef HAVE_LIBUMP_CACHE_CONTROL
        privates->handle = ump_ref_drv_allocate(privates->size,
                                    UMP_REF_DRV_CONSTRAINT_PHYSICALLY_LINEAR |
                                    UMP_REF_DRV_CONSTRAINT_USE_CACHE);
        ump_cache_operations_control(UMP_CACHE_OP_START);
        ump_switch_hw_usage_secure_id(ump_secure_id_get(privates->handle),
                                      UMP_USED_BY_MALI);
        ump_cache_operations_control(UMP_CACHE_OP_FINISH);
#else
        privates->handle = ump_ref_drv_allocate(privates->size,
                                    UMP_REF_DRV_CONSTRAINT_PHYSICALLY_LINEAR);
#endif
        if (privates->handle == UMP_INVALID_MEMORY_HANDLE) {
            ErrorF("Failed to allocate UMP buffer (size=%d)\n",
                   (int)privates->size);
        }
        privates->addr = ump_mapped_pointer_get(privates->handle);
        buffer->name = ump_secure_id_get(privates->handle);
        buffer->flags = 0;

        /* Replace the old UMP buffer with the newly allocated one */
        if (window_state->ump_mem_buffer_ptr)
            unref_ump_buffer_info(window_state->ump_mem_buffer_ptr);

        window_state->ump_mem_buffer_ptr = privates;
        privates->refcount++;
    }

    DebugMsg("DRI2CreateBuffer win=%p, buf=%p:%p, att=%d, ump=%d:%d, w=%d, h=%d, cpp=%d, depth=%d\n",
             pDraw, buffer, privates, attachment, buffer->name, buffer->flags,
             privates->width, privates->height, buffer->cpp, privates->depth);

    return validate_dri2buf(buffer);
}

static void MaliDRI2DestroyBuffer(DrawablePtr pDraw, DRI2Buffer2Ptr buffer)
{
    UMPBufferInfoPtr privates;
    ScreenPtr pScreen = pDraw->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SunxiMaliDRI2 *mali = SUNXI_MALI_UMP_DRI2(pScrn);

    if (mali->pOverlayDirtyUMP == buffer->driverPrivate)
        mali->pOverlayDirtyUMP = NULL;

    DebugMsg("DRI2DestroyBuffer %s=%p, buf=%p:%p, att=%d\n",
             pDraw->type == DRAWABLE_WINDOW ? "win" : "pix",
             pDraw, buffer, buffer->driverPrivate, buffer->attachment);

    if (buffer != NULL) {
        privates = (UMPBufferInfoPtr)buffer->driverPrivate;
        unref_ump_buffer_info(privates);
        free(buffer);
    }
}

/* Do ordinary copy */
static void MaliDRI2CopyRegion_copy(DrawablePtr      pDraw,
                                    RegionPtr        pRegion,
                                    UMPBufferInfoPtr umpbuf)
{
    GCPtr pGC;
    RegionPtr copyRegion;
    ScreenPtr pScreen = pDraw->pScreen;
    UMPBufferInfoPtr privates;
    PixmapPtr pScratchPixmap;

#ifdef HAVE_LIBUMP_CACHE_CONTROL
    if (umpbuf->handle != UMP_INVALID_MEMORY_HANDLE) {
        /* That's a normal UMP allocation, not a wrapped framebuffer */
        ump_cache_operations_control(UMP_CACHE_OP_START);
        ump_switch_hw_usage_secure_id(umpbuf->secure_id, UMP_USED_BY_CPU);
        ump_cache_operations_control(UMP_CACHE_OP_FINISH);
    }
#endif

    pGC = GetScratchGC(pDraw->depth, pScreen);
    pScratchPixmap = GetScratchPixmapHeader(pScreen,
                                            umpbuf->width, umpbuf->height,
                                            umpbuf->depth, umpbuf->cpp * 8,
                                            umpbuf->pitch,
                                            umpbuf->addr + umpbuf->offs);
    copyRegion = REGION_CREATE(pScreen, NULL, 0);
    REGION_COPY(pScreen, copyRegion, pRegion);
    (*pGC->funcs->ChangeClip)(pGC, CT_REGION, copyRegion, 0);
    ValidateGC(pDraw, pGC);
    (*pGC->ops->CopyArea)((DrawablePtr)pScratchPixmap, pDraw, pGC, 0, 0,
                          pDraw->width, pDraw->height, 0, 0);
    FreeScratchPixmapHeader(pScratchPixmap);
    FreeScratchGC(pGC);

#ifdef HAVE_LIBUMP_CACHE_CONTROL
    if (umpbuf->handle != UMP_INVALID_MEMORY_HANDLE) {
        /* That's a normal UMP allocation, not a wrapped framebuffer */
        ump_cache_operations_control(UMP_CACHE_OP_START);
        ump_switch_hw_usage_secure_id(umpbuf->secure_id, UMP_USED_BY_MALI);
        ump_cache_operations_control(UMP_CACHE_OP_FINISH);
    }
#endif
}

static void FlushOverlay(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SunxiMaliDRI2 *mali = SUNXI_MALI_UMP_DRI2(pScrn);

    if (mali->pOverlayWin && mali->pOverlayDirtyUMP) {
        DebugMsg("Flushing overlay content from DRI2 buffer to window\n");
        MaliDRI2CopyRegion_copy((DrawablePtr)mali->pOverlayWin,
                                &pScreen->root->winSize,
                                mali->pOverlayDirtyUMP);
        mali->pOverlayDirtyUMP = NULL;
    }
}

#ifdef DEBUG_WITH_RGB_PATTERN
static void check_rgb_pattern(DRI2WindowStatePtr window_state,
                              UMPBufferInfoPtr umpbuf)
{
    switch (*(uint32_t *)(umpbuf->addr + umpbuf->offs)) {
    case 0xFFFF0000:
        if (window_state->rgb_pattern_state == 0) {
            ErrorF("starting RGB pattern with [Red]\n");
        }
        else if (window_state->rgb_pattern_state != 'B') {
            ErrorF("warning - transition to [Red] not from [Blue]\n");
        }
        window_state->rgb_pattern_state = 'R';
        break;
    case 0xFF00FF00:
        if (window_state->rgb_pattern_state == 0) {
            ErrorF("starting RGB pattern with [Green]\n");
        }
        else if (window_state->rgb_pattern_state != 'R') {
            ErrorF("warning - transition to [Green] not from [Red]\n");
        }
        window_state->rgb_pattern_state = 'G';
        break;
    case 0xFF0000FF:
        if (window_state->rgb_pattern_state == 0) {
            ErrorF("starting RGB pattern with [Blue]\n");
        }
        else if (window_state->rgb_pattern_state != 'G') {
            ErrorF("warning - transition to [Blue] not from [Green]\n");
        }
        window_state->rgb_pattern_state = 'B';
        break;
    default:
        if (window_state->rgb_pattern_state != 0) {
            ErrorF("stopping RGB pattern\n");
        }
        window_state->rgb_pattern_state = 0;
    }
}
#endif

static void MaliDRI2CopyRegion(DrawablePtr   pDraw,
                               RegionPtr     pRegion,
                               DRI2BufferPtr pDstBuffer,
                               DRI2BufferPtr pSrcBuffer)
{
    ScreenPtr pScreen = pDraw->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SunxiMaliDRI2 *mali = SUNXI_MALI_UMP_DRI2(pScrn);
    UMPBufferInfoPtr umpbuf;
    sunxi_disp_t *disp = SUNXI_DISP(xf86Screens[pScreen->myNum]);
    DRI2WindowStatePtr window_state = NULL;
    HASH_FIND_PTR(mali->HashWindowState, &pDraw, window_state);

    if (pDraw->type == DRAWABLE_PIXMAP) {
        DebugMsg("MaliDRI2CopyRegion has been called for pixmap %p\n", pDraw);
        return;
    }

    if (!window_state) {
        DebugMsg("MaliDRI2CopyRegion: can't find window %p in the hash\n", pDraw);
        return;
    }

    /* Try to fetch a new UMP buffer from the queue */
    umpbuf = umpbuf_fetch_from_queue(window_state);

    /*
     * Swap back and front buffers. But also ensure that the buffer
     * flags UMPBUF_MUST_BE_ODD_FRAME and UMPBUF_MUST_BE_EVEN_FRAME
     * are respected. In the case if swapping the buffers would result
     * in fetching the UMP buffer from the queue in the wrong order,
     * just skip the swap. This is a hack, which causes some temporary
     * glitch when resizing windows, but prevents a bigger problem.
     */
    if (!umpbuf || (!((umpbuf->extra_flags & UMPBUF_MUST_BE_ODD_FRAME) &&
                     (window_state->buf_swap_cnt & 1)) &&
                    !((umpbuf->extra_flags & UMPBUF_MUST_BE_EVEN_FRAME) &&
                     !(window_state->buf_swap_cnt & 1)))) {
        UMPBufferInfoPtr tmp               = window_state->ump_back_buffer_ptr;
        window_state->ump_back_buffer_ptr  = window_state->ump_front_buffer_ptr;
        window_state->ump_front_buffer_ptr = tmp;
        window_state->buf_swap_cnt++;
    }

    /* Try to replace the front buffer with a new UMP buffer from the queue */
    if (umpbuf) {
        if (window_state->ump_front_buffer_ptr)
            unref_ump_buffer_info(window_state->ump_front_buffer_ptr);
        window_state->ump_front_buffer_ptr = umpbuf;
    }
    else {
        umpbuf = window_state->ump_front_buffer_ptr;
    }

    if (!umpbuf)
        umpbuf = window_state->ump_mem_buffer_ptr;

    if (!umpbuf || !umpbuf->addr)
        return;

#ifdef DEBUG_WITH_RGB_PATTERN
    check_rgb_pattern(window_state, umpbuf);
#endif

    UpdateOverlay(pScreen);

    if (!mali->bOverlayWinEnabled || umpbuf->handle != UMP_INVALID_MEMORY_HANDLE) {
        MaliDRI2CopyRegion_copy(pDraw, pRegion, umpbuf);
        mali->pOverlayDirtyUMP = NULL;
        return;
    }

    /* Mark the overlay as "dirty" and remember the last up to date UMP buffer */
    mali->pOverlayDirtyUMP = umpbuf;

    /* Activate the overlay */
    sunxi_layer_set_output_window(disp, pDraw->x, pDraw->y, pDraw->width, pDraw->height);
    sunxi_layer_set_x8r8g8b8_input_buffer(disp, umpbuf->offs, umpbuf->width,
                                          umpbuf->height, umpbuf->pitch / 4);
    sunxi_layer_show(disp);

    if (mali->bSwapbuffersWait) {
        /* FIXME: blocking here for up to 1/60 second is not nice */
        sunxi_wait_for_vsync(disp);
    }
}

/************************************************************************/

static void UpdateOverlay(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SunxiMaliDRI2 *mali = SUNXI_MALI_UMP_DRI2(pScrn);
    sunxi_disp_t *disp = SUNXI_DISP(pScrn);

    if (!mali->pOverlayWin || !disp)
        return;

    /* Disable overlays if the hardware cursor is not in use */
    if (!mali->bHardwareCursorIsInUse) {
        if (mali->bOverlayWinEnabled) {
            DebugMsg("Disabling overlay (no hardware cursor)\n");
            sunxi_layer_hide(disp);
            mali->bOverlayWinEnabled = FALSE;
        }
        return;
    }

    /* If the window is not mapped, make sure that the overlay is disabled */
    if (!mali->pOverlayWin->mapped)
    {
        if (mali->bOverlayWinEnabled) {
            DebugMsg("Disabling overlay (window is not mapped)\n");
            sunxi_layer_hide(disp);
            mali->bOverlayWinEnabled = FALSE;
        }
        return;
    }

    /*
     * Walk the windows tree to get the obscured/unobscured status of
     * the window (because we can't rely on self->pOverlayWin->visibility
     * for redirected windows).
     */

    mali->bWalkingAboveOverlayWin = FALSE;
    mali->bOverlayWinOverlapped = FALSE;
    FancyTraverseTree(pScreen->root, WindowWalker, mali);

    /* If the window got overlapped -> disable overlay */
    if (mali->bOverlayWinOverlapped && mali->bOverlayWinEnabled) {
        DebugMsg("Disabling overlay (window is obscured)\n");
        FlushOverlay(pScreen);
        mali->bOverlayWinEnabled = FALSE;
        sunxi_layer_hide(disp);
        return;
    }

    /* If the window got moved -> update overlay position */
    if (!mali->bOverlayWinOverlapped &&
        (mali->overlay_x != mali->pOverlayWin->drawable.x ||
         mali->overlay_y != mali->pOverlayWin->drawable.y))
    {
        mali->overlay_x = mali->pOverlayWin->drawable.x;
        mali->overlay_y = mali->pOverlayWin->drawable.y;

        sunxi_layer_set_output_window(disp, mali->pOverlayWin->drawable.x,
                                      mali->pOverlayWin->drawable.y,
                                      mali->pOverlayWin->drawable.width,
                                      mali->pOverlayWin->drawable.height);
        DebugMsg("Move overlay to (%d, %d)\n", mali->overlay_x, mali->overlay_y);
    }

    /* If the window got unobscured -> enable overlay */
    if (!mali->bOverlayWinOverlapped && !mali->bOverlayWinEnabled) {
        DebugMsg("Enabling overlay (window is fully unobscured)\n");
        mali->bOverlayWinEnabled = TRUE;
        sunxi_layer_show(disp);
    }
}

static Bool
DestroyWindow(WindowPtr pWin)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SunxiMaliDRI2 *mali = SUNXI_MALI_UMP_DRI2(pScrn);
    Bool ret;
    DrawablePtr pDraw = &pWin->drawable;
    DRI2WindowStatePtr window_state = NULL;
    HASH_FIND_PTR(mali->HashWindowState, &pDraw, window_state);
    if (window_state) {
        DebugMsg("Free DRI2 bookkeeping for window %p\n", pWin);
        HASH_DEL(mali->HashWindowState, window_state);
        if (window_state->ump_mem_buffer_ptr)
            unref_ump_buffer_info(window_state->ump_mem_buffer_ptr);
        if (window_state->ump_back_buffer_ptr)
            unref_ump_buffer_info(window_state->ump_back_buffer_ptr);
        if (window_state->ump_front_buffer_ptr)
            unref_ump_buffer_info(window_state->ump_front_buffer_ptr);
        free(window_state);
    }

    if (pWin == mali->pOverlayWin) {
        sunxi_disp_t *disp = SUNXI_DISP(pScrn);
        sunxi_layer_hide(disp);
        mali->pOverlayWin = NULL;
        DebugMsg("DestroyWindow %p\n", pWin);
    }

    pScreen->DestroyWindow = mali->DestroyWindow;
    ret = (*pScreen->DestroyWindow) (pWin);
    mali->DestroyWindow = pScreen->DestroyWindow;
    pScreen->DestroyWindow = DestroyWindow;

    return ret;
}

static void
PostValidateTree(WindowPtr pWin, WindowPtr pLayerWin, VTKind kind)
{
    ScreenPtr pScreen = pWin ? pWin->drawable.pScreen : pLayerWin->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SunxiMaliDRI2 *mali = SUNXI_MALI_UMP_DRI2(pScrn);

    if (mali->PostValidateTree) {
        pScreen->PostValidateTree = mali->PostValidateTree;
        (*pScreen->PostValidateTree) (pWin, pLayerWin, kind);
        mali->PostValidateTree = pScreen->PostValidateTree;
        pScreen->PostValidateTree = PostValidateTree;
    }

    UpdateOverlay(pScreen);
}

/*
 * If somebody is trying to make a screenshot, we want to have DRI2 buffer
 * flushed.
 */
static void
GetImage(DrawablePtr pDrawable, int x, int y, int w, int h,
         unsigned int format, unsigned long planeMask, char *d)
{
    ScreenPtr pScreen = pDrawable->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SunxiMaliDRI2 *mali = SUNXI_MALI_UMP_DRI2(pScrn);

    /* FIXME: more precise check */
    if (mali->pOverlayDirtyUMP)
        FlushOverlay(pScreen);

    if (mali->GetImage) {
        pScreen->GetImage = mali->GetImage;
        (*pScreen->GetImage) (pDrawable, x, y, w, h, format, planeMask, d);
        mali->GetImage = pScreen->GetImage;
        pScreen->GetImage = GetImage;
    }
}

static Bool
DestroyPixmap(PixmapPtr pPixmap)
{
    ScreenPtr pScreen = pPixmap->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SunxiMaliDRI2 *mali = SUNXI_MALI_UMP_DRI2(pScrn);
    Bool result;
    UMPBufferInfoPtr umpbuf;
    HASH_FIND_PTR(mali->HashPixmapToUMP, &pPixmap, umpbuf);

    if (umpbuf) {
        DebugMsg("DestroyPixmap %p for migrated UMP pixmap (UMP buffer=%p)\n", pPixmap, umpbuf);

        pPixmap->devKind = umpbuf->BackupDevKind;
        pPixmap->devPrivate.ptr = umpbuf->BackupDevPrivatePtr;

        HASH_DEL(mali->HashPixmapToUMP, umpbuf);
        umpbuf->pPixmap = NULL;
        unref_ump_buffer_info(umpbuf);
    }

    pScreen->DestroyPixmap = mali->DestroyPixmap;
    result = (*pScreen->DestroyPixmap) (pPixmap);
    mali->DestroyPixmap = pScreen->DestroyPixmap;
    pScreen->DestroyPixmap = DestroyPixmap;


    return result;
}

static void EnableHWCursor(ScrnInfoPtr pScrn)
{
    SunxiMaliDRI2 *mali = SUNXI_MALI_UMP_DRI2(pScrn);
    SunxiDispHardwareCursor *hwc = SUNXI_DISP_HWC(pScrn);

    if (!mali->bHardwareCursorIsInUse) {
        DebugMsg("EnableHWCursor\n");
        mali->bHardwareCursorIsInUse = TRUE;
    }

    UpdateOverlay(screenInfo.screens[pScrn->scrnIndex]);

    if (mali->EnableHWCursor) {
        hwc->EnableHWCursor = mali->EnableHWCursor;
        (*hwc->EnableHWCursor) (pScrn);
        mali->EnableHWCursor = hwc->EnableHWCursor;
        hwc->EnableHWCursor = EnableHWCursor;
    }
}

static void DisableHWCursor(ScrnInfoPtr pScrn)
{
    SunxiMaliDRI2 *mali = SUNXI_MALI_UMP_DRI2(pScrn);
    SunxiDispHardwareCursor *hwc = SUNXI_DISP_HWC(pScrn);

    if (mali->bHardwareCursorIsInUse) {
        mali->bHardwareCursorIsInUse = FALSE;
        DebugMsg("DisableHWCursor\n");
    }

    UpdateOverlay(screenInfo.screens[pScrn->scrnIndex]);

    if (mali->DisableHWCursor) {
        hwc->DisableHWCursor = mali->DisableHWCursor;
        (*hwc->DisableHWCursor) (pScrn);
        mali->DisableHWCursor = hwc->DisableHWCursor;
        hwc->DisableHWCursor = DisableHWCursor;
    }
}

SunxiMaliDRI2 *SunxiMaliDRI2_Init(ScreenPtr pScreen,
                                  Bool      bUseOverlay,
                                  Bool      bSwapbuffersWait)
{
    int drm_fd;
    DRI2InfoRec info;
    SunxiMaliDRI2 *mali;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    sunxi_disp_t *disp = SUNXI_DISP(pScrn);

    if (!xf86LoadKernelModule("mali"))
        xf86DrvMsg(pScreen->myNum, X_INFO, "can't load 'mali' kernel module\n");
    if (!xf86LoadKernelModule("mali_drm"))
        xf86DrvMsg(pScreen->myNum, X_INFO, "can't load 'mali_drm' kernel module\n");

    if (!xf86LoadSubModule(xf86Screens[pScreen->myNum], "dri2"))
        return NULL;

    if ((drm_fd = drmOpen("mali_drm", NULL)) < 0) {
        ErrorF("SunxiMaliDRI2_Init: drmOpen failed!\n");
        return NULL;
    }

    if (ump_open() != UMP_OK) {
        drmClose(drm_fd);
        ErrorF("SunxiMaliDRI2_Init: ump_open() != UMP_OK\n");
        return NULL;
    }

    if (!(mali = calloc(1, sizeof(SunxiMaliDRI2)))) {
        ErrorF("SunxiMaliDRI2_Init: calloc failed\n");
        return NULL;
    }

    if (disp && bUseOverlay) {
        /* Try to get UMP framebuffer wrapper with secure id 1 */
        ioctl(disp->fd_fb, GET_UMP_SECURE_ID_BUF1, &mali->ump_alternative_fb_secure_id);
        /* Try to allocate a small dummy UMP buffer to secure id 2 */
        mali->ump_null_handle1 = ump_ref_drv_allocate(4096, UMP_REF_DRV_CONSTRAINT_NONE);
        if (mali->ump_null_handle1 != UMP_INVALID_MEMORY_HANDLE)
            mali->ump_null_secure_id = ump_secure_id_get(mali->ump_null_handle1);
        mali->ump_null_handle2 = UMP_INVALID_MEMORY_HANDLE;
        /* Try to get UMP framebuffer for the secure id other than 1 and 2 */
        if (ioctl(disp->fd_fb, GET_UMP_SECURE_ID_SUNXI_FB, &mali->ump_fb_secure_id) ||
                                   mali->ump_fb_secure_id == UMP_INVALID_SECURE_ID) {
            xf86DrvMsg(pScreen->myNum, X_INFO,
                  "GET_UMP_SECURE_ID_SUNXI_FB ioctl failed, overlays can't be used\n");
            mali->ump_fb_secure_id = UMP_INVALID_SECURE_ID;
        }
    }
    else {
        /* Try to allocate small dummy UMP buffers to secure id 1 and 2 */
        mali->ump_null_handle1 = ump_ref_drv_allocate(4096, UMP_REF_DRV_CONSTRAINT_NONE);
        if (mali->ump_null_handle1 != UMP_INVALID_MEMORY_HANDLE)
            mali->ump_null_secure_id = ump_secure_id_get(mali->ump_null_handle1);
        mali->ump_null_handle2 = ump_ref_drv_allocate(4096, UMP_REF_DRV_CONSTRAINT_NONE);
        /* No UMP wrappers for the framebuffer are available */
        mali->ump_fb_secure_id = UMP_INVALID_SECURE_ID;
        mali->ump_alternative_fb_secure_id = UMP_INVALID_SECURE_ID;
    }

    if (mali->ump_null_secure_id > 2) {
        xf86DrvMsg(pScreen->myNum, X_INFO,
                   "warning, can't workaround Mali r3p0 window resize bug\n");
    }

    if (disp && mali->ump_fb_secure_id != UMP_INVALID_SECURE_ID)
        xf86DrvMsg(pScreen->myNum, X_INFO,
              "enabled display controller hardware overlays for DRI2\n");
    else if (bUseOverlay)
        xf86DrvMsg(pScreen->myNum, X_INFO,
              "display controller hardware overlays can't be used for DRI2\n");
    else
        xf86DrvMsg(pScreen->myNum, X_INFO,
              "display controller hardware overlays are not used for DRI2\n");

    xf86DrvMsg(pScreen->myNum, X_INFO, "Wait on SwapBuffers? %s\n",
               bSwapbuffersWait ? "enabled" : "disabled");

    info.version = 3;

    info.driverName = "sunxi-mali";
    info.deviceName = "/dev/dri/card0";
    info.fd = drm_fd;

    info.CreateBuffer = MaliDRI2CreateBuffer;
    info.DestroyBuffer = MaliDRI2DestroyBuffer;
    info.CopyRegion = MaliDRI2CopyRegion;

    if (!DRI2ScreenInit(pScreen, &info)) {
        drmClose(drm_fd);
        free(mali);
        return NULL;
    }
    else {
        SunxiDispHardwareCursor *hwc = SUNXI_DISP_HWC(pScrn);

        /* Wrap the current DestroyWindow function */
        mali->DestroyWindow = pScreen->DestroyWindow;
        pScreen->DestroyWindow = DestroyWindow;
        /* Wrap the current PostValidateTree function */
        mali->PostValidateTree = pScreen->PostValidateTree;
        pScreen->PostValidateTree = PostValidateTree;
        /* Wrap the current GetImage function */
        mali->GetImage = pScreen->GetImage;
        pScreen->GetImage = GetImage;
        /* Wrap the current DestroyPixmap function */
        mali->DestroyPixmap = pScreen->DestroyPixmap;
        pScreen->DestroyPixmap = DestroyPixmap;

        /* Wrap hardware cursor callback functions */
        if (hwc) {
            mali->EnableHWCursor = hwc->EnableHWCursor;
            hwc->EnableHWCursor = EnableHWCursor;
            mali->DisableHWCursor = hwc->DisableHWCursor;
            hwc->DisableHWCursor = DisableHWCursor;
        }

        mali->drm_fd = drm_fd;
        mali->bSwapbuffersWait = bSwapbuffersWait;
        return mali;
    }
}

void SunxiMaliDRI2_Close(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SunxiMaliDRI2 *mali = SUNXI_MALI_UMP_DRI2(pScrn);
    SunxiDispHardwareCursor *hwc = SUNXI_DISP_HWC(pScrn);

    /* Unwrap functions */
    pScreen->DestroyWindow    = mali->DestroyWindow;
    pScreen->PostValidateTree = mali->PostValidateTree;
    pScreen->GetImage         = mali->GetImage;
    pScreen->DestroyPixmap    = mali->DestroyPixmap;

    if (hwc) {
        hwc->EnableHWCursor  = mali->EnableHWCursor;
        hwc->DisableHWCursor = mali->DisableHWCursor;
    }

    if (mali->ump_null_handle1 != UMP_INVALID_MEMORY_HANDLE)
        ump_reference_release(mali->ump_null_handle1);
    if (mali->ump_null_handle2 != UMP_INVALID_MEMORY_HANDLE)
        ump_reference_release(mali->ump_null_handle2);

    drmClose(mali->drm_fd);
    DRI2CloseScreen(pScreen);
}
