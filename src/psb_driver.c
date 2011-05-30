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

#include <string.h>
#include <unistd.h>
#include <psb_reg.h>
#include "psb_driver.h"
#include "i810_reg.h"
#include "libmm/mm_defines.h"
#include "libmm/mm_interface.h"
#include "mipointer.h"
#include "mibstore.h"
#include "micmap.h"
#include "xf86cmap.h"
#include "vgaHW.h"
#define DPMS_SERVER
#include "xf86Priv.h"

#ifndef XFree86LOADER
#include <stdio.h>
#include <sys/mman.h>
#endif

/* Mandatory functions */
static const OptionInfoRec *psbAvailableOptions(int chipid, int busid);
static void psbIdentify(int flags);
static Bool psbProbe(DriverPtr drv, int flags);
static Bool psbPreInit(ScrnInfoPtr pScrn, int flags);
static Bool psbScreenInit(int Index, ScreenPtr pScreen, int argc,
			  char **argv);
static Bool psbEnterVT(int scrnIndex, int flags);
static void psbLeaveVT(int scrnIndex, int flags);
static Bool psbCloseScreen(int scrnIndex, ScreenPtr pScreen);
static Bool psbSaveScreen(ScreenPtr pScreen, int mode);

static Bool psbSwitchMode(int scrnIndex, DisplayModePtr pMode, int flags);
static void psbAdjustFrame(int scrnIndex, int x, int y, int flags);
static void psbFreeScreen(int scrnIndex, int flags);
static void psbFreeRec(ScrnInfoPtr pScrn);
static Bool psbDeviceInit(PsbDevicePtr pDevice, int scrnIndex);
static Bool psbDeviceFinishInit(PsbDevicePtr pDevice, int scrnIndex);
static Bool psbRestoreHWState(PsbDevicePtr pDevice);
static Bool psbSaveHWState(PsbDevicePtr pDevice);
static Bool psbCreateScreenResources(ScreenPtr pScreen);
static void psbSetVGAOff(ScrnInfoPtr pScrn);
static Bool psbUnLockMM(PsbDevicePtr pDevice, Bool scanoutOnly);
static Bool psbLockMM(PsbDevicePtr pDevice, Bool scanoutOnly);
static void
psbDisplayPowerManagementSet(ScrnInfoPtr pScrn, int mode, int flags);

/* locally used functions */
static void psbLoadPalette(ScrnInfoPtr pScrn, int numColors,
			   int *indices, LOCO * colors, VisualPtr pVisual);

static int psbEntityIndex = -1;

#ifndef makedev
#define makedev(x,y)    ((dev_t)(((x) << 8) | (y)))
#endif

/*
 * This contains the functions needed by the server after loading the
 * driver module.  It must be supplied, and gets added the driver list by
 * the Module Setup funtion in the dynamic case.  In the static case a
 * reference to this is compiled in, and this requires that the name of
 * this DriverRec be an upper-case version of the driver name.
 */

DriverRec psb = {
    PSB_VERSION,
    PSB_DRIVER_NAME,
    psbIdentify,
    psbProbe,
    psbAvailableOptions,
    NULL,
    0
};

enum GenericTypes
{
    CHIP_PSB
};

/* Supported chipsets */
static SymTabRec psbChipsets[] = {
    {CHIP_PSB, "Intel GMA500"},
    {-1, NULL}
};

static PciChipsets psbPCIchipsets[] = {
    {CHIP_PSB, PCI_CHIP_PSB1, RES_SHARED_VGA},
    {CHIP_PSB, PCI_CHIP_PSB2, RES_SHARED_VGA},
    {-1, -1, RES_UNDEFINED},
};

typedef enum
{
    OPTION_SHADOWFB,
    OPTION_NOACCEL,
    OPTION_SWCURSOR,
    OPTION_EXAMEM,
    OPTION_EXASCRATCH,
    OPTION_IGNORE_ACPI,
    OPTION_NOPANEL,
    OPTION_LIDTIMER,
    OPTION_NOFITTING,
    OPTION_DOWNSCALE,
    OPTION_VSYNC
} psbOpts;

static const OptionInfoRec psbOptions[] = {
    {OPTION_SHADOWFB, "ShadowFB", OPTV_BOOLEAN, {0}, FALSE},
    {OPTION_NOACCEL, "NoAccel", OPTV_BOOLEAN, {0}, FALSE},
    {OPTION_SWCURSOR, "SWcursor", OPTV_BOOLEAN, {0}, FALSE},
    {OPTION_EXAMEM, "ExaMem", OPTV_INTEGER, {0}, FALSE},
    {OPTION_EXASCRATCH, "ExaScratch", OPTV_INTEGER, {0}, FALSE},
    {OPTION_IGNORE_ACPI, "IgnoreACPI", OPTV_BOOLEAN, {0}, FALSE},
    {OPTION_NOPANEL, "NoPanel", OPTV_BOOLEAN, {0}, FALSE},
    {OPTION_LIDTIMER, "LidTimer", OPTV_BOOLEAN, {0}, FALSE},
    {OPTION_NOFITTING, "NoFitting", OPTV_BOOLEAN, {0}, FALSE},
    {OPTION_DOWNSCALE, "DownScale", OPTV_BOOLEAN, {0}, FALSE},
    {OPTION_VSYNC, "Vsync", OPTV_BOOLEAN, {0}, FALSE},
    {-1, NULL, OPTV_NONE, {0}, FALSE},
};

/*
 * List of symbols from other modules that this module references.  This
 * list is used to tell the loader that it is OK for symbols here to be
 * unresolved providing that it hasn't been told that they haven't been
 * told that they are essential via a call to xf86LoaderReqSymbols() or
 * xf86LoaderReqSymLists().  The purpose is this is to avoid warnings about
 * unresolved symbols that are not required.
 */

static const char *vbeSymbols[] = {
    "VBEInit",
    "vbeFree",
    NULL
};

static const char *fbSymbols[] = {
    "fbPictureInit",
    "fbScreenInit",
    NULL
};

static const char *shadowSymbols[] = {
    "shadowInit",
    "shadowUpdatePackedWeak",
    NULL
};

static const char *exaSymbols[] = {
    "exaDriverAlloc",
    "exaDriverInit",
    "exaDriverFini",
    "exaOffscreenAlloc",
    "exaOffscreenFree",
    "exaGetPixmapPitch",
    "exaGetPixmapOffset",
    "exaWaitSync",
    NULL
};

static const char *psbvgahwSymbols[] = {
    "vgaHWFreeHWRec",
    "vgaHWGetHWRec",
    "vgaHWGetIOBase",
    "vgaHWGetIndex",
    "vgaHWInit",
    "vgaHWLock",
    "vgaHWMapMem",
    "vgaHWProtect",
    "vgaHWRestore",
    "vgaHWSave",
    "vgaHWSaveScreen",
    "vgaHWSetMmioFuncs",
    "vgaHWUnlock",
    "vgaHWUnmapMem",
    NULL
};

#ifdef XF86DRI
static const char *psbDRISymbols[] = {
    "DRICloseScreen",
    "DRICreateInfoRec",
    "DRIDestroyInfoRec",
    "DRIFinishScreenInit",
    "DRIGetSAREAPrivate",
    "DRILock",
    "DRIQueryVersion",
    "DRIScreenInit",
    "DRIUnlock",
    "DRIOpenConnection",
    "DRICloseConnection",
    "GlxSetVisualConfigs",
    NULL
};

static const char *psbDRMSymbols[] = {
    "drmAddBufs",
    "drmAddMap",
    "drmAgpAcquire",
    "drmAgpAlloc",
    "drmAgpBase",
    "drmAgpBind",
    "drmAgpDeviceId",
    "drmAgpEnable",
    "drmAgpFree",
    "drmAgpGetMode",
    "drmAgpRelease",
    "drmAgpVendorId",
    "drmCtlInstHandler",
    "drmCtlUninstHandler",
    "drmCommandNone",
    "drmCommandWrite",
    "drmCommandWriteRead",
    "drmFreeVersion",
    "drmGetInterruptFromBusID",
    "drmGetLibVersion",
    "drmGetVersion",
    "drmMap",
    "drmMapBufs",
    "drmUnmap",
    "drmUnmapBufs",
    "drmAgpUnbind",
    "drmRmMap",
    "drmCreateContext",
    "drmAuthMagic",
    "drmDestroyContext",
    "drmSetContextFlags",
    NULL
};

static const char *psbXpsbSymbols[] = {
    "XpsbInit",
    "XpsbTakedown",
    "psbBlitYUV",
    "psb3DPrepareComposite",
    "psb3DCompositeQuad",
    "psb3DCompositeFinish",
    NULL
};

#endif

#ifdef XFree86LOADER

static const char *ddcSymbols[] = {
    "xf86PrintEDID",
    "xf86SetDDCproperties",
    NULL
};

/* Module loader interface */
static MODULESETUPPROTO(psbSetup);

static XF86ModuleVersionInfo psbVersionRec = {
    PSB_DRIVER_NAME,
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    PSB_MAJOR_VERSION, PSB_MINOR_VERSION, PSB_PATCHLEVEL,
    ABI_CLASS_VIDEODRV,
    ABI_VIDEODRV_VERSION,
    MOD_CLASS_VIDEODRV,
    {0, 0, 0, 0}
};

/* compat cruft */
_X_EXPORT BoxRec miEmptyBox;
_X_EXPORT RegDataRec miEmptyData, miBrokenData;

/*
 * This data is accessed by the loader.  The name must be the module name
 * followed by "ModuleData".
 */
_X_EXPORT XF86ModuleData psbModuleData = { &psbVersionRec, psbSetup, NULL };

