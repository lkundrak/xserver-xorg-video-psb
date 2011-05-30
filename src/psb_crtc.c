/*
 * Copyright ?2006-2007 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *    Thomas Hellstrom <thomas-at-tungstengraphics-dot-com>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include <psb_reg.h>
#include "xf86.h"
#include "i810_reg.h"
#include "psb_driver.h"
#include "xf86Modes.h"
#include "psb_lvds.h"
#include "psb_overlay.h"

typedef struct
{
    int min, max;
} intel_range_t;

typedef struct
{
    int dot_limit;
    int p2_slow, p2_fast;
} intel_p2_t;

#define INTEL_P2_NUM		      2

typedef struct
{
    intel_range_t dot, vco, n, m, m1, m2, p, p1;
    intel_p2_t p2;
} intel_limit_t;

#define I9XX_DOT_MIN		  20000
#define I9XX_DOT_MAX		 400000
#define I9XX_VCO_MIN		1400000
#define I9XX_VCO_MAX		2800000
#define I9XX_N_MIN		      3
#define I9XX_N_MAX		      8
#define I9XX_M_MIN		     70
#define I9XX_M_MAX		    120
#define I9XX_M1_MIN		     10
#define I9XX_M1_MAX		     20
#define I9XX_M2_MIN		      5
#define I9XX_M2_MAX		      9
#define I9XX_P_SDVO_DAC_MIN	      5
#define I9XX_P_SDVO_DAC_MAX	     80
#define I9XX_P_LVDS_MIN		      7
#define I9XX_P_LVDS_MAX		     98
#define I9XX_P1_MIN		      1
#define I9XX_P1_MAX		      8
#define I9XX_P2_SDVO_DAC_SLOW		     10
#define I9XX_P2_SDVO_DAC_FAST		      5
#define I9XX_P2_SDVO_DAC_SLOW_LIMIT	 200000
#define I9XX_P2_LVDS_SLOW		     14
#define I9XX_P2_LVDS_FAST		      7
#define I9XX_P2_LVDS_SLOW_LIMIT		 112000

#define INTEL_LIMIT_I9XX_SDVO_DAC   0
#define INTEL_LIMIT_I9XX_LVDS	    1

#define HWCURSOR_SIZE 4096
#define HWCURSOR_SIZE_ARGB 4*HWCURSOR_SIZE

static const intel_limit_t intel_limits[] = {
    {				       /* INTEL_LIMIT_I9XX_SDVO_DAC */
     .dot = {.min = I9XX_DOT_MIN,.max = I9XX_DOT_MAX},
     .vco = {.min = I9XX_VCO_MIN,.max = I9XX_VCO_MAX},
     .n = {.min = I9XX_N_MIN,.max = I9XX_N_MAX},
     .m = {.min = I9XX_M_MIN,.max = I9XX_M_MAX},
     .m1 = {.min = I9XX_M1_MIN,.max = I9XX_M1_MAX},
     .m2 = {.min = I9XX_M2_MIN,.max = I9XX_M2_MAX},
     .p = {.min = I9XX_P_SDVO_DAC_MIN,.max = I9XX_P_SDVO_DAC_MAX},
     .p1 = {.min = I9XX_P1_MIN,.max = I9XX_P1_MAX},
     .p2 = {.dot_limit = I9XX_P2_SDVO_DAC_SLOW_LIMIT,
	    .p2_slow = I9XX_P2_SDVO_DAC_SLOW,.p2_fast =
	    I9XX_P2_SDVO_DAC_FAST},
     },
    {				       /* INTEL_LIMIT_I9XX_LVDS */
     .dot = {.min = I9XX_DOT_MIN,.max = I9XX_DOT_MAX},
     .vco = {.min = I9XX_VCO_MIN,.max = I9XX_VCO_MAX},
     .n = {.min = I9XX_N_MIN,.max = I9XX_N_MAX},
     .m = {.min = I9XX_M_MIN,.max = I9XX_M_MAX},
     .m1 = {.min = I9XX_M1_MIN,.max = I9XX_M1_MAX},
     .m2 = {.min = I9XX_M2_MIN,.max = I9XX_M2_MAX},
     .p = {.min = I9XX_P_LVDS_MIN,.max = I9XX_P_LVDS_MAX},
     .p1 = {.min = I9XX_P1_MIN,.max = I9XX_P1_MAX},
     /* The single-channel range is 25-112Mhz, and dual-channel
      * is 80-224Mhz.  Prefer single channel as much as possible.
      */
     .p2 = {.dot_limit = I9XX_P2_LVDS_SLOW_LIMIT,
	    .p2_slow = I9XX_P2_LVDS_SLOW,.p2_fast = I9XX_P2_LVDS_FAST},
     },
};

static Bool psbPipeHasType(xf86CrtcPtr crtc, int type);

static const intel_limit_t *
intel_limit(xf86CrtcPtr crtc)
{
    const intel_limit_t *limit;

    if (psbPipeHasType(crtc, PSB_OUTPUT_LVDS))
	limit = &intel_limits[INTEL_LIMIT_I9XX_LVDS];
    else
	limit = &intel_limits[INTEL_LIMIT_I9XX_SDVO_DAC];

    return limit;
}

/** Derive the pixel clock for the given refclk and divisors for 8xx chips. */

static void
intel_clock(int refclk, intel_clock_t * clock)
{
    clock->m = 5 * (clock->m1 + 2) + (clock->m2 + 2);
    clock->p = clock->p1 * clock->p2;
    clock->vco = refclk * clock->m / (clock->n + 2);
    clock->dot = clock->vco / clock->p;
}

void
psbPrintPll(int scrnIndex, char *prefix, intel_clock_t * clock)
{
    PSB_DEBUG(scrnIndex, 3,
	      "%s: dotclock %d vco %d ((m %d, m1 %d, m2 %d), n %d, (p %d, p1 %d, p2 %d))\n",
	      prefix, clock->dot, clock->vco, clock->m, clock->m1, clock->m2,
	      clock->n, clock->p, clock->p1, clock->p2);
}

/**
 * Returns whether any output on the specified pipe is of the specified type
 */
static Bool
psbPipeHasType(xf86CrtcPtr crtc, int type)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    int i;

    for (i = 0; i < xf86_config->num_output; i++) {
	xf86OutputPtr output = xf86_config->output[i];

	if (output->crtc == crtc) {
	    PsbOutputPrivatePtr intel_output = output->driver_private;

	    if (intel_output->type == type)
		return TRUE;
	}
    }
    return FALSE;
}

#define psbPllInvalid(s)   { /* ErrorF (s) */; return FALSE; }
/**
 * Returns whether the given set of divisors are valid for a given refclk with
 * the given outputs.
 */

static Bool
psbPllIsValid(xf86CrtcPtr crtc, intel_clock_t * clock)
{
    const intel_limit_t *limit = intel_limit(crtc);

    if (clock->p1 < limit->p1.min || limit->p1.max < clock->p1)
	psbPllInvalid("p1 out of range\n");
    if (clock->p < limit->p.min || limit->p.max < clock->p)
	psbPllInvalid("p out of range\n");
    if (clock->m2 < limit->m2.min || limit->m2.max < clock->m2)
	psbPllInvalid("m2 out of range\n");
    if (clock->m1 < limit->m1.min || limit->m1.max < clock->m1)
	psbPllInvalid("m1 out of range\n");
    if (clock->m1 <= clock->m2)
	psbPllInvalid("m1 <= m2\n");
    if (clock->m < limit->m.min || limit->m.max < clock->m)
	psbPllInvalid("m out of range\n");
    if (clock->n < limit->n.min || limit->n.max < clock->n)
	psbPllInvalid("n out of range\n");
    if (clock->vco < limit->vco.min || limit->vco.max < clock->vco)
	psbPllInvalid("vco out of range\n");
    /* XXX: We may need to be checking "Dot clock" depending on the multiplier,
     * output, etc., rather than just a single range.
     */
    if (clock->dot < limit->dot.min || limit->dot.max < clock->dot)
	psbPllInvalid("dot out of range\n");

    return TRUE;
}

/**
 * Returns a set of divisors for the desired target clock with the given refclk,
 * or FALSE.  Divisor values are the actual divisors for
 */
static Bool
psbFindBestPLL(xf86CrtcPtr crtc, int target, int refclk,
	       intel_clock_t * best_clock)
{
    intel_clock_t clock;
    const intel_limit_t *limit = intel_limit(crtc);
    int err = target;

    if (target < limit->p2.dot_limit)
	clock.p2 = limit->p2.p2_slow;
    else
	clock.p2 = limit->p2.p2_fast;

    memset(best_clock, 0, sizeof(*best_clock));

    for (clock.m1 = limit->m1.min; clock.m1 <= limit->m1.max; clock.m1++) {
	for (clock.m2 = limit->m2.min;
	     clock.m2 < clock.m1 && clock.m2 <= limit->m2.max; clock.m2++) {
	    for (clock.n = limit->n.min; clock.n <= limit->n.max; clock.n++) {
		for (clock.p1 = limit->p1.min; clock.p1 <= limit->p1.max;
		     clock.p1++) {
		    int this_err;

		    intel_clock(refclk, &clock);

		    if (!psbPllIsValid(crtc, &clock))
			continue;

		    this_err = abs(clock.dot - target);
		    if (this_err < err) {
			*best_clock = clock;
			err = this_err;
		    }
		}
	    }
	}
    }
    return (err != target);
}

