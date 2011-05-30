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
#include <stdio.h>
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <psb_reg.h>
#include "psb_driver.h"
#include "psb_dri.h"
#define _XF86DRI_SERVER_
#include "GL/glxtokens.h"
#include "GL/glxint.h"
#include "sarea.h"
#define PSB_DRI_LOCK_SAREA_SIZE SAREA_MAX

#define PSB_NUM_ACCUM 2
#define PSB_NUM_STENCIL 2
#define PSB_NUM_DB 2

typedef struct
{
    int major;
    int minor;
    int patchlevel;
} PsbDRMVersion;

static char *psbKernelDriverName = "psb";
static char *psbClientDriverName = "psb";

/* The earliest version this code works with */
static const PsbDRMVersion drmExpected = { 3, 0, 0 };

/* But also compatible with the major version of : */
static const PsbDRMVersion drmCompat = { 4, 0, 0 };

static const PsbDRMVersion boExpected = { 1, 0, 0 };
static const PsbDRMVersion boCompat = { 1, 0, 0 };

static const char *driReqSymbols[] = {
    "GlxSetVisualConfigs",
    "drmAvailable",
    "DRIQueryVersion",
    NULL
};

extern void GlxSetVisualConfigs(int nconfigs,
				__GLXvisualConfig * configs,
				void **configprivs);

static Bool
psbDRICreateContext(ScreenPtr pScreen, VisualPtr visual,
		    drm_context_t hwContext, void *pVisualConfigPriv,
		    DRIContextType contextStore)
{
    return TRUE;
}

static void
psbDRIDestroyContext(ScreenPtr pScreen, drm_context_t hwContext,
		     DRIContextType contextStore)
{
}

static void
psbDRISwapContext(ScreenPtr pScreen, DRISyncType syncType,
		  DRIContextType oldContextType, void *oldContext,
		  DRIContextType newContextType, void *newContext)
{
}

static void
psbDRIInitBuffers(WindowPtr pWin, RegionPtr prgn, CARD32 index)
{
}

static void
psbDRIMoveBuffers(WindowPtr pParent, DDXPointRec ptOldOrg,
		  RegionPtr prgnSrc, CARD32 index)
{
}

static Bool
psbDRICheckVersion(ScrnInfoPtr pScrn,
		   const char *component, int major, int minor,
		   int patchlevel, const PsbDRMVersion * expected,
		   const PsbDRMVersion * compat)
{
    if ((major < expected->major) ||
	(major > compat->major) ||
	((major == expected->major) && (minor < expected->minor))) {
	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "[drm] %s is not compatible with this driver.\n"
		   "\tKernel %s version: %d.%d.%d\n"
		   "\tand I can work with versions %d.%d.x - %d.x.x\n"
		   "\tPlease update either this 2D driver or your kernel DRM.\n"
		   "\tDisabling DRI.\n",
		   component, component,
		   major, minor, patchlevel,
		   expected->major, expected->minor, compat->major);
	return FALSE;
    }
    return TRUE;
}

void
psbDRMIrqInit(PsbDevicePtr pDevice)
{
    pDevice->irq = -1;
    if (!pDevice->hasDRM)
	return;

    #if (XORG_VERSION_MINOR >= 5 || (XORG_VERSION_MINOR == 4 && XORG_VERSION_PATCH == 99))
    pDevice->irq = drmGetInterruptFromBusID(pDevice->drmFD,
					    pDevice->pciInfo->bus,
                                            pDevice->pciInfo->dev,
					    pDevice->pciInfo->func);
    #else
    pDevice->irq = drmGetInterruptFromBusID(pDevice->drmFD,
                                            pDevice->pciInfo->bus,
                                            pDevice->pciInfo->device,
                                            pDevice->pciInfo->func);
    #endif

    if ((drmCtlInstHandler(pDevice->drmFD, pDevice->irq))) {
	xf86DrvMsg(-1, X_WARNING, "[drm] Failed to install IRQ handler.\n");
	pDevice->irq = -1;
    } else {
	xf86DrvMsg(-1, X_INFO, "[drm] Irq handler installed for IRQ %d.\n",
		   pDevice->irq);
    }
}

