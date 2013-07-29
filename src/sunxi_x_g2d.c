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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <pixman.h>

#include "xorgVersion.h"
#include "xf86_OSproc.h"
#include "xf86.h"
#include "xf86drm.h"
#include "dri2.h"
#include "damage.h"
#include "fb.h"
#include "gcstruct.h"

#include "fbdev_priv.h"
#include "sunxi_x_g2d.h"

/*
 * The code below is borrowed from "xserver/fb/fbwindow.c"
 */

static void
xCopyWindowProc(DrawablePtr pSrcDrawable,
                 DrawablePtr pDstDrawable,
                 GCPtr pGC,
                 BoxPtr pbox,
                 int nbox,
                 int dx,
                 int dy,
                 Bool reverse, Bool upsidedown, Pixel bitplane, void *closure)
{
    FbBits *src;
    FbStride srcStride;
    int srcBpp;
    int srcXoff, srcYoff;
    FbBits *dst;
    FbStride dstStride;
    int dstBpp;
    int dstXoff, dstYoff;
    ScreenPtr pScreen = pDstDrawable->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SunxiG2D *private = SUNXI_G2D(pScrn);

    fbGetDrawable(pSrcDrawable, src, srcStride, srcBpp, srcXoff, srcYoff);
    fbGetDrawable(pDstDrawable, dst, dstStride, dstBpp, dstXoff, dstYoff);

    while (nbox--) {
        if (!private->blt2d_overlapped_blt(private->blt2d_self,
                                           (uint32_t *)src, (uint32_t *)dst,
                                           srcStride, dstStride,
                                           srcBpp, dstBpp, (pbox->x1 + dx + srcXoff),
                                           (pbox->y1 + dy + srcYoff), (pbox->x1 + dstXoff),
                                           (pbox->y1 + dstYoff), (pbox->x2 - pbox->x1),
                                           (pbox->y2 - pbox->y1))) {
            /* fallback to fbBlt */
            fbBlt(src + (pbox->y1 + dy + srcYoff) * srcStride,
                  srcStride,
                  (pbox->x1 + dx + srcXoff) * srcBpp,
                  dst + (pbox->y1 + dstYoff) * dstStride,
                  dstStride,
                  (pbox->x1 + dstXoff) * dstBpp,
                  (pbox->x2 - pbox->x1) * dstBpp,
                  (pbox->y2 - pbox->y1),
                  GXcopy, FB_ALLONES, dstBpp, reverse, upsidedown);
        }
        pbox++;
    }

    fbFinishAccess(pDstDrawable);
    fbFinishAccess(pSrcDrawable);
}

static void
xCopyWindow(WindowPtr pWin, DDXPointRec ptOldOrg, RegionPtr prgnSrc)
{
    RegionRec rgnDst;
    int dx, dy;

    PixmapPtr pPixmap = fbGetWindowPixmap(pWin);
    DrawablePtr pDrawable = &pPixmap->drawable;

    dx = ptOldOrg.x - pWin->drawable.x;
    dy = ptOldOrg.y - pWin->drawable.y;
    RegionTranslate(prgnSrc, -dx, -dy);

    RegionNull(&rgnDst);

    RegionIntersect(&rgnDst, &pWin->borderClip, prgnSrc);

#ifdef COMPOSITE
    if (pPixmap->screen_x || pPixmap->screen_y)
        RegionTranslate(&rgnDst, -pPixmap->screen_x, -pPixmap->screen_y);
#endif

    miCopyRegion(pDrawable, pDrawable,
                 0, &rgnDst, dx, dy, xCopyWindowProc, 0, 0);

    RegionUninit(&rgnDst);
    fbValidateDrawable(&pWin->drawable);
}

/*****************************************************************************/

