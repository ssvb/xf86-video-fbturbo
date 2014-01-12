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

static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
    0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
    0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
    0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
    0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
    0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
    0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
    0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
    0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
    0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
    0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
    0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
    0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
    0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
    0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
    0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
    0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
    0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
    0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
    0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
    0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
    0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

static inline uint32_t
crc32_byte(uint32_t crc32, uint8_t data)
{
    crc32 ^= 0xFFFFFFFF;
    crc32 = (crc32 >> 8) ^ crc32_table[(crc32 ^ data) & 0xFF];
    return crc32 ^ 0xFFFFFFFF;
}

static uint32_t calc_ump_checksum(UMPBufferInfoPtr umpbuf, uint32_t seed)
{
    int i;
    uint8_t *buf = umpbuf->addr + umpbuf->offs;
    uint32_t result = 0;
    uint32_t hi, lo;
    for (i = 0; i < RANDOM_SAMPLES_COUNT; i++) {
        /* LCG pseudorandom number generation */
        seed = seed * 1103515245 + 12345;
        hi = seed & 0xFFFF0000;
        seed = seed * 1103515245 + 12345;
        lo = seed >> 16;
        result = crc32_byte(result, buf[(hi | lo) % umpbuf->size]);
    }
    return result;
}

static void save_ump_checksum(UMPBufferInfoPtr umpbuf, uint32_t seed)
{
    umpbuf->has_checksum = TRUE;
    umpbuf->checksum_seed = seed;
    umpbuf->checksum = calc_ump_checksum(umpbuf, seed);
}

static Bool test_ump_checksum(UMPBufferInfoPtr umpbuf)
{
    return calc_ump_checksum(umpbuf, umpbuf->checksum_seed) == umpbuf->checksum;
}

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
    if (pDraw->bitsPerPixel != 32 && pDraw->bitsPerPixel != 16)
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
     * In the case if the queue of incoming buffers is already empty and
     * we are just swapping two allocated DRI2 buffers, we can do an extra
     * sanity check. The normal behaviour is that the back buffer may change,
     * and the front buffer may not. But if this is not the case, we need to
     * take some corrective actions here.
     */
    if (!umpbuf && window_state->ump_front_buffer_ptr &&
                   window_state->ump_front_buffer_ptr->addr &&
                   window_state->ump_back_buffer_ptr &&
                   window_state->ump_back_buffer_ptr->addr &&
                   window_state->ump_front_buffer_ptr != window_state->ump_back_buffer_ptr) {

        UMPBufferInfoPtr ump_front = window_state->ump_front_buffer_ptr;
        UMPBufferInfoPtr ump_back = window_state->ump_back_buffer_ptr;

        Bool front_modified = ump_front->has_checksum && !test_ump_checksum(ump_front);
        Bool back_modified = ump_back->has_checksum && !test_ump_checksum(ump_back);

        ump_front->has_checksum = FALSE;
        ump_back->has_checksum = FALSE;

        if (back_modified && !front_modified) {
            /* That's normal, we have successfully passed this check */
            ump_back->extra_flags |= UMPBUF_PASSED_ORDER_CHECK;
            ump_front->extra_flags |= UMPBUF_PASSED_ORDER_CHECK;
        }
        else if (front_modified) {
            /* That's bad, the order of buffers is messed up, but we can exchange them */
            UMPBufferInfoPtr tmp               = window_state->ump_back_buffer_ptr;
            window_state->ump_back_buffer_ptr  = window_state->ump_front_buffer_ptr;
            window_state->ump_front_buffer_ptr = tmp;
            DebugMsg("Unexpected modification of the front buffer detected.\n");
        }
        else {
            /* Not enough information to make a decision yet */
        }
    }

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

    /*
     * Here we can calculate checksums over randomly sampled bytes from UMP
     * buffers in order to check later whether they had been modified. This
     * is skipped if the buffers have UMPBUF_PASSED_ORDER_CHECK flag set.
     */
    if (window_state->ump_front_buffer_ptr && window_state->ump_front_buffer_ptr->addr &&
        window_state->ump_back_buffer_ptr && window_state->ump_back_buffer_ptr->addr &&
        window_state->ump_front_buffer_ptr != window_state->ump_back_buffer_ptr) {

        UMPBufferInfoPtr ump_front = window_state->ump_front_buffer_ptr;
        UMPBufferInfoPtr ump_back = window_state->ump_back_buffer_ptr;

        if (!(ump_front->extra_flags & UMPBUF_PASSED_ORDER_CHECK))
            save_ump_checksum(ump_front, window_state->buf_swap_cnt);
        if (!(ump_back->extra_flags & UMPBUF_PASSED_ORDER_CHECK))
            save_ump_checksum(ump_back, window_state->buf_swap_cnt);
    }

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
    sunxi_layer_set_rgb_input_buffer(disp, umpbuf->cpp * 8, umpbuf->offs,
                                     umpbuf->width, umpbuf->height, umpbuf->pitch / 4);
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

