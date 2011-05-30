#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xf86.h"
#include "xf86_OSproc.h"
#include "xf86Pci.h"
#include "xf86PciInfo.h"
#include "dgaproc.h"
#include "i810_reg.h"
#include <psb_reg.h>
#include "psb_accel.h"
#include "psb_driver.h"

static Bool PSB_OpenFramebuffer(ScrnInfoPtr, char **, unsigned char **,
				int *, int *, int *);
static Bool PSB_SetMode(ScrnInfoPtr, DGAModePtr);
static void PSB_Sync(ScrnInfoPtr);
static int PSB_GetViewport(ScrnInfoPtr);
static void PSB_SetViewport(ScrnInfoPtr, int, int, int);
static void PSB_FillRect(ScrnInfoPtr, int, int, int, int, unsigned long);
static void PSB_BlitRect(ScrnInfoPtr, int, int, int, int, int, int);

DisplayModePtr saved = NULL;
int maxx;
int maxy;

static DGAFunctionRec PSBDGAFuncs = {
    PSB_OpenFramebuffer,
    NULL,
    PSB_SetMode,
    PSB_SetViewport,
    PSB_GetViewport,
    PSB_Sync,
    PSB_FillRect,
    PSB_BlitRect,
    NULL
};

static Bool
xf86_dga_get_modes(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    DGAModePtr mode, modes;
    PsbPtr pPsb = psbPTR(pScrn);
    DisplayModePtr display_mode;
    int bpp = pScrn->bitsPerPixel >> 3;
    int num = 0;
    PsbDevicePtr pDevice = pPsb->pDevice;

    num = 0;
    display_mode = pScrn->modes;
    while (display_mode) {
	num++;
	display_mode = display_mode->next;
	if (display_mode == pScrn->modes)
	    break;
    }

    if (!num)
	return FALSE;

    modes = xalloc(num * sizeof(DGAModeRec));
    if (!modes)
	return FALSE;

    num = 0;
    display_mode = pScrn->modes;
    while (display_mode) {
	mode = modes + num++;

	mode->mode = display_mode;
	mode->flags = DGA_PIXMAP_AVAILABLE | DGA_FILL_RECT | DGA_BLIT_RECT;
	mode->byteOrder = pScrn->imageByteOrder;
	mode->depth = pScrn->depth;
	mode->bitsPerPixel = pScrn->bitsPerPixel;

	mode->red_mask = pScrn->mask.red;
	mode->green_mask = pScrn->mask.green;
	mode->blue_mask = pScrn->mask.blue;
	mode->visualClass = (bpp == 1) ? PseudoColor : TrueColor;
	mode->viewportWidth = display_mode->HDisplay;
	mode->viewportHeight = display_mode->VDisplay;
	mode->xViewportStep = (bpp == 3) ? 2 : 1;
	mode->yViewportStep = 1;
	mode->viewportFlags = 0;
	mode->offset = 0;
	mode->address = (unsigned char *)pDevice->fbMap;
	mode->bytesPerScanline = pScrn->displayWidth * bpp;
	mode->imageWidth = maxx;
	mode->imageHeight = maxy;
	mode->pixmapWidth = mode->imageWidth;
	mode->pixmapHeight = mode->imageHeight;
	mode->maxViewportX = mode->imageWidth - mode->viewportWidth;
	mode->maxViewportY = mode->imageHeight - mode->viewportHeight;

	display_mode = display_mode->next;
	if (display_mode == pScrn->modes)
	    break;
    }

    pPsb->numDGAModes = num;
    pPsb->DGAModes = modes;

    return TRUE;
}

Bool
PSBDGAReInit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    PsbPtr pPsb = psbPTR(pScrn);

    if (!xf86_dga_get_modes(pScreen))
	return FALSE;

    return DGAReInitModes(pScreen, pPsb->DGAModes, pPsb->numDGAModes);
}

typedef struct _dga_mode {
	int x;
	int y;
}dga_mode;

void PSB_Calc_Maxxy(int screensize, int * max_x, int * max_y)
{
    dga_mode standard_modes [] = {
			{640, 350},
			{640, 400},
			{720, 400},
			{640, 480},
			{800, 600},
			{1024, 768},
			{1152, 864},
			{1280, 960},
			{1280, 1024},
			{1600, 1200},
			{1792, 1344},
			{1856, 1392},
			{1920, 1440},
			{832, 624},
			{1152, 768},
			{1400, 1050},
			{1600, 1024},
			{2048, 1536},
			{0, 0}};
    int i;
    
    *max_x = 0;
    *max_y = 0;

    for (i=0; standard_modes[i].x != 0; i++) {
	if ((standard_modes[i].x * standard_modes[i].y * 2) < screensize) {
	    if (standard_modes[i].x > *max_x) *max_x = standard_modes[i].x;
	    if (standard_modes[i].y > *max_y) *max_y = standard_modes[i].y;
	}
    }

    *max_y *= 2;
}

Bool
PSBDGAInit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = pPsb->pDevice;
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    int screen_buf = pDevice->fbSize;

    xf86_config->dga_address = (unsigned long)pDevice->fbMap;
    xf86_config->dga_stride =
	pScrn->displayWidth * (pScrn->bitsPerPixel >> 3);
 /*
    maxx = pScrn->virtualX;
    maxy = pScrn->virtualY;
    if( (pDevice->fbSize / (maxx * (pScrn->bitsPerPixel >> 3)) ) > maxy)
        maxy = pDevice->fbSize / (maxx * (pScrn->bitsPerPixel >> 3));
    //xf86_config->dga_stride = maxx * (pScrn->bitsPerPixel >> 3);
 */  
    PSB_Calc_Maxxy( screen_buf/(pScrn->bitsPerPixel >> 3), &maxx, &maxy );

    xf86_config->dga_width = maxx;
    xf86_config->dga_height = maxy;

    if (!xf86_dga_get_modes(pScreen))
	return FALSE;

    return DGAInit(pScreen, &PSBDGAFuncs, pPsb->DGAModes, pPsb->numDGAModes);
}