void
psbWaitForVblank(ScrnInfoPtr pScrn)
{
    /* Wait for 20ms, i.e. one cycle at 50hz. */
    usleep(20000);
}

void
psbPipeSetBase(xf86CrtcPtr crtc, int x, int y)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);
    PsbCrtcPrivatePtr pCrtc = crtc->driver_private;
    int pipe = pCrtc->pipe;
    unsigned long Start, Offset;
    int dspbase = (pipe == 0 ? DSPABASE : DSPBBASE);
    int dspstride_reg = (pipe == 0) ? DSPASTRIDE : DSPBSTRIDE;

    PSB_DEBUG(pScrn->scrnIndex, 3, "psbPipeSetBase\n");

    if (!pPsb->front) {
	/* During startup we may be called as part of monitor detection while
	 * there is no memory allocation done, so just supply a dummy base
	 * address.
	 */
	Offset = 0;
	Start = 0;
    } else if (crtc->rotatedData != NULL) {
	PSB_DEBUG(pScrn->scrnIndex, 3, "Rotated base\n");
	/* offset is done by shadow painting code, not here */
	Start = psbScanoutOffset(pCrtc->rotate);
	Offset = 0;
    } else {
	Offset = ((y * pScrn->displayWidth + x) * psbScanoutCpp(pPsb->front));
	Start = psbScanoutOffset(pPsb->front);
    }

    /* Update stride as Resize can cause the pitch to re-adjust */
    PSB_WRITE32(dspstride_reg, (crtc->rotatedData != NULL) ?
		psbScanoutStride(pCrtc->rotate) : psbScanoutStride(pPsb->
								   front));

    PSB_WRITE32(dspbase, Start + Offset);
    (void)PSB_READ32(dspbase);
}

static U32
psbGetMemClock(void)
{
    //calculate memory access timing
    PCITAG host = pciTag(0, 0, 0);
    CARD32 clock;

#if (XORG_VERSION_MINOR >= 5 || (XORG_VERSION_MINOR == 4 && XORG_VERSION_PATCH == 99))
    struct pci_device dev;

    dev.domain = PCI_DOM_FROM_TAG(host);
    dev.bus = PCI_BUS_FROM_TAG(host);
    dev.dev = PCI_DEV_FROM_TAG(host);
    dev.func = PCI_FUNC_FROM_TAG(host);

    pci_device_cfg_write_u32(&dev, PCI_PORT5_REGISTER3, PCI_DEV0_FUNC0_MCR_WRITE);
    pci_device_cfg_read_u32(&dev, &clock, PCI_DEV0_FUNC0_MDR_READ);
#else
    pciWriteLong(host, PCI_DEV0_FUNC0_MDR_READ, 0x00C32004);
    pciWriteLong(host, PCI_DEV0_FUNC0_MCR_WRITE, 0xE0033000);
    pciWriteLong(host, PCI_DEV0_FUNC0_MCR_WRITE, PCI_PORT5_REGISTER3);
    clock = pciReadLong(host, PCI_DEV0_FUNC0_MDR_READ);
#endif

    if ((clock & 0x80) >> 3)
        return MEM_CTRL_CLK_133;
    else
        return MEM_CTRL_CLK_100;
}

static U32 
psbCalculateWaterMark(U32 in_start, Watermark_Type whichWM, U32 bpp, CePlane plane, U32 pixelClock, BOOL duel)
{
	// watermark values
	U32 queueSize = MEMORY_QUEUE_SIZE;
	U32 queueItemLatency = 0;
	U32 latencySRExittoData = 0;
	U32 SRExitLatency = SELF_REFRESH_EXIT_LATENCY;
	U32 pixelClk = pixelClock;
	U32 pixelClk_MemContClkRatio = 0;
	U32 bitsPerCacheLine = BITS_PER_CACHE_LINE;
	U32 bitsPerPixel = bpp;
	U32 pixelsPerCacheLine = 1;
	U32 watermark = 0;
	U32 data;

	// Get the memory controller clock value
	U32 memoryContClk = psbGetMemClock();

	if (plane == DCplane_A)
	{
		queueItemLatency = MAX_FIFO_A_QITEM_LATENCY;
		if (duel)
		{
			queueItemLatency = 15;
		}
	}
	else
	{
		queueItemLatency = MAX_FIFO_B_QITEM_LATENCY;
	}

	latencySRExittoData = queueSize * queueItemLatency;
	pixelClk_MemContClkRatio = (pixelClk * WATERMARK_PRECISION) / memoryContClk;
	pixelsPerCacheLine = bitsPerCacheLine / bitsPerPixel;

	switch (whichWM)
	{
		case WATERMARK_2:
			watermark = in_start - (((latencySRExittoData * (pixelClk_MemContClkRatio/pixelsPerCacheLine)) + WATERMARK_ROUNDING) / WATERMARK_PRECISION);
			break;
		case WATERMARK_1:
			watermark = in_start - (((SRExitLatency * (pixelClk_MemContClkRatio/pixelsPerCacheLine)) + WATERMARK_ROUNDING) / WATERMARK_PRECISION) - 
						(((latencySRExittoData * (pixelClk_MemContClkRatio/pixelsPerCacheLine)) + WATERMARK_ROUNDING) / WATERMARK_PRECISION);
			break;
		default:
			return 0;
	}

	return watermark;
}

static void
psbSetWatermarks(ScrnInfoPtr pScrn)
{
	PsbDevicePtr pDevice = psbDevicePTR(psbPTR(pScrn));
	xf86OutputPtr output;
	xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
	
	Bool bPlaneAEnabled = FALSE;
	Bool bPlaneBEnabled = FALSE;
	int BPPA = 0;
	int BPPB = 0;
	unsigned long DotClockA = 0;
	unsigned long DotClockB = 0;
	CARD32 PlaneACtrl = PSB_READ32(DSPACNTR);
	CARD32 PlaneBCtrl = PSB_READ32(DSPBCNTR);
	CARD32 DisplayARBReg = PSB_READ32(DSPARB);

	if (PlaneACtrl & DISPLAY_PLANE_ENABLE)
		bPlaneAEnabled = TRUE;

	if (PlaneBCtrl & DISPLAY_PLANE_ENABLE)
		bPlaneBEnabled = TRUE;

	if (!bPlaneAEnabled && !bPlaneBEnabled)
	{
		// There isn't any plane enabled, so don't change the watermarks.
		return;
	}

	int i;
	for (i = 0; i < xf86_config->num_output; i++)
	{
		output = xf86_config->output[i];

		//port LVDS0, pipe1, planeB
		if (!strcmp(output->name, "LVDS0"))
		{
			if (bPlaneBEnabled)
			{   
				//current mode dot clock, HZ
 				if( output->crtc ){
 					DotClockB = output->crtc->mode.Clock * 1000 / WM_DOTCLOCK_DIVISOR;
 				}
				BPPB = output->scrn->bitsPerPixel;
			}
		}
		//port SDVO, pipe0, planeA
		else
		{
			if (bPlaneAEnabled)
			{
 				if( output->crtc ){
 					DotClockA = output->crtc->mode.Clock * 1000 / WM_DOTCLOCK_DIVISOR;
 				}
				BPPA = output->scrn->bitsPerPixel;
			}
		}

	}

	// Set the watermarks for both plane A and B
	if (bPlaneAEnabled && bPlaneBEnabled)
	{
		CARD32 planeAWatermark_2, planeAWatermark_1, planeBWatermark_2, planeBWatermark_1;

		// read c_start and b_start from the DSPARB register
		DSPARB_Register dsparb;

		dsparb.entireRegister = PSB_READ32(DSPARB);

		planeAWatermark_1 = psbCalculateWaterMark(dsparb.b_Start, WATERMARK_1, BPPA, DCplane_A, DotClockA, TRUE);
		planeAWatermark_2 = psbCalculateWaterMark(dsparb.b_Start, WATERMARK_2, BPPA, DCplane_A, DotClockA, TRUE);
		planeBWatermark_1 = psbCalculateWaterMark(dsparb.c_Start - dsparb.b_Start, WATERMARK_1, BPPB, DCplane_B, DotClockB, TRUE);
		planeBWatermark_2 = psbCalculateWaterMark(dsparb.c_Start - dsparb.b_Start, WATERMARK_2, BPPB, DCplane_B, DotClockB, TRUE);

		// Write the watermark 1 register for A and B
		Watermark_Register1 wm1;
		wm1.entireRegister = PSB_READ32(FWATER_BLC1);
		wm1.DispA_WM1 = planeAWatermark_1;
		wm1.DispB_WM1 = planeBWatermark_1;

		PSB_WRITE32(FWATER_BLC1, wm1.entireRegister);

		// Write the watermark 2 register for both A and B
		FIFOWatermark_Register wm2;
		wm2.entireRegister = PSB_READ32(FWATER_BLC_SELF);
		wm2.DispAB_WM2 = planeAWatermark_2 > planeBWatermark_2 ? planeBWatermark_2 : planeAWatermark_2;

		PSB_WRITE32(FWATER_BLC_SELF, wm2.entireRegister); 
	}	
	// Set the MaxFIFO watermark
	else
	{
		U32 start = MAX_FIFO_START;
		U32 maxFIFOWatermark1_status0, maxFIFOWatermark1_status1;

		if (bPlaneAEnabled)
		{
			maxFIFOWatermark1_status0 = psbCalculateWaterMark(start, WATERMARK_2, BPPA, DCplane_A, DotClockA, FALSE);
			maxFIFOWatermark1_status1 = psbCalculateWaterMark(start, WATERMARK_1, BPPA, DCplane_A, DotClockA, FALSE);
		}
		else
		{
			maxFIFOWatermark1_status0 = psbCalculateWaterMark(start, WATERMARK_2, BPPB, DCplane_B, DotClockB, FALSE);
			maxFIFOWatermark1_status1 = psbCalculateWaterMark(start, WATERMARK_1, BPPB, DCplane_B, DotClockB, FALSE);
		}
		
		// Write the MAX FIFO Watermark register
		MAXFIFOWatermark_Register wmRegister;
		wmRegister.entireRegister = PSB_READ32(FWATER_BLC3);
		wmRegister.WM1_STATUS0 = maxFIFOWatermark1_status0;
		wmRegister.WM1_STATUS1 = maxFIFOWatermark1_status1;

		PSB_WRITE32(FWATER_BLC3, wmRegister.entireRegister);
	}

	return;
}