static pointer
psbSetup(pointer Module, pointer Options, int *ErrorMajor, int *ErrorMinor)
{
    static Bool Initialised = FALSE;

    miEmptyBox = RegionEmptyBox;
    miEmptyData = RegionEmptyData;
    miBrokenData = RegionBrokenData;

    PSB_DEBUG(-1, 3, "psbSetup\n");

    if (!Initialised) {
	Initialised = TRUE;
	xf86AddDriver(&psb, Module, 0);
	return (pointer) TRUE;
    }

    if (ErrorMajor)
	*ErrorMajor = LDR_ONCEONLY;
    return (NULL);
}

#endif

static const OptionInfoRec *
psbAvailableOptions(int chipid, int busid)
{
    return (psbOptions);
}

static void
psbIdentify(int flags)
{
    xf86PrintChipsets(PSB_NAME, "driver for Intel GMA500 chipsets",
		      psbChipsets);
}

/*
 * This function is called once, at the start of the first server generation to
 * do a minimal probe for supported hardware.
 */

static Bool
psbProbe(DriverPtr drv, int flags)
{
    Bool foundScreen = FALSE;
    int numDevSections, numUsed;
    GDevPtr *devSections = NULL;
    int *usedChips = NULL;
    int i;
    EntityInfoPtr pEnt;
    PsbDevicePtr pPsbDev;
    DevUnion *pPriv;

    PSB_DEBUG(-1, 3, "psbProbe\n");
    numUsed = 0;

    /*
     * Find the config file Device sections that match this
     * driver, and return if there are none.
     */

    if ((numDevSections = xf86MatchDevice(PSB_NAME, &devSections)) <= 0)
	return (FALSE);

#if (!(XORG_VERSION_MINOR >= 5 || (XORG_VERSION_MINOR == 4 && XORG_VERSION_PATCH == 99)))
    if (xf86GetPciVideoInfo()) {
#endif

	/*
	 * This function allocates screens to devices according to
	 * bus ids in the config file. Multiple device sections may point
	 * to the same PCI device.
	 */

	numUsed = xf86MatchPciInstances(PSB_NAME, PCI_VENDOR_INTEL,
					psbChipsets, psbPCIchipsets,
					devSections, numDevSections, drv,
					&usedChips);

#if (!(XORG_VERSION_MINOR >= 5 || (XORG_VERSION_MINOR == 4 && XORG_VERSION_PATCH == 99)))
    }
#endif

    if (numUsed <= 0)
	goto out;

    if (flags & PROBE_DETECT) {
	foundScreen = TRUE;
	goto out;
    }

    if (psbEntityIndex == -1)
	psbEntityIndex = xf86AllocateEntityPrivateIndex();

    for (i = 0; i < numUsed; i++) {
	ScrnInfoPtr pScrn = NULL;

	/* Allocate a ScrnInfoRec  */
	if ((pScrn = xf86ConfigPciEntity(pScrn, 0, usedChips[i],
					 psbPCIchipsets, NULL, NULL, NULL,
					 NULL, NULL))) {
	    pScrn->driverVersion = PSB_VERSION;
	    pScrn->driverName = PSB_DRIVER_NAME;
	    pScrn->name = PSB_NAME;
	    pScrn->Probe = psbProbe;
	    pScrn->PreInit = psbPreInit;
	    pScrn->ScreenInit = psbScreenInit;
	    pScrn->SwitchMode = psbSwitchMode;
	    pScrn->AdjustFrame = psbAdjustFrame;
	    pScrn->EnterVT = psbEnterVT;
	    pScrn->LeaveVT = psbLeaveVT;
	    pScrn->FreeScreen = psbFreeScreen;
	    pScrn->ValidMode = NULL;
	    foundScreen = TRUE;
	}

	/*
	 * We support dual head, And need a per-device structure.
	 */

	pPsbDev = NULL;
	pEnt = xf86GetEntityInfo(usedChips[i]);
	xf86SetEntitySharable(usedChips[i]);

	pPriv = xf86GetEntityPrivate(pScrn->entityList[0], psbEntityIndex);

	if (!pPriv->ptr) {
	    PSB_DEBUG(pScrn->scrnIndex, 3, "Allocating new device\n");

	    pPriv->ptr = xnfcalloc(sizeof(PsbDevice), 1);
	    pPsbDev = pPriv->ptr;
	    pPsbDev->lastInstance = -1;
	    pPsbDev->pEnt = pEnt;
	    pPsbDev->pciInfo = xf86GetPciInfoForEntity(pEnt->index);
           
            #if (XORG_VERSION_MINOR >= 5 || (XORG_VERSION_MINOR == 4 && XORG_VERSION_PATCH == 99))
	    pPsbDev->pciTag = pciTag(pPsbDev->pciInfo->bus,
				     pPsbDev->pciInfo->dev,
				     pPsbDev->pciInfo->func);
            #else
	    pPsbDev->pciTag = pciTag(pPsbDev->pciInfo->bus,
                                     pPsbDev->pciInfo->device,
                                     pPsbDev->pciInfo->func);
            #endif
	    pPsbDev->refCount = 0;
	    pPsbDev->pScrns[0] = pScrn;
	} else {
	    pPsbDev = pPriv->ptr;
	    PSB_DEBUG(pScrn->scrnIndex, 3, "Secondary screen %d\n",
		      pPsbDev->lastInstance + 1);
	    pPsbDev->pScrns[1] = pScrn;
	}

	pPsbDev->lastInstance++;
	pPsbDev->numScreens = pPsbDev->lastInstance + 1;

	xf86SetEntityInstanceForScreen(pScrn, pScrn->entityList[0],
				       pPsbDev->lastInstance);
	pPsbDev->device = xf86GetDevFromEntity(pScrn->entityList[0],
					       pScrn->entityInstanceList[0]);
    }

  out:
    if (usedChips != NULL)
	xfree(usedChips);
    if (devSections != NULL)
	xfree(devSections);

    return foundScreen;
}

static PsbPtr
psbGetRec(ScrnInfoPtr pScrn)
{
    if (!pScrn->driverPrivate)
	pScrn->driverPrivate = xcalloc(sizeof(PsbRec), 1);

    return ((PsbPtr) pScrn->driverPrivate);
}

static void
psbFreeRec(ScrnInfoPtr pScrn)
{

    xfree(pScrn->driverPrivate);
    pScrn->driverPrivate = NULL;
}

static void
psbInitI830(PsbPtr pPsb)
{
    PsbDevicePtr pDevice = pPsb->pDevice;
    I830Ptr pI830 = &pPsb->i830Ptr;

    pI830->pEnt = pDevice->pEnt;
    pI830->PciTag = pDevice->pciTag;
    pI830->PciInfo = pDevice->pciInfo;
    pI830->pDevice = pDevice;
}

static Bool
psbSetFront(ScrnInfoPtr pScrn)
{
    PsbPtr pPsb = psbPTR(pScrn);
    ScreenPtr pScreen = pScrn->pScreen;
    PixmapPtr rootPixmap = pScreen->GetScreenPixmap(pScreen);
    Bool fbAccessDisabled;
    CARD8 *fbMap;

    pPsb->front = psbScanoutCreate(pScrn, pScrn->bitsPerPixel >> 3,
				   pScrn->depth, pScrn->virtualX,
				   pScrn->virtualY, 0, TRUE, 0);

    if (!pPsb->front)
	return FALSE;

    pPsb->fbMap = psbScanoutVirtual(pPsb->front);
    pPsb->cpp = psbScanoutCpp(pPsb->front);
    pPsb->stride = psbScanoutStride(pPsb->front);
    pScrn->displayWidth = pPsb->stride / pPsb->cpp;
    fbMap = pPsb->fbMap;

    if (pPsb->shadowFB) {
	if (pPsb->shadowMem)
	    xfree(pPsb->shadowMem);
	pPsb->shadowMem = shadowAlloc(pScrn->displayWidth, pScrn->virtualY,
				      pScrn->bitsPerPixel);
	if (!pPsb->shadowMem) {
	    psbScanoutDestroy(pPsb->front);
	    return FALSE;
	}
	fbMap = pPsb->shadowMem;
    }

    /*
     * If we are in a fb disabled state, the virtual address of the root
     * pixmap should always be NULL, and it will be overwritten later
     * if we try to set it to something.
     *
     * Therefore, set it to NULL, and modify the backup copy instead.
     */

    fbAccessDisabled = (rootPixmap->devPrivate.ptr == NULL);

    pScreen->ModifyPixmapHeader(pScreen->GetScreenPixmap(pScreen),
				pScrn->virtualX, pScrn->virtualY,
				pScrn->depth, pScrn->bitsPerPixel,
				psbScanoutStride(pPsb->front), fbMap);

    if (fbAccessDisabled) {
	rootPixmap->devPrivate.ptr = NULL;
    }

    return TRUE;

}

static Bool
psbXf86CrtcResize(ScrnInfoPtr pScrn, int width, int height)
{
    PsbPtr pPsb = psbPTR(pScrn);
    int ret = TRUE;

    PSB_DEBUG(pScrn->scrnIndex, 3, "psbXf86CrtcResize %d %d\n",
	      width, height);

    if (width == pScrn->virtualX && height == pScrn->virtualY)
	return TRUE;

    psbDRILock(pScrn, 0);

    /* Move out cursor buffers, but only ours. */
    if (!pPsb->sWCursor)
	psbCrtcSaveCursors(pScrn, FALSE);

    psbScanoutDestroy(pPsb->front);
    pPsb->front = NULL;

    pScrn->virtualX = width;
    pScrn->virtualY = height;
    ret = psbSetFront(pScrn);
    memset(pPsb->fbMap, 0, psbScanoutStride(pPsb->front) * pScrn->virtualY);
    if (!pPsb->sWCursor) {
	if (!psbCrtcSetupCursors(pScrn)) {
	    /*
	     * FIXME: is there a way to revert to software cursors at this point?
	     */

	    FatalError("Could not set up hardware cursors.\n");
	}
    }

    PSBDGAReInit(pScrn->pScreen);
    pScrn->frameX0 = 0;
    pScrn->frameY0 = 0;
    psbAdjustFrame(pScrn->scrnIndex, pScrn->frameX0, pScrn->frameY0, 0);
    psbDRIUnlock(pScrn);
    return ret;
}

