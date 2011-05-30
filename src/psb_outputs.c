/**************************************************************************
 *
 * Copyright (c) Intel Corp. 2007.
 * All Rights Reserved.
 *
 * Intel funded Tungsten Graphics (http://www.tungstengraphics.com) to
 * develop this driver.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xf86i2c.h>
#include <xf86str.h>
#include <xf86RandR12.h>
#include <xf86Crtc.h>
#include "psb_driver.h"

typedef struct _PsbPtrtItem
{
    MMListHead head;
    void *pointer;
} PsbPtrItem, *PsbPtrItemPtr;

void
psbOutputPrepare(xf86OutputPtr output)
{
    PSB_DEBUG(output->scrn->scrnIndex, 3,
	      "i830_psbOutputPrepare, output->dpms,off\n");

    output->funcs->dpms(output, DPMSModeOff);
}

void
psbOutputCommit(xf86OutputPtr output)
{
    PSB_DEBUG(output->scrn->scrnIndex, 3,
	      "i830_psbOutputCommi, output->dpms, ont\n");

    output->funcs->dpms(output, DPMSModeOn);
}

/*Add this function to get the EDID data from DDC bus, got the code from Xserver Jamesx*/
unsigned char *
psbDDCRead_DDC2(int scrnIndex, I2CBusPtr pBus, int start, int len)
{
    I2CDevPtr dev;
    unsigned char W_Buffer[2];
    int w_bytes;
    unsigned char *R_Buffer;
    int i;

    /*
     * Slow down the bus so that older monitors don't
     * miss things.
     */
    pBus->RiseFallTime = 20;

    if (!(dev = xf86I2CFindDev(pBus, 0x00A0))) {
	dev = xf86CreateI2CDevRec();
	dev->DevName = "ddc2";
	dev->SlaveAddr = 0xA0;
	dev->ByteTimeout = 2200;       /* VESA DDC spec 3 p. 43 (+10 %) */
	dev->StartTimeout = 550;
	dev->BitTimeout = 40;
	dev->AcknTimeout = 40;

	dev->pI2CBus = pBus;
	if (!xf86I2CDevInit(dev)) {
	    xf86DrvMsg(scrnIndex, X_PROBED, "No DDC2 device\n");
	    return NULL;
	}
    }
    if (start < 0x100) {
	w_bytes = 1;
	W_Buffer[0] = start;
    } else {
	w_bytes = 2;
	W_Buffer[0] = start & 0xFF;
	W_Buffer[1] = (start & 0xFF00) >> 8;
    }
    R_Buffer = xcalloc(1, sizeof(unsigned char)
		       * (len));
    for (i = 0; i < 4 /*RETRIES*/; i++) {
	if (xf86I2CWriteRead(dev, W_Buffer, w_bytes, R_Buffer, len)) {
	    if (!DDC_checksum(R_Buffer, len))
		return R_Buffer;
	}
    }

    xf86DestroyI2CDevRec(dev, TRUE);
    xfree(R_Buffer);
    return NULL;
}

DisplayModePtr
psbOutputDDCGetModes(xf86OutputPtr output)
{
    PsbOutputPrivatePtr intel_output = output->driver_private;
    xf86MonPtr edid_mon;
    DisplayModePtr modes;

    PSB_DEBUG(output->scrn->scrnIndex, 3, "i830_psbDDCGetModes\n");

    edid_mon = xf86OutputGetEDID(output, intel_output->pDDCBus);
    xf86OutputSetEDID(output, edid_mon);

    modes = xf86OutputGetEDIDModes(output);
    return modes;
}

Bool
psbPtrAddToList(MMListHead * head, void *ptr)
{
    PsbPtrItemPtr item = xcalloc(sizeof(*item), 1);

    PSB_DEBUG(-1, 3, "i830_psbPtrAddToList\n");

    if (!item)
	return FALSE;

    item->pointer = ptr;
    mmListAddTail(&item->head, head);
    return TRUE;
}

/*
 * Clone an output for the given screen.
 */