void
psbDRMIrqTakeDown(PsbDevicePtr pDevice)
{
    if (!pDevice->hasDRM || pDevice->irq == -1)
	return;

    if (drmCtlUninstHandler(pDevice->drmFD)) {
	xf86DrvMsg(-1, X_ERROR, "[drm] Could not uninstall irq handler.\n");
    } else {
	xf86DrvMsg(-1, X_INFO, "[drm] Irq handler uninstalled.\n");
	pDevice->irq = -1;
    }
}

#if PSB_LEGACY_DRI
static void
psbDeviceLegacyDRITakedown(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);

    psbDRMIrqTakeDown(pDevice);
    if (pDevice->drmContext != -1) {
	drmDestroyContext(pDevice->drmFD, pDevice->drmContext);
    }
    pDevice->drmContext = -1;
    pDevice->drmFD = -1;
    pDevice->pLSAREA = NULL;
    pDevice->busIdString = NULL;
}

static Bool
psbDeviceLegacyDRIInit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);
    DRIInfoPtr pDRIInfo = pDRIInfo = pPsb->pDRIInfo;
    drmVersionPtr drmVer;
    unsigned int boMajor;
    unsigned int boMinor;
    unsigned int boPatchLevel;

    pDevice->drmFD = pPsb->drmFD;
    pDevice->busIdString = pDRIInfo->busIdString;
    pDevice->lockRefCount = 0;
    pDevice->pLSAREA = (void *)
	((unsigned long)DRIGetSAREAPrivate(pScreen) -
	 sizeof(XF86DRISAREARec));

    drmVer = drmGetVersion(pDevice->drmFD);
    if (!drmVer) {
	xf86DrvMsg(pScrn->scrnIndex,
		   X_ERROR, "[drm] Could not get the DRM version.\n");
	goto out_err;
    }

    pDevice->drmVerMajor = drmVer->version_major;
    pDevice->drmVerMinor = drmVer->version_minor;
    pDevice->drmVerPL = drmVer->version_patchlevel;
    drmFreeVersion(drmVer);

    if (!psbDRICheckVersion(pScrn, "DRM", pDevice->drmVerMajor,
			    pDevice->drmVerMinor,
			    pDevice->drmVerPL, &drmExpected, &drmCompat))
	goto out_err;

    if (drmBOVersion(pDevice->drmFD, &boMajor, &boMinor, &boPatchLevel)
	!= 0) {
	xf86DrvMsg(pScrn->scrnIndex,
		   X_ERROR, "[drm] Could not get the DRM "
		   "Buffer manager version: %s\n", strerror(errno));
	goto out_err;
    }

    if (!psbDRICheckVersion(pScrn, "Buffer Manager", boMajor,
			    boMinor, boPatchLevel, &boExpected, &boCompat))
	goto out_err;

    if (drmCreateContext(pDevice->drmFD, &pDevice->drmContext) < 0) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "[drm] Could not create a drm device context.\n");
	goto out_err;
    } else {
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "[drm] Allocated device DRM context %d.\n",
		   (int)pDevice->drmContext);
    }

    pDevice->hasDRM = TRUE;
    psbDRMIrqInit(pDevice);
    return TRUE;
  out_err:
    psbDeviceLegacyDRITakedown(pScreen);
    return FALSE;
}
#else

void
psbDRMDeviceTakeDown(PsbDevicePtr pDevice)
{
    psbDRMIrqTakeDown(pDevice);
    if (pDevice->drmContext != -1) {
	drmDestroyContext(pDevice->drmFD, pDevice->drmContext);
	pDevice->drmContext = -1;
    }
    if (pDevice->drmFD != -1) {
	drmClose(pDevice->drmFD);
	pDevice->drmFD = -1;
    }
    if (pDevice->busIdString != NULL) {
	xfree(pDevice->busIdString);
	pDevice->busIdString = NULL;
    }
}