static const xf86CrtcConfigFuncsRec psbXf86crtcConfigFuncs = {
    psbXf86CrtcResize
};

#ifdef XF86DRI
static Bool
psbPreInitDRI(ScrnInfoPtr pScrn)
{
    PsbPtr pPsb = psbPTR(pScrn);

    mmInitListHead(&pPsb->sAreaList);
    pPsb->dri = TRUE;

    if (!xf86LoadSubModule(pScrn, "dri"))
	return FALSE;

    return TRUE;
}

/* removed in Xserver 1.7, add it again so that proprietary Xpsb can be loaded */
void
xf86AddModuleInfo(pointer info, pointer module)
{
}

/* removed in mesa, add it again so that proprietary Xpsb can be loaded */
typedef void (*_glapi_warning_func)(void *ctx, const char *str, ...);
void
_glapi_set_warning_func( _glapi_warning_func func )
{
}

static Bool
psbPreInitXpsb(ScrnInfoPtr pScrn)
{
    PsbPtr pPsb = psbPTR(pScrn);

    PSB_DEBUG(pScrn->scrnIndex, 3, "psbPreinitXpsb\n");
    pPsb->xpsb = FALSE;

    if (!xf86LoadSubModule(pScrn, "Xpsb")) {
	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "Poulsbo Xpsb driver not available. "
		   "XVideo and 3D acceleration will not work.\n");
	return FALSE;
    }

    pPsb->xpsb = TRUE;

    return TRUE;
}
#endif

static Bool
psbPreInitAccel(ScrnInfoPtr pScrn)
{
    PsbPtr pPsb = psbPTR(pScrn);
    MessageType from;
    int tmp;

    pPsb->noAccel = FALSE;
    from = xf86GetOptValBool(pPsb->options, OPTION_NOACCEL, &pPsb->noAccel)
	? X_CONFIG : X_DEFAULT;

    if (!pPsb->noAccel) {
	if (!xf86LoadSubModule(pScrn, "exa"))
	    return FALSE;

    }

    xf86DrvMsg(pScrn->scrnIndex, from, "Acceleration %sabled\n",
	       pPsb->noAccel ? "dis" : "en");

    tmp = 32 * 1024;
    from = xf86GetOptValInteger(pPsb->options, OPTION_EXAMEM, &tmp)
	? X_CONFIG : X_DEFAULT;

    if (!pPsb->noAccel)
	xf86DrvMsg(pScrn->scrnIndex, from,
		   "[EXA] Allocate %d kiB for EXA pixmap cache.\n", tmp);
    pPsb->exaSize = tmp * 1024;

    tmp = 4;
    from = xf86GetOptValInteger(pPsb->options, OPTION_EXASCRATCH, &tmp)
	? X_CONFIG : X_DEFAULT;

    if (!pPsb->noAccel)
	xf86DrvMsg(pScrn->scrnIndex, from,
		   "[EXA] Allocate %d kiB for scratch memory.\n", tmp);
    pPsb->exaScratchSize = tmp * 1024;

    return TRUE;
}

static Bool
psbPreInitShadowFB(ScrnInfoPtr pScrn)
{
    PsbPtr pPsb = psbPTR(pScrn);
    MessageType from;

    from = xf86GetOptValBool(pPsb->options, OPTION_SHADOWFB, &pPsb->shadowFB)
	? X_CONFIG : X_DEFAULT;

    if (pPsb->shadowFB) {
	if (!xf86LoadSubModule(pScrn, "shadow"))
	    return FALSE;

    }

    xf86DrvMsg(pScrn->scrnIndex, from, "Shadow framebuffer %sabled\n",
	       pPsb->shadowFB ? "en" : "dis");

    return TRUE;
}

static Bool
psbAddToOutputList(PsbPtr pPsb, xf86OutputPtr xOutput)
{
    if (!xOutput)
	return FALSE;

    if (!psbPtrAddToList(&pPsb->outputs, xOutput)) {
	xf86OutputDestroy(xOutput);
	return FALSE;
    }

    return TRUE;
}

static void
psbInitOutputs(ScrnInfoPtr pScrn)
{
    PsbDevicePtr pDevice = psbDevicePTR(psbPTR(pScrn));
    PsbPtr pPsb = psbPTR(pScrn);

    PSB_DEBUG(pScrn->scrnIndex, 3, "psbInitOutputs\n");

    xf86GetOptValBool(pPsb->options, OPTION_NOPANEL, &pPsb->noPanel);

    pPsb->lidTimer = FALSE;
    xf86GetOptValBool(pPsb->options, OPTION_LIDTIMER, &pPsb->lidTimer);

    xf86GetOptValBool(pPsb->options, OPTION_NOFITTING, &pPsb->noFitting);

    xf86GetOptValBool(pPsb->options, OPTION_DOWNSCALE, &pPsb->downScale);

    xf86GetOptValBool(pPsb->options, OPTION_VSYNC, &pPsb->vsync);

    xf86CrtcConfigInit(pScrn, &psbXf86crtcConfigFuncs);
    if (pScrn == pDevice->pScrns[0]) {
	(void)psbAddToOutputList(pPsb, psbLVDSInit(pScrn, "LVDS0"));
	strcpy(pDevice->sdvoBName, "SDVOB");
	(void)psbAddToOutputList(pPsb, psbSDVOInit(pScrn, SDVOB,
						   pDevice->sdvoBName));
	(void)psbOutputCompat(pScrn);
    } else {
	ScrnInfoPtr origScrn = pDevice->pScrns[0];

	(void)psbAddToOutputList(pPsb, psbOutputClone(pScrn, origScrn,
						      "LVDS0"));
	(void)psbAddToOutputList(pPsb, psbOutputClone(pScrn, origScrn,
						      pDevice->sdvoBName));
	(void)psbOutputCompat(pScrn);
    }
}

static Bool
psbInitCrtcs(ScrnInfoPtr pScrn)
{
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);
    PsbPtr pPsb0 = psbPTR(pDevice->pScrns[0]);
    unsigned i;

    PSB_DEBUG(pScrn->scrnIndex, 3, "xxi830_psbInitCrtcs\n");
    pPsb->numCrtcs = 0;

    for (i = 0; i < PSB_MAX_CRTCS; ++i) {
	if (pScrn == pDevice->pScrns[0])
	    pPsb->crtcs[i] = psbCrtcInit(pScrn, i);
	else
	    pPsb->crtcs[i] = psbCrtcClone(pScrn, pPsb0->crtcs[i]);
	if (!pPsb->crtcs[i])
	    return FALSE;
	pPsb->numCrtcs++;
    }
    psbCheckCrtcs(psbDevicePTR(pPsb));
    return TRUE;
}

/*
 * This function is called once for each screen at the start of the first
 * server generation to initialise the screen for all server generations.
 */
static Bool
psbPreInit(ScrnInfoPtr pScrn, int flags)
{
    PsbPtr pPsb;
    PsbPtr pPsb0;
    Gamma gzeros = { 0.0, 0.0, 0.0 };
    rgb rzeros = { 0, 0, 0 };
    DevUnion *pPriv;
    PsbDevicePtr pDevice;
    MessageType from;

    if (flags & PROBE_DETECT) {
	return FALSE;
    }

    PSB_DEBUG(pScrn->scrnIndex, 3, "psbPreInit\n");

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "psb_drv - %s\n", PSB_PACKAGE_VERSION);

    pPriv = xf86GetEntityPrivate(pScrn->entityList[0], psbEntityIndex);
    pDevice = (PsbDevicePtr) pPriv->ptr;
    if (pDevice == NULL)
	return FALSE;

    pPsb = psbGetRec(pScrn);

    if (!pPsb)
	return FALSE;

    pPsb->pDevice = pDevice;
    pPsb->pScrn = pScrn;
    pPsb->multiHead = (pPsb->pDevice->pScrns[1] != NULL);
    pPsb->secondary = (pPsb->pDevice->pScrns[1] == pScrn);
    mmInitListHead(&pPsb->outputs);

    if (pPsb->secondary) {
	pPsb0 = psbPTR(pDevice->pScrns[0]);
	if (pPsb0->serverGeneration == 0)
	    return FALSE;
    }

    if (psbEntityIndex == -1)
	return FALSE;

    if (!xf86LoadSubModule(pScrn, "vbe"))
	return FALSE;

    /*
     * Parse options and load required modules here.
     */

    if (!xf86SetDepthBpp(pScrn, 0, 0, 0, Support32bppFb)) {
	return FALSE;
    }

    if (pScrn->depth != 16 && pScrn->depth != 24 && pScrn->depth != 8) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "Invalid depth %d\n", pScrn->depth);
	return (FALSE);
    }

    xf86PrintDepthBpp(pScrn);

    if (!xf86LoadSubModule(pScrn, "fb"))
	return (FALSE);

    pScrn->chipset = "Intel GMA500";
    pScrn->monitor = pScrn->confScreen->monitor;
    pScrn->rgbBits = 8;
    pScrn->videoRam = pDevice->stolenSize / 1024;

    xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "Linear framebuffer at 0x%lX\n",
	       (unsigned long)pScrn->memPhysBase);

    /* color weight */
    if (!xf86SetWeight(pScrn, rzeros, rzeros)) {
	return (FALSE);
    }
    /* visual init */
    if (!xf86SetDefaultVisual(pScrn, -1)) {
	return (FALSE);
    }

    /* We can't do this until we have a
     * pScrn->display. */
    xf86CollectOptions(pScrn, NULL);
    if (!(pPsb->options = xalloc(sizeof(psbOptions))))
	return (FALSE);

    memcpy(pPsb->options, psbOptions, sizeof(psbOptions));
    xf86ProcessOptions(pScrn->scrnIndex, pScrn->options, pPsb->options);

    pPsb->sWCursor = FALSE;
    from = xf86GetOptValBool(pPsb->options, OPTION_SWCURSOR, &pPsb->sWCursor)
	? X_CONFIG : X_DEFAULT;

    xf86DrvMsg(pScrn->scrnIndex, from, "Use %s cursor.\n",
	       pPsb->sWCursor ? "software" : "hardware");

    pPsb->ignoreACPI = TRUE;
    from =
	xf86GetOptValBool(pPsb->options, OPTION_IGNORE_ACPI,
			  &pPsb->ignoreACPI)
	? X_CONFIG : X_DEFAULT;

    xf86DrvMsg(pScrn->scrnIndex, from,
	       "%s ACPI for LVDS detection.\n",
	       pPsb->ignoreACPI ? "Not using" : "Using");

