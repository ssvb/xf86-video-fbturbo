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
#include "xf86.h"
#include "xf86drm.h"
#include "dri2.h"
#include "damage.h"
#include "fb.h"

#include "fbdev_priv.h"
#include "sunxi_disp.h"
#include "sunxi_disp_ioctl.h"
#include "sunxi_mali_ump_dri2.h"

#ifdef DEBUG
#define DebugMsg(...) ErrorF(__VA_ARGS__)
#else
#define DebugMsg(...)
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
    SunxiMaliDRI2 *private = (SunxiMaliDRI2 *)value;

    if (private->bWalkingAboveOverlayWin) {
        if (pWin->mapped && pWin->realized && pWin->drawable.class != InputOnly) {
            BoxRec sboxrec1;
            BoxPtr sbox1 = WindowExtents(pWin, &sboxrec1);
            BoxRec sboxrec2;
            BoxPtr sbox2 = WindowExtents(private->pOverlayWin, &sboxrec2);
            if (BOXES_OVERLAP(sbox1, sbox2)) {
                private->bOverlayWinOverlapped = TRUE;
                DebugMsg("overlapped by %p, x=%d, y=%d, w=%d, h=%d\n", pWin,
                         pWin->drawable.x, pWin->drawable.y,
                         pWin->drawable.width, pWin->drawable.height);
                return WT_STOPWALKING;
            }
        }
    }
    else if (pWin == private->pOverlayWin) {
        private->bWalkingAboveOverlayWin = TRUE;
    }

    return WT_WALKCHILDREN;
}

static void UpdateOverlay(ScreenPtr pScreen);

typedef struct
{
    ump_handle   handle;
    size_t       size;
    uint8_t *    addr;
    int          depth;
    size_t       width;
    size_t       height;
} MaliDRI2BufferPrivateRec, *MaliDRI2BufferPrivatePtr;

static DRI2Buffer2Ptr MaliDRI2CreateBuffer(DrawablePtr  pDraw,
                                           unsigned int attachment,
                                           unsigned int format)
{
    ScreenPtr                pScreen  = pDraw->pScreen;
    ScrnInfoPtr              pScrn    = xf86Screens[pScreen->myNum];
    DRI2Buffer2Ptr           buffer   = calloc(1, sizeof *buffer);
    MaliDRI2BufferPrivatePtr privates = calloc(1, sizeof *privates);
    ump_handle               handle;
    SunxiMaliDRI2 *private = SUNXI_MALI_UMP_DRI2(pScrn);
    sunxi_disp_t            *disp = SUNXI_DISP(pScrn);
    Bool                     can_use_overlay = TRUE;

    if (!buffer || !privates) {
        free(buffer);
        free(privates);
        ErrorF("MaliDRI2CreateBuffer: calloc failed\n\n");
        return NULL;
    }

    /* The drawable must be a window */
    if (pDraw->type != DRAWABLE_WINDOW)
        can_use_overlay = FALSE;

    /* We could not allocate disp layer or get framebuffer secure id */
    if (!disp || private->ump_fb_secure_id == UMP_INVALID_SECURE_ID)
        can_use_overlay = FALSE;

    /* Overlay is already used by a different window */
    if (private->pOverlayWin && private->pOverlayWin != (void *)pDraw)
        can_use_overlay = FALSE;

    /* TODO: try to support other color depths later */
    if (pDraw->bitsPerPixel != 32)
        can_use_overlay = FALSE;

    /* The default common values */
    buffer->attachment    = attachment;
    buffer->driverPrivate = privates;
    buffer->format        = format;
    buffer->flags         = 0;
    buffer->cpp           = pDraw->bitsPerPixel / 8;
    /* Stride must be 8 bytes aligned for Mali400 */
    buffer->pitch         = ((buffer->cpp * pDraw->width + 7) / 8) * 8;

    privates->size   = pDraw->height * buffer->pitch;
    privates->width  = pDraw->width;
    privates->height = pDraw->height;
    privates->depth  = pDraw->depth;

    if (disp && disp->framebuffer_size - disp->gfx_layer_size < privates->size) {
        DebugMsg("Not enough space in the offscreen framebuffer (wanted %d for DRI2)\n",
                 privates->size);
        can_use_overlay = FALSE;
    }

    if (can_use_overlay) {
        /* Use offscreen part of the framebuffer as an overlay */
        privates->handle = UMP_INVALID_MEMORY_HANDLE;
        privates->addr = disp->framebuffer_addr;

        buffer->name = private->ump_fb_secure_id;
        buffer->flags = disp->gfx_layer_size; /* this is offset */

        private->pOverlayWin = (WindowPtr)pDraw;

        if (sunxi_layer_set_x8r8g8b8_input_buffer(disp, buffer->flags,
                    privates->width, privates->height, buffer->pitch / 4) < 0) {
            ErrorF("Failed to set the source buffer for sunxi disp layer\n");
        }
    }
    else {
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
    }

    DebugMsg("MaliDRI2CreateBuffer attachment=%d %p, format=%d, width=%d, height=%d, cpp=%d, depth=%d\n",
           attachment, buffer, format, privates->width, privates->height, buffer->cpp, privates->depth);

    return buffer;
}

