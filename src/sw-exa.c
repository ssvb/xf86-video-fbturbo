/* Texas Instruments OMAP framebuffer driver for X.Org
 * Copyright 2008 Kalle Vahlman, <zuh@iki.fi>
 *
 * Permission to use, copy, modify, distribute and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the names of the authors and/or copyright holders
 * not be used in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  The authors and
 * copyright holders make no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without any express
 * or implied warranty.
 *
 * THE AUTHORS AND COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
 * ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xorgVersion.h"
#include "xf86.h"
#include "fb.h"

#include "fbdev_priv.h"
#include "sw-exa.h"

#ifdef LOG_CALLS
# define FALLBACK do { ErrorF("Fallback from %s\n", __FUNCTION__); } while (0)
#else
# define FALLBACK do { } while (0) 
#endif

/*** Solid fill */

static Bool
SWPrepareSolid(PixmapPtr pPixmap, int alu, Pixel planemask, Pixel fg)
{
	FALLBACK;
	return FALSE;
}

static void
SWSolid(PixmapPtr pPixmap, int x1, int y1, int x2, int y2)
{
	FALLBACK;
}

static void
SWDoneSolid(PixmapPtr pPixmap)
{
	FALLBACK;
}

/*** Copy */

static Bool
SWPrepareCopy(PixmapPtr pSrcPixmap, PixmapPtr pDstPixmap, int dx, int dy, int alu, Pixel planemask) 
{
	FALLBACK;
	return FALSE;
}

static void
SWCopy(PixmapPtr pDstPixmap, int srcX, int srcY, int dstX, int dstY, int width, int height) 
{
	FALLBACK;
}

static void
SWDoneCopy(PixmapPtr pDstPixmap) 
{
	FALLBACK;
}

/*** Composite */

static Bool
SWCheckComposite(int op, PicturePtr pSrcPicture, PicturePtr pMaskPicture, PicturePtr pDstPicture) 
{
	FALLBACK;
	return FALSE;
}

static Bool
SWPrepareComposite(int op, PicturePtr pSrcPicture, PicturePtr pMaskPicture, PicturePtr pDstPicture, PixmapPtr pSrc, PixmapPtr pMask, PixmapPtr pDst)
{
	FALLBACK;
	return FALSE;
}

static void
SWComposite(PixmapPtr pDst, int srcX, int srcY, int maskX, int maskY, int dstX, int dstY, int width, int height)
{
}

static void
SWDoneComposite(PixmapPtr pDst)
{
}

/*** General */

static void
SWWaitMarker(ScreenPtr pScreen, int marker)
{
}

static Bool
SWPrepareAccess(PixmapPtr pPix, int index)
{
	return TRUE;
}

static void
SWFinishAccess(PixmapPtr pPix, int index)
{
}

/*** Setup */

Bool OMAPFBSetupExa(ScreenPtr pScreen, FBDevPtr ofb)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];

	ofb->exa->exa_major = 2;
	ofb->exa->exa_minor = 0;
	ofb->exa->flags = EXA_OFFSCREEN_PIXMAPS;
	
	ofb->exa->memoryBase = ofb->fbmem;
	ofb->exa->memorySize = ofb->lineLength * pScrn->virtualY;
	ofb->exa->offScreenBase = ofb->exa->memorySize;

	ofb->exa->pixmapOffsetAlign = 4;
	ofb->exa->pixmapPitchAlign = 4;

	ofb->exa->maxX = 8192;
	ofb->exa->maxY = 8192;

#define EXA_FUNC(s) ofb->exa->s = SW ## s
	
	EXA_FUNC(PrepareSolid);
	EXA_FUNC(Solid);
	EXA_FUNC(DoneSolid);

	EXA_FUNC(PrepareCopy);
	EXA_FUNC(Copy);
	EXA_FUNC(DoneCopy);

	EXA_FUNC(CheckComposite);
	EXA_FUNC(PrepareComposite);
	EXA_FUNC(Composite);
	EXA_FUNC(DoneComposite);

	EXA_FUNC(WaitMarker);
	EXA_FUNC(PrepareAccess);
	EXA_FUNC(FinishAccess);

	return TRUE;
}