#ifdef XF86DRI
    if (!psbPreInitDRI(pScrn))
	return FALSE;
    psbPreInitXpsb(pScrn);
#endif

    if (!pPsb->dri && !pPsb->secondary) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "This driver will currently not work without DRI.\n");
	return FALSE;
    }

    if (!psbDeviceInit(pPsb->pDevice, pScrn->scrnIndex))
	return FALSE;

    psbInitI830(pPsb);
    psbInitOutputs(pScrn);

    if (!psbInitCrtcs(pScrn)) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed setting up Crtcs.\n");
	return FALSE;
    }
    if (!psbDeviceFinishInit(pPsb->pDevice, pScrn->scrnIndex))
	return FALSE;

    if (!psbPreInitShadowFB(pScrn))
	return (FALSE);

    if (!pPsb->shadowFB && !psbPreInitAccel(pScrn))
	return (FALSE);

    pScrn->progClock = TRUE;

    if (pPsb->secondary) {

	/*
	 * FIXME: We need to figure out how to assign outputs to
	 * screens here. Perhaps a RandR property on each output that
	 * details what screen it belongs to. That way you could easily
	 * migrate outputs between screens.
	 *
	 * As the code stands now, the LVDS0 output will be assigned to
	 * the second screen if there are two screens configured.
	 */

	;
	;
	psbOutputAssignToScreen(pScrn, "LVDS0");
    } else {
	psbOutputAssignToScreen(pScrn, pDevice->sdvoBName);
	if (pDevice->numScreens != 2)
	    psbOutputAssignToScreen(pScrn, "LVDS0");
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	       "Searching for matching Poulsbo mode(s):\n");

    xf86CrtcSetSizeRange(pScrn, 320, 200, 4096, 4096);

    /* Unfortunately the Xserver is broken with regard to canGrow, so
     * we need to set this to FALSE for now.
     */
    if (!xf86InitialConfiguration(pScrn, FALSE)) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "Could not find a valid initial configuration "
		   "for this screen.\n");
	return FALSE;
    }

    psbCheckCrtcs(pDevice);

    pScrn->currentMode = pScrn->modes;
//    xf86PrintModes(pScrn);

    if (pScrn->modes == NULL) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No modes.\n");
	return (FALSE);
    }

    xf86SetGamma(pScrn, gzeros);
    xf86SetDpi(pScrn, 0, 0);

    mmInitListHead(&pPsb->buffers);
    pPsb->serverGeneration = 1;
    return TRUE;
}