static void MaliDRI2DestroyBuffer(DrawablePtr pDraw, DRI2Buffer2Ptr buffer)
{
    MaliDRI2BufferPrivatePtr privates;
    ScreenPtr pScreen = pDraw->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SunxiMaliDRI2 *drvpriv = SUNXI_MALI_UMP_DRI2(pScrn);

    if (drvpriv->pOverlayDirtyDRI2Buf == buffer)
        drvpriv->pOverlayDirtyDRI2Buf = NULL;

    DebugMsg("Destroying attachment %d for drawable %p\n", buffer->attachment, pDraw);

    if (buffer != NULL) {
        privates = (MaliDRI2BufferPrivatePtr)buffer->driverPrivate;
        if (privates->handle != UMP_INVALID_MEMORY_HANDLE) {
            ump_mapped_pointer_release(privates->handle);
            ump_reference_release(privates->handle);
        }
        free(privates);
        free(buffer);
    }
}

/* Do ordinary copy */
static void MaliDRI2CopyRegion_copy(DrawablePtr   pDraw,
                                    RegionPtr     pRegion,
                                    DRI2BufferPtr pDstBuffer,
                                    DRI2BufferPtr pSrcBuffer)
{
    GCPtr pGC;
    RegionPtr copyRegion;
    ScreenPtr pScreen = pDraw->pScreen;
    MaliDRI2BufferPrivatePtr privates;
    PixmapPtr pScratchPixmap;
    privates = (MaliDRI2BufferPrivatePtr)pSrcBuffer->driverPrivate;

#ifdef HAVE_LIBUMP_CACHE_CONTROL
    if (privates->handle != UMP_INVALID_MEMORY_HANDLE) {
        /* That's a normal UMP allocation, not a wrapped framebuffer */
        ump_cache_operations_control(UMP_CACHE_OP_START);
        ump_switch_hw_usage_secure_id(pSrcBuffer->name, UMP_USED_BY_CPU);
        ump_cache_operations_control(UMP_CACHE_OP_FINISH);
    }
#endif

    pGC = GetScratchGC(pDraw->depth, pScreen);
    pScratchPixmap = GetScratchPixmapHeader(pScreen,
                                            privates->width, privates->height,
                                            privates->depth, pSrcBuffer->cpp * 8,
                                            pSrcBuffer->pitch,
                                            privates->addr + pSrcBuffer->flags);
    copyRegion = REGION_CREATE(pScreen, NULL, 0);
    REGION_COPY(pScreen, copyRegion, pRegion);
    (*pGC->funcs->ChangeClip)(pGC, CT_REGION, copyRegion, 0);
    ValidateGC(pDraw, pGC);
    (*pGC->ops->CopyArea)((DrawablePtr)pScratchPixmap, pDraw, pGC, 0, 0,
                          pDraw->width, pDraw->height, 0, 0);
    FreeScratchPixmapHeader(pScratchPixmap);
    FreeScratchGC(pGC);

#ifdef HAVE_LIBUMP_CACHE_CONTROL
    if (privates->handle != UMP_INVALID_MEMORY_HANDLE) {
        /* That's a normal UMP allocation, not a wrapped framebuffer */
        ump_cache_operations_control(UMP_CACHE_OP_START);
        ump_switch_hw_usage_secure_id(pSrcBuffer->name, UMP_USED_BY_MALI);
        ump_cache_operations_control(UMP_CACHE_OP_FINISH);
    }
#endif
}

static void FlushOverlay(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SunxiMaliDRI2 *self = SUNXI_MALI_UMP_DRI2(pScrn);

    if (self->pOverlayWin && self->pOverlayDirtyDRI2Buf) {
        DebugMsg("Flushing overlay content from DRI2 buffer to window\n");
        MaliDRI2CopyRegion_copy((DrawablePtr)self->pOverlayWin,
                                &pScreen->root->winSize,
                                self->pOverlayDirtyDRI2Buf,
                                self->pOverlayDirtyDRI2Buf);
        self->pOverlayDirtyDRI2Buf = NULL;
    }
}