Bool
psbDRMDeviceInit(PsbDevicePtr pDevice)
{
    drmVersionPtr drmVer;
    unsigned int boMajor;
    unsigned int boMinor;
    unsigned int boPatchLevel;

    pDevice->drmFD = -1;
    pDevice->busIdString = NULL;
    pDevice->drmContext = -1;

    pDevice->busIdString = (char *)xcalloc(1, 64);
    if (!pDevice->busIdString)
	goto out_err;

   #if (XORG_VERSION_MINOR >= 5 || (XORG_VERSION_MINOR == 4 && XORG_VERSION_PATCH == 99))
    sprintf(pDevice->busIdString, "PCI:%d:%d:%d",
	    pDevice->pciInfo->bus,
	    pDevice->pciInfo->dev, pDevice->pciInfo->func);
   #else
    sprintf(pDevice->busIdString, "PCI:%d:%d:%d",
           pDevice->pciInfo->bus,
           pDevice->pciInfo->device, pDevice->pciInfo->func);
   #endif

    if (!DRIOpenDRMMaster(pDevice->pScrns[0],
			  PSB_DRI_LOCK_SAREA_SIZE, pDevice->busIdString,
			  psbKernelDriverName))
	goto out_err;

    pDevice->drmFD = DRIMasterFD(pDevice->pScrns[0]);
    pDevice->pLSAREA = DRIMasterSareaPointer(pDevice->pScrns[0]);
    pDevice->lockRefCount = 0;

    drmVer = drmGetVersion(pDevice->drmFD);
    if (!drmVer) {
	xf86DrvMsg(-1, X_ERROR, "[drm] Could not get the DRM version.\n");
	goto out_err;
    }

    pDevice->drmVerMajor = drmVer->version_major;
    pDevice->drmVerMinor = drmVer->version_minor;
    pDevice->drmVerPL = drmVer->version_patchlevel;
    drmFreeVersion(drmVer);

    if (!psbDRICheckVersion(pScrn, "DRM", pDevice->drmVerMajor,
			    pDevice->drmVerMinor,
			    pDevice->drmVerPL, &drmExpected, &drmCompat))
	goto out_err;

    if (drmBOVersion(pDevice->drmFD, &boMajor, &boMinor, &boPatchLevel)
	!= 0) {
	xf86DrvMsg(pScrn->scrnIndex,
		   X_ERROR, "[drm] Could not get the DRM "
		   "Buffer manager version.\n");
	goto out_err;
    }

    if (!psbDRICheckVersion(pScrn, "Buffer Manager", boMajor,
			    boMinor, boPatchLevel, &boExpected, &boCompat))
	goto out_err;

    /*
     * Create a device context.
     */

    if (drmCreateContext(pDevice->drmFD, &pDevice->drmContext) < 0) {
	xf86DrvMsg(-1, X_ERROR,
		   "[drm] Could not create a drm device context.\n");
	goto out_err;
    } else {
	xf86DrvMsg(-1, X_INFO,
		   "[drm] Allocated device DRM context %d.\n",
		   (int)pDevice->drmContext);
    }

    pDevice->hasDRM = TRUE;
    psbDRMIrqInit(pDevice);
    return TRUE;
  out_err:
    psbDRMDeviceTakeDown(pDevice);
    return FALSE;
}
#endif

static void
psbFreeConfigRec(PsbGLXConfigPtr pGLX)
{
    if (pGLX->pConfigs) {
	xfree(pGLX->pConfigs);
	pGLX->pConfigs = NULL;
    }
    if (pGLX->pPsbConfigs) {
	xfree(pGLX->pPsbConfigs);
	pGLX->pPsbConfigs = NULL;
    }
    if (pGLX->pPsbConfigPtrs) {
	xfree(pGLX->pPsbConfigPtrs);
	pGLX->pPsbConfigPtrs = NULL;
    }
}

static Bool
psbAllocConfigRec(int numConfigs, PsbGLXConfigPtr pGLX)
{
    if (!(pGLX->pConfigs = (__GLXvisualConfig *)
	  xcalloc(sizeof(__GLXvisualConfig), numConfigs)))
	goto out_err;
    if (!(pGLX->pPsbConfigs = (PsbConfigPrivPtr)
	  xcalloc(sizeof(PsbConfigPrivRec), numConfigs)))
	goto out_err;
    if (!(pGLX->pPsbConfigPtrs = (PsbConfigPrivPtr *)
	  xcalloc(sizeof(PsbConfigPrivPtr), numConfigs)))
	goto out_err;
    pGLX->numConfigs = numConfigs;

    return TRUE;
  out_err:
    psbFreeConfigRec(pGLX);
    return FALSE;
}