static Bool
psbDeviceInit(PsbDevicePtr pDevice, int scrnIndex)
{
    #if (XORG_VERSION_MINOR >= 5 || (XORG_VERSION_MINOR == 4 && XORG_VERSION_PATCH == 99))
        struct pci_device dev;
    #endif

    PSB_DEBUG(scrnIndex, 3, "psbDeviceScreenInit\n");

    if (pDevice->refCount++ != 0) {
	PSB_DEBUG(scrnIndex, 3,
		  "Skipping device initialization for additional head.\n");
	return TRUE;
    }

    PSB_DEBUG(scrnIndex, 3, "Initializing device\n");

#ifndef XSERVER_LIBPCIACCESS
    if (xf86RegisterResources(pDevice->pEnt->index, NULL, ResExclusive)) {
	xf86DrvMsg(scrnIndex, X_ERROR,
		   "Could not registrer device. Resource conflict.\n");
	return FALSE;
    }
#endif

    if (!xf86LoadSubModule(pDevice->pScrns[0], "vgahw"))
	return FALSE;

    if (!vgaHWGetHWRec(pDevice->pScrns[0]))
	return FALSE;
  
   #if (XORG_VERSION_MINOR >= 5 || (XORG_VERSION_MINOR == 4 && XORG_VERSION_PATCH == 99))
        pDevice->regPhys = pDevice->pciInfo->regions[0].base_addr;
        pDevice->regSize = pDevice->pciInfo->regions[0].size;
   #else
        pDevice->regPhys = pDevice->pciInfo->memBase[0];
        pDevice->regSize = 1 << pDevice->pciInfo->size[0];
   #endif    

    pDevice->regMap = xf86MapVidMem(scrnIndex, VIDMEM_MMIO_32BIT,
				    pDevice->regPhys, pDevice->regSize);

    PSB_DEBUG(scrnIndex, 3, "MMIO virtual address is 0x%08lx\n",
	      (unsigned long)pDevice->regMap);

    if (!pDevice->regMap) {
	xf86DrvMsg(scrnIndex, X_ERROR, "Could not map PCI memory space\n");
	return FALSE;
    }
    xf86DrvMsg(scrnIndex, X_PROBED,
	       "Mapped PCI MMIO at physical address 0x%08lx\n"
	       "\twith size %lu kiB\n", pDevice->regPhys,
	       pDevice->regSize / 1024);


    /*
     * Map the OpRegion SCI region
     */
    {
	CARD32 OpRegion_Phys;
	unsigned int OpRegion_Size = 0x100;
	OpRegionPtr OpRegion;
	char *OpRegion_String = "IntelGraphicsMem";


        #if (XORG_VERSION_MINOR >= 5 || (XORG_VERSION_MINOR == 4 && XORG_VERSION_PATCH == 99))
            dev.domain = PCI_DOM_FROM_TAG(pDevice->pciTag);
            dev.bus = PCI_BUS_FROM_TAG(pDevice->pciTag);
            dev.dev = PCI_DEV_FROM_TAG(pDevice->pciTag);
            dev.func = PCI_FUNC_FROM_TAG(pDevice->pciTag);
            pci_device_cfg_read_u32(&dev,&OpRegion_Phys,0xFC);
        #else
            OpRegion_Phys = pciReadLong(pDevice->pciTag, 0xFC); 
        #endif

	pDevice->OpRegion = xf86MapVidMem(scrnIndex, VIDMEM_MMIO_32BIT,
					  OpRegion_Phys, OpRegion_Size);

	OpRegion = (OpRegionPtr) pDevice->OpRegion;

	if (!memcmp(OpRegion->sign, OpRegion_String, 16)) {
	    unsigned int OpRegion_NewSize;

	    OpRegion_NewSize = OpRegion->size * 1024;

	    xf86UnMapVidMem(scrnIndex, (pointer) pDevice->OpRegion,
			    OpRegion_Size);
	    pDevice->OpRegionSize = OpRegion_NewSize;

	    pDevice->OpRegion = xf86MapVidMem(scrnIndex, VIDMEM_MMIO_32BIT,
					      OpRegion_Phys,
					      pDevice->OpRegionSize);
	} else {
	    xf86UnMapVidMem(scrnIndex, (pointer) pDevice->OpRegion,
			    OpRegion_Size);
	    pDevice->OpRegion = NULL;
	}
    }
   
    #if (XORG_VERSION_MINOR >= 5 || (XORG_VERSION_MINOR == 4 && XORG_VERSION_PATCH == 99))
    struct pci_device device;
    uint32_t temp;
 
    device.domain = PCI_DOM_FROM_TAG(pDevice->pciTag);
    device.bus = PCI_BUS_FROM_TAG(pDevice->pciTag);
    device.dev = PCI_DEV_FROM_TAG(pDevice->pciTag);
    device.func = PCI_FUNC_FROM_TAG(pDevice->pciTag);
    pci_device_cfg_read_u32(&device,&temp, PSB_BSM);
 
    pDevice->stolenBase = (unsigned long)temp & 0xFFFFF000;
    xf86DrvMsg(scrnIndex,X_ERROR,"the stolenBase is:0x%08x\n",pDevice->stolenBase);
    #else
     pDevice->stolenBase = 
       (unsigned long)pciReadLong(pDevice->pciTag, PSB_BSM) & 0xFFFFF000;
    #endif

     pDevice->stolenSize = PSB_READ32(PSB_PGETBL_CTL) & 0xFFFFF000;
     pDevice->stolenSize -= pDevice->stolenBase;
     pDevice->stolenSize -= PSB_BIOS_POPUP_SIZE;
     pDevice->stolenSize >>= PSB_PAGE_SHIFT;
  
     xf86DrvMsg(scrnIndex, X_PROBED,
	       "Detected %lu kiB of \"stolen\" memory set aside as video RAM.\n",
	       pDevice->stolenSize << 2);

     
#undef PSB_USE_MMU
#undef PSB_WRITECOMBINING

#ifdef PSB_USE_MMU
    pDevice->fbPhys = pDevice->pciInfo->memBase[2];
    pDevice->fbSize = 1 << pDevice->pciInfo->size[2];

#ifdef PSB_WRITECOMBINING
    pDevice->fbMap = xf86MapVidMem(scrnIndex, VIDMEM_FRAMEBUFFER,
				   pDevice->fbPhys, pDevice->fbSize);
#else
    pDevice->fbMap = xf86MapVidMem(scrnIndex, VIDMEM_MMIO_32BIT,
				   pDevice->fbPhys, pDevice->fbSize);
#endif /* PSB_WRITECOMBINING */

#else
   
     pDevice->fbPhys = pDevice->stolenBase;
     pDevice->fbSize = pDevice->stolenSize << PSB_PAGE_SHIFT;
    
    xf86DrvMsg(scrnIndex,X_ERROR,"screnIndex is:%d;fbPhys is:0x%08x; fbsize is:0x%08x\n",scrnIndex,pDevice->fbPhys,pDevice->fbSize);
    pDevice->fbMap = xf86MapVidMem(scrnIndex, VIDMEM_MMIO_32BIT,
				   pDevice->fbPhys, pDevice->fbSize);
#endif /* PSB_USE_MMU */

    if (!pDevice->fbMap) {
	xf86DrvMsg(scrnIndex, X_ERROR,
		   "Could not map graphics aperture space\n");
	xf86UnMapVidMem(scrnIndex, (pointer) pDevice->regMap,
			pDevice->regSize);
	pDevice->regMap = 0;
	return FALSE;
    }

    xf86DrvMsg(scrnIndex, X_PROBED,
	       "Mapped graphics aperture at physical address 0x%08lx\n"
	       "\twith size %lu MiB\n",
	       pDevice->fbPhys, pDevice->fbSize / (1024 * 1024));

    pDevice->saveSWF0 = PSB_READ32(SWF0);
    pDevice->saveSWF4 = PSB_READ32(SWF4);
    pDevice->swfSaved = TRUE;

    PSB_WRITE32(SWF0, pDevice->saveSWF0 | (1 << 21));
    PSB_WRITE32(SWF4, (pDevice->saveSWF4 &
		       ~((3 << 19) | (7 << 16))) | (1 << 23) | (2 << 16));

    PSB_DEBUG(scrnIndex, 3, "DRM device init\n");

#if !PSB_LEGACY_DRI
#ifdef XF86DRI
    pDevice->hasDRM = psbDRMDeviceInit(pDevice);
    if (pDevice->hasDRM) {
	pDevice->man = mmCreateDRM(pDevice->drmFD);
	if (!pDevice->man) {
	    xf86DrvMsg(-1, X_ERROR,
		       "Could not create a DRM memory manager.\n");
	    return FALSE;
	}
    } else {
	xf86DrvMsg(-1, X_ERROR,
		   "This driver currently needs DRM to operate.\n");
	return FALSE;
    }
#else
    xf86DrvMsg(-1, X_ERROR, "This driver currently needs DRM to operate.\n");
    return FALSE;
#endif
#else /* !PSB_LEGACY_DRI */
    pDevice->hasDRM = TRUE;
#endif /* !PSB_LEGACY_DRI */

    {
	unsigned int CoreClocks[] = {
	    100,
	    133,
	    150,
	    178,
	    200,
	    266,
	    266,
	    266
	};
	unsigned int MemClocks[] = {
	    400,
	    533,
	};
	PCITAG host = pciTag(0, 0, 0);
	CARD32 clock, period;

        #if (XORG_VERSION_MINOR >= 5 || (XORG_VERSION_MINOR == 4 && XORG_VERSION_PATCH == 99))
        struct pci_device dev;
        dev.domain = PCI_DOM_FROM_TAG(host);
        dev.bus = PCI_BUS_FROM_TAG(host);
        dev.dev = PCI_DEV_FROM_TAG(host);
        dev.func = PCI_FUNC_FROM_TAG(host);
 
        pci_device_cfg_write_u32(&dev, 0xD0050300, 0xD0);
        pci_device_cfg_read_u32(&dev, &clock, 0xD4);
        #else
        pciWriteLong(host, 0xD4, 0x00C32004);
	pciWriteLong(host, 0xD0, 0xE0033000);
	pciWriteLong(host, 0xD0, 0xD0050300);
	clock = pciReadLong(host, 0xD4);
        #endif

	pDevice->CoreClock = CoreClocks[clock & 0x07];
	pDevice->MemClock = MemClocks[(clock & 0x08) >> 3];
	period = 2000 / pDevice->MemClock;
#define TIMETRANSACTION(x) (period * x / 16)
	pDevice->Latency[0] = 8000 * period / 1000 + 2 * (50 * period);
	pDevice->Latency[1] =
	    2 * 8000 * period / 1000 + TIMETRANSACTION(512) +
	    4 * (50 * period);
	pDevice->WorstLatency[0] =
	    3 * 8000 * period / 1000 + TIMETRANSACTION(512) +
	    TIMETRANSACTION(128) + (period * 6);
	pDevice->WorstLatency[1] =
	    5 * 8000 * period / 1000 + 3 * TIMETRANSACTION(512) +
	    TIMETRANSACTION(128) + (period * 6);
	xf86DrvMsg(scrnIndex, X_INFO,
		   "Poulsbo MemClock %d, CoreClock %d\n", pDevice->MemClock,
		   pDevice->CoreClock);
	xf86DrvMsg(scrnIndex, X_INFO,
		   "Poulsbo Latencies %d %d %d %d\n", pDevice->Latency[0],
		   pDevice->Latency[1], pDevice->WorstLatency[0],
		   pDevice->WorstLatency[1]);

    }
	
#define PCI_PORT5_REG80_FFUSE				0xD0058000
#define PCI_PORT5_REG80_SDVO_DISABLE		0x0020
#define PCI_PORT5_REG80_MAXRES_INT_EN		0x0040
//To read PBO.MSR.CCF Mode and Status Register C-Spec -p112
	{
	PCITAG host = pciTag(0, 0, 0);

        #if (XORG_VERSION_MINOR >= 5 || (XORG_VERSION_MINOR == 4 && XORG_VERSION_PATCH == 99))
          struct pci_device dev;
          dev.domain = PCI_DOM_FROM_TAG(host);
          dev.bus = PCI_BUS_FROM_TAG(host);
          dev.dev = PCI_DEV_FROM_TAG(host);
          dev.func = PCI_FUNC_FROM_TAG(host);
 
          pci_device_cfg_write_u32(&dev, PCI_PORT5_REG80_FFUSE, 0xD0);
          pci_device_cfg_read_u32(&dev, &(pDevice->sku_value), 0xD4);
        #else
          pciWriteLong(host, 0xD0, PCI_PORT5_REG80_FFUSE);
	  pDevice->sku_value= pciReadLong(host, 0xD4);
        #endif

	pDevice->sku_bSDVOEnable = (pDevice->sku_value & PCI_PORT5_REG80_SDVO_DISABLE)?FALSE : TRUE;
	pDevice->sku_bMaxResEnableInt = (pDevice->sku_value & PCI_PORT5_REG80_MAXRES_INT_EN)?TRUE: FALSE;
	xf86DrvMsg(scrnIndex, X_INFO,
		   "sku_value is 0x%08x, sku_bSDVOEnable is %d, sku_bMaxResEnableInt is %d\n",
		   pDevice->sku_value, pDevice->sku_bSDVOEnable,
		   pDevice->sku_bMaxResEnableInt);
			
	}

    pDevice->vtRefCount = 0;
    pDevice->deviceUp = TRUE;

    return TRUE;
}

static void
psbDeviceTakeDown(PsbDevicePtr pDevice, int scrnIndex)
{
    PSB_DEBUG(scrnIndex, 3, "psbDeviceTakeDown\n");

    if (scrnIndex != 0)
	goto out;

    if (!pDevice->deviceUp)
	goto out;

    PSB_DEBUG(scrnIndex, 3, "Taking device down.\n");

    if (pDevice->man) {
	pDevice->man->destroy(pDevice->man);
	pDevice->man = NULL;
    }
#ifdef XF86DRI
    if (pDevice->hasDRM) {
#if !PSB_LEGACY_DRI
	psbDRMDeviceTakeDown(pDevice);
#endif
	pDevice->hasDRM = FALSE;
    }
#endif

    if (pDevice->swfSaved) {
	PSB_WRITE32(SWF0, pDevice->saveSWF0);
	PSB_WRITE32(SWF4, pDevice->saveSWF4);
    }

    if (pDevice->hwSaved && pDevice->pScrns[0]->vtSema) {
	psbRestoreHWState(pDevice);
	pDevice->pScrns[0]->vtSema = FALSE;
    }

    if (pDevice->fbMap) {
	xf86UnMapVidMem(scrnIndex, (pointer) pDevice->fbMap, pDevice->fbSize);
	pDevice->fbMap = NULL;
    }
    if (pDevice->regMap) {
	xf86UnMapVidMem(scrnIndex, (pointer) pDevice->regMap,
			pDevice->regSize);
	pDevice->regMap = NULL;
    }
    if (pDevice->OpRegion) {
	xf86UnMapVidMem(scrnIndex, (pointer) pDevice->OpRegion,
			pDevice->OpRegionSize);
	pDevice->OpRegion = NULL;
    }
    pDevice->deviceUp = FALSE;
  out:

    if (--pDevice->refCount == 0)
	xfree(pDevice);
}

static Bool
psbScreenInit(int scrnIndex, ScreenPtr pScreen, int argc, char **argv)
{
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);
    VisualPtr visual;
    int flags;
    CARD8 *fbstart;

#ifdef XF86DRI
    Bool driEnabled = FALSE;
#endif

    PSB_DEBUG(scrnIndex, 3, "psbScreenInit\n");

    if (!pDevice->deviceUp) {
	xf86DrvMsg(scrnIndex, X_ERROR,
		   "Initalization of one or more screens failed for this device.\n"
		   "\tBailing out.\n");
	return FALSE;
    }

    /*
     * Initialize DRI early on to get a device file descriptor.
     */

#ifdef XF86DRI
    pPsb->driEnabled = FALSE;
#if PSB_LEGACY_DRI
/*
 * No DRI on secondary.
 */
    if (!pPsb->secondary) {
	driEnabled = psbDRIScreenInit(pScreen);
	if (!driEnabled) {
	    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		       "This driver currently needs DRM to operate.\n");
	    return FALSE;
	}
	pDevice->man = mmCreateDRM(pDevice->drmFD);
	if (!pDevice->man) {
	    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		       "Could not create a memory manager.\n");
	    return FALSE;
	}
    }
