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

#include "xf86.h"
#include "xf86Cursor.h"

#include "sunxi_disp_hwcursor.h"
#include "sunxi_disp.h"
#include "fbdev_priv.h"

static void ShowCursor(ScrnInfoPtr pScrn)
{
    sunxi_disp_t *disp = SUNXI_DISP(pScrn);
    sunxi_hw_cursor_show(disp);
}

static void HideCursor(ScrnInfoPtr pScrn)
{
    sunxi_disp_t *disp = SUNXI_DISP(pScrn);
    sunxi_hw_cursor_hide(disp);
}

static void SetCursorPosition(ScrnInfoPtr pScrn, int x, int y)
{
    sunxi_disp_t *disp = SUNXI_DISP(pScrn);
    sunxi_hw_cursor_set_position(disp, x, y);
}

static void SetCursorColors(ScrnInfoPtr pScrn, int bg, int fg)
{
    sunxi_disp_t *disp = SUNXI_DISP(pScrn);
    uint32_t palette[4] = { 0, 0, bg | 0xFF000000, fg | 0xFF000000 };
    sunxi_hw_cursor_load_palette(disp, &palette[0]);
}

static void LoadCursorImage(ScrnInfoPtr pScrn, unsigned char *bits)
{
    sunxi_disp_t *disp = SUNXI_DISP(pScrn);
    sunxi_hw_cursor_load_pixeldata(disp, bits);
}

SunxiDispHardwareCursor *SunxiDispHardwareCursor_Init(ScreenPtr pScreen)
{
    xf86CursorInfoPtr InfoPtr;
    SunxiDispHardwareCursor *private;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    sunxi_disp_t *disp = SUNXI_DISP(pScrn);

    if (!disp)
        return NULL;

    if (!(InfoPtr = xf86CreateCursorInfoRec())) {
        ErrorF("SunxiDispHardwareCursor_Init: xf86CreateCursorInfoRec() failed\n");
        return NULL;
    }

    InfoPtr->ShowCursor = ShowCursor;
    InfoPtr->HideCursor = HideCursor;
    InfoPtr->SetCursorPosition = SetCursorPosition;
    InfoPtr->SetCursorColors = SetCursorColors;
    InfoPtr->LoadCursorImage = LoadCursorImage;
    InfoPtr->MaxWidth = InfoPtr->MaxHeight = 64;
    InfoPtr->Flags = HARDWARE_CURSOR_TRUECOLOR_AT_8BPP |
                     HARDWARE_CURSOR_SOURCE_MASK_INTERLEAVE_1 |
                     HARDWARE_CURSOR_ARGB;

    if (!xf86InitCursor(pScreen, InfoPtr)) {
        ErrorF("SunxiDispHardwareCursor_Init: xf86InitCursor(pScreen, InfoPtr) failed\n");
        xf86DestroyCursorInfoRec(InfoPtr);
        return NULL;
    }

    private = calloc(1, sizeof(SunxiDispHardwareCursor));
    if (!private) {
        ErrorF("SunxiDispHardwareCursor_Init: calloc failed\n");
        xf86DestroyCursorInfoRec(InfoPtr);
        return NULL;
    }

    private->hwcursor = InfoPtr;
    return private;
}

void SunxiDispHardwareCursor_Close(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SunxiDispHardwareCursor *private = SUNXI_DISP_HWC(pScrn);
    if (private) {
        xf86DestroyCursorInfoRec(private->hwcursor);
    }
}