static void
psbSetupStencil(int stencil, __GLXvisualConfig * pConfigs)
{
    switch (stencil) {
    case 0:
	pConfigs->depthSize = 24;
	pConfigs->stencilSize = 8;
	break;
#if 0
    case 1:
	pConfigs->depthSize = 16;
	pConfigs->stencilSize = 0;
	break;
#endif
    case 1:
	pConfigs->depthSize = 0;
	pConfigs->stencilSize = 0;
	break;
    }
}

static void
psbSetupAccum(int accum, Bool alpha, __GLXvisualConfig * pConfigs)
{
    if (accum) {
	pConfigs->accumRedSize = 16;
	pConfigs->accumGreenSize = 16;
	pConfigs->accumBlueSize = 16;
	pConfigs->accumAlphaSize = (alpha) ? 16 : 0;
	pConfigs->visualRating = GLX_SLOW_VISUAL_EXT;
    } else {
	pConfigs->accumRedSize = 0;
	pConfigs->accumGreenSize = 0;
	pConfigs->accumBlueSize = 0;
	pConfigs->accumAlphaSize = 0;
	pConfigs->visualRating = GLX_NONE_EXT;
    }
}

static void
psbSetupDB(int db, __GLXvisualConfig * pConfigs)
{
    pConfigs->doubleBuffer = ((db == 0));
}

static void
psbSetupInitial(__GLXvisualConfig * pConfigs)
{
    pConfigs->vid = -1;
    pConfigs->class = -1;
    pConfigs->rgba = TRUE;
    pConfigs->redSize = -1;
    pConfigs->greenSize = -1;
    pConfigs->blueSize = -1;
    pConfigs->redMask = -1;
    pConfigs->greenMask = -1;
    pConfigs->blueMask = -1;
    pConfigs->alphaSize = 0;
    pConfigs->alphaMask = 0;
    pConfigs->stereo = FALSE;
    pConfigs->bufferSize = -1;
    pConfigs->auxBuffers = 0;
    pConfigs->level = 0;
    pConfigs->transparentPixel = GLX_NONE_EXT;
    pConfigs->transparentRed = 0;
    pConfigs->transparentGreen = 0;
    pConfigs->transparentBlue = 0;
    pConfigs->transparentAlpha = 0;
    pConfigs->transparentIndex = 0;
}

static Bool
psbInitVisualConfigs(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    PsbPtr pPsb = psbPTR(pScrn);
    int numConfigs = 0;
    PsbGLXConfigRec psbGLX;
    __GLXvisualConfig *pConfigs;
    int i, db, stencil, accum;

    memset(&psbGLX, 0, sizeof(psbGLX));

    switch (pScrn->depth) {
    case 8:
	break;
    case 15:
    case 16:
	numConfigs = PSB_NUM_ACCUM * PSB_NUM_STENCIL * PSB_NUM_DB;
	if (!psbAllocConfigRec(numConfigs, &psbGLX))
	    return FALSE;

	i = 0;
	for (accum = 0; accum < PSB_NUM_ACCUM; accum++) {
	    for (stencil = 0; stencil < PSB_NUM_STENCIL; stencil++) {
		for (db = 0; db < PSB_NUM_DB; db++) {
		    pConfigs = &psbGLX.pConfigs[i];
		    psbSetupInitial(pConfigs);
		    psbSetupDB(db, pConfigs);
		    psbSetupStencil(stencil, pConfigs);
		    psbSetupAccum(accum, FALSE, pConfigs);
		    i++;
		}
	    }
	}
	break;
    case 24:
    case 32:
	numConfigs = PSB_NUM_ACCUM * PSB_NUM_STENCIL * PSB_NUM_DB;
	if (!psbAllocConfigRec(numConfigs, &psbGLX))
	    return FALSE;

	i = 0;
	for (accum = 0; accum < PSB_NUM_ACCUM; accum++) {
	    for (stencil = 0; stencil < PSB_NUM_STENCIL; stencil++) {
		for (db = 0; db < PSB_NUM_DB; db++) {
		    pConfigs = &psbGLX.pConfigs[i];
		    psbSetupInitial(pConfigs);
		    pConfigs->alphaSize = 8;
		    pConfigs->alphaMask = 0xFF000000;
		    psbSetupDB(db, pConfigs);
		    psbSetupStencil(stencil, pConfigs);
		    psbSetupAccum(accum, TRUE, pConfigs);
		    i++;
		}
	    }
	}
	break;
    }

    pPsb->glx = psbGLX;
    GlxSetVisualConfigs(numConfigs, psbGLX.pConfigs,
			(void **)psbGLX.pPsbConfigPtrs);
    return TRUE;
}