/**
 * Sets the power management mode of the pipe and plane.
 *
 * This code should probably grow support for turning the cursor off and back
 * on appropriately at the same time as we're turning the pipe off/on.
 */
static void
psbCrtcDpms(xf86CrtcPtr crtc, int mode)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    PsbDevicePtr pDevice = psbDevicePTR(psbPTR(pScrn));
    PsbPtr pPsb = psbPTR(pScrn);

    PsbCrtcPrivatePtr pCrtc = crtc->driver_private;
    int pipe = pCrtc->pipe;
    int dpll_reg = (pipe == 0) ? DPLL_A : DPLL_B;
    int dspcntr_reg = (pipe == 0) ? DSPACNTR : DSPBCNTR;
    int dspbase_reg = (pipe == 0) ? DSPABASE : DSPBBASE;
    int pipeconf_reg = (pipe == 0) ? PIPEACONF : PIPEBCONF;
    CARD32 temp;

    /* XXX: When our outputs are all unaware of DPMS modes other than off and
     * on, we should map those modes to DPMSModeOff in the CRTC.
     */
    PSB_DEBUG(crtc->scrn->scrnIndex, 3, "xxi830_psbCrtcDpms pipe %d\n", pipe);
    if (!psbOutputCrtcValid(crtc->scrn, pipe))
	return;

    switch (mode) {
    case DPMSModeOn:
    case DPMSModeStandby:
    case DPMSModeSuspend:
	PSB_DEBUG(crtc->scrn->scrnIndex, 3, "Crtc DPMS On / Sb /SS \n");
	/* Enable the DPLL */
	temp = PSB_READ32(dpll_reg);
	if ((temp & DPLL_VCO_ENABLE) == 0) {
	    PSB_WRITE32(dpll_reg, temp);
	    (void)PSB_READ32(dpll_reg);
	    /* Wait for the clocks to stabilize. */
	    usleep(150);
	    PSB_WRITE32(dpll_reg, temp | DPLL_VCO_ENABLE);
	    (void)PSB_READ32(dpll_reg);
	    /* Wait for the clocks to stabilize. */
	    usleep(150);
	    PSB_WRITE32(dpll_reg, temp | DPLL_VCO_ENABLE);
	    (void)PSB_READ32(dpll_reg);
	    /* Wait for the clocks to stabilize. */
	    usleep(150);
	}

	/* Enable the pipe */
	temp = PSB_READ32(pipeconf_reg);
	if ((temp & PIPEACONF_ENABLE) == 0) {
	    PSB_WRITE32(pipeconf_reg, temp | PIPEACONF_ENABLE);
	    (void)PSB_READ32(pipeconf_reg);
	}

	/* Enable the plane */
	temp = PSB_READ32(dspcntr_reg);
	if ((temp & DISPLAY_PLANE_ENABLE) == 0) {
	    PSB_WRITE32(dspcntr_reg, temp | DISPLAY_PLANE_ENABLE);
	    /* Flush the plane changes */
	    PSB_WRITE32(dspbase_reg, PSB_READ32(dspbase_reg));
	    (void)PSB_READ32(dspbase_reg);
	}

	psbCrtcLoadLut(crtc);

	/* Enable the Overlay Plane when LVDS is turned on, and will
	   update the Overlay Plane when SDVO is turned on. This is
	   because the SDVO and LVDS share the same framebuffer, and
	   SDVO's mode change will affect the Overlay Plane's input.*/
	if (pPsb->downScale ) { 
		psb_dpms_overlay(crtc, TRUE);
	}
	break;
    case DPMSModeOff:
	PSB_DEBUG(crtc->scrn->scrnIndex, 3, "Crtc DPMS Off\n");
	/* Give the overlay scaler a chance to disable if it's on LVDS */
	if (pPsb->downScale && pipe==1) {
		psb_dpms_overlay(crtc, FALSE);
	}

	/* Disable display plane */
	temp = PSB_READ32(dspcntr_reg);
	if ((temp & DISPLAY_PLANE_ENABLE) != 0) {
	    PSB_WRITE32(dspcntr_reg, temp & ~DISPLAY_PLANE_ENABLE);
	    /* Flush the plane changes */
	    PSB_WRITE32(dspbase_reg, PSB_READ32(dspbase_reg));
	}

	/* Next, disable display pipes */
	temp = PSB_READ32(pipeconf_reg);
	if ((temp & PIPEACONF_ENABLE) != 0) {
	    PSB_WRITE32(pipeconf_reg, temp & ~PIPEACONF_ENABLE);
	    (void)PSB_READ32(pipeconf_reg);
	}

	/* Wait for vblank for the disable to take effect. */
	psbWaitForVblank(pScrn);

	temp = PSB_READ32(dpll_reg);
	if ((temp & DPLL_VCO_ENABLE) != 0) {
	    PSB_WRITE32(dpll_reg, temp & ~DPLL_VCO_ENABLE);
	    (void)PSB_READ32(dpll_reg);
	}

	/* Wait for the clocks to turn off. */
	usleep(150);
	break;
    }

    psbWaitForVblank(pScrn);
    psbSetWatermarks(pScrn);
}

static Bool
psbCrtcLock(xf86CrtcPtr crtc)
{
    PSB_DEBUG(crtc->scrn->scrnIndex, 3, "xxi830_psbCrtcLock\n");

    psbDRILock(crtc->scrn, 0);
    return TRUE;
}

static void
psbCrtcUnlock(xf86CrtcPtr crtc)
{
    PSB_DEBUG(crtc->scrn->scrnIndex, 3, "xxi830_psbCrtcUnlock\n");

    psbDRIUnlock(crtc->scrn);
}

static void
psbCrtcPrepare(xf86CrtcPtr crtc)
{
    PSB_DEBUG(crtc->scrn->scrnIndex, 3, "xxi830_psbCrtcPrepare\n");
    crtc->funcs->dpms(crtc, DPMSModeOff);
}

static void
psbCrtcCommit(xf86CrtcPtr crtc)
{
    PSB_DEBUG(crtc->scrn->scrnIndex, 3, "xxi830_psbCrtcCommit, crtc->dpms\n");
    crtc->funcs->dpms(crtc, DPMSModeOn);
    if (crtc->scrn->pScreen != NULL)
	xf86_reload_cursors(crtc->scrn->pScreen);
}

static Bool
psbCrtcModeFixup(xf86CrtcPtr crtc, DisplayModePtr mode,
		 DisplayModePtr adjusted_mode)
{
    PSB_DEBUG(crtc->scrn->scrnIndex, 3, "xxi830_psbCrtcModeFixup, NULL\n");
    return TRUE;
}

static int
psbGetCoreClockSpeed(ScrnInfoPtr pScrn)
{
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);

    PSB_DEBUG(0, 3, "i830_psbGetCoreClockSpeed\n");
    return pDevice->CoreClock * 1000;
}

/**
 * Return the pipe currently connected to the panel fitter,
 * or -1 if the panel fitter is not present or not in use
 */

int
psbPanelFitterPipe(CARD32 pfitControl)
{
    /* See if the panel fitter is in use */
    if ((pfitControl & PFIT_ENABLE) == 0)
	return -1;

    /* Poulsbo chips can only use pipe 1 */
    return 1;
}

/* Adjust the mode clock, refer to the codes in Vista driver Jamesx */
typedef struct _PBDCPLLregisters
{
    U32 m1:6;
    U32 m2:6;
    U32 n:6;
    U32 p1:8;
    U32 p2:2;
} PBDCPLLregisters;

typedef struct _PBDCPLLtimings
{
    U32 reference_freq;		       /* in KHz */
    U32 target_error;
    U32 minvco;			       /* in KHz */
    U32 maxvco;			       /* in KHz */
    U32 max_n;
    U32 min_n;
    U32 step_n;
    U32 max_p;
    U32 min_p;
    U32 step_p;
    /* NOTE:  M counter range is not computed from min/max m1 and m2 value */
    /* i.e., min_m_counter != (5*timings.min_m1) + timings.min_m2 */
    U32 max_m_counter;		       /* M counter = 5*(m1+5) + m */
    U32 min_m_counter;
    U32 max_m1;
    U32 min_m1;
    U32 step_m1;
    U32 max_m2;
    U32 min_m2;
    U32 step_m2;
} PBDCPLLtimings;