static void MaliDRI2CopyRegion(DrawablePtr   pDraw,
                               RegionPtr     pRegion,
                               DRI2BufferPtr pDstBuffer,
                               DRI2BufferPtr pSrcBuffer)
{
    ScreenPtr pScreen = pDraw->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SunxiMaliDRI2 *drvpriv = SUNXI_MALI_UMP_DRI2(pScrn);
    MaliDRI2BufferPrivatePtr bufpriv = (MaliDRI2BufferPrivatePtr)pSrcBuffer->driverPrivate;
    sunxi_disp_t *disp = SUNXI_DISP(xf86Screens[pScreen->myNum]);

    UpdateOverlay(pScreen);

    if (!drvpriv->bOverlayWinEnabled || bufpriv->handle != UMP_INVALID_MEMORY_HANDLE) {
        MaliDRI2CopyRegion_copy(pDraw, pRegion, pDstBuffer, pSrcBuffer);
        drvpriv->pOverlayDirtyDRI2Buf = NULL;
        return;
    }

    /* Mark the overlay as "dirty" and remember the last up to date DRI2 buffer */
    drvpriv->pOverlayDirtyDRI2Buf = pSrcBuffer;

    /* Activate the overlay */
    sunxi_layer_set_output_window(disp, pDraw->x, pDraw->y, pDraw->width, pDraw->height);
    sunxi_layer_show(disp);
}

/************************************************************************/

static void UpdateOverlay(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SunxiMaliDRI2 *self = SUNXI_MALI_UMP_DRI2(pScrn);
    sunxi_disp_t *disp = SUNXI_DISP(pScrn);

    if (!self->pOverlayWin || !disp)
        return;

    /* If the window is not mapped, make sure that the overlay is disabled */
    if (!self->pOverlayWin->mapped)
    {
        if (self->bOverlayWinEnabled) {
            DebugMsg("Disabling overlay (window is not mapped)\n");
            sunxi_layer_hide(disp);
            self->bOverlayWinEnabled = FALSE;
        }
        return;
    }

    /*
     * Walk the windows tree to get the obscured/unobscured status of
     * the window (because we can't rely on self->pOverlayWin->visibility
     * for redirected windows).
     */

    self->bWalkingAboveOverlayWin = FALSE;
    self->bOverlayWinOverlapped = FALSE;
    FancyTraverseTree(pScreen->root, WindowWalker, self);

    /* If the window got overlapped -> disable overlay */
    if (self->bOverlayWinOverlapped && self->bOverlayWinEnabled) {
        DebugMsg("Disabling overlay (window is obscured)\n");
        FlushOverlay(pScreen);
        self->bOverlayWinEnabled = FALSE;
        sunxi_layer_hide(disp);
        return;
    }

    /* If the window got moved -> update overlay position */
    if (!self->bOverlayWinOverlapped &&
        (self->overlay_x != self->pOverlayWin->drawable.x ||
         self->overlay_y != self->pOverlayWin->drawable.y))
    {
        self->overlay_x = self->pOverlayWin->drawable.x;
        self->overlay_y = self->pOverlayWin->drawable.y;

        sunxi_layer_set_output_window(disp, self->pOverlayWin->drawable.x,
                                      self->pOverlayWin->drawable.y,
                                      self->pOverlayWin->drawable.width,
                                      self->pOverlayWin->drawable.height);
        DebugMsg("Move overlay to (%d, %d)\n", self->overlay_x, self->overlay_y);
    }

    /* If the window got unobscured -> enable overlay */
    if (!self->bOverlayWinOverlapped && !self->bOverlayWinEnabled) {
        DebugMsg("Enabling overlay (window is fully unobscured)\n");
        self->bOverlayWinEnabled = TRUE;
        sunxi_layer_show(disp);
    }
}

static Bool
DestroyWindow(WindowPtr pWin)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SunxiMaliDRI2 *private = SUNXI_MALI_UMP_DRI2(pScrn);
    Bool ret;

    if (pWin == private->pOverlayWin) {
        sunxi_disp_t *disp = SUNXI_DISP(pScrn);
        sunxi_layer_hide(disp);
        private->pOverlayWin = NULL;
        DebugMsg("DestroyWindow %p\n", pWin);
    }

    pScreen->DestroyWindow = private->DestroyWindow;
    ret = (*pScreen->DestroyWindow) (pWin);
    private->DestroyWindow = pScreen->DestroyWindow;
    pScreen->DestroyWindow = DestroyWindow;

    return ret;
}