#else
    if (pDevice->hasDRM)
	driEnabled = psbDRIScreenInit(pScreen);
#endif
#endif
    if (serverGeneration != pPsb->serverGeneration) {

	/*
	 * Mode layer requires us to rerun this each server
	 * generation, but not the first, as it's done in psbPreInit.
	 */

	pPsb->serverGeneration = serverGeneration;
	/* Unfortunately the Xserver is broken with regard to canGrow, so
	 * we need to set this to FALSE for now.
	 */
	if (!xf86InitialConfiguration(pScrn, FALSE)) {
	    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		       "Could not find a valid initial configuration "
		       "for this screen\n");
	    goto out_err;
	}
	psbCheckCrtcs(pDevice);
    }

    pScrn->pScreen = pScreen;

    pPsb->front = psbScanoutCreate(pScrn, pScrn->bitsPerPixel >> 3,
				   pScrn->depth, pScrn->virtualX,
				   pScrn->virtualY, 0, -1, 0);

    if (!pPsb->front) {
	xf86DrvMsg(scrnIndex, X_ERROR, "Failed allocating a framebuffer.\n");
	goto out_err;
    }

    pScrn->displayWidth =
	psbScanoutStride(pPsb->front) / psbScanoutCpp(pPsb->front);
    pPsb->fbMap = psbScanoutVirtual(pPsb->front);
    pPsb->stride = psbScanoutStride(pPsb->front);
    pPsb->cpp = psbScanoutCpp(pPsb->front);

    /* mi layer */
    miClearVisualTypes();
    if (!miSetVisualTypes(pScrn->depth, miGetDefaultVisualMask(pScrn->depth),
			  pScrn->rgbBits, pScrn->defaultVisual))
	goto out_err;
    if (!miSetPixmapDepths())
	goto out_err;

    /* shadowfb */
    PSB_DEBUG(scrnIndex, 3, "Shadow\n");
    if (pPsb->shadowFB) {
	if ((pPsb->shadowMem =
	     shadowAlloc(pScrn->displayWidth, pScrn->virtualY,
			 pScrn->bitsPerPixel)) == NULL) {
	    xf86DrvMsg(scrnIndex, X_ERROR,
		       "Allocation of shadow memory failed\n");
	    goto out_err;
	}
	fbstart = pPsb->shadowMem;
    } else {
	fbstart = pPsb->fbMap;
    }

    PSB_DEBUG(scrnIndex, 3, "Calling fbScreenInit.\n");
    if (!fbScreenInit(pScreen, fbstart, pScrn->virtualX, pScrn->virtualY,
		      pScrn->xDpi, pScrn->yDpi, pScrn->displayWidth,
		      pScrn->bitsPerPixel))
	goto out_err;

    /* Fixup RGB ordering */

    PSB_DEBUG(scrnIndex, 3, "Fix up visuals.\n");
    if (pScrn->bitsPerPixel > 8) {
	visual = pScreen->visuals + pScreen->numVisuals;
	while (--visual >= pScreen->visuals) {
	    if ((visual->class | DynamicClass) == DirectColor) {
		visual->offsetRed = pScrn->offset.red;
		visual->offsetGreen = pScrn->offset.green;
		visual->offsetBlue = pScrn->offset.blue;
		visual->redMask = pScrn->mask.red;
		visual->greenMask = pScrn->mask.green;
		visual->blueMask = pScrn->mask.blue;
	    }
	}
    }

    /* must be after RGB ordering fixed */

    PSB_DEBUG(scrnIndex, 3, "fbPictureInitInit\n");
    fbPictureInit(pScreen, NULL, 0);
    if (pPsb->shadowFB) {
	pPsb->update = shadowUpdatePackedWeak();
	if (!shadowSetup(pScreen)) {
	    xf86DrvMsg(scrnIndex, X_ERROR,
		       "Shadow framebuffer initialization failed.\n");
	    goto out_err;
	}

	pPsb->createScreenResources = pScreen->CreateScreenResources;
	pScreen->CreateScreenResources = psbCreateScreenResources;
    }

    xf86SetBlackWhitePixels(pScreen);

    PSBDGAInit(pScreen);

#ifdef XF86DRI
    if (!pPsb->shadowFB && !pPsb->noAccel && pDevice->hasDRM) {
	pPsb->has2DBuffer = psbInit2DBuffer(pDevice->drmFD, &pPsb->superC);
	if (pPsb->has2DBuffer) {
	    pPsb->pPsbExa = psbExaInit(pScrn);
	    if (!pPsb->pPsbExa) {
		xf86DrvMsg(scrnIndex, X_ERROR,
			   "[EXA] Acceleration initialization failed. "
			   "Disabling EXA acceleration.\n");
	    }
	}
    }
#endif

    PSB_DEBUG(scrnIndex, 3, "Backing store\n");
    miInitializeBackingStore(pScreen);
    xf86SetBackingStore(pScreen);
    xf86SetSilkenMouse(pScreen);

    /* software cursor */
    miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

    /* colormap */
    if (!miCreateDefColormap(pScreen))
	goto out_err;

    flags = CMAP_RELOAD_ON_MODE_SWITCH | CMAP_PALETTED_TRUECOLOR;

    if (!xf86HandleColormaps(pScreen, 256, 8, psbLoadPalette, NULL, flags))
	goto out_err;

    xf86DPMSInit(pScreen, psbDisplayPowerManagementSet, 0);

    /* Report any unused options (only for the first generation) */
    if (serverGeneration == 1)
	xf86ShowUnusedOptions(pScrn->scrnIndex, pScrn->options);

    if (!xf86CrtcScreenInit(pScreen)) {
	xf86DrvMsg(scrnIndex, X_ERROR,
		   "Could not initialize RandR properly. Bailing out.\n");
	goto out_err;
    }

    pPsb->closeScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = psbCloseScreen;
    pScreen->SaveScreen = psbSaveScreen;

#ifdef XF86DRI
    if (driEnabled) {
	pPsb->driEnabled = psbDRIFinishScreenInit(pScreen);
	if (!pPsb->driEnabled)
	    xf86DrvMsg(scrnIndex, X_ERROR, "Failed setting up DRI.\n");
    }

    if (pPsb->xpsb && pDevice->hasDRM && pPsb->pPsbExa &&
	XpsbInit(pScrn, pDevice->regMap, pDevice->drmFD)) {
	pPsb->hasXpsb = TRUE;
	xf86DrvMsg(scrnIndex, X_INFO,
		   "Xpsb extension for 3D engine acceleration enabled.\n");
	pPsb->adaptor = psbInitVideo(pScreen);
	xf86DrvMsg(scrnIndex, X_INFO, "Xv video acceleration %sabled.\n",
		   (pPsb->adaptor) ? "en" : "dis");
    }
#endif

    if (!pPsb->sWCursor) {
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Initializing HW Cursor.\n");
	if (!psbCursorInit(pScreen)) {
	    xf86DrvMsg(scrnIndex, X_ERROR, "Hardware Cursor init failed.\n");
	    pPsb->sWCursor = TRUE;
	}
    }
    if (pPsb->sWCursor)
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Initializing SW Cursor.\n");

    psbDRILock(pScrn, 0);
    pScrn->vtSema = TRUE;

    return psbEnterVT(pScreen->myNum, 0);
  out_err:
    psbRestoreHWState(pDevice);
    return FALSE;
}

static void
psbSetVGAOff(ScrnInfoPtr pScrn)
{
    vgaHWPtr hwp = VGAHWPTR(pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(psbPTR(pScrn));
    CARD8 seq01;

    PSB_DEBUG(pScrn->scrnIndex, 3, "psbSetVGAOff\n");

    seq01 = hwp->readSeq(hwp, 0x01);
    hwp->writeSeq(hwp, 0x01, seq01 | 0x20);
    usleep(30000);
    PSB_WRITE32(VGACNTRL, VGA_DISP_DISABLE);
    (void)PSB_READ32(VGACNTRL);
}

static Bool
psbLockMM(PsbDevicePtr pDevice, Bool scanoutOnly)
{
    int ret = 0;

    PSB_DEBUG(-1, 3, "psbLockMM\n");

    ret = pDevice->man->lock(pDevice->man, MM_MEM_VRAM, 1, 0);
    if (ret)
	goto out_err;
    ret = pDevice->man->lock(pDevice->man, MM_MEM_TT, 0, 0);
    if (ret)
	goto out_err;
#if 0
    if (!scanoutOnly)
	ret = pDevice->man->lock(pDevice->man, MM_MEM_PRIV0, 0, 0);
    if (ret)
	goto out_err;
#endif
    return TRUE;

  out_err:
    xf86DrvMsg(-1, X_ERROR, "Failed locking memory manager: \"%s\".\n",
	       strerror(-ret));
    return TRUE;
}

static Bool
psbUnLockMM(PsbDevicePtr pDevice, Bool scanoutOnly)
{
    int ret = 0;

    PSB_DEBUG(-1, 3, "psbUnLockMM\n");

#if 0
    if (!scanoutOnly)
	ret = pDevice->man->unLock(pDevice->man, MM_MEM_PRIV0, 0);
    if (ret)
	goto out_err;
#endif
    ret = pDevice->man->unLock(pDevice->man, MM_MEM_TT, 0);
    if (ret)
	goto out_err;
    ret = pDevice->man->unLock(pDevice->man, MM_MEM_VRAM, 1);
    if (ret)
	goto out_err;

    return TRUE;

  out_err:
    xf86DrvMsg(-1, X_ERROR, "Failed unlocking memory manager: \"%s\".\n",
	       strerror(-ret));
    return TRUE;
}

static void
psbTurnOffKernelModeSetting(int scrnIndex)
{
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);

    drmCommandNone(pDevice->drmFD, 0x04);
}