U32
PBDCDisplaySDVO_GetPortMultiplier(U32 ulDotClock)
{
    U32 ulPortMultiplier;

    /*ULONG ulTemp; */
    /* The port multipliers 3 and 5 are not necessry. */
    /*There were initially accomadated to facilitate new silicon testing */

    U32 ulTemp;

    for (ulPortMultiplier = 1; ulPortMultiplier <= 5; ulPortMultiplier++) {
	ulTemp = ulDotClock * ulPortMultiplier;
	if ((ulTemp >= 100000) && (ulTemp <= 200000)) {
	    if ((ulPortMultiplier == 3) || (ulPortMultiplier == 5))
		continue;
	    else
		return (ulPortMultiplier);
	}
    }

    return ulPortMultiplier;
}

void
PBDCDisplay_FindOptimalTiming(U32 in_dotClock,
			      PBDCPLLtimings * in_pPLLtimings,
			      U32 in_p2divide,
			      /* must be computed depending on display type */
			      PBDCPLLregisters * out_pPllregisters)
{
    U32 minimum_m = 0;
    U32 minimum_delta = 0;
    U32 p;
    U32 m1, m2;

    U32 dotClock = in_dotClock / 1000; /* everything here in kilohertz */

    U32 referenceFrequency = in_pPLLtimings->reference_freq;

    /* compute range of combined M, where M = (5*m1) + m2 */
    U32 min_m = in_pPLLtimings->min_m_counter;

    /* NOT EQUAL TO (5*in_pPLLtimings->min_m1) + in_pPLLtimings->min_m2; */
    U32 max_m = in_pPLLtimings->max_m_counter;

    /* NOT EQUAL TO (5*in_pPLLtimings->max_m1) + in_pPLLtimings->max_m2; */

    out_pPllregisters->n = in_pPLLtimings->min_n;
    minimum_m = min_m;

    /* set delta initially very large. */
    /* we are trying to find minimum delta. */
    minimum_delta =
	dotClock * (in_pPLLtimings->max_p * 10) * in_pPLLtimings->max_n;

    for (p = in_pPLLtimings->min_p; p <= in_pPLLtimings->max_p;
	 p += in_pPLLtimings->step_p) {
	U32 pdiv = in_p2divide * p;
	U32 targetvco = dotClock * pdiv;
	U32 n = 0;

	if ((targetvco < in_pPLLtimings->minvco)
	    || (targetvco > in_pPLLtimings->maxvco))
	    continue;

	for (n = in_pPLLtimings->min_n; n <= in_pPLLtimings->max_n;
	     n += in_pPLLtimings->step_n) {
	    /* WARNING:  this may be within 1 order of magnitude of 0xFFFFFFFF (maximum 32-bit integer) */
	    /*   (max_clock=400,000) * (max_p1=8) * (max_p2=10) * (max_n=10) */
	    U32 leftSide = dotClock * pdiv * n;

	    U32 m;

	    for (m = min_m; m <= max_m; m++) {
		U32 rightSide = referenceFrequency * m;
		U32 delta = 0;

		if (leftSide > rightSide) {
		    delta = leftSide - rightSide;
		} else {
		    delta = rightSide - leftSide;
		}
		if ((U32) (delta) < minimum_delta) {
		    out_pPllregisters->n = n - 2;
		    out_pPllregisters->p1 = 1 << (p - 1);
		    minimum_m = m;
		    minimum_delta = delta;
		}
	    }			       /* end loop over p */
	}			       /* end loop over n */
    }				       /* end loop over m */

    minimum_delta = 2 * max_m;
    for (m1 = in_pPLLtimings->max_m1;
	 (m1 >= in_pPLLtimings->min_m1) && (minimum_delta > 0);
	 m1 -= in_pPLLtimings->step_m1) {
	for (m2 = in_pPLLtimings->min_m2; (m2 < m1) && (minimum_delta > 0);
	     m2 += in_pPLLtimings->step_m2) {
	    U32 test = (5 * m1) + m2;

	    if (test <= minimum_m) {
		U32 delta = minimum_m - test;

		if (delta < minimum_delta) {
		    minimum_delta = delta;
		    /* register values for m1 and m2 */
		    out_pPllregisters->m1 = m1 - 2;
		    out_pPllregisters->m2 = m2 - 2;
		}		       /* end if new M */
	    }
	}			       /* end loop over m2 */
    }				       /* end loop over m1 */
}

void
PBDCDisplaySDVO_FindOptimalTimingSDVO(U32 in_dotclock, U32 ulPortMultiplier,
				      PBDCPLLregisters * out_pPLLregisters)
{
    PBDCPLLtimings PLL_Timings_SDVO_DAC = {
	96000,			       /*reference_freq in KHz */
	46,			       /* target_error */
	1398850,		       /* minvco   in KHz */
	2800000,		       /* maxvco   in KHz */
	6 + 2,			       /* max_n */
	1 + 2,			       /* min_n */
	1,			       /* step_n */
	8,			       /* max_p */
	1,			       /* min_p */
	1,			       /* step_p */

	/* NOTE:  M counter range is not computed from min/max m1 and m2 values */
	130,			       /* maximum M counter (M = 5*(m1+5) + m2) */
	90,			       /* minimum M counter */

	22 + 2,			       /* max_m1 */
	10 + 2,			       /* min_m1 */
	1,			       /* step_m1 */
	9 + 2,			       /* max_m2 */
	5 + 2,			       /* min_m2 */
	1			       /* step_m2 */
    };

    /* SDVO can have a 5 or 10 divisor, which corresponds to setting a 1 or 0 in the p2 divisor */
    U32 p2divide = 5;

    out_pPLLregisters->p2 = 1;
    /* divide by 10 is used when Dot Clock  =< 200MHz in sDVO or DAC modes */
    if (in_dotclock <= 200000000) {
	p2divide = 10;
	out_pPLLregisters->p2 = 0;
    }
    PBDCDisplay_FindOptimalTiming(in_dotclock * ulPortMultiplier,
				  &PLL_Timings_SDVO_DAC, p2divide,
				  out_pPLLregisters);
}


/**
 * Sets up registers for the given mode/adjusted_mode pair.
 *
 * The clocks, CRTCs and outputs attached to this CRTC must be off.
 *
 * This shouldn't enable any clocks, CRTCs, or outputs, but they should
 * be easily turned on/off after this.
 */