static void
xCopyNtoN(DrawablePtr pSrcDrawable,
          DrawablePtr pDstDrawable,
          GCPtr pGC,
          BoxPtr pbox,
          int nbox,
          int dx,
          int dy,
          Bool reverse, Bool upsidedown, Pixel bitplane, void *closure)
{
    CARD8 alu = pGC ? pGC->alu : GXcopy;
    FbBits pm = pGC ? fbGetGCPrivate(pGC)->pm : FB_ALLONES;
    FbBits *src;
    FbStride srcStride;
    int srcBpp;
    int srcXoff, srcYoff;
    FbBits *dst;
    FbStride dstStride;
    int dstBpp;
    int dstXoff, dstYoff;
    ScreenPtr pScreen = pDstDrawable->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SunxiG2D *private = SUNXI_G2D(pScrn);

    fbGetDrawable(pSrcDrawable, src, srcStride, srcBpp, srcXoff, srcYoff);
    fbGetDrawable(pDstDrawable, dst, dstStride, dstBpp, dstXoff, dstYoff);

    while (nbox--) {
        /* first try G2D */
        Bool done = private->blt2d_overlapped_blt(
                             private->blt2d_self,
                             (uint32_t *)src, (uint32_t *)dst,
                             srcStride, dstStride,
                             srcBpp, dstBpp, (pbox->x1 + dx + srcXoff),
                             (pbox->y1 + dy + srcYoff), (pbox->x1 + dstXoff),
                             (pbox->y1 + dstYoff), (pbox->x2 - pbox->x1),
                             (pbox->y2 - pbox->y1));

        /* then pixman (NEON) */
        if (!done && !reverse && !upsidedown) {
            done = pixman_blt((uint32_t *)src, (uint32_t *)dst, srcStride, dstStride,
                 srcBpp, dstBpp, (pbox->x1 + dx + srcXoff),
                 (pbox->y1 + dy + srcYoff), (pbox->x1 + dstXoff),
                 (pbox->y1 + dstYoff), (pbox->x2 - pbox->x1),
                 (pbox->y2 - pbox->y1));
        }

        /* fallback to fbBlt if other methods did not work */
        if (!done) {
            fbBlt(src + (pbox->y1 + dy + srcYoff) * srcStride,
                  srcStride,
                  (pbox->x1 + dx + srcXoff) * srcBpp,
                  dst + (pbox->y1 + dstYoff) * dstStride,
                  dstStride,
                  (pbox->x1 + dstXoff) * dstBpp,
                  (pbox->x2 - pbox->x1) * dstBpp,
                  (pbox->y2 - pbox->y1), alu, pm, dstBpp, reverse, upsidedown);
        }
        pbox++;
    }

    fbFinishAccess(pDstDrawable);
    fbFinishAccess(pSrcDrawable);
}

static RegionPtr
xCopyArea(DrawablePtr pSrcDrawable,
         DrawablePtr pDstDrawable,
         GCPtr pGC,
         int xIn, int yIn, int widthSrc, int heightSrc, int xOut, int yOut)
{
    CARD8 alu = pGC ? pGC->alu : GXcopy;
    FbBits pm = pGC ? fbGetGCPrivate(pGC)->pm : FB_ALLONES;

    if (pm == FB_ALLONES && alu == GXcopy && 
        pSrcDrawable->bitsPerPixel == pDstDrawable->bitsPerPixel &&
        (pSrcDrawable->bitsPerPixel == 32 || pSrcDrawable->bitsPerPixel == 16))
    {
        return miDoCopy(pSrcDrawable, pDstDrawable, pGC, xIn, yIn,
                    widthSrc, heightSrc, xOut, yOut, xCopyNtoN, 0, 0);
    }
    return fbCopyArea(pSrcDrawable,
                      pDstDrawable,
                      pGC,
                      xIn, yIn, widthSrc, heightSrc, xOut, yOut);
}

/*
 * The following function is adapted from xserver/fb/fbPutImage.c.
 */