static xf86OutputPtr
psbOutputCloneSingle(ScrnInfoPtr pScrn, const xf86OutputPtr output)
{
    xf86OutputPtr nOutput;
    PsbOutputPrivatePtr psbOutput =
	(PsbOutputPrivatePtr) output->driver_private;
    PSB_DEBUG(pScrn->scrnIndex, 3, "i830_psbOutputCloneForScreen\n");

    nOutput = xf86OutputCreate(pScrn, output->funcs, output->name);
    if (!nOutput)
	return NULL;

    PSB_DEBUG(pScrn->scrnIndex, 3, "i3830_Cloning an output\n");

    psbOutput->refCount++;
    nOutput->driver_private = output->driver_private;
    nOutput->subpixel_order = output->subpixel_order;
    nOutput->interlaceAllowed = output->interlaceAllowed;
    nOutput->doubleScanAllowed = output->doubleScanAllowed;
    return nOutput;
}

xf86OutputPtr
psbOutputClone(ScrnInfoPtr pScrn, ScrnInfoPtr origScrn, const char *name)
{
    PsbPtr origPsb = psbPTR(origScrn);
    xf86OutputPtr xOutput;
    MMListHead *list;

    PSB_DEBUG(pScrn->scrnIndex, 3, "i3830_psbOutputClone\n");

    mmListForEach(list, &origPsb->outputs) {
	xOutput =
	    (xf86OutputPtr) mmListEntry(list, PsbPtrItem, head)->pointer;
	if (strcmp(name, xOutput->name) == 0)
	    return psbOutputCloneSingle(pScrn, xOutput);
    }

    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
	       "Could not find output %s to clone.\n", name);

    return NULL;
}

/*
 * Transform an output type mask to an output index mask.
 */

static unsigned
psbOutputTypesToIndex(ScrnInfoPtr pScrn, unsigned typeMask)
{
    PsbPtr pPsb = psbPTR(pScrn);
    MMListHead *list;
    xf86OutputPtr xOutput;
    PsbOutputPrivatePtr pOutput;
    int count = 0;
    unsigned indexMask = 0;

    PSB_DEBUG(pScrn->scrnIndex, 3, "i830_psbOutputTypesToIndex\n");

    mmListForEach(list, &pPsb->outputs) {
	xOutput =
	    (xf86OutputPtr) mmListEntry(list, PsbPtrItem, head)->pointer;
	pOutput = (PsbOutputPrivatePtr) xOutput->driver_private;

	if (typeMask & (1 << pOutput->type))
	    indexMask |= (1 << count);
	++count;
    }

    return indexMask;
}

Bool
psbOutputCompat(ScrnInfoPtr pScrn)
{
    PsbPtr pPsb = psbPTR(pScrn);
    PsbOutputPrivatePtr pOutput;
    MMListHead *list;
    unsigned crtcMask;
    unsigned cloneMask;
    xf86OutputPtr xOutput;

    PSB_DEBUG(-1, 3, "i830_psbOutputCompat\n");

    mmListForEach(list, &pPsb->outputs) {
	xOutput =
	    (xf86OutputPtr) mmListEntry(list, PsbPtrItem, head)->pointer;
	pOutput = (PsbOutputPrivatePtr) xOutput->driver_private;

	switch (pOutput->type) {
	case PSB_OUTPUT_SDVO:
	    crtcMask = PSB_CRTC0;
	    cloneMask = (1 << PSB_OUTPUT_SDVO);
	    break;
	case PSB_OUTPUT_LVDS:
	    crtcMask = PSB_CRTC1;
	    cloneMask = (1 << PSB_OUTPUT_LVDS);
	    break;
	default:
	    return FALSE;
	}

	xOutput->possible_crtcs = crtcMask;
	pOutput->crtcMask = crtcMask;
	xOutput->possible_clones = psbOutputTypesToIndex(pScrn, cloneMask);
	PSB_DEBUG(pScrn->scrnIndex, 3, "Output crtc mask is 0x%08x, "
		  "compat mask is 0x%08x\n",
		  (unsigned)xOutput->possible_crtcs,
		  (unsigned)xOutput->possible_clones);
    }
    return TRUE;
}

/*
 * Uninit the device-independent part of an output.
 */

void
psbOutputDestroy(PsbOutputPrivatePtr pOutput)
{
    PSB_DEBUG(-1, 3, "i830_psbOutputDestroy\n");

    if (pOutput->pDDCBus) {
	xf86DestroyI2CBusRec(pOutput->pDDCBus, TRUE, TRUE);
	pOutput->pDDCBus = NULL;
    }
}

/*
 * Initialize the device-independent part of an output.
 */