static void
psbCrtcModeSet(xf86CrtcPtr crtc, DisplayModePtr mode,
	       DisplayModePtr adjusted_mode, int x, int y)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    PsbCrtcPrivatePtr pCrtc = crtc->driver_private;
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);

    int pipe = pCrtc->pipe;
    int fp_reg = (pipe == 0) ? FPA0 : FPB0;
    int dpll_reg = (pipe == 0) ? DPLL_A : DPLL_B;
    int dspcntr_reg = (pipe == 0) ? DSPACNTR : DSPBCNTR;
    int pipeconf_reg = (pipe == 0) ? PIPEACONF : PIPEBCONF;
    int htot_reg = (pipe == 0) ? HTOTAL_A : HTOTAL_B;
    int hblank_reg = (pipe == 0) ? HBLANK_A : HBLANK_B;
    int hsync_reg = (pipe == 0) ? HSYNC_A : HSYNC_B;
    int vtot_reg = (pipe == 0) ? VTOTAL_A : VTOTAL_B;
    int vblank_reg = (pipe == 0) ? VBLANK_A : VBLANK_B;
    int vsync_reg = (pipe == 0) ? VSYNC_A : VSYNC_B;
    int dspsize_reg = (pipe == 0) ? DSPASIZE : DSPBSIZE;
    int dsppos_reg = (pipe == 0) ? DSPAPOS : DSPBPOS;
    int pipesrc_reg = (pipe == 0) ? PIPEASRC : PIPEBSRC;
    int i;
    int refclk;
    intel_clock_t clock;
    CARD32 dpll = 0, fp = 0, dspcntr, pipeconf;
    Bool ok, is_sdvo = FALSE;
    Bool is_lvds = FALSE;
    int centerX = 0, centerY = 0;

    /*
     * Save a copy for current mode settings
     */
    PSB_DEBUG(crtc->scrn->scrnIndex, 3, "xxi830_psbCrtcModeSet\n");

    if (0 && pDevice->TVEnabled) {
	PSB_DEBUG(0, 3, "xiaoin, TV is enabled\n");
    } else {
	memcpy(&pCrtc->saved_mode, mode, sizeof(DisplayModeRec));
	memcpy(&pCrtc->saved_adjusted_mode, adjusted_mode,
	       sizeof(DisplayModeRec));
	pCrtc->x = x;
	pCrtc->y = y;

	/* Set up some convenient bools for what outputs are connected to
	 * our pipe, used in DPLL setup.
	 */

	PSB_DEBUG(crtc->scrn->scrnIndex, 3, "i830_psbCrtcModeSet\n");

	for (i = 0; i < xf86_config->num_output; i++) {
	    xf86OutputPtr output = xf86_config->output[i];
	    PsbOutputPrivatePtr intel_output = output->driver_private;

	    if (output->crtc != crtc)
		continue;

	    switch (intel_output->type) {
	    case PSB_OUTPUT_LVDS:
		is_lvds = TRUE;
		break;
	    case PSB_OUTPUT_SDVO:
		is_sdvo = TRUE;
		break;
	    }
	}

	refclk = 96000;

	ok = psbFindBestPLL(crtc, adjusted_mode->Clock, refclk, &clock);
	if (!ok)
	    FatalError("Couldn't find PLL settings for mode!\n");

	/* Change the Clock Timing for HDMI Jamesx */
	if (is_sdvo) {
	    for (i = 0; i < xf86_config->num_output; i++) {
		xf86OutputPtr output = xf86_config->output[i];
		PsbOutputPrivatePtr intel_output = output->driver_private;

		if (output->crtc != crtc)
		    continue;

		if (intel_output->isHDMI_Device
		    && intel_output->isHDMI_Monitor) {
		    PSB_DEBUG(crtc->scrn->scrnIndex, 3,
			      "Adjust HDMI Clock\n");
		    U32 dot_clock = mode->Clock * 1000;
		    PBDCPLLregisters out_pPLLregisters;
		    U32 Multiplier =
			PBDCDisplaySDVO_GetPortMultiplier(dot_clock / 1000);
		    PBDCDisplaySDVO_FindOptimalTimingSDVO(dot_clock,
							  Multiplier,
							  &out_pPLLregisters);
		    clock.p1 = out_pPLLregisters.p1;
		    clock.m1 = out_pPLLregisters.m1;
		    clock.m2 = out_pPLLregisters.m2;
		    clock.n = out_pPLLregisters.n;
		}
	    }
	}

	fp = clock.n << 16 | clock.m1 << 8 | clock.m2;

	dpll = (DPLL_VGA_MODE_DIS | DPLL_CLOCK_PHASE_9);

	if (is_lvds)
	    dpll |= DPLLB_MODE_LVDS | DPLL_DVO_HIGH_SPEED;
	else
	    dpll |= DPLLB_MODE_DAC_SERIAL;
	if (is_sdvo) {
	    int sdvo_pixel_multiply = adjusted_mode->Clock / mode->Clock;

	    dpll |= DPLL_DVO_HIGH_SPEED;
	    dpll |= (sdvo_pixel_multiply - 1) << SDVO_MULTIPLIER_SHIFT_HIRES;
	}

	/* compute bitmask from p1 value */
	dpll |= (1 << (clock.p1 - 1)) << 16;
	switch (clock.p2) {
	case 5:
	    dpll |= DPLL_DAC_SERIAL_P2_CLOCK_DIV_5;
	    break;
	case 7:
	    dpll |= DPLLB_LVDS_P2_CLOCK_DIV_7;
	    break;
	case 10:
	    dpll |= DPLL_DAC_SERIAL_P2_CLOCK_DIV_10;
	    break;
	case 14:
	    dpll |= DPLLB_LVDS_P2_CLOCK_DIV_14;
	    break;
	}

	if (is_lvds)
	    dpll |= PLLB_REF_INPUT_SPREADSPECTRUMIN;
	else
	    dpll |= PLL_REF_INPUT_DREFCLK;

	/* Set up the display plane register */
	dspcntr = DISPPLANE_GAMMA_ENABLE;
	switch (pScrn->bitsPerPixel) {
	case 8:
	    dspcntr |= DISPPLANE_8BPP;
	    break;
	case 16:
	    if (pScrn->depth == 15)
		dspcntr |= DISPPLANE_15_16BPP;
	    else
		dspcntr |= DISPPLANE_16BPP;
	    break;
	case 32:
	    dspcntr |= DISPPLANE_32BPP_NO_ALPHA;
	    break;
	default:
	    FatalError("unknown display bpp\n");
	}

	if (pipe == 0)
	    dspcntr |= DISPPLANE_SEL_PIPE_A;
	else
	    dspcntr |= DISPPLANE_SEL_PIPE_B;

	pipeconf = PSB_READ32(pipeconf_reg);
	if (pipe == 0) {
	    /* Enable pixel doubling when the dot clock is > 90% of the (display)
	     * core speed.
	     *
	     * XXX: No double-wide on 915GM pipe B. Is that the only reason for the
	     * pipe == 0 check?
	     */
	    if (mode->Clock > psbGetCoreClockSpeed(pScrn) * 9 / 10)
		pipeconf |= PIPEACONF_DOUBLE_WIDE;
	    else
		pipeconf &= ~PIPEACONF_DOUBLE_WIDE;
	}
#if 1
	dspcntr |= DISPLAY_PLANE_ENABLE;
	pipeconf |= PIPEACONF_ENABLE;
	dpll |= DPLL_VCO_ENABLE;
#endif

	if (is_lvds) {
	    /* The LVDS pin pair needs to be on before the DPLLs are enabled.
	     * This is an exception to the general rule that mode_set doesn't turn
	     * things on.
	     */
	    PSB_WRITE32(LVDS,
			PSB_READ32(LVDS) | LVDS_PORT_EN | LVDS_PIPEB_SELECT);
	}

	/* Disable the panel fitter if it was on our pipe */
	if (psbPanelFitterPipe(PSB_READ32(PFIT_CONTROL)) == pipe)
	    PSB_WRITE32(PFIT_CONTROL, 0);

	psbPrintPll(pScrn->scrnIndex, "chosen", &clock);
	PSB_DEBUG(pScrn->scrnIndex, 3, "clock regs: 0x%08x, 0x%08x\n",
		  (int)dpll, (int)fp);

	if (dpll & DPLL_VCO_ENABLE) {
	    PSB_WRITE32(fp_reg, fp);
	    PSB_WRITE32(dpll_reg, dpll & ~DPLL_VCO_ENABLE);
	    (void)PSB_READ32(dpll_reg);
	    usleep(150);
	}
	PSB_WRITE32(fp_reg, fp);
	PSB_WRITE32(dpll_reg, dpll);
	(void)PSB_READ32(dpll_reg);
	/* Wait for the clocks to stabilize. */
	usleep(150);

	/* write it again -- the BIOS does, after all */
	PSB_WRITE32(dpll_reg, dpll);
	(void)PSB_READ32(dpll_reg);

	/* Wait for the clocks to stabilize. */
	usleep(150);

	PSB_WRITE32(htot_reg, (adjusted_mode->CrtcHDisplay - 1) |
		    ((adjusted_mode->CrtcHTotal - 1) << 16));
	PSB_WRITE32(hblank_reg, (adjusted_mode->CrtcHBlankStart - 1) |
		    ((adjusted_mode->CrtcHBlankEnd - 1) << 16));
	PSB_WRITE32(hsync_reg, (adjusted_mode->CrtcHSyncStart - 1) |
		    ((adjusted_mode->CrtcHSyncEnd - 1) << 16));
	PSB_WRITE32(vtot_reg, (adjusted_mode->CrtcVDisplay - 1) |
		    ((adjusted_mode->CrtcVTotal - 1) << 16));
	PSB_WRITE32(vblank_reg, (adjusted_mode->CrtcVBlankStart - 1) |
		    ((adjusted_mode->CrtcVBlankEnd - 1) << 16));
	PSB_WRITE32(vsync_reg, (adjusted_mode->CrtcVSyncStart - 1) |
		    ((adjusted_mode->CrtcVSyncEnd - 1) << 16));

	if (pPsb->panelFittingMode != PSB_PANELFITTING_FIT) {

	    centerX = (adjusted_mode->CrtcHDisplay - mode->HDisplay) / 2;
	    centerY = (adjusted_mode->CrtcVDisplay - mode->VDisplay) / 2;
	    PSB_WRITE32(dspsize_reg,
			((mode->VDisplay - 1) << 16) | (mode->HDisplay - 1));

	    PSB_WRITE32(dsppos_reg, centerY << 16 | centerX);
	    PSB_WRITE32(pipesrc_reg,
			((adjusted_mode->CrtcHDisplay -
			  1) << 16) | (adjusted_mode->CrtcVDisplay - 1));
	} else {
	    /* pipesrc and dspsize control the size that is scaled from, which should
	     * always be the user's requested size.
	     */
	    PSB_WRITE32(dspsize_reg,
			((mode->VDisplay - 1) << 16) | (mode->HDisplay - 1));
	    PSB_WRITE32(dsppos_reg, 0);
	    PSB_WRITE32(pipesrc_reg,
			((mode->HDisplay - 1) << 16) | (mode->VDisplay - 1));

	}
	PSB_WRITE32(pipeconf_reg, pipeconf);
	(void)PSB_READ32(pipeconf_reg);
	psbWaitForVblank(pScrn);

	PSB_WRITE32(dspcntr_reg, dspcntr);
	/* Flush the plane changes */
	psbPipeSetBase(crtc, x, y);

	psbWaitForVblank(pScrn);
    }
}