Bool
psbDRIScreenInit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);
    DRIInfoPtr pDRIInfo;
    PsbDRIPtr pPsbDRI;
    int major, minor, patch;
    pciVideoPtr pciInfo;

    PSB_DEBUG(pScrn->scrnIndex, 3, "psbDRIScreenInit\n");

    xf86LoaderReqSymLists(driReqSymbols, NULL);

    pPsb->pDRIInfo = NULL;

    /*
     * Make sure we have recent versions of required modules.
     */

    DRIQueryVersion(&major, &minor, &patch);
    if (major != DRIINFO_MAJOR_VERSION || minor < DRIINFO_MINOR_VERSION) {
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[dri] Incorrect Xserver libdri version %d.%d.%d.\n"
		   "\tVersion %d.%d.x needed. Disabling dri.\n",
		   major, minor, patch,
		   DRIINFO_MAJOR_VERSION, DRIINFO_MINOR_VERSION);
	goto out_err;
    }

    pPsb->pDRIInfo = DRICreateInfoRec();
    if (!pPsb->pDRIInfo) {
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[dri] Failed allocating DRIInfo structure. Disabling DRI.\n");
	goto out_err;
    }

    pDRIInfo = pPsb->pDRIInfo;
    pDRIInfo->drmDriverName = psbKernelDriverName;
    /* if Xpsb not available hack it that there's no driver available */
    if (!pPsb->noAccel && pPsb->xpsb)
	pDRIInfo->clientDriverName = psbClientDriverName;
    else
	pDRIInfo->clientDriverName = NULL;
    pDRIInfo->busIdString = xalloc(64);
    
    #if (XORG_VERSION_MINOR >= 5 || (XORG_VERSION_MINOR == 4 && XORG_VERSION_PATCH == 99))
    sprintf(pDRIInfo->busIdString, "PCI:%d:%d:%d",
	    pDevice->pciInfo->bus,
	    pDevice->pciInfo->dev, pDevice->pciInfo->func);
    #else
    sprintf(pDRIInfo->busIdString, "PCI:%d:%d:%d",
            pDevice->pciInfo->bus,
            pDevice->pciInfo->device, pDevice->pciInfo->func);
    #endif

    pDRIInfo->ddxDriverMajorVersion = PSB_DRIDDX_VERSION_MAJOR;
    pDRIInfo->ddxDriverMinorVersion = PSB_DRIDDX_VERSION_MINOR;
    pDRIInfo->ddxDriverPatchVersion = PSB_DRIDDX_VERSION_PATCH;

    /*
     * These are now passed in the sarea for RandR compatibility.
     * pass fake values.
     */

    pDRIInfo->frameBufferPhysicalAddress = (void *)pDevice->fbPhys;
    pDRIInfo->frameBufferSize = 4096;
    pDRIInfo->frameBufferStride = 4096;

    pDRIInfo->ddxDrawableTableEntry = SAREA_MAX_DRAWABLES;
    pDRIInfo->maxDrawableTableEntry = SAREA_MAX_DRAWABLES;
    pDRIInfo->SAREASize =
	(sizeof(XF86DRISAREARec) + sizeof(struct drm_psb_sarea));
    pDRIInfo->SAREASize = ALIGN_TO(pDRIInfo->SAREASize, PSB_PAGE_SIZE);

    /*
     * Mesa always maps SAREA_MAX bytes, so we need to make sure
     * the sarea is at least this size.
     */

    pDRIInfo->SAREASize = (pDRIInfo->SAREASize > SAREA_MAX) ?
	pDRIInfo->SAREASize : SAREA_MAX;

    PSB_DEBUG(pScrn->scrnIndex, 3, "SAREA size is %ld\n",
	      pDRIInfo->SAREASize);

    pDRIInfo->devPrivate = NULL;
    if (!(pPsbDRI = (PsbDRIPtr) xcalloc(sizeof(*pPsbDRI), 1))) {
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[dri] Failed allocating DRI driver structure. Disabling DRI.\n");
	goto out_err;
    }

    pPsbDRI->pciVendor = PCI_VENDOR_INTEL;
    pciInfo = xf86GetPciInfoForEntity(pDevice->pEnt->index);
    #if (XORG_VERSION_MINOR >= 5 || (XORG_VERSION_MINOR == 4 && XORG_VERSION_PATCH == 99))
        pPsbDRI->pciDevice = pciInfo->device_id;
    #else
        pPsbDRI->pciDevice = pciInfo->chipType;
    #endif

    pPsbDRI->lockSAreaSize = PSB_DRI_LOCK_SAREA_SIZE;