static Bool
PSB_SetMode(ScrnInfoPtr pScrn, DGAModePtr pMode)
{
    if (!pMode) {
	if (saved) {
	    pScrn->AdjustFrame(pScrn->pScreen->myNum, 0, 0, 0);
	    xf86SwitchMode(pScrn->pScreen, saved);
	    pScrn->displayWidth = saved->HDisplay;
	    saved = NULL;
	}
    } else {
	if (!saved) {
	    saved = pScrn->currentMode;
	    xf86SwitchMode(pScrn->pScreen, pMode->mode);
	    pScrn->displayWidth = pMode->mode->HDisplay;
	    pMode->bytesPerScanline =
		pScrn->displayWidth * (pScrn->bitsPerPixel >> 3);
	    pMode->pixmapWidth = pScrn->displayWidth;
	}
    }

    return TRUE;
}

static int
PSB_GetViewport(ScrnInfoPtr pScrn)
{
    PsbPtr pPsb = psbPTR(pScrn);

    return pPsb->DGAViewportStatus;
}

static void
PSB_SetViewport(ScrnInfoPtr pScrn, int x, int y, int flags)
{
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = pPsb->pDevice;
    unsigned long Start, Offset;
    int bpp = pScrn->bitsPerPixel >> 3;

    Offset = (y * pScrn->displayWidth + x) * bpp;
    Start = 0;

    /* write both pipes as DGA is single screen only */
    PSB_WRITE32(DSPASTRIDE, pScrn->displayWidth * bpp);
    PSB_WRITE32(DSPABASE, Start + Offset);
    (void)PSB_READ32(DSPABASE);

    PSB_WRITE32(DSPBSTRIDE, pScrn->displayWidth * bpp);
    PSB_WRITE32(DSPBBASE, Start + Offset);
    (void)PSB_READ32(DSPBBASE);

    pPsb->DGAViewportStatus = 0;       /* PSBAdjustFrame loops until finished */
}

static Bool
xf86_dga_get_drawable_and_gc(ScrnInfoPtr scrn, DrawablePtr * ppDrawable,
			     GCPtr * ppGC)
{
    ScreenPtr pScreen = scrn->pScreen;
    PixmapPtr pPixmap;
    GCPtr pGC;
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(scrn);

    pPixmap = GetScratchPixmapHeader(pScreen, scrn->displayWidth, maxy,
				     scrn->depth, scrn->bitsPerPixel,
				     scrn->displayWidth *
				     (scrn->bitsPerPixel >> 3),
				     (char *)xf86_config->dga_address);
    if (!pPixmap)
	return FALSE;

    pGC = GetScratchGC(scrn->depth, pScreen);
    if (!pGC) {
	FreeScratchPixmapHeader(pPixmap);
	return FALSE;
    }
    *ppDrawable = &pPixmap->drawable;
    *ppGC = pGC;
    return TRUE;
}

static void
xf86_dga_release_drawable_and_gc(ScrnInfoPtr scrn, DrawablePtr pDrawable,
				 GCPtr pGC)
{
    FreeScratchGC(pGC);
    FreeScratchPixmapHeader((PixmapPtr) pDrawable);
}

static void
PSB_BlitRect(ScrnInfoPtr pScrn,
	     int srcx, int srcy, int w, int h, int dstx, int dsty)
{
    DrawablePtr pDrawable;
    GCPtr pGC;

    if (!xf86_dga_get_drawable_and_gc(pScrn, &pDrawable, &pGC))
	return;
    ValidateGC(pDrawable, pGC);
    pGC->ops->CopyArea(pDrawable, pDrawable, pGC, srcx, srcy, w, h, dstx,
		       dsty);
    xf86_dga_release_drawable_and_gc(pScrn, pDrawable, pGC);
}

static void
PSB_Sync(ScrnInfoPtr pScrn)
{
    ScreenPtr pScreen = pScrn->pScreen;
    WindowPtr pRoot = WindowTable[pScreen->myNum];
    char buffer[4];

    pScreen->GetImage(&pRoot->drawable, 0, 0, 1, 1, ZPixmap, ~0L, buffer);
}

static void
PSB_FillRect(ScrnInfoPtr pScrn,
	     int x, int y, int w, int h, unsigned long color)
{
    GCPtr pGC;
    DrawablePtr pDrawable;
    XID vals[1];
    xRectangle r;

    if (!xf86_dga_get_drawable_and_gc(pScrn, &pDrawable, &pGC))
	return;
    vals[0] = color;
    ChangeGC(pGC, GCForeground, vals);
    ValidateGC(pDrawable, pGC);
    r.x = x;
    r.y = y;
    r.width = w;
    r.height = h;
    pGC->ops->PolyFillRect(pDrawable, pGC, 1, &r);
    xf86_dga_release_drawable_and_gc(pScrn, pDrawable, pGC);
}

static Bool
PSB_OpenFramebuffer(ScrnInfoPtr pScrn,
		    char **name,
		    unsigned char **mem, int *size, int *offset, int *flags)
{
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = pPsb->pDevice;

    *name = NULL;
    *mem = (unsigned char *)pDevice->fbPhys;
    *size = pDevice->fbSize;
    *offset = 0;
    *flags = DGA_NEED_ROOT;

    return TRUE;
}