/** Loads the palette/gamma unit for the CRTC with the prepared values */
void
psbCrtcLoadLut(xf86CrtcPtr crtc)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    PsbDevicePtr pDevice = psbDevicePTR(psbPTR(pScrn));
    PsbCrtcPrivatePtr pCrtc = crtc->driver_private;
    int palreg = (pCrtc->pipe == 0) ? PALETTE_A : PALETTE_B;
    int i;

    PSB_DEBUG(pScrn->scrnIndex, 3, "xxi830_psbCrtcLoadLut %p \n", crtc);

    /* The clocks have to be on to load the palette. */
    if (!crtc->enabled)
	return;

    for (i = 0; i < 256; i++) {
	PSB_WRITE32(palreg + 4 * i,
		    (pCrtc->lutR[i] << 16) | (pCrtc->lutG[i] << 8) | pCrtc->
		    lutB[i]);

    }
    (void)PSB_READ32(palreg + 4 * (i - 1));
}

/** Sets the color ramps on behalf of RandR */
static void
psbCrtcGammaSet(xf86CrtcPtr crtc, CARD16 * red, CARD16 * green,
		CARD16 * blue, int size)
{
    PsbCrtcPrivatePtr pCrtc = crtc->driver_private;
    int i;

    assert(size == 256);
    PSB_DEBUG(0, 3, "xxi830_psbCrtcGammaSet\n");
    for (i = 0; i < 256; i++) {
	pCrtc->lutR[i] = red[i] >> 8;
	pCrtc->lutG[i] = green[i] >> 8;
	pCrtc->lutB[i] = blue[i] >> 8;
    }

    psbCrtcLoadLut(crtc);
}

/**
 * Allocates memory for a locked-in-framebuffer shadow of the given
 * width and height for this CRTC's rotated shadow framebuffer.
 */

static void *
psbCrtcShadowAllocate(xf86CrtcPtr crtc, int width, int height)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    PsbCrtcPrivatePtr pCrtc = (PsbCrtcPrivatePtr) crtc->driver_private;

    PSB_DEBUG(pScrn->scrnIndex, 3, "xxi830_psbCrtcShadowAllocate\n");

    psbScanoutDestroy(pCrtc->rotate);
    pCrtc->rotate = psbScanoutCreate(pScrn, pScrn->bitsPerPixel >> 3,
				     pScrn->depth, width, height, 0, FALSE,
				     crtc->rotation);
    if (!pCrtc->rotate)
	return NULL;

    memset(psbScanoutVirtual(pCrtc->rotate), 0,
	   height * psbScanoutStride(pCrtc->rotate));

    return psbScanoutVirtual(pCrtc->rotate);
}

/**
 * Creates a pixmap for this CRTC's rotated shadow framebuffer.
 */
static PixmapPtr
psbCrtcShadowCreate(xf86CrtcPtr crtc, void *data, int width, int height)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    unsigned long rotate_pitch;
    PixmapPtr rotate_pixmap;
    PsbCrtcPrivatePtr pCrtc = (PsbCrtcPrivatePtr) crtc->driver_private;

    PSB_DEBUG(pScrn->scrnIndex, 3, "xxi830_psbCrtcShadowCreate\n");
    PSB_DEBUG(pScrn->scrnIndex, 3, "width, height %d %d\n", width, height);

    if (!data) {
	data = psbCrtcShadowAllocate(crtc, width, height);
    }

    rotate_pitch = psbScanoutStride(pCrtc->rotate);

    rotate_pixmap = GetScratchPixmapHeader(pScrn->pScreen,
					   width, height, pScrn->depth,
					   pScrn->bitsPerPixel, rotate_pitch,
					   data);

    if (rotate_pixmap == NULL) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "Couldn't allocate shadow pixmap for rotated CRTC\n");
    }
    return rotate_pixmap;
}

static void
psbCrtcShadowDestroy(xf86CrtcPtr crtc, PixmapPtr rotate_pixmap, void *data)
{
    PSB_DEBUG(crtc->scrn->scrnIndex, 3, "xxi830_psbCrtcShadowDestroy\n");

    if (rotate_pixmap)
	FreeScratchPixmapHeader(rotate_pixmap);

    if (data) {
	PsbCrtcPrivatePtr pCrtc = (PsbCrtcPrivatePtr) crtc->driver_private;

	psbScanoutDestroy(pCrtc->rotate);
	pCrtc->rotate = NULL;
    }
}

void
psbDescribeOutputConfiguration(ScrnInfoPtr pScrn)
{
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(psbPTR(pScrn));
    int i;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "xxi830_Output configuration:\n");

    for (i = 0; i < xf86_config->num_crtc; i++) {
	xf86CrtcPtr crtc = xf86_config->crtc[i];
	CARD32 dspcntr = PSB_READ32(DSPACNTR + (DSPBCNTR - DSPACNTR) * i);
	CARD32 pipeconf = PSB_READ32(PIPEACONF + (PIPEBCONF - PIPEACONF) * i);
	Bool hw_plane_enable = (dspcntr & DISPLAY_PLANE_ENABLE) != 0;
	Bool hw_pipe_enable = (pipeconf & PIPEACONF_ENABLE) != 0;

	if (!psbOutputCrtcValid(pScrn, i)) {
	    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		       "  Pipe %c is not available to this screen.\n",
		       'A' + i);
	    continue;
	}

	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "  Pipe %c is %s\n", 'A' + i,
		   crtc->enabled ? "on" : "off");
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "  Display plane %c is now %s and connected to pipe %c.\n",
		   'A' + i, crtc->enabled ? "enabled" : "disabled",
		   dspcntr & DISPPLANE_SEL_PIPE_MASK ? 'B' : 'A');
	if (hw_pipe_enable != crtc->enabled) {
	    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		       "  Hardware claims pipe %c is %s while software "
		       "believes it is %s\n",
		       'A' + i, hw_pipe_enable ? "on" : "off",
		       crtc->enabled ? "on" : "off");
	}
	if (hw_plane_enable != crtc->enabled) {
	    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		       "  Hardware claims plane %c is %s while software "
		       "believes it is %s\n",
		       'A' + i, hw_plane_enable ? "on" : "off",
		       crtc->enabled ? "on" : "off");
	}
    }

    for (i = 0; i < xf86_config->num_output; i++) {
	xf86OutputPtr output = xf86_config->output[i];
	xf86CrtcPtr crtc = output->crtc;
	PsbCrtcPrivatePtr pCrtc = (crtc) ? crtc->driver_private : NULL;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "  Output %s is connected to pipe %s\n",
		   output->name, pCrtc == NULL ? "none" :
		   (pCrtc->pipe == 0 ? "A" : "B"));
    }
}

/**
 * Get a pipe with a simple mode set on it for doing load-based monitor
 * detection.
 *
 * It will be up to the load-detect code to adjust the pipe as appropriate for
 * its requirements.  The pipe will be connected to no other outputs.
 *
 * Currently this code will only succeed if there is a pipe with no outputs
 * configured for it.  In the future, it could choose to temporarily disable
 * some outputs to free up a pipe for its use.
 *
 * \return crtc, or NULL if no pipes are available.
 */

xf86CrtcPtr
psbGetLoadDetectPipe(xf86OutputPtr output)
{
    ScrnInfoPtr pScrn = output->scrn;
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    PsbOutputPrivatePtr intel_output = output->driver_private;
    xf86CrtcPtr crtc;
    int i;

    xf86DrvMsg(pScrn->scrnIndex, 3, "xxi830_psbGetLoadDetectPipe:\n");
    if (output->crtc)
	return output->crtc;

    for (i = 0; i < xf86_config->num_crtc; i++)
	if (!xf86CrtcInUse(xf86_config->crtc[i]))
	    break;

    if (i == xf86_config->num_crtc)
	return NULL;

    crtc = xf86_config->crtc[i];

    output->crtc = crtc;
    intel_output->load_detect_temp = TRUE;

    return crtc;
}

void
psbReleaseLoadDetectPipe(xf86OutputPtr output)
{
    ScrnInfoPtr pScrn = output->scrn;
    PsbOutputPrivatePtr intel_output = output->driver_private;

    xf86DrvMsg(pScrn->scrnIndex, 3, "xxi830_psbReleaseLoadDetectPipe\n");
    if (intel_output->load_detect_temp) {
	output->crtc = NULL;
	intel_output->load_detect_temp = FALSE;
	xf86DisableUnusedFunctions(pScrn);
    }
}

/* Returns the clock of the currently programmed mode of the given pipe. */
static int
psbCrtcClockGet(ScrnInfoPtr pScrn, xf86CrtcPtr crtc)
{
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);

    PsbCrtcPrivatePtr pCrtc = crtc->driver_private;
    int pipe = pCrtc->pipe;
    CARD32 dpll = PSB_READ32((pipe == 0) ? DPLL_A : DPLL_B);
    CARD32 fp;
    intel_clock_t clock;

    xf86DrvMsg(pScrn->scrnIndex, 3, "xxi830_psbCrtcClockGet\n");
    if ((dpll & DISPLAY_RATE_SELECT_FPA1) == 0)
	fp = PSB_READ32((pipe == 0) ? FPA0 : FPB0);
    else
	fp = PSB_READ32((pipe == 0) ? FPA1 : FPB1);

    clock.m1 = (fp & FP_M1_DIV_MASK) >> FP_M1_DIV_SHIFT;
    clock.m2 = (fp & FP_M2_DIV_MASK) >> FP_M2_DIV_SHIFT;
    clock.n = (fp & FP_N_DIV_MASK) >> FP_N_DIV_SHIFT;
    clock.p1 = ffs((dpll & DPLL_FPA01_P1_POST_DIV_MASK) >>
		   DPLL_FPA01_P1_POST_DIV_SHIFT);

    switch (dpll & DPLL_MODE_MASK) {
    case DPLLB_MODE_DAC_SERIAL:
	clock.p2 = dpll & DPLL_DAC_SERIAL_P2_CLOCK_DIV_5 ? 5 : 10;
	break;
    case DPLLB_MODE_LVDS:
	clock.p2 = dpll & DPLLB_LVDS_P2_CLOCK_DIV_7 ? 7 : 14;
	break;
    default:
	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "Unknown DPLL mode %08x in programmed mode\n",
		   (int)(dpll & DPLL_MODE_MASK));
	return 0;
    }

    /* XXX: Handle the 100Mhz refclk */
    intel_clock(96000, &clock);

    return clock.dot;
}