#if PSB_LEGACY_DRI
    pPsbDRI->lockSAreaHandle = 0;
#else
    pPsbDRI->lockSAreaHandle = DRIMasterSareaHandle(pScrn);
    pDRIInfo->keepFDOpen = TRUE;
#endif
    pPsbDRI->sAreaSize = pDRIInfo->SAREASize;
    pPsbDRI->sAreaPrivOffset = sizeof(XF86DRISAREARec);
    pPsbDRI->cpp = pScrn->bitsPerPixel >> 3;
    pPsbDRI->exaBufHandle = 0;

    pDRIInfo->devPrivate = pPsbDRI;
    pDRIInfo->devPrivateSize = sizeof(*pPsbDRI);
    pDRIInfo->contextSize = sizeof(PsbDRIContextRec);

    pDRIInfo->CreateContext = psbDRICreateContext;
    pDRIInfo->DestroyContext = psbDRIDestroyContext;
    pDRIInfo->SwapContext = psbDRISwapContext;
    pDRIInfo->InitBuffers = psbDRIInitBuffers;
    pDRIInfo->MoveBuffers = psbDRIMoveBuffers;
    pDRIInfo->bufferRequests = DRI_ALL_WINDOWS;

#if (XORG_VERSION_MINOR == 4 && XORG_VERSION_PATCH != 99)
    pDRIInfo->driverSwapMethod = DRI_KERNEL_SWAP;
#endif

#if DRIINFO_MAJOR_VERSION > 5 || \
    (DRIINFO_MAJOR_VERSION == 5 && DRIINFO_MINOR_VERSION >= 3)
      if (!pPsb->shadowFB && !pPsb->noAccel && pDevice->hasDRM)
	 pDRIInfo->texOffsetStart = psbTexOffsetStart;
#endif

    if (!DRIScreenInit(pScreen, pDRIInfo, &pPsb->drmFD)) {
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[dri] DRIScreenInit failed. Disabling DRI.\n");
	goto out_err;
    }
#if (XORG_VERSION_MINOR == 4 && XORG_VERSION_PATCH != 99)
    pDRIInfo->driverSwapMethod = DRI_HIDE_X_CONTEXT;
#endif

    if (!psbInitVisualConfigs(pScreen)) {
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[dri] Could not initialize GLX visuals. Disabling DRI.\n");
	goto out_err;
    }
#if PSB_LEGACY_DRI
    if (!psbDeviceLegacyDRIInit(pScreen))
	goto out_err;
#endif

    return TRUE;

  out_err:
    psbDRICloseScreen(pScreen);
    return FALSE;
}

Bool
psbDRIFinishScreenInit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    PsbPtr pPsb = psbPTR(pScrn);

#if (!(XORG_VERSION_MINOR == 4 && XORG_VERSION_PATCH != 99))
    /* avoid install sigio handler here */
    pPsb->pDRIInfo->driverSwapMethod = DRI_KERNEL_SWAP;
#endif

    if (!DRIFinishScreenInit(pScreen))
	goto out_err;

