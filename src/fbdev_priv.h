/*
 * Copyright (C) 1994-2003 The XFree86 Project, Inc.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is fur-
 * nished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FIT-
 * NESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * XFREE86 PROJECT BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CON-
 * NECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the XFree86 Project shall not
 * be used in advertising or otherwise to promote the sale, use or other deal-
 * ings in this Software without prior written authorization from the XFree86
 * Project.
 */

/*
 * Authors:  Alan Hourihane, <alanh@fairlite.demon.co.uk>
 *	     Michel Dänzer, <michel@tungstengraphics.com>
 */

#include "compat-api.h"

#ifdef DEBUG
#define DebugMsg(...) ErrorF(__VA_ARGS__)
#else
#define DebugMsg(...)
#endif

typedef struct {
	unsigned char*			fbstart;
	unsigned char*			fbmem;
	int				fboff;
	int				lineLength;
	int				rotate;
	Bool				shadowFB;
	void				*shadow;
	CloseScreenProcPtr		CloseScreen;
	CreateScreenResourcesProcPtr	CreateScreenResources;
	void				(*PointerMoved)(SCRN_ARG_TYPE arg, int x, int y);
	EntityInfoPtr			pEnt;
	/* DGA info */
	DGAModePtr			pDGAMode;
	int				nDGAMode;
	OptionInfoPtr			Options;

	void				*cpu_backend_private;
	void				*backing_store_tuner_private;
	void				*sunxi_disp_private;
	void				*fb_copyarea_private;
	void				*SunxiDispHardwareCursor_private;
	void				*SunxiMaliDRI2_private;
	void				*SunxiG2D_private;
	void				*SunxiVideo_private;
} FBDevRec, *FBDevPtr;

#define FBDEVPTR(p) ((FBDevPtr)((p)->driverPrivate))

#define BACKING_STORE_TUNER(p) ((BackingStoreTuner *) \
                       (FBDEVPTR(p)->backing_store_tuner_private))

#define SUNXI_DISP(p) ((sunxi_disp_t *) \
                       (FBDEVPTR(p)->sunxi_disp_private))

#define SUNXI_G2D(p) ((SunxiG2D *) \
                       (FBDEVPTR(p)->SunxiG2D_private))

#define SUNXI_DISP_HWC(p) ((SunxiDispHardwareCursor *) \
                          (FBDEVPTR(p)->SunxiDispHardwareCursor_private))

#define SUNXI_MALI_UMP_DRI2(p) ((SunxiMaliDRI2 *) \
                                (FBDEVPTR(p)->SunxiMaliDRI2_private))

#define SUNXI_VIDEO(p) ((SunxiVideo *) \
                        (FBDEVPTR(p)->SunxiVideo_private))