/** Returns the currently programmed mode of the given pipe. */
DisplayModePtr
psbCrtcModeGet(ScrnInfoPtr pScrn, xf86CrtcPtr crtc)
{
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);

    PsbCrtcPrivatePtr pCrtc = crtc->driver_private;
    DisplayModePtr mode;
    int pipe = 1;
    int htot, hsync, vtot, vsync;

    xf86DrvMsg(pScrn->scrnIndex, 3, "xxi830_psbCrtcModeGet\n");
    if (pCrtc)
	pipe = pCrtc->pipe;

    htot = PSB_READ32((pipe == 0) ? HTOTAL_A : HTOTAL_B);
    hsync = PSB_READ32((pipe == 0) ? HSYNC_A : HSYNC_B);
    vtot = PSB_READ32((pipe == 0) ? VTOTAL_A : VTOTAL_B);
    vsync = PSB_READ32((pipe == 0) ? VSYNC_A : VSYNC_B);

    mode = xcalloc(1, sizeof(DisplayModeRec));
    if (mode == NULL)
	return NULL;

    mode->Clock = psbCrtcClockGet(pScrn, crtc);
    mode->HDisplay = (htot & 0xffff) + 1;
    mode->HTotal = ((htot & 0xffff0000) >> 16) + 1;
    mode->HSyncStart = (hsync & 0xffff) + 1;
    mode->HSyncEnd = ((hsync & 0xffff0000) >> 16) + 1;
    mode->VDisplay = (vtot & 0xffff) + 1;
    mode->VTotal = ((vtot & 0xffff0000) >> 16) + 1;
    mode->VSyncStart = (vsync & 0xffff) + 1;
    mode->VSyncEnd = ((vsync & 0xffff0000) >> 16) + 1;
    xf86SetModeDefaultName(mode);
    xf86SetModeCrtc(mode, 0);

    return mode;
}

static void
psbCrtcSave(xf86CrtcPtr crtc)
{
    PsbPtr pPsb = psbPTR(crtc->scrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);
    PsbCrtcPrivatePtr pCrtc = (PsbCrtcPrivatePtr) crtc->driver_private;
    Bool pipeA = (pCrtc->pipe == 0);
    unsigned paletteReg;
    unsigned i;

    PSB_DEBUG(crtc->scrn->scrnIndex, 3, "xxi830_psbCrtcSave pipe %d.\n",
	      pCrtc->pipe);

    pCrtc->saveDSPCNTR = PSB_READ32(pipeA ? DSPACNTR : DSPBCNTR);
    pCrtc->savePIPECONF = PSB_READ32(pipeA ? PIPEACONF : PIPEBCONF);
    pCrtc->savePIPESRC = PSB_READ32(pipeA ? PIPEASRC : PIPEBSRC);
    pCrtc->saveFP0 = PSB_READ32(pipeA ? FPA0 : FPB0);
    pCrtc->saveFP1 = PSB_READ32(pipeA ? FPA1 : FPB1);
    pCrtc->saveDPLL = PSB_READ32(pipeA ? DPLL_A : DPLL_B);
    pCrtc->saveHTOTAL = PSB_READ32(pipeA ? HTOTAL_A : HTOTAL_B);
    pCrtc->saveHBLANK = PSB_READ32(pipeA ? HBLANK_A : HBLANK_B);
    pCrtc->saveHSYNC = PSB_READ32(pipeA ? HSYNC_A : HSYNC_B);
    pCrtc->saveVTOTAL = PSB_READ32(pipeA ? VTOTAL_A : VTOTAL_B);
    pCrtc->saveVBLANK = PSB_READ32(pipeA ? VBLANK_A : VBLANK_B);
    pCrtc->saveVSYNC = PSB_READ32(pipeA ? VSYNC_A : VSYNC_B);
    pCrtc->saveDSPSTRIDE = PSB_READ32(pipeA ? DSPASTRIDE : DSPBSTRIDE);
    pCrtc->saveDSPSIZE = PSB_READ32(pipeA ? DSPASIZE : DSPBSIZE);
    pCrtc->saveDSPPOS = PSB_READ32(pipeA ? DSPAPOS : DSPBPOS);
    pCrtc->saveDSPBASE = PSB_READ32(pipeA ? DSPABASE : DSPBBASE);
    pCrtc->savePFITCTRL = PSB_READ32(PFIT_CONTROL);

    paletteReg = pipeA ? PALETTE_A : PALETTE_B;
    for (i = 0; i < 256; ++i) {
	pCrtc->savePalette[i] = PSB_READ32(paletteReg + (i << 2));
    }
}

static void
psbCrtcRestore(xf86CrtcPtr crtc)
{
    PsbPtr pPsb = psbPTR(crtc->scrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);
    PsbCrtcPrivatePtr pCrtc = (PsbCrtcPrivatePtr) crtc->driver_private;
    Bool pipeA = (pCrtc->pipe == 0);
    unsigned paletteReg;
    unsigned i;

    PSB_DEBUG(crtc->scrn->scrnIndex, 3, "xxi830_psbCrtcRestore pipe %d.\n",
	      pCrtc->pipe);

    crtc->funcs->dpms(crtc, DPMSModeOff);
    psbWaitForVblank(crtc->scrn);

    if (psbPanelFitterPipe(pCrtc->savePFITCTRL) == pCrtc->pipe)
	PSB_WRITE32(PFIT_CONTROL, pCrtc->savePFITCTRL);

    if (pCrtc->saveDPLL & DPLL_VCO_ENABLE) {
	PSB_WRITE32(pipeA ? DPLL_A : DPLL_B,
		    pCrtc->saveDPLL & ~DPLL_VCO_ENABLE);
	(void)PSB_READ32(pipeA ? DPLL_A : DPLL_B);
	usleep(150);
    }

    PSB_WRITE32(pipeA ? FPA0 : FPB0, pCrtc->saveFP0);
    PSB_WRITE32(pipeA ? FPA1 : FPB1, pCrtc->saveFP1);
    PSB_WRITE32(pipeA ? DPLL_A : DPLL_B, pCrtc->saveDPLL);
    (void)PSB_READ32(pipeA ? DPLL_A : DPLL_B);
    usleep(150);

    PSB_WRITE32(pipeA ? HTOTAL_A : HTOTAL_B, pCrtc->saveHTOTAL);
    PSB_WRITE32(pipeA ? HBLANK_A : HBLANK_B, pCrtc->saveHBLANK);
    PSB_WRITE32(pipeA ? HSYNC_A : HSYNC_B, pCrtc->saveHSYNC);
    PSB_WRITE32(pipeA ? VTOTAL_A : VTOTAL_B, pCrtc->saveVTOTAL);
    PSB_WRITE32(pipeA ? VBLANK_A : VBLANK_B, pCrtc->saveVBLANK);
    PSB_WRITE32(pipeA ? VSYNC_A : VSYNC_B, pCrtc->saveVSYNC);
    PSB_WRITE32(pipeA ? DSPASTRIDE : DSPBSTRIDE, pCrtc->saveDSPSTRIDE);
    PSB_WRITE32(pipeA ? DSPASIZE : DSPBSIZE, pCrtc->saveDSPSIZE);
    PSB_WRITE32(pipeA ? PIPEASRC : PIPEBSRC, pCrtc->savePIPESRC);
    PSB_WRITE32(pipeA ? DSPABASE : DSPBBASE, pCrtc->saveDSPBASE);
    PSB_WRITE32(pipeA ? PIPEACONF : PIPEBCONF, pCrtc->savePIPECONF);
    psbWaitForVblank(crtc->scrn);
    PSB_WRITE32(pipeA ? DSPACNTR : DSPBCNTR, pCrtc->saveDSPCNTR);
    PSB_WRITE32(pipeA ? DSPABASE : DSPBBASE, pCrtc->saveDSPBASE);
    psbWaitForVblank(crtc->scrn);
    paletteReg = pipeA ? PALETTE_A : PALETTE_B;

    for (i = 0; i < 256; ++i) {
	PSB_WRITE32(paletteReg + (i << 2), pCrtc->savePalette[i]);
    }
}

static void
psbCrtcHWCursorDestroy(xf86CrtcPtr crtc)
{
    PsbCrtcPrivatePtr pCrtc = psbCrtcPrivate(crtc);
    struct _MMBuffer *buf = pCrtc->cursor;

    xf86DrvMsg(0, 3, "xxi830_psbCrtcHWCursorDestroy\n");
    if (!buf)
	return;

    buf->man->destroyBuf(buf);
    pCrtc->cursor = NULL;
}