#if (!(XORG_VERSION_MINOR == 4 && XORG_VERSION_PATCH != 99))
    /* now put it back to X, for the blockhandlers to work */
    pPsb->pDRIInfo->driverSwapMethod = DRI_HIDE_X_CONTEXT;
#endif

    pPsb->driEnabled = TRUE;
    psbDRIUpdateScanouts(pScrn);
    return TRUE;

  out_err:
    psbDRICloseScreen(pScreen);
    return FALSE;
}

void
psbDRICloseScreen(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDRIPtr pPsbDRI;

#if PSB_LEGACY_DRI
    psbDeviceLegacyDRITakedown(pScreen);
#endif

    if (pPsb->driEnabled)
	DRICloseScreen(pScreen);

    if (pPsb->pDRIInfo) {
	if ((pPsbDRI = (PsbDRIPtr) pPsb->pDRIInfo->devPrivate)) {
	    xfree(pPsbDRI);
	    pPsb->pDRIInfo->devPrivate = NULL;
	}
	DRIDestroyInfoRec(pPsb->pDRIInfo);
	pPsb->pDRIInfo = NULL;
    }
    pPsb->drmFD = -1;

    pPsb->glx.pPsbConfigPtrs = NULL;   /* This one is freed elsewhere ? */
    psbFreeConfigRec(&pPsb->glx);
}

void
psbDRILock(ScrnInfoPtr pScrn, int flags)
{
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);
    PsbPtr pPsb0 = psbPTR(pDevice->pScrns[0]);

    if (pPsb0->driEnabled) {
	DRILock(pDevice->pScrns[0]->pScreen, flags);
	return;
    } else {
	if (pDevice->hasDRM) {
	    if (!pDevice->lockRefCount)
		DRM_LOCK(pDevice->drmFD, pDevice->pLSAREA,
			 pDevice->drmContext, flags);
	    pDevice->lockRefCount++;
	}
    }
}

void
psbDRIUnlock(ScrnInfoPtr pScrn)
{
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);
    PsbPtr pPsb0 = psbPTR(pDevice->pScrns[0]);

    if (pPsb0->driEnabled) {
	DRIUnlock(pDevice->pScrns[0]->pScreen);
	return;
    } else {
	if (pDevice->hasDRM) {
	    if (pDevice->lockRefCount > 0)
		--pDevice->lockRefCount;
	    else {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "psbDRIUnlock called when not locked.\n");
		return;
	    }
	    if (!pDevice->lockRefCount)
		DRM_UNLOCK(pDevice->drmFD, pDevice->pLSAREA,
			   pDevice->drmContext);
	}
    }
}

void
psbDRIUpdateScanouts(ScrnInfoPtr pScrn)
{
    PsbPtr pPsb = psbPTR(pScrn);
    MMListHead *list;
    PsbScanoutPtr scanOut;
    int index = 0;
    struct drm_psb_scanout *drmScanOut;
    struct drm_psb_sarea *pSarea;
    struct _MMBuffer *buf;

    PSB_DEBUG(pScrn->scrnIndex, 3, "PsbDRIUpdateScanouts\n");

    if (!pPsb->driEnabled)
	return;

    pSarea = (struct drm_psb_sarea *)DRIGetSAREAPrivate(pScrn->pScreen);

    mmListForEach(list, &pPsb->sAreaList) {
	scanOut = mmListEntry(list, PsbScanoutRec, sAreaList);
	drmScanOut = &pSarea->scanouts[index];
	buf = psbScanoutBuf(scanOut);

	drmScanOut->buffer_id = buf->man->bufHandle(buf);
	drmScanOut->rotation = psbScanoutRotation(scanOut);
	drmScanOut->stride = psbScanoutStride(scanOut);
	drmScanOut->depth = psbScanoutDepth(scanOut);
	drmScanOut->width = psbScanoutWidth(scanOut);
	drmScanOut->height = psbScanoutHeight(scanOut);

	PSB_DEBUG(pScrn->scrnIndex, 3,
		  "Buffer %d rotation %d handle 0x%08x\n", index,
		  drmScanOut->rotation, drmScanOut->buffer_id);

	++index;
    }

    pSarea->num_scanouts = index;
}