static void
psbTurnOnKernelModeSetting(int scrnIndex)
{
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);

    drmCommandNone(pDevice->drmFD, 0x05);
}

static Bool
psbEnterVT(int scrnIndex, int flags)
{
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);

    PSB_DEBUG(scrnIndex, 3, "psbEnterVT %d\n", pScrn->vtSema);
    if (++pDevice->vtRefCount == 1) {
#ifdef XF86DRI
	if (pDevice->irq == -1)
	    psbDRMIrqInit(pDevice);
#endif

	if (pDevice->mmLocked && !psbUnLockMM(pDevice, FALSE))
	    return FALSE;

	pDevice->mmLocked = FALSE;

#ifdef XF86DRI
	psbTurnOffKernelModeSetting(scrnIndex);
#endif

#ifdef WA_NOFB_GARBAGE_DISPLAY
    {
	FILE* fb0_file;
	fb0_file = fopen("/dev/fb0", "r");
	if(fb0_file) {
	    xf86DrvMsg(scrnIndex, X_ERROR, "has_fbdev is true\n");
	    fclose(fb0_file);
	    pPsb->has_fbdev=TRUE;
	} else {
	    xf86DrvMsg(scrnIndex, X_ERROR, "has_fbdev is false\n");
	    pPsb->has_fbdev=FALSE;
	}
    }
#endif
	if (!psbSaveHWState(pDevice))
	    return FALSE;

	psbSetVGAOff(pScrn);

    }

    /* Reallocate front buffer */
    if (!pPsb->front && !psbSetFront(pScrn))
	return FALSE;
    memset(pPsb->fbMap, 0, psbScanoutStride(pPsb->front) * pScrn->virtualY);
    if (!pPsb->sWCursor) {
	if (psbCrtcSetupCursors(pScrn))
	    psbInitHWCursor(pScrn);
	else
	    return FALSE;
    }

    if (!xf86SetDesiredModes(pScrn))
	return FALSE;
    psbDescribeOutputConfiguration(pScrn);

    PSB_WRITE32(DSPARB, (0x7E<<7 | 0x40<<0));

    psbAdjustFrame(pScrn->scrnIndex, pScrn->frameX0, pScrn->frameY0, 0);
    psbDRIUnlock(pScrn);
    return TRUE;
}

static void
psbLeaveVT(int scrnIndex, int flags)
{
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);

    PSB_DEBUG(scrnIndex, 3, "psbLeaveVT\n");

    psbDRILock(pScrn, 0);
    xf86DPMSSet(pScrn, DPMSModeStandby, 0);

    xf86_hide_cursors(pScrn);
    /* Move out cursor buffers */
    if (!pPsb->sWCursor)
	psbCrtcSaveCursors(pScrn, !pPsb->secondary);
    /* Free all rotated buffers */
    xf86RotateCloseScreen(pScrn->pScreen);

    /* Free front buffer */
    pPsb->fbMap = NULL;
    psbScanoutDestroy(pPsb->front);
    pPsb->front = NULL;

    if (--pDevice->vtRefCount != 0) {
	pScrn->vtSema = FALSE;
	return;
    }

    psbRestoreHWState(psbDevicePTR(pPsb));

    /*
     * Clean the GATT, and save VRAM contents.
     */

    if (flags == 0) {
	psbLockMM(pDevice, FALSE);
	pDevice->mmLocked = TRUE;
    }
#ifdef XF86DRI
    psbTurnOnKernelModeSetting(scrnIndex);
    psbDRMIrqTakeDown(pDevice);
#endif

    pScrn->vtSema = FALSE;
}

static Bool
psbCloseScreen(int scrnIndex, ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);

    PSB_DEBUG(scrnIndex, 3, "psbCloseScreen\n");
    pScreen->CloseScreen = pPsb->closeScreen;
    pScreen->CreateScreenResources = pPsb->createScreenResources;

    if (pPsb->adaptor) {
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Shutting down Xvideo.\n");
	psbFreeAdaptor(pScrn, pPsb->adaptor);
	pPsb->adaptor = NULL;
    }

    if (pScrn->vtSema) {

	psbLeaveVT(scrnIndex, 1);

	psbDRIUnlock(pScrn);

    }
    if (!pPsb->secondary && pDevice->mmLocked) {
	psbUnLockMM(pDevice, FALSE);
	pDevice->mmLocked = FALSE;
    }

    if (pPsb->pPsbExa) {
	psbExaClose(pPsb->pPsbExa, pScreen);
	pPsb->pPsbExa = NULL;
    }
    if (pPsb->has2DBuffer) {
	psbTakedown2DBuffer(pDevice->drmFD, &pPsb->superC);
	pPsb->has2DBuffer = FALSE;
    }

    xf86_cursors_fini(pScreen);

    /*
     * FIXME: Need to free validate list.
     */

    if (!pPsb->sWCursor)
	psbCrtcFreeCursors(pScrn);

#ifdef XF86DRI
    if (pPsb->driEnabled) {
	if (pPsb->hasXpsb) {
	    XpsbTakeDown(pScrn);
	    pPsb->hasXpsb = FALSE;
	}
	psbDRIUnlock(pScrn);
	psbDRICloseScreen(pScreen);
    }
#endif

    if (pPsb->shadowMem) {
	xfree(pPsb->shadowMem);
	pPsb->shadowMem = NULL;
    }

    return pScreen->CloseScreen(scrnIndex, pScreen);
}

static Bool
psbSwitchMode(int scrnIndex, DisplayModePtr pMode, int flags)
{
    PSB_DEBUG(scrnIndex, 3, "psbSwitchMode\n");
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];

    /*
     * FIXME: Sync accel.
     */

    return xf86SetSingleMode(pScrn, pMode, RR_Rotate_0);
}

static void
psbAdjustFrame(int scrnIndex, int x, int y, int flags)
{
    PSB_DEBUG(scrnIndex, 3, "psbAdjustFrame\n");
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    PsbPtr pPsb = psbPTR(pScrn);
    int i;
    xf86CrtcPtr crtc;

    for (i = 0; i < pPsb->numCrtcs; ++i) {
	crtc = pPsb->crtcs[i];
	if (crtc->enabled)
	    psbPipeSetBase(crtc, x + crtc->x, y + crtc->y);
    }
}

static void
psbFreeScreen(int scrnIndex, int flags)
{
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice;
    unsigned i;

    /*
     * Note that this function may be called in arbitrary order, so
     * Make sure to take down common resources on the correct
     * scrnIndex.
     */

    PSB_DEBUG(scrnIndex, 3, "psbFreeScreen\n");

    if (!pPsb)
	return;

    pDevice = psbDevicePTR(pPsb);
    if (pDevice)
	psbDeviceTakeDown(pDevice, scrnIndex);

    for (i = 0; i < pPsb->numCrtcs; ++i)
	xf86CrtcDestroy(pPsb->crtcs[i]);

    psbOutputDestroyAll(pScrn);

    psbFreeRec(pScrn);
}

static void
psbLoadPalette(ScrnInfoPtr pScrn, int numColors, int *indices,
	       LOCO * colors, VisualPtr pVisual)
{
    int i, j, index;
    int p;
    CARD16 lutR[256], lutG[256], lutB[256];
    PsbPtr pPsb = psbPTR(pScrn);

    PSB_DEBUG(pScrn->scrnIndex, 3, "psbLoadPalette\n");

    for (p = 0; p < pPsb->numCrtcs; p++) {
	xf86CrtcPtr crtc = pPsb->crtcs[p];
	PsbCrtcPrivatePtr pCrtc = (PsbCrtcPrivatePtr) crtc->driver_private;

	for (i = 0; i < 256; ++i) {
	    lutR[i] = pCrtc->lutR[i] << 8;
	    lutG[i] = pCrtc->lutG[i] << 8;
	    lutB[i] = pCrtc->lutB[i] << 8;
	}

	switch (pScrn->depth) {
	case 15:
	    for (i = 0; i < numColors; ++i) {
		index = indices[i];
		for (j = 0; j < 8; j++) {
		    lutR[index * 8 + j] = colors[index].red << 8;
		    lutG[index * 8 + j] = colors[index].green << 8;
		    lutB[index * 8 + j] = colors[index].blue << 8;
		}
	    }
	    break;
	case 16:
	    for (i = 0; i < numColors; i++) {
		index = indices[i];

		if (i <= 31) {
		    for (j = 0; j < 8; j++) {
			lutR[index * 8 + j] = colors[index].red << 8;
			lutB[index * 8 + j] = colors[index].blue << 8;
		    }
		}

		for (j = 0; j < 4; j++) {
		    lutG[index * 4 + j] = colors[index].green << 8;
		}
	    }
	    break;
	default:
	    for (i = 0; i < numColors; i++) {
		index = indices[i];
		lutR[index] = colors[index].red << 8;
		lutG[index] = colors[index].green << 8;
		lutB[index] = colors[index].blue << 8;
	    }
	    break;
	}

#ifdef RANDR_12_INTERFACE
	RRCrtcGammaSet(crtc->randr_crtc, lutR, lutG, lutB);
#else
	crtc->funcs->gamma_set(crtc, lutR, lutG, lutB, 256);
#endif
    }
}

static Bool
psbSaveScreen(ScreenPtr pScreen, int mode)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    Bool on = xf86IsUnblank(mode);

    PSB_DEBUG(pScrn->scrnIndex, 3, "psbSaveScreen\n");

    if (on)
	SetTimeSinceLastInputEvent();

    if (pScrn->vtSema) {
	return xf86SaveScreen(pScreen, mode);
    }

    return TRUE;
}