void
psbOutputInit(PsbDevicePtr pDevice, PsbOutputPrivatePtr pOutput)
{
    PSB_DEBUG(-1, 3, "i830_psbOutputInit\n");

    pOutput->refCount = 0;
    pOutput->pDDCBus = NULL;
    pOutput->pDevice = pDevice;
    pOutput->load_detect_temp = FALSE;
    pOutput->crtcMask = 1;
    pOutput->pScrn = NULL;
}

Bool
psbOutputCrtcValid(ScrnInfoPtr pScrn, int crtc)
{
    PsbPtr pPsb = psbPTR(pScrn);
    unsigned mask = (1 << crtc);
    MMListHead *list;
    xf86OutputPtr xOutput;

    mmListForEach(list, &pPsb->outputs) {
	xOutput =
	    (xf86OutputPtr) mmListEntry(list, PsbPtrItem, head)->pointer;

	if (xOutput->possible_crtcs & mask)
	    return TRUE;
    }

    return FALSE;
}

/*
 * Mark the outputs of all screens to use the crtc indicated by
 * crtc iff they support it.
 */

void
psbOutputEnableCrtcForAllScreens(PsbDevicePtr pDevice, int crtc)
{
    PsbPtr curPsb;
    ScrnInfoPtr curScrn;
    xf86OutputPtr xOutput;
    PsbOutputPrivatePtr pOutput;
    MMListHead *list;
    unsigned mask = (1 << crtc);
    int i;

    PSB_DEBUG(-1, 3, "i830_psbOutputEnableCrtcForAllScreens\n");
    PSB_DEBUG(-1, 3, "Marking crtc %d as available for all screens.\n", crtc);

    for (i = 0; i < pDevice->numScreens; ++i) {

	curScrn = pDevice->pScrns[i];
	if (!curScrn)
	    continue;

	curPsb = psbPTR(curScrn);
	if (!curPsb)
	    continue;

	mmListForEach(list, &curPsb->outputs) {
	    xOutput =
		(xf86OutputPtr) mmListEntry(list, PsbPtrItem, head)->pointer;
	    pOutput = (PsbOutputPrivatePtr) xOutput->driver_private;
	    xOutput->possible_crtcs |= (mask & pOutput->crtcMask);
	}
    }
}

/*
 * Mark the outputs of all screens other than pScrn NOT to use the
 * crtc indicated by crtc.
 */

void
psbOutputDisableCrtcForOtherScreens(ScrnInfoPtr pScrn, int crtc)
{
    PsbPtr curPsb;
    ScrnInfoPtr curScrn;
    PsbDevicePtr pDevice = psbDevicePTR(psbPTR(pScrn));
    xf86OutputPtr xOutput;
    MMListHead *list;
    unsigned mask = ~(1 << crtc);
    int i;

    PSB_DEBUG(-1, 3, "i830_psbOutputDisableCrtcForOtherScreens\n");
    PSB_DEBUG(-1, 3, "Grabbing crtc %d for screen %d\n", crtc,
	      pScrn->scrnIndex);

    for (i = 0; i < pDevice->numScreens; ++i) {

	curScrn = pDevice->pScrns[i];
	if (!curScrn)
	    continue;
	if (curScrn == pScrn)
	    continue;

	curPsb = psbPTR(curScrn);
	if (!curPsb)
	    continue;

	mmListForEach(list, &curPsb->outputs) {
	    xOutput =
		(xf86OutputPtr) mmListEntry(list, PsbPtrItem, head)->pointer;
	    if (xOutput->scrn != pScrn)
		xOutput->possible_crtcs &= mask;
	}
    }
}

/*
 * Destroy all outputs of a screen.
 */

void
psbOutputDestroyAll(ScrnInfoPtr pScrn)
{
    PsbPtr pPsb = psbPTR(pScrn);
    PsbPtrItemPtr pItem;
    xf86OutputPtr xOutput;
    MMListHead *list, *next;

    PSB_DEBUG(-1, 3, "i830_psbOutputDestroyAll\n");

    mmListForEachSafe(list, next, &pPsb->outputs) {
	pItem = mmListEntry(list, PsbPtrItem, head);
	xOutput = (xf86OutputPtr) pItem->pointer;
	mmListDel(list);
	xfree(pItem);
	xf86OutputDestroy(xOutput);
    }
}