static void
PostValidateTree(WindowPtr pWin, WindowPtr pLayerWin, VTKind kind)
{
    ScreenPtr pScreen = pWin ? pWin->drawable.pScreen : pLayerWin->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SunxiMaliDRI2 *private = SUNXI_MALI_UMP_DRI2(pScrn);

    if (private->PostValidateTree) {
        pScreen->PostValidateTree = private->PostValidateTree;
        (*pScreen->PostValidateTree) (pWin, pLayerWin, kind);
        private->PostValidateTree = pScreen->PostValidateTree;
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
    SunxiMaliDRI2 *private = SUNXI_MALI_UMP_DRI2(pScrn);

    /* FIXME: more precise check */
    if (private->pOverlayDirtyDRI2Buf)
        FlushOverlay(pScreen);

    if (private->GetImage) {
        pScreen->GetImage = private->GetImage;
        (*pScreen->GetImage) (pDrawable, x, y, w, h, format, planeMask, d);
        private->GetImage = pScreen->GetImage;
        pScreen->GetImage = GetImage;
    }
}

SunxiMaliDRI2 *SunxiMaliDRI2_Init(ScreenPtr pScreen, Bool bUseOverlay)
{
    int drm_fd;
    DRI2InfoRec info;
    ump_secure_id ump_id1, ump_id2, ump_id_fb;
    sunxi_disp_t *disp = SUNXI_DISP(xf86Screens[pScreen->myNum]);
    ump_id1 = ump_id2 = ump_id_fb = UMP_INVALID_SECURE_ID;

    if (disp && bUseOverlay) {
        /*
         * Workaround some glitches with secure id assignment (make sure
         * that GET_UMP_SECURE_ID_BUF1 and GET_UMP_SECURE_ID_BUF2 allocate
         * lower id numbers than GET_UMP_SECURE_ID_SUNXI_FB)
         */
        ioctl(disp->fd_fb, GET_UMP_SECURE_ID_BUF1, &ump_id1);
        ioctl(disp->fd_fb, GET_UMP_SECURE_ID_BUF2, &ump_id2);

        if (ioctl(disp->fd_fb, GET_UMP_SECURE_ID_SUNXI_FB, &ump_id_fb) < 0 ||
                                   ump_id_fb == UMP_INVALID_SECURE_ID) {
            xf86DrvMsg(pScreen->myNum, X_INFO,
                  "GET_UMP_SECURE_ID_SUNXI_FB ioctl failed, overlays can't be used\n");
            ump_id_fb = UMP_INVALID_SECURE_ID;
        }
    }

    if (!xf86LoadSubModule(xf86Screens[pScreen->myNum], "dri2"))
        return FALSE;

    if ((drm_fd = drmOpen("mali_drm", NULL)) < 0) {
        ErrorF("SunxiMaliDRI2_Init: drmOpen failed!\n");
        return FALSE;
    }

    if (ump_open() != UMP_OK) {
        drmClose(drm_fd);
        ErrorF("SunxiMaliDRI2_Init: ump_open() != UMP_OK\n");
        return FALSE;
    }

    if (disp && ump_id_fb != UMP_INVALID_SECURE_ID)
        xf86DrvMsg(pScreen->myNum, X_INFO,
              "enabled display controller hardware overlays for DRI2\n");
    else if (bUseOverlay)
        xf86DrvMsg(pScreen->myNum, X_INFO,
              "display controller hardware overlays can't be used for DRI2\n");
    else
        xf86DrvMsg(pScreen->myNum, X_INFO,
              "display controller hardware overlays are not used for DRI2\n");

    info.version = 3;

    info.driverName = "sunxi-mali";
    info.deviceName = "/dev/dri/card0";
    info.fd = drm_fd;

    info.CreateBuffer = MaliDRI2CreateBuffer;
    info.DestroyBuffer = MaliDRI2DestroyBuffer;
    info.CopyRegion = MaliDRI2CopyRegion;

    if (!DRI2ScreenInit(pScreen, &info)) {
        drmClose(drm_fd);
        return NULL;
    }
    else {
        SunxiMaliDRI2 *private = calloc(1, sizeof(SunxiMaliDRI2));

        /* Wrap the current DestroyWindow function */
        private->DestroyWindow = pScreen->DestroyWindow;
        pScreen->DestroyWindow = DestroyWindow;
        /* Wrap the current PostValidateTree function */
        private->PostValidateTree = pScreen->PostValidateTree;
        pScreen->PostValidateTree = PostValidateTree;
        /* Wrap the current GetImage function */
        private->GetImage = pScreen->GetImage;
        pScreen->GetImage = GetImage;

        private->ump_fb_secure_id = ump_id_fb;
        private->drm_fd = drm_fd;
        return private;
    }
}

void SunxiMaliDRI2_Close(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SunxiMaliDRI2 *private = SUNXI_MALI_UMP_DRI2(pScrn);

    /* Unwrap functions */
    pScreen->DestroyWindow    = private->DestroyWindow;
    pScreen->PostValidateTree = private->PostValidateTree;
    pScreen->GetImage         = private->GetImage;

    drmClose(private->drm_fd);
    DRI2CloseScreen(pScreen);
}
