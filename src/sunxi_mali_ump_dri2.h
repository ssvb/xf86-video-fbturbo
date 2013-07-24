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

#ifndef SUNXI_MALI_UMP_DRI2_H
#define SUNXI_MALI_UMP_DRI2_H

#include <ump/ump.h>
#include <ump/ump_ref_drv.h>

#include "uthash.h"

/* Data structure with the information about an UMP buffer */
typedef struct
{
    /* The migrated pixmap (may be NULL if it is a window) */
    PixmapPtr               pPixmap;
    int                     BackupDevKind;
    void                   *BackupDevPrivatePtr;
    int                     refcount;
    UT_hash_handle          hh;

    ump_handle              handle;
    size_t                  size;
    uint8_t                *addr;
    int                     depth;
    size_t                  width;
    size_t                  height;
} UMPBufferInfoRec, *UMPBufferInfoPtr;

/*
 * DRI2 related bookkeeping for windows. Because Mali r3p0 blob has
 * quirks and needs workarounds, we can't fully rely on the Xorg DRI2
 * framework. But instead have to predict what is happening on the
 * client side based on the typical blob behavior.
 *
 * The blob is doing something like this:
 *  1. Requests BackLeft DRI2 buffer (buffer A) and renders to it
 *  2. Swaps buffers
 *  3. Requests BackLeft DRI2 buffer (buffer B)
 *  4. Checks window geometry, and if it has changed - go back to step 1.
 *  5. Renders to the current back buffer (either buffer A or B)
 *  6. Swaps buffers
 *  7. Go back to step 4
 *
 * The main problem is that The Mali blob ignores DRI2-InvalidateBuffers
 * events and just uses GetGeometry polling to check whether the window
 * size has changed. Unfortunately this is racy and we may end up with a
 * size mismatch between buffer A and buffer B. This is particularly easy
 * to trigger when the window size changes exactly between steps 1 and 3.
 * See test/gles-yellow-blue-flip.c program which demonstrates this.
 */
typedef struct
{
    UT_hash_handle          hh;
    DrawablePtr             pDraw;
    /* width and height must be the same for back and front buffers */
    int                     width, height;
    /* the number of back buffer requests */
    int                     buf_request_cnt;
} DRI2WindowStateRec, *DRI2WindowStatePtr;

typedef struct {
    int                     overlay_x;
    int                     overlay_y;

    WindowPtr               pOverlayWin;
    void                   *pOverlayDirtyDRI2Buf;
    Bool                    bOverlayWinEnabled;
    Bool                    bOverlayWinOverlapped;
    Bool                    bWalkingAboveOverlayWin;

    Bool                    bHardwareCursorIsInUse;
    EnableHWCursorProcPtr   EnableHWCursor;
    DisableHWCursorProcPtr  DisableHWCursor;

    DestroyWindowProcPtr    DestroyWindow;
    PostValidateTreeProcPtr PostValidateTree;
    GetImageProcPtr         GetImage;
    DestroyPixmapProcPtr    DestroyPixmap;

    /* the primary UMP secure id for accessing framebuffer */
    ump_secure_id           ump_fb_secure_id;
    /* the alternative UMP secure id used for the window resize workaround */
    ump_secure_id           ump_alternative_fb_secure_id;
    /* the UMP secure id for a dummy buffer */
    ump_secure_id           ump_null_secure_id;
    ump_handle              ump_null_handle1;
    ump_handle              ump_null_handle2;

    UMPBufferInfoPtr        HashPixmapToUMP;
    DRI2WindowStatePtr      HashWindowState;

    int                     drm_fd;
} SunxiMaliDRI2;

SunxiMaliDRI2 *SunxiMaliDRI2_Init(ScreenPtr pScreen, Bool bUseOverlay);
void SunxiMaliDRI2_Close(ScreenPtr pScreen);

#endif