static void
psbCrtcDestroy(xf86CrtcPtr crtc)
{
    PsbCrtcPrivatePtr pCrtc = (PsbCrtcPrivatePtr) crtc->driver_private;

    PSB_DEBUG(crtc->scrn->scrnIndex, 3, "xxi830_psbCrtcDestroy\n");
    if (--pCrtc->refCount == 0) {
	PSB_DEBUG(crtc->scrn->scrnIndex, 3,
		  "Destroying a crtc private rec\n");
	psbCrtcHWCursorDestroy(crtc);
	xfree(pCrtc);
    }
}

static const xf86CrtcFuncsRec psbCrtcFuncs = {
    .dpms = psbCrtcDpms,
    .save = psbCrtcSave,
    .restore = psbCrtcRestore,
    .lock = psbCrtcLock,
    .unlock = psbCrtcUnlock,
    .mode_fixup = psbCrtcModeFixup,
    .prepare = psbCrtcPrepare,
    .mode_set = psbCrtcModeSet,
    .commit = psbCrtcCommit,
    .gamma_set = psbCrtcGammaSet,
    .shadow_create = psbCrtcShadowCreate,
    .shadow_allocate = psbCrtcShadowAllocate,
    .shadow_destroy = psbCrtcShadowDestroy,
    .set_cursor_colors = psb_crtc_set_cursor_colors,
    .set_cursor_position = psb_crtc_set_cursor_position,
    .show_cursor = psb_crtc_show_cursor,
    .hide_cursor = psb_crtc_hide_cursor,
    .load_cursor_argb = psb_crtc_load_cursor_argb,
    .destroy = psbCrtcDestroy,
};

xf86CrtcPtr
psbCrtcClone(ScrnInfoPtr pScrn, xf86CrtcPtr origCrtc)
{
    PsbCrtcPrivatePtr pCrtc = (PsbCrtcPrivatePtr) origCrtc->driver_private;
    xf86CrtcPtr crtc;

    PSB_DEBUG(pScrn->scrnIndex, 3, "i830_psbCrtcClone\n");
    crtc = xf86CrtcCreate(pScrn, &psbCrtcFuncs);
    if (crtc == NULL)
	return NULL;

    pCrtc->refCount++;
    crtc->driver_private = pCrtc;
    return crtc;
}

static void
psbCrtcHWCursorSave(xf86CrtcPtr crtc)
{
    PsbCrtcPrivatePtr pCrtc = psbCrtcPrivate(crtc);
    struct _MMBuffer *buf = pCrtc->cursor;

    PSB_DEBUG(crtc->scrn->scrnIndex, 3, "i830_psbCrtcHWCursorSave\n");

    if (!buf)
	return;

    if (buf->man->validateBuffer(buf, MM_FLAG_MEM_LOCAL,
				 MM_MASK_MEM | MM_FLAG_NO_EVICT,
				 MM_HINT_DONT_FENCE))
	xf86DrvMsg(crtc->scrn->scrnIndex, X_WARNING,
		   "Failed saving hw cursor for pipe %d\n", pCrtc->pipe);
}

static void
psbCrtcHWCursorAlloc(xf86CrtcPtr crtc)
{
    PsbCrtcPrivatePtr pCrtc = psbCrtcPrivate(crtc);
    ScrnInfoPtr pScrn = crtc->scrn;
    PsbDevicePtr pDevice = psbDevicePTR(psbPTR(pScrn));
    MMManager *man = pDevice->man;
    struct _MMBuffer *buf = pCrtc->cursor;
    unsigned long offset;
    int ret;

    PSB_DEBUG(pScrn->scrnIndex, 3, "i830_psbCrtcHWCursorAlloc\n");

    if (!buf) {
	buf = man->createBuf(man, HWCURSOR_SIZE + HWCURSOR_SIZE_ARGB, 0,
			     MM_FLAG_READ | MM_FLAG_MEM_VRAM |
			     MM_FLAG_NO_EVICT | MM_FLAG_MAPPABLE,
			     MM_HINT_DONT_FENCE);
	if (!buf) {
	    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		       "Failed allocating HW cursor for pipe %d\n",
		       pCrtc->pipe);
	    return;
	}
	ret = man->mapBuf(buf, MM_FLAG_READ | MM_FLAG_WRITE, 0);
	if (ret) {
	    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		       "Failed mapping hw cursor for pipe %d\n", pCrtc->pipe);
	    goto out_err;
	}
	man->unMapBuf(buf);
	pCrtc->cursor = buf;
    } else {
	ret =
	    buf->man->validateBuffer(buf, MM_FLAG_MEM_VRAM | MM_FLAG_NO_EVICT,
				     MM_MASK_MEM | MM_FLAG_NO_EVICT,
				     MM_HINT_DONT_FENCE);
	if (ret) {
	    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		       "Failed restoring hw cursor for pipe %d\n",
		       pCrtc->pipe);
	    goto out_err;
	}
    }

    /*
     * VDC wants the offset relative to the aperture start.
     */

    offset = mmBufOffset(pCrtc->cursor) & 0x0FFFFFFF;
    pCrtc->cursor_argb_addr = pDevice->stolenBase + offset;
    pCrtc->cursor_argb_offset = 0;

    PSB_DEBUG(pScrn->scrnIndex, 3,
	      "Cursor %d ARGB addresses 0x%08lx, 0x%08lx\n", pCrtc->pipe,
	      pCrtc->cursor_argb_addr, pCrtc->cursor_argb_offset);

    offset += HWCURSOR_SIZE;

    pCrtc->cursor_addr = pDevice->stolenBase + offset;
    pCrtc->cursor_offset = HWCURSOR_SIZE;
    return;

  out_err:
    buf->man->destroyBuf(buf);
    pCrtc->cursor = NULL;
}

xf86CrtcPtr
psbCrtcInit(ScrnInfoPtr pScrn, int pipe)
{
    xf86CrtcPtr crtc;
    PsbCrtcPrivatePtr pCrtc;
    int i;

    PSB_DEBUG(pScrn->scrnIndex, 3, "xxi830_psbCrtcInit\n");

    crtc = xf86CrtcCreate(pScrn, &psbCrtcFuncs);
    if (crtc == NULL)
	return NULL;

    pCrtc = xcalloc(sizeof(PsbCrtcPrivateRec), 1);
    pCrtc->pipe = pipe;
    pCrtc->refCount = 1;

    /* Initialize the LUTs for when we turn on the CRTC. */
    for (i = 0; i < 256; i++) {
	pCrtc->lutR[i] = i;
	pCrtc->lutG[i] = i;
	pCrtc->lutB[i] = i;
    }

    /*
     * Incase of 8 bit depth the lut's are used as palette and the
     * X server assumes that the color at index 1 is white.
     */
    if (pScrn->depth == 8) {
	pCrtc->lutR[1] = 255;
	pCrtc->lutG[1] = 255;
	pCrtc->lutB[1] = 255;
    }

    crtc->driver_private = pCrtc;

    /*
     * If the output on this crtc is disabled, then powerdown the CRTC
     */
    if (psbOutputIsDisabled(pScrn, pipe)) {
	psbCrtcDpms(crtc, DPMSModeOff);
    }

    return crtc;
}

int
psbCrtcSetupCursors(ScrnInfoPtr pScrn)
{
    PsbPtr pPsb = psbPTR(pScrn);
    PsbCrtcPrivatePtr pCrtc;
    int i;

    PSB_DEBUG(pScrn->scrnIndex, 3, "i830_psbCrtcSetupCursors\n");

    for (i = 0; i < pPsb->numCrtcs; ++i) {
	pCrtc = psbCrtcPrivate(pPsb->crtcs[i]);

	psbCrtcHWCursorAlloc(pPsb->crtcs[i]);
	if (pCrtc->cursor == NULL)
	    goto out_err;
    }
    return TRUE;

  out_err:

    for (i = 0; i < pPsb->numCrtcs; ++i) {
	pCrtc = psbCrtcPrivate(pPsb->crtcs[i]);

	psbCrtcHWCursorDestroy(pPsb->crtcs[i]);
    }

    return FALSE;
}

void
psbCrtcSaveCursors(ScrnInfoPtr pScrn, Bool force)
{
    PsbPtr pPsb = psbPTR(pScrn);
    PsbCrtcPrivatePtr pCrtc;
    int i;

    PSB_DEBUG(pScrn->scrnIndex, 3, "i830_psbCrtcSaveCursors\n");

    for (i = 0; i < pPsb->numCrtcs; ++i) {
	pCrtc = psbCrtcPrivate(pPsb->crtcs[i]);

	psbCrtcHWCursorSave(pPsb->crtcs[i]);
    }
}

void
psbCrtcFreeCursors(ScrnInfoPtr pScrn)
{
    PsbPtr pPsb = psbPTR(pScrn);
    PsbCrtcPrivatePtr pCrtc;
    int i;

    PSB_DEBUG(pScrn->scrnIndex, 3, "i830_psbCrtcFreeCursors\n");

    for (i = 0; i < pPsb->numCrtcs; ++i) {
	pCrtc = psbCrtcPrivate(pPsb->crtcs[i]);

	psbCrtcHWCursorDestroy(pPsb->crtcs[i]);
    }
}