static void
psbDisplayPowerManagementSet(ScrnInfoPtr pScrn, int mode, int flags)
{
    PSB_DEBUG(pScrn->scrnIndex, 3, "psbDisplayPowerManagementSet\n");

    if (!pScrn->vtSema)
	return;

    switch (mode) {
    case DPMSModeOn:
	/* Screen: On; HSync: On, VSync: On */
	/* Higher layer should turn on, but for FC 6, it is not when lid is opened */
	xf86DPMSSet(pScrn, DPMSModeOn, 0);
	break;
    case DPMSModeStandby:
	/* Screen: Off; HSync: Off, VSync: On -- Not Supported */
	break;
    case DPMSModeSuspend:
	/* Screen: Off; HSync: On, VSync: Off -- Not Supported */
	break;
    case DPMSModeOff:
	/* Screen: Off; HSync: Off, VSync: Off */
	xf86DPMSSet(pScrn, DPMSModeOff, 0);
	break;
    }
}

void
psbCheckCrtcs(PsbDevicePtr pDevice)
{
    ScrnInfoPtr pScrn, crtcScrn;
    PsbCrtcPrivatePtr pCrtc;
    int i, j;

    for (i = 0; i < PSB_MAX_CRTCS; ++i) {
	crtcScrn = NULL;

	for (j = 0; j < pDevice->numScreens; ++j) {
	    PsbPtr pPsb = psbPTR(pScrn = pDevice->pScrns[j]);

	    if (pPsb && pPsb->crtcs[i] && xf86CrtcInUse(pPsb->crtcs[i])) {
		if (crtcScrn)
		    xf86DrvMsg(-1, X_ERROR, "Duplicate use of Crtc\n");
		crtcScrn = pScrn;
	    }
	}

	pCrtc =
	    (PsbCrtcPrivatePtr) psbPTR(pDevice->pScrns[0])->crtcs[i]->
	    driver_private;

	if (crtcScrn)
	    psbOutputDisableCrtcForOtherScreens(crtcScrn, i);
	else
	    psbOutputEnableCrtcForAllScreens(pDevice, i);
    }
}

static Bool
psbSaveHWState(PsbDevicePtr pDevice)
{
    ScrnInfoPtr pScrn = pDevice->pScrns[0];
    PsbPtr pPsb = psbPTR(pScrn);
    xf86CrtcPtr xCrtc;
    vgaHWPtr hwp = VGAHWPTR(pScrn);
    vgaRegPtr vgaReg = &hwp->SavedReg;
    int i;

    xf86DrvMsg(0, 3, "i830_psbSaveHWState\n");
    psbOutputSave(pScrn);

    for (i = 0; i < pPsb->numCrtcs; ++i) {
	xCrtc = pPsb->crtcs[i];
	xCrtc->funcs->save(xCrtc);
    }

    pDevice->saveVCLK_DIVISOR_VGA0 = PSB_READ32(VCLK_DIVISOR_VGA0);
    pDevice->saveVCLK_DIVISOR_VGA1 = PSB_READ32(VCLK_DIVISOR_VGA1);
    pDevice->saveVCLK_POST_DIV = PSB_READ32(VCLK_POST_DIV);
    pDevice->saveVGACNTRL = PSB_READ32(VGACNTRL);

    for (i = 0; i < 7; ++i) {
	pDevice->saveSWF[i] = PSB_READ32(SWF0 + (i << 2));
	pDevice->saveSWF[i + 7] = PSB_READ32(SWF00 + (i << 2));
    }

    pDevice->saveSWF[14] = PSB_READ32(SWF30);
    pDevice->saveSWF[15] = PSB_READ32(SWF31);
    pDevice->saveSWF[16] = PSB_READ32(SWF32);

    vgaHWUnlock(hwp);
#ifdef WA_NOFB_GARBAGE_DISPLAY
    if(pPsb->has_fbdev == FALSE)
#endif
	vgaHWSave(pScrn, vgaReg, VGA_SR_FONTS);

    pDevice->hwSaved = TRUE;
    return TRUE;
}

static Bool
psbRestoreHWState(PsbDevicePtr pDevice)
{
    ScrnInfoPtr pScrn = pDevice->pScrns[0];
    PsbPtr pPsb = psbPTR(pScrn);
    xf86CrtcPtr xCrtc;
    vgaHWPtr hwp = VGAHWPTR(pScrn);
    vgaRegPtr vgaReg = &hwp->SavedReg;
    int i;

    PSB_DEBUG(-1, 3, "psbRestoreHWState\n");

    psbOutputDPMS(pScrn, DPMSModeOff);
    psbWaitForVblank(pScrn);

    for (i = 0; i < pPsb->numCrtcs; ++i) {
	xCrtc = pPsb->crtcs[i];
	xCrtc->funcs->dpms(xCrtc, DPMSModeOff);
    }
    psbWaitForVblank(pScrn);

    for (i = 0; i < pPsb->numCrtcs; ++i) {
	xCrtc = pPsb->crtcs[i];
	xCrtc->funcs->restore(xCrtc);
    }
    psbOutputRestore(pScrn);

    PSB_WRITE32(VGACNTRL, pDevice->saveVGACNTRL);
    PSB_WRITE32(VCLK_DIVISOR_VGA0, pDevice->saveVCLK_DIVISOR_VGA0);
    PSB_WRITE32(VCLK_DIVISOR_VGA1, pDevice->saveVCLK_DIVISOR_VGA1);
    PSB_WRITE32(VCLK_POST_DIV, pDevice->saveVCLK_POST_DIV);

    for (i = 0; i < 7; i++) {
	PSB_WRITE32(SWF0 + (i << 2), pDevice->saveSWF[i]);
	PSB_WRITE32(SWF00 + (i << 2), pDevice->saveSWF[i + 7]);
    }

    PSB_WRITE32(SWF30, pDevice->saveSWF[14]);
    PSB_WRITE32(SWF31, pDevice->saveSWF[15]);
    PSB_WRITE32(SWF32, pDevice->saveSWF[16]);

#ifdef WA_NOFB_GARBAGE_DISPLAY
    if(pPsb->has_fbdev == FALSE)
#endif
	vgaHWRestore(pScrn, vgaReg, VGA_SR_FONTS);
    vgaHWLock(hwp);

    return TRUE;
}

static Bool
psbDeviceFinishInit(PsbDevicePtr pDevice, int scrnIndex)
{
    PSB_DEBUG(scrnIndex, 3, "psbDeviceFinishInit\n");
    if (pDevice->refCount != 1)
	return TRUE;

    PSB_DEBUG(-1, 3, "Really running psbDeviceFinishInit\n");

    if (!psbSaveHWState(pDevice))
	return FALSE;

    return TRUE;
}

static Bool
psbCreateScreenResources(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    PsbPtr pPsb = psbPTR(pScrn);
    Bool ret;

    pScreen->CreateScreenResources = pPsb->createScreenResources;
    ret = pScreen->CreateScreenResources(pScreen);
    pScreen->CreateScreenResources = psbCreateScreenResources;

    shadowAdd(pScreen, pScreen->GetScreenPixmap(pScreen),
	      pPsb->update, psbWindowLinear, 0, 0);

    return ret;
}

static void
psbPointerMoved(int scrnIndex, int x, int y)
{
    Bool frameChanged = FALSE;
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    PsbPtr pPsb = psbPTR(pScrn);
    int newX = x, newY = y;
    int i;

    /* CHECK FOR MULTIHEAD!! */
    /* RANDR 1.2 needs fixing here too. */

    for (i = 0; i < pPsb->numCrtcs; i++) {
	xf86CrtcPtr crtc = pPsb->crtcs[i];

	if (pScrn == crtc->scrn && crtc->enabled) {

#if 0
	    ErrorF("POINTER MOVED TO %d %d %d %d %d %d\n",x,y,pScrn->frameX0,pScrn->frameY0,pScrn->frameX1,pScrn->frameY1);
#endif
	    switch (crtc->rotation) {
		case RR_Rotate_0:
		    break;
		case RR_Rotate_90:
		    newX = y;
		    newY = pScrn->pScreen->width - x - 1;
		    break;
		case RR_Rotate_180:
		    newX = pScrn->pScreen->width - x - 1;
		    newY = pScrn->pScreen->height - y - 1;
		    break;
		case RR_Rotate_270:
		    newX = pScrn->pScreen->height - y - 1;
		    newY = x;
		    break;
	    }

	    if ( pScrn->frameX0 > newX) { 
		pScrn->frameX0 = newX;
		pScrn->frameX1 = newX + crtc->mode.HDisplay - 1;
		frameChanged = TRUE;
	    }
  
	    if ( pScrn->frameX1 < newX) { 
		pScrn->frameX1 = newX + 1;
		pScrn->frameX0 = newX - crtc->mode.HDisplay + 1;
		frameChanged = TRUE;
	    }
  
	    if ( pScrn->frameY0 > newY) { 
		pScrn->frameY0 = newY;
		pScrn->frameY1 = newY + crtc->mode.VDisplay - 1;
		frameChanged = TRUE;
	    }
  
	    if ( pScrn->frameY1 < newY) { 
		pScrn->frameY1 = newY;
		pScrn->frameY0 = newY - crtc->mode.VDisplay + 1;
		frameChanged = TRUE; 
	    }

	    if (pScrn->frameX0 < 0) {
		pScrn->frameX1 -= pScrn->frameX0;
		pScrn->frameX0 = 0;
	    }

	    if (pScrn->frameY0 < 0) {
		pScrn->frameY1 -= pScrn->frameY0;
		pScrn->frameY0 = 0;
	    }
  
	    if (frameChanged && pScrn->AdjustFrame != NULL)
		psbAdjustFrame(pScrn->scrnIndex, pScrn->frameX0, pScrn->frameY0, 0);
	}
    }
}

void
psbEngineHang(ScrnInfoPtr pScrn)
{
    PsbDevicePtr pDevice = psbDevicePTR(psbPTR(pScrn));

    xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Detected 2D engine hang. "
	"0x%08x 0x%08x\n", (unsigned)PSB_RSGX32(PSB_CR_BIF_FAULT),
	(unsigned)PSB_RSGX32(PSB_CR_BIF_INT_STAT));
    FatalError("2D engine hang\n");
}