static void xPutImage(DrawablePtr pDrawable,
           GCPtr pGC,
           int depth,
           int x, int y, int w, int h, int leftPad, int format, char *pImage)
{
    FbGCPrivPtr pPriv;

    FbStride srcStride;
    FbStip *src;
    RegionPtr pClip;
    FbStip *dst;
    FbStride dstStride;
    int dstBpp;
    int dstXoff, dstYoff;
    int nbox;
    BoxPtr pbox;
    int x1, y1, x2, y2;

    if (format == XYBitmap || format == XYPixmap ||
    pDrawable->bitsPerPixel != BitsPerPixel(pDrawable->depth)) {
        fbPutImage(pDrawable, pGC, depth, x, y, w, h, leftPad, format, pImage);
        return;
    }

    pPriv =fbGetGCPrivate(pGC);
    if (pPriv->pm != FB_ALLONES || pGC->alu != GXcopy) {
        fbPutImage(pDrawable, pGC, depth, x, y, w, h, leftPad, format, pImage);
        return;
    }

    ScreenPtr pScreen = pDrawable->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SunxiG2D *private = SUNXI_G2D(pScrn);

    src = (FbStip *)pImage;

    x += pDrawable->x;
    y += pDrawable->y;

    srcStride = PixmapBytePad(w, pDrawable->depth) / sizeof(FbStip);
    pClip = fbGetCompositeClip(pGC);

    fbGetStipDrawable(pDrawable, dst, dstStride, dstBpp, dstXoff, dstYoff);

    for (nbox = RegionNumRects(pClip),
        pbox = RegionRects(pClip); nbox--; pbox++) {
        x1 = x;
        y1 = y;
        x2 = x + w;
        y2 = y + h;
        if (x1 < pbox->x1)
            x1 = pbox->x1;
        if (y1 < pbox->y1)
            y1 = pbox->y1;
        if (x2 > pbox->x2)
            x2 = pbox->x2;
        if (y2 > pbox->y2)
            y2 = pbox->y2;
        if (x1 >= x2 || y1 >= y2)
            continue;
        Bool done = FALSE;
        int w = x2 - x1;
        int h = y2 - y1;
        /* first try pixman (NEON) */
        if (!done) {
            done = pixman_blt((uint32_t *)src, (uint32_t *)dst, srcStride, dstStride,
                 dstBpp, dstBpp, x1 - x,
                 y1 - y, x1 + dstXoff,
                 y1 + dstYoff, w,
                 h);
        }
        /* otherwise fall back to fb */
        if (!done)
            fbBlt(src + (y1 - y) * srcStride,
                  srcStride,
                  (x1 - x) * dstBpp,
                  dst + (y1 + dstYoff) * dstStride,
                  dstStride,
                  (x1 + dstXoff) * dstBpp,
                  w * dstBpp,
                  h, GXcopy, FB_ALLONES, dstBpp, FALSE, FALSE);
    }
    fbFinishAccess(pDrawable);
}

static Bool
xCreateGC(GCPtr pGC)
{
    ScreenPtr pScreen = pGC->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SunxiG2D *self = SUNXI_G2D(pScrn);
    Bool result;

    if (!fbCreateGC(pGC))
        return FALSE;

    if (!self->pGCOps) {
        self->pGCOps = calloc(1, sizeof(GCOps));
        memcpy(self->pGCOps, pGC->ops, sizeof(GCOps));

        /* Add our own hook for CopyArea function */
        self->pGCOps->CopyArea = xCopyArea;
        /* Add our own hook for PutImage */
        self->pGCOps->PutImage = xPutImage;
    }
    pGC->ops = self->pGCOps;

    return TRUE;
}

/*****************************************************************************/

SunxiG2D *SunxiG2D_Init(ScreenPtr pScreen, blt2d_i *blt2d)
{
    SunxiG2D *private = calloc(1, sizeof(SunxiG2D));
    if (!private) {
        xf86DrvMsg(pScreen->myNum, X_INFO,
            "SunxiG2D_Init: calloc failed\n");
        return NULL;
    }

    /* Cache the pointers from blt2d_i here */
    private->blt2d_self = blt2d->self;
    private->blt2d_overlapped_blt = blt2d->overlapped_blt;

    /* Wrap the current CopyWindow function */
    private->CopyWindow = pScreen->CopyWindow;
    pScreen->CopyWindow = xCopyWindow;

    /* Wrap the current CreateGC function */
    private->CreateGC = pScreen->CreateGC;
    pScreen->CreateGC = xCreateGC;

    return private;
}

void SunxiG2D_Close(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SunxiG2D *private = SUNXI_G2D(pScrn);

    pScreen->CopyWindow = private->CopyWindow;
    pScreen->CreateGC   = private->CreateGC;

    if (private->pGCOps) {
        free(private->pGCOps);
    }
}