void
psbOutputReleaseFromScreen(ScrnInfoPtr pScrn, const char *name)
{
    PsbPtr pPsb = psbPTR(pScrn);
    xf86OutputPtr xOutput;
    PsbOutputPrivatePtr pOutput;
    MMListHead *list;

    PSB_DEBUG(-1, 3, "i830_psbOutputReleaseFromScreen\n");

    mmListForEach(list, &pPsb->outputs) {
	xOutput =
	    (xf86OutputPtr) mmListEntry(list, PsbPtrItem, head)->pointer;
	pOutput = (PsbOutputPrivatePtr) xOutput->driver_private;
	if (strcmp(xOutput->name, name) == 0 && pOutput->pScrn == pScrn) {
	    pOutput->pScrn = NULL;
	    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		       "Output \"%s\" was released from this screen.\n",
		       name);
	}
    }
}

Bool
psbOutputAssignToScreen(ScrnInfoPtr pScrn, const char *name)
{
    PsbPtr pPsb = psbPTR(pScrn);
    PsbOutputPrivatePtr pOutput;
    xf86OutputPtr xOutput;
    MMListHead *list;

    PSB_DEBUG(-1, 3, "i830_psbOutputAssignToScreen\n");

    mmListForEach(list, &pPsb->outputs) {
	xOutput =
	    (xf86OutputPtr) mmListEntry(list, PsbPtrItem, head)->pointer;
	pOutput = (PsbOutputPrivatePtr) xOutput->driver_private;
	if (strcmp(xOutput->name, name) == 0) {
	    if (pOutput->pScrn == NULL) {
		pOutput->pScrn = pScrn;
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			   "Output \"%s\" is assigned to this screen.\n",
			   name);
		return TRUE;
	    } else {
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			   "Output \"%s\" is busy and cannot be "
			   "assigned to this screen.\n", name);
		return FALSE;
	    }
	}
    }
    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
	       "Output \"%s\" was not found and cannot be "
	       "assigned to this screen.\n", name);
    return FALSE;
}

void
psbOutputSave(ScrnInfoPtr pScrn)
{
    PsbPtr pPsb = psbPTR(pScrn);
    MMListHead *list;
    xf86OutputPtr xOutput;

    PSB_DEBUG(pScrn->scrnIndex, 3, "xxi830_psbOutputSave\n");

    mmListForEach(list, &pPsb->outputs) {
	xOutput =
	    (xf86OutputPtr) mmListEntry(list, PsbPtrItem, head)->pointer;
	xOutput->funcs->save(xOutput);
    }
}
void
psbOutputRestore(ScrnInfoPtr pScrn)
{
    PsbPtr pPsb = psbPTR(pScrn);
    MMListHead *list;
    xf86OutputPtr xOutput;

    PSB_DEBUG(pScrn->scrnIndex, 3, "i830_psbOutputRestore\n");

    mmListForEach(list, &pPsb->outputs) {
	xOutput =
	    (xf86OutputPtr) mmListEntry(list, PsbPtrItem, head)->pointer;
	xOutput->funcs->restore(xOutput);
    }
}

void
psbOutputDPMS(ScrnInfoPtr pScrn, int mode)
{
    PsbPtr pPsb = psbPTR(pScrn);
    MMListHead *list;
    xf86OutputPtr xOutput;

    PSB_DEBUG(pScrn->scrnIndex, 3, "i830_psbOutputDPMS\n");

    mmListForEach(list, &pPsb->outputs) {
	xOutput =
	    (xf86OutputPtr) mmListEntry(list, PsbPtrItem, head)->pointer;
	xOutput->funcs->dpms(xOutput, mode);
    }
}

Bool
psbOutputIsDisabled(ScrnInfoPtr pScrn, int pipe)
{
    Bool isdisabled = TRUE;
    PsbPtr pPsb = psbPTR(pScrn);
    MMListHead *list;
    xf86OutputPtr xOutput;
    PsbOutputPrivatePtr pOutput;

    mmListForEach(list, &pPsb->outputs) {
	xOutput =
	    (xf86OutputPtr) mmListEntry(list, PsbPtrItem, head)->pointer;
	pOutput = (PsbOutputPrivatePtr) xOutput->driver_private;

	switch (pOutput->type) {
	case PSB_OUTPUT_SDVO:
	    if (pipe == 0) {
		isdisabled = FALSE;
	    }
	    break;
	case PSB_OUTPUT_LVDS:
	    if (pipe == 1) {
		isdisabled = FALSE;
	    }
	    break;
	default:
	    return FALSE;
	}

    }
    return isdisabled;
}