static unsigned long ump_get_size_from_secure_id(ump_secure_id secure_id)
{
    unsigned long size;
    ump_handle handle;
    if (secure_id == UMP_INVALID_SECURE_ID)
        return 0;
    handle = ump_handle_create_from_secure_id(secure_id);
    if (handle == UMP_INVALID_MEMORY_HANDLE)
        return 0;
    size = ump_size_get(handle);
    ump_reference_release(handle);
    return size;
}

static const char *driverNames[1] = {
    "lima" /* DRI2DriverDRI */
};

static const char *driverNamesWithVDPAU[2] = {
    "lima", /* DRI2DriverDRI */
    "sunxi" /* DRI2DriverVDPAU */
};

SunxiMaliDRI2 *SunxiMaliDRI2_Init(ScreenPtr pScreen,
                                  Bool      bUseOverlay,
                                  Bool      bSwapbuffersWait)
{
    int drm_fd;
    DRI2InfoRec info = { 0 };
    SunxiMaliDRI2 *mali;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    sunxi_disp_t *disp = SUNXI_DISP(pScrn);
    Bool have_sunxi_cedar = TRUE;

    if (!xf86LoadKernelModule("mali"))
        xf86DrvMsg(pScreen->myNum, X_INFO, "can't load 'mali' kernel module\n");
    if (!xf86LoadKernelModule("mali_drm"))
        xf86DrvMsg(pScreen->myNum, X_INFO, "can't load 'mali_drm' kernel module\n");

    if (!xf86LoadKernelModule("sunxi_cedar_mod")) {
        xf86DrvMsg(pScreen->myNum, X_INFO, "can't load 'sunxi_cedar_mod' kernel module\n");
        have_sunxi_cedar = FALSE;
    }

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

    mali->ump_alternative_fb_secure_id = UMP_INVALID_SECURE_ID;

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
        if (mali->ump_alternative_fb_secure_id == UMP_INVALID_SECURE_ID ||
            ump_get_size_from_secure_id(mali->ump_alternative_fb_secure_id) !=
                                          disp->framebuffer_size) {
            xf86DrvMsg(pScreen->myNum, X_INFO,
                  "UMP does not wrap the whole framebuffer, overlays can't be used\n");
            mali->ump_fb_secure_id = UMP_INVALID_SECURE_ID;
            mali->ump_alternative_fb_secure_id = UMP_INVALID_SECURE_ID;
        }
        if (disp->framebuffer_size - disp->gfx_layer_size <
                                                 disp->xres * disp->yres * 4 * 2) {
            int needed_fb_num = (disp->xres * disp->yres * 4 * 2 +
                                 disp->gfx_layer_size - 1) / disp->gfx_layer_size + 1;
            xf86DrvMsg(pScreen->myNum, X_INFO,
                "tear-free zero-copy double buffering needs more video memory\n");
            xf86DrvMsg(pScreen->myNum, X_INFO,
                "please set fb0_framebuffer_num >= %d in the fex file\n", needed_fb_num);
            xf86DrvMsg(pScreen->myNum, X_INFO,
                "and sunxi_fb_mem_reserve >= %d in the kernel cmdline\n",
                (needed_fb_num * disp->gfx_layer_size + 1024 * 1024 - 1) / (1024 * 1024));
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

    info.version = 4;

    if (have_sunxi_cedar) {
        info.numDrivers = ARRAY_SIZE(driverNamesWithVDPAU);
        info.driverName = driverNamesWithVDPAU[0];
        info.driverNames = driverNamesWithVDPAU;
    }
    else {
        info.numDrivers = ARRAY_SIZE(driverNames);
        info.driverName = driverNames[0];
        info.driverNames = driverNames;
    }
    info.deviceName = drmGetDeviceNameFromFd(drm_fd);
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
