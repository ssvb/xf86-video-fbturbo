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

#include "xorgVersion.h"
#include "xf86.h"
#include "fb.h"
#include "inputstr.h"

#include "fbdev_priv.h"
#include "backing_store_tuner.h"

/*
 * This code implements a heuristics, which enables backing store for some
 * windows. When backing store is enabled for a window, the window gets a
 * backing pixmap (via automatic redirection provided by composite extension).
 * It acts a bit similar to ShadowFB, but for individual windows.
 *
 * The advantage of backing store is that we can avoid "expose event -> redraw"
 * animated trail in the exposed area when dragging another window on top of it.
 * Dragging windows becomes much smoother and faster.
 *
 * But the disadvantage of backing store is the same as for ShadowFB. That's a
 * loss of precious RAM, extra buffer copy when somebody tries to update window
 * content, potentially skip of some frames on fast animation (they just do
 * not reach screen). Also hardware accelerated scrolling does not currently
 * work for the windows with backing store enabled.
 *
 * We try to make the best use of backing store by enabling backing store for
 * all the windows that are direct children of root, except the one which has
 * keyboard focus (either directly or via one of its children). In practice this
 * heuristics seems to provide nearly perfect results:
 *  1) dragging windows is fast and smooth.
 *  2) the top level window with the keyboard focus (typically the application
 *     that a user is working with) is G2D accelerated and does not suffer from
 *     any intermediate buffer copy overhead.
 */

static void
xPostValidateTree(WindowPtr pWin, WindowPtr pLayerWin, VTKind kind)
{
    ScreenPtr pScreen = pWin ? pWin->drawable.pScreen :
                               pLayerWin->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    BackingStoreTuner *private = BACKING_STORE_TUNER(pScrn);
    WindowPtr curWin, focusWin = NULL;
    /*
     * Increment and backup the current counter. Because ChangeWindowAttributes
     * may trigger nested PostValidateTree calls, we want to detect this
     * situation and bail out (assuming that the nested PostValidateTree
     * call already did the job)
     */
    unsigned int CurrentCount = ++private->PostValidateTreeCount;

    /* Call the original PostValidateTree */
    if (private->PostValidateTree) {
        pScreen->PostValidateTree = private->PostValidateTree;
        (*pScreen->PostValidateTree) (pWin, pLayerWin, kind);
        private->PostValidateTree = pScreen->PostValidateTree;
        pScreen->PostValidateTree = xPostValidateTree;
    }

    /* Find the window with keyboard focus */
    if (inputInfo.keyboard && inputInfo.keyboard->focus)
        focusWin = inputInfo.keyboard->focus->win;

    if (!pWin || !focusWin || focusWin == NoneWin || focusWin == PointerRootWin)
        return;

    /* Descend down to the window, which has the root window as a parent */
    while (focusWin->parent && focusWin->parent != pScreen->root)
        focusWin = focusWin->parent;

    if (focusWin->parent != pScreen->root)
        return;

    /*
     * We are a bit paranoid here and want to eliminate any possibility
     * of infinite recursion
     */
    if (private->PostValidateTreeNestingLevel > 4) {
        DebugMsg("Oops, too much nesting for PostValidateTree, bailing out\n");
        return;
    }

    private->PostValidateTreeNestingLevel++;

    /* Disable backing store for the focus window */
    if (!private->ForceBackingStore && focusWin->backStorage) {
        DebugMsg("Disable backing store for the focus window 0x%x\n",
                 (unsigned int)focusWin->drawable.id);
        pScreen->backingStoreSupport = Always;
        focusWin->backingStore = NotUseful;
        (*pScreen->ChangeWindowAttributes) (focusWin, CWBackingStore);
        if (CurrentCount != private->PostValidateTreeCount) {
            DebugMsg("Nested PostValidateTree in ChangeWindowAttributes\n");
            private->PostValidateTreeNestingLevel--;
            return;
        }
    }

    /* And enable backing store for all the other children of root */
    curWin = pScreen->root->firstChild;
    while (curWin) {
        if (!curWin->backStorage && (private->ForceBackingStore ||
                                     curWin != focusWin)) {
            DebugMsg("Enable backing store for window 0x%x\n",
                     (unsigned int)curWin->drawable.id);
            pScreen->backingStoreSupport = Always;
            curWin->backingStore = WhenMapped;
            (*pScreen->ChangeWindowAttributes) (curWin, CWBackingStore);
            if (CurrentCount != private->PostValidateTreeCount) {
                DebugMsg("Nested PostValidateTree in ChangeWindowAttributes\n");
                private->PostValidateTreeNestingLevel--;
                return;
            }
        }
        curWin = curWin->nextSib;
    }
    private->PostValidateTreeNestingLevel--;
}

static void
xReparentWindow(WindowPtr pWin, WindowPtr pPriorParent)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    BackingStoreTuner *private = BACKING_STORE_TUNER(pScrn);

    if (private->ReparentWindow) {
        pScreen->ReparentWindow = private->ReparentWindow;
        (*pScreen->ReparentWindow) (pWin, pPriorParent);
        private->ReparentWindow = pScreen->ReparentWindow;
        pScreen->ReparentWindow = xReparentWindow;
    }

    /* We only want backing store set for direct children of root */
    if (pPriorParent == pScreen->root && pWin->backStorage) {
        DebugMsg("Reparent window 0x%x from root, disabling backing store\n",
                 (unsigned int)pWin->drawable.id);
        pScreen->backingStoreSupport = Always;
        pWin->backingStore = NotUseful;
        (*pScreen->ChangeWindowAttributes) (pWin, CWBackingStore);
    }
}

/*****************************************************************************/

BackingStoreTuner *BackingStoreTuner_Init(ScreenPtr pScreen, Bool force)
{
    BackingStoreTuner *private = calloc(1, sizeof(BackingStoreTuner));
    if (!private) {
        xf86DrvMsg(pScreen->myNum, X_INFO,
            "BackingStoreTuner_Init: calloc failed\n");
        return NULL;
    }

    private->ForceBackingStore = force;

    if (private->ForceBackingStore)
        xf86DrvMsg(pScreen->myNum, X_INFO,
                   "automatically forcing backing store for all windows\n");
    else
        xf86DrvMsg(pScreen->myNum, X_INFO,
                   "using backing store heuristics\n");

    /* Wrap the current PostValidateTree function */
    private->PostValidateTree = pScreen->PostValidateTree;
    pScreen->PostValidateTree = xPostValidateTree;

    /* Wrap the current ReparentWindow function */
    private->ReparentWindow = pScreen->ReparentWindow;
    pScreen->ReparentWindow = xReparentWindow;

    return private;
}

void BackingStoreTuner_Close(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    BackingStoreTuner *private = BACKING_STORE_TUNER(pScrn);

    pScreen->PostValidateTree = private->PostValidateTree;
    pScreen->ReparentWindow   = private->ReparentWindow;
}
