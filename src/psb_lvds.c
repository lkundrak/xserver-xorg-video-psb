/*
 * Copyright © 2006-2007 Intel Corporation
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
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *    Thomas Hellstrom <thomas-at-tungstengraphics-dot-com>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "stdio.h"
#include "string.h"
#include "psb_lvds.h"

/*Add for Lid-Switch*/
static PsbLidState parse_acpi_video_lidstate(void);
static void psbLVDSSetPanelPower(PsbLVDSOutputPtr pLVDS, Bool on);

/*
 * Check LidSwitch status via OpRegion SCI
 *
 * It'd be nicer at some point to handle the ACPI event for lidswitch.
 * And then we can remove this timer.
 *
 * Unfortunately, the xserver acpi handler only deals with "video" events
 * and not hotkey or button events. We should change that to handle
 * all events and let the driver to what it wants to.
 */
static PsbLidState
psbCheckLvdsLidStatus(PsbDevicePtr pDevice)
{
    PsbLidState lidState = PSB_LIDSTATE_OPEN;
    ScrnInfoPtr pScrn = pDevice->pScrns[0];
    volatile CARD32 *SciAddr;
    volatile CARD32 *SciParam;
    volatile CARD32 *SciDSLP;
    int loop;

    if (!pScrn->vtSema)
	return 1000;

#define SCIADDR 0x200/4
#define SCIPARAM 0x204/4
#define SCIDSLP 0x208/4

    SciAddr = pDevice->OpRegion + SCIADDR;
    SciParam = pDevice->OpRegion + SCIPARAM;
    SciDSLP = pDevice->OpRegion + SCIDSLP;

#define GET_PANEL_DETAILS 0x509

    *SciAddr = GET_PANEL_DETAILS;
    *SciParam = 0;

#define MAGIC_SCI 0x8001
    /* Go get the data from SCI */
    pciWriteLong(pDevice->pciTag, 0xE0, MAGIC_SCI);

    /* wait for the data */
    loop = (*SciDSLP == 0) ? 10 : *SciDSLP;
    while (loop--) {
	if (!(*SciAddr & 0x01))
	    break;
    }

    /* If the lidswitch is activated, put the display to sleep */
#define LID_MASK 0x10000

    if (*SciParam & LID_MASK) {
	lidState = PSB_LIDSTATE_CLOSED;
    }
    return lidState;
}

static CARD32
psbCheckDevicesLidStatusTimer(OsTimerPtr timer, CARD32 now, pointer arg)
{
    PsbLidState lidState = PSB_LIDSTATE_OPEN;
    PsbLVDSOutputPtr pLVDS = (PsbLVDSOutputPtr) arg;
    PsbDevicePtr pDevice = pLVDS->psbOutput.pDevice;
    ScrnInfoPtr pScrn = pDevice->pScrns[0];
    static PsbLidState oldSci = PSB_LIDSTATE_OPEN;

    if (!pScrn->vtSema)
	return 1000;

    //lidState = psbCheckLvdsLidStatus(pDevice);
    lidState = parse_acpi_video_lidstate();

    if (lidState == PSB_LIDSTATE_CLOSED) {
/*        xf86DrvMsg(pScrn->scrnIndex, X_INFO,"Lid Closed lidstate=%d oldstate=%d\n", lidState, oldSci);*/
	if (lidState != oldSci)
	    psbLVDSSetPanelPower(pLVDS, FALSE);
	//xf86DPMSSet(pScrn, DPMSModeStandby, 0);
    } else {
/*        xf86DrvMsg(pScrn->scrnIndex, X_INFO,"Lid Open\n");*/
	if (lidState != oldSci)
	    xf86DPMSSet(pScrn, DPMSModeOn, 0);
    }

    oldSci = lidState;
    return 1000;
}

/*
 * Sets the backlight data block from BIOS VBT
 */
void
i830_set_lvds_blc_data(ScrnInfoPtr pScrn, CARD8 blctype, CARD8 pol,
		       CARD16 freq, CARD8 minlevel, CARD8 address, CARD8 cmd)
{
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);

    pDevice->blc_data.type = blctype;
    pDevice->blc_data.pol = pol;
    pDevice->blc_data.freq = freq;
    pDevice->blc_data.minbrightness = minlevel;
    pDevice->blc_data.i2caddr = address;
    pDevice->blc_data.brightnesscmd = cmd;
}

/** Set BLC through I2C*/
static Bool
psbLVDSI2CSetBacklight(PsbLVDSOutputPtr pLVDS, unsigned char ch)
{
    PsbPtr pPsb = psbPTR(pLVDS->psbOutput.pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);

    if (xf86I2CProbeAddress(pLVDS->blc_d.pI2CBus, pLVDS->blc_d.SlaveAddr)) {
	if (!xf86I2CWriteByte
	    (&pLVDS->blc_d, pDevice->blc_data.brightnesscmd, ch)) {

	    xf86DrvMsg(pLVDS->blc_d.pI2CBus->scrnIndex, X_ERROR,
		       "Unable to write to %s Slave 0x%02x.\n",
		       pLVDS->blc_d.pI2CBus->BusName, pLVDS->blc_d.SlaveAddr);
	    return FALSE;
	}
    } else {
	xf86DrvMsg(pLVDS->blc_d.pI2CBus->scrnIndex, X_ERROR,
		   "Probe Address %s Slave 0x%02x failed.\n",
		   pLVDS->blc_d.pI2CBus->BusName, pLVDS->blc_d.SlaveAddr);
    }

    return TRUE;
}

/**
 * Calculate PWM control register value.
 */
static Bool
psbLVDSCalculatePWMCtrlRegFreq(PsbLVDSOutputPtr pLVDS)
{
    PsbPtr pPsb = psbPTR(pLVDS->psbOutput.pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);
    unsigned long long value = 0;

    if (pDevice->blc_data.freq == 0) {
	xf86DrvMsg(pLVDS->psbOutput.pScrn->scrnIndex, X_ERROR,
		   "psbLVDSCalculatePWMCtrlRegFreq:  Frequency Requested is 0.\n");
	return FALSE;
    }
    value = (pDevice->CoreClock * MHz);
    value = (value / BLC_PWM_FREQ_CALC_CONSTANT);
    value = (value * BLC_PWM_PRECISION_FACTOR);
    value = (value / pDevice->blc_data.freq);
    value = (value / BLC_PWM_PRECISION_FACTOR);

    if (value > (unsigned long long)BLC_MAX_PWM_REG_FREQ ||
	value < (unsigned long long)BLC_MIN_PWM_REG_FREQ) {
	return FALSE;
    } else {
	pDevice->PWMControlRegFreq =
	    ((CARD32) value & ~BLC_PWM_LEGACY_MODE_ENABLE);
	return TRUE;
    }
}

/**
 * Returns the maximum level of the backlight duty cycle field.
 */
static CARD32
psbLVDSGetPWMMaxBacklight(PsbLVDSOutputPtr pLVDS)
{
    PsbDevicePtr pDevice = pLVDS->psbOutput.pDevice;
    CARD32 max_pwm_blc = 0;

    max_pwm_blc =
	((PSB_READ32(BLC_PWM_CTL) & BACKLIGHT_MODULATION_FREQ_MASK) >>
	 BACKLIGHT_MODULATION_FREQ_SHIFT) * 2;

    if (!(max_pwm_blc & BLC_MAX_PWM_REG_FREQ)) {
	if (psbLVDSCalculatePWMCtrlRegFreq(pLVDS)) {
	    max_pwm_blc = pDevice->PWMControlRegFreq;
	}
    }

    return max_pwm_blc;
}

/**
 * Sets the backlight level.
 *
 * \param level backlight level, from 0 to psbLVDSGetMaxBacklight().
 */
static void
psbLVDSSetBacklight(PsbLVDSOutputPtr pLVDS, int level)
{
    PsbDevicePtr pDevice = pLVDS->psbOutput.pDevice;
    CARD32 blc_pwm_duty_cycle;
    CARD32 max_pwm_blc;
    unsigned long newbacklight = 0;

    PSB_DEBUG(pLVDS->psbOutput.pScrn->scrnIndex, 3,
	      "BLCType=%d Backlightg level = %d\n", pDevice->blc_data.type,
	      level);
    if (pDevice->blc_data.type == BLC_I2C_TYPE) {

	newbacklight = BRIGHTNESS_MASK & ((unsigned long)level *
					  BRIGHTNESS_MASK /
					  BRIGHTNESS_MAX_LEVEL);

	if (pDevice->blc_data.pol == BLC_POLARITY_INVERSE) {
	    newbacklight = BRIGHTNESS_MASK - newbacklight;
	}

	psbLVDSI2CSetBacklight(pLVDS, newbacklight);
    } else if (pDevice->blc_data.type == BLC_PWM_TYPE) {
	/* Provent LVDS going to total black */
	if (level < 20) {
	    level = 20;
	}

	max_pwm_blc = psbLVDSGetPWMMaxBacklight(pLVDS);

	blc_pwm_duty_cycle = level * max_pwm_blc / BRIGHTNESS_MAX_LEVEL;

	if (pDevice->blc_data.pol == BLC_POLARITY_INVERSE) {
	    blc_pwm_duty_cycle = max_pwm_blc - blc_pwm_duty_cycle;
	}

	blc_pwm_duty_cycle &= BACKLIGHT_PWM_POLARITY_BIT_CLEAR;

	PSB_WRITE32(BLC_PWM_CTL,
		    (max_pwm_blc << BACKLIGHT_PWM_CTL_SHIFT) |
		    (blc_pwm_duty_cycle));
    }
}

static CARD32
psbLVDSGetMaxBacklight(PsbLVDSOutputPtr pLVDS)
{
    return BRIGHTNESS_MAX_LEVEL;
}

static void
psbLVDSCheckState(PsbLVDSOutputPtr pLVDS)
{
    PsbDevicePtr pDevice = pLVDS->psbOutput.pDevice;

    PSB_DEBUG(-1, 3, "PanelPower Status = 0x%08x\n",
	      (unsigned)PSB_READ32(PP_STATUS));
    PSB_DEBUG(-1, 3, "Pipe B PLL 0x%08x\n", (unsigned)PSB_READ32(DPLL_B));
    PSB_DEBUG(-1, 3, "Pipe B Enabled 0x%08x\n",
	      (unsigned)PSB_READ32(PIPEBCONF) & (1 << 31));
}

/**
 * Sets the power state for the panel.
 */
static void
psbLVDSSetPanelPower(PsbLVDSOutputPtr pLVDS, Bool on)
{
    PsbDevicePtr pDevice = pLVDS->psbOutput.pDevice;
    CARD32 pp_status;
    PsbLidState lidState = PSB_LIDSTATE_OPEN;

    psbLVDSCheckState(pLVDS);
    if (on) {
	/*Check the Lid status*/
	lidState = parse_acpi_video_lidstate();

	PSB_DEBUG(-1, 3, "%s: lidState= %d\n", __FUNCTION__, lidState);
	if (lidState == PSB_LIDSTATE_CLOSED)
	    return;

	PSB_WRITE32(PP_CONTROL, PSB_READ32(PP_CONTROL) | POWER_TARGET_ON);
	do {
	    pp_status = PSB_READ32(PP_STATUS);
	} while ((pp_status & (PP_ON | PP_READY)) == PP_READY);

	psbLVDSSetBacklight(pLVDS, pLVDS->backlight);
    } else {
	psbLVDSSetBacklight(pLVDS, 0);

	PSB_WRITE32(PP_CONTROL, PSB_READ32(PP_CONTROL) & ~POWER_TARGET_ON);
	do {
	    pp_status = PSB_READ32(PP_STATUS);
	} while ((pp_status & PP_ON) == PP_ON);
    }
}

static void
psbLVDSDPMS(xf86OutputPtr output, int mode)
{
    PsbLVDSOutputPtr pLVDS =
	containerOf(output->driver_private, PsbLVDSOutputRec, psbOutput);

    PSB_DEBUG(output->scrn->scrnIndex, 3, "psbLVDSDPMS\n");

    if (output->scrn != pLVDS->psbOutput.pScrn)
	return;

    if (mode == DPMSModeOn)
	psbLVDSSetPanelPower(pLVDS, TRUE);
    else
	psbLVDSSetPanelPower(pLVDS, FALSE);

    /* XXX: We never power down the LVDS pair. */
}

static void
psbLVDSSave(xf86OutputPtr output)
{
    PsbLVDSOutputPtr pLVDS =
	containerOf(output->driver_private, PsbLVDSOutputRec, psbOutput);
    PsbDevicePtr pDevice = pLVDS->psbOutput.pDevice;

    PSB_DEBUG(output->scrn->scrnIndex, 3, "psbLVDSSave\n");

    pLVDS->savePP_ON = PSB_READ32(LVDSPP_ON);
    pLVDS->savePP_OFF = PSB_READ32(LVDSPP_OFF);
    pLVDS->saveLVDS = PSB_READ32(LVDS);
    pLVDS->savePP_CONTROL = PSB_READ32(PP_CONTROL);
    pLVDS->savePP_CYCLE = PSB_READ32(PP_CYCLE);
    pLVDS->saveBLC_PWM_CTL = PSB_READ32(BLC_PWM_CTL);
    pLVDS->backlight_duty_cycle = (pLVDS->saveBLC_PWM_CTL &
				   BACKLIGHT_DUTY_CYCLE_MASK);

    /*
     * If the light is off at server startup, just make it full brightness
     */
    if (pLVDS->backlight_duty_cycle == 0) {
	pLVDS->backlight = psbLVDSGetMaxBacklight(pLVDS);
//      pLVDS->backlight_duty_cycle = psbLVDSGetPWMMaxBacklight(pLVDS);
    }
}

static void
psbLVDSRestore(xf86OutputPtr output)
{
    PsbLVDSOutputPtr pLVDS =
	containerOf(output->driver_private, PsbLVDSOutputRec, psbOutput);
    PsbDevicePtr pDevice = pLVDS->psbOutput.pDevice;

    PSB_DEBUG(output->scrn->scrnIndex, 3, "psbLVDSRestore\n");

    PSB_WRITE32(LVDSPP_ON, pLVDS->savePP_ON);
    PSB_WRITE32(LVDSPP_OFF, pLVDS->savePP_OFF);
    PSB_WRITE32(PP_CYCLE, pLVDS->savePP_CYCLE);
    PSB_WRITE32(LVDS, pLVDS->saveLVDS);
    if (pLVDS->savePP_CONTROL & POWER_TARGET_ON)
	psbLVDSSetPanelPower(pLVDS, TRUE);
    else
	psbLVDSSetPanelPower(pLVDS, FALSE);
}

static int
psbLVDSModeValid(xf86OutputPtr output, DisplayModePtr pMode)
{
    PsbLVDSOutputPtr pLVDS =
	containerOf(output->driver_private, PsbLVDSOutputRec, psbOutput);
    DisplayModePtr pFixedMode = pLVDS->panelFixedMode;
    PsbDevicePtr pDevice = pLVDS->psbOutput.pDevice;
    PsbPtr pPsb = psbPTR(output->scrn);

    PSB_DEBUG(output->scrn->scrnIndex, 3, "psbLVDSModeValid\n");

    /* just in case */
    if (pMode->Flags & V_DBLSCAN)
	return MODE_NO_DBLESCAN;

    /* just in case */
    if (pMode->Flags & V_INTERLACE)
	return MODE_NO_INTERLACE;

    if (pFixedMode) {
	    if ( !pPsb->downScale) { 
		    if (pMode->HDisplay > pFixedMode->HDisplay) 
			    return MODE_PANEL;
		    if (pMode->VDisplay > pFixedMode->VDisplay)  
			    return MODE_PANEL;
	    } else {
		    /* downscaling supports up to 1024x768 */
		    /* XXX is it 1024x768, or 1024x1024? */
		    if (pMode->HDisplay > 1024) 
			    return MODE_PANEL;
		    if (pMode->VDisplay > 768)
			    return MODE_PANEL;

		    /* the overlay plane only supports downscaling,
		       not upscaling, for either X or Y direction */
		    if (pMode->HDisplay > pFixedMode->HDisplay &&
			pMode->VDisplay < pFixedMode->VDisplay) 
			    return MODE_PANEL;
		    if (pMode->VDisplay > pFixedMode->VDisplay && 
			pMode->HDisplay < pFixedMode->HDisplay)
			    return MODE_PANEL;
	    }
    }

#define MAX_HDISPLAY 800
#define MAX_VDISPLAY 480
    if (pDevice->sku_bMaxResEnableInt == TRUE) {
	    if (pMode->HDisplay > MAX_HDISPLAY)
		    return MODE_PANEL;
	    if (pMode->VDisplay > MAX_VDISPLAY)
		    return MODE_PANEL;
    }

    return MODE_OK;
}

static Bool
psbLVDSModeFixup(xf86OutputPtr output, DisplayModePtr mode,
		 DisplayModePtr adjusted_mode)
{
    ScrnInfoPtr pScrn = output->scrn;
    PsbPtr pPsb = psbPTR(pScrn);
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    PsbLVDSOutputPtr pLVDS =
	containerOf(output->driver_private, PsbLVDSOutputRec, psbOutput);
    PsbCrtcPrivatePtr intel_crtc = output->crtc->driver_private;
    DisplayModePtr pFixedMode = pLVDS->panelFixedMode;
    int i;

    PSB_DEBUG(output->scrn->scrnIndex, 3, "psbLVDSModeFixup\n");

    psbCheckCrtcs(pLVDS->psbOutput.pDevice);
    for (i = 0; i < xf86_config->num_output; i++) {
	xf86OutputPtr other_output = xf86_config->output[i];

	if (other_output != output && other_output->crtc == output->crtc) {
	    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		       "Can't enable LVDS and another output on the same pipe\n");
	    return FALSE;
	}
    }

    if (intel_crtc->pipe == 0) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "Can't support LVDS on pipe A\n");
	return FALSE;
    }

    /* If we have timings from the BIOS for the panel, put them in
     * to the adjusted mode.  The CRTC will be set up for this mode,
     * with the panel scaling set up to source from the H/VDisplay
     * of the original mode.
     */
    /* if support downscale, the LVDS's mode will not be adjusted
       unless the mode is smaller than the FixedMode */
    if (!pPsb->downScale ||
	mode->HDisplay <= pFixedMode->HDisplay && mode->VDisplay <= pFixedMode->VDisplay) {
	    if (pFixedMode != NULL) {
		    adjusted_mode->HDisplay = pFixedMode->HDisplay;
		    adjusted_mode->HSyncStart = pFixedMode->HSyncStart;
		    adjusted_mode->HSyncEnd = pFixedMode->HSyncEnd;
		    adjusted_mode->HTotal = pFixedMode->HTotal;
		    adjusted_mode->VDisplay = pFixedMode->VDisplay;
		    adjusted_mode->VSyncStart = pFixedMode->VSyncStart;
		    adjusted_mode->VSyncEnd = pFixedMode->VSyncEnd;
		    adjusted_mode->VTotal = pFixedMode->VTotal;
		    adjusted_mode->Clock = pFixedMode->Clock;
		    xf86SetModeCrtc(adjusted_mode, INTERLACE_HALVE_V);
	    }
    }

    /* XXX: if we don't have BIOS fixed timings (or we have
     * a preferred mode from DDC, probably), we should use the
     * DDC mode as the fixed timing.
     */

    /* XXX: It would be nice to support lower refresh rates on the
     * panels to reduce power consumption, and perhaps match the
     * user's requested refresh rate.
     */

    return TRUE;
}

static void
psbLVDSModeSet(xf86OutputPtr output, DisplayModePtr mode,
	       DisplayModePtr adjusted_mode)
{
    PsbLVDSOutputPtr pLVDS =
	containerOf(output->driver_private, PsbLVDSOutputRec, psbOutput);
    CARD32 pfit_control;
    PsbDevicePtr pDevice = pLVDS->psbOutput.pDevice;
    PsbPtr pPsb = psbPTR(output->scrn);

    PSB_DEBUG(output->scrn->scrnIndex, 3, "psbLVDSModeSet\n");

#if 0
    /* The LVDS pin pair needs to be on before the DPLLs are enabled.
     * This is an exception to the general rule that mode_set doesn't turn
     * things on.
     */
    PSB_WRITE32(LVDS, PSB_READ32(LVDS) | LVDS_PORT_EN | LVDS_PIPEB_SELECT);
#endif

    /* Enable automatic panel scaling so that non-native modes fill the
     * screen.  Should be enabled before the pipe is enabled, according to
     * register description and PRM.
     */
    if (pPsb->panelFittingMode == PSB_PANELFITTING_FIT) {
	pfit_control = (PFIT_ENABLE |
			VERT_AUTO_SCALE | HORIZ_AUTO_SCALE |
			VERT_INTERP_BILINEAR | HORIZ_INTERP_BILINEAR);
    } else {
	/*
	 * Currently only support centered mode.
	 */
	pfit_control = 0;
    }

    if (pLVDS->panelWantsDither)
	pfit_control |= PANEL_8TO6_DITHER_ENABLE;

    PSB_WRITE32(PFIT_CONTROL, pfit_control);

}

/**
 * Check panel lid status.
 *
 * This will check the panel lid status from /proc/acpi/button/lid entries
 */
static PsbLidState
parse_acpi_video_lidstate(void)
{
    int i;
    FILE *fin;
    char line[256];
    PsbLidState lidState = PSB_LIDSTATE_OPEN;

    fin = fopen(PROCLIDSTATE, "r");

    if (fin == NULL)
	return lidState;

    while (fgets(line, LIDSTAT_LINE_SIZE, fin)) {
	if ((line[0] == 's') &&
	    (line[1] == 't') &&
	    (line[2] == 'a') &&
	    (line[3] == 't') && (line[4] == 'e') && (line[5] == ':')) {
	    i = 6;
	    while (line[i] == ' ')
		i++;

	    if ((line[i++] == 'c') &&
		(line[i++] == 'l') &&
		(line[i++] == 'o') &&
		(line[i++] == 's') &&
		(line[i++] == 'e') && (line[i++] == 'd')) {

		lidState = PSB_LIDSTATE_CLOSED;

	    }
	}
    }
    fclose(fin);
    return lidState;

}

static PsbLidState
psbGetLidStatus(xf86OutputPtr output)
{
    ScrnInfoPtr pScrn = output->scrn;
    PsbDevicePtr pDevice = psbDevicePTR(psbPTR(pScrn));
    PsbLidState lidState = PSB_LIDSTATE_OPEN;

/* Opregion lid state detection is not working */
#if 0
    if (pDevice->OpRegion) {
	lidState = psbCheckLvdsLidStatus(pDevice);
    } else {
	lidState = parse_acpi_video_lidstate();
    }
#endif 

    lidState = parse_acpi_video_lidstate();

    PSB_DEBUG(output->scrn->scrnIndex, 2, "psbGetLidStatus lidState= %d\n",
	      lidState);
    return lidState;
}

/**
 * Detect the LVDS connection.
 *
 * This always returns OUTPUT_STATUS_CONNECTED.  This output should only have
 * been set up if the LVDS was actually connected anyway.
 */
static xf86OutputStatus
psbLVDSDetect(xf86OutputPtr output)
{
    PsbOutputPrivatePtr pOutput =
	(PsbOutputPrivatePtr) output->driver_private;

    PSB_DEBUG(output->scrn->scrnIndex, 3, "psbLVDSDetect %d\n",
	      pOutput->pScrn == output->scrn);

    if (pOutput->pScrn != output->scrn) {
	return XF86OutputStatusDisconnected;
    }

    if (psbGetLidStatus(output) == PSB_LIDSTATE_CLOSED) {
	return XF86OutputStatusDisconnected;
    }

    return XF86OutputStatusConnected;
}

/**
 * Return the list of DDC modes if available, or the BIOS fixed mode otherwise.
 */
static DisplayModePtr
psbLVDSGetModes(xf86OutputPtr output)
{
    PsbLVDSOutputPtr pLVDS =
	containerOf(output->driver_private, PsbLVDSOutputRec, psbOutput);
    xf86MonPtr edid_mon;
    DisplayModePtr modes;

    PSB_DEBUG(output->scrn->scrnIndex, 3, "psbLVDSGetModes\n");

    edid_mon = xf86OutputGetEDID(output, pLVDS->psbOutput.pDDCBus);
    xf86OutputSetEDID(output, edid_mon);

    modes = xf86OutputGetEDIDModes(output);
    if (modes != NULL)
	return modes;

    if (!output->MonInfo) {
	edid_mon = xcalloc(1, sizeof(xf86Monitor));
	if (edid_mon) {
	    /* Set wide sync ranges so we get all modes
	     * handed to valid_mode for checking
	     */
	    edid_mon->det_mon[0].type = DS_RANGES;
	    edid_mon->det_mon[0].section.ranges.min_v = 0;
	    edid_mon->det_mon[0].section.ranges.max_v = 200;
	    edid_mon->det_mon[0].section.ranges.min_h = 0;
	    edid_mon->det_mon[0].section.ranges.max_h = 200;

	    output->MonInfo = edid_mon;
	}
    }

    if (pLVDS->panelFixedMode != NULL)
	return xf86DuplicateMode(pLVDS->panelFixedMode);

    return NULL;
}

static void
psbLVDSDestroy(xf86OutputPtr output)
{
    PsbLVDSOutputPtr pLVDS =
	containerOf(output->driver_private, PsbLVDSOutputRec, psbOutput);
    PsbDevicePtr pDevice = pLVDS->psbOutput.pDevice;

    PSB_DEBUG(output->scrn->scrnIndex, 3, "psbLVDSDestroy\n");

    if (pLVDS && --pLVDS->psbOutput.refCount == 0) {
	psbOutputDestroy(&pLVDS->psbOutput);
	xfree(pLVDS);
	TimerCancel(pDevice->devicesTimer);
    }
    output->driver_private = NULL;
}

#ifdef RANDR_12_INTERFACE
#define BACKLIGHT_NAME	"BACKLIGHT"
static Atom backlight_atom;

#define PANELFITTING_NAME      "PANELFITTING"
static Atom panelfitting_atom;
#endif /* RANDR_12_INTERFACE */

static void
psbLVDSCreateResources(xf86OutputPtr output)
{
#ifdef RANDR_12_INTERFACE
    ScrnInfoPtr pScrn = output->scrn;
    PsbPtr pPsb = psbPTR(pScrn);
    PsbLVDSOutputPtr pLVDS =
	containerOf(output->driver_private, PsbLVDSOutputRec, psbOutput);
    PsbDevicePtr pDevice = pLVDS->psbOutput.pDevice;
    INT32 range[2];
    int data, err;

    /* Set up the Panel fitting property, which takes effect immediately
     */

    PSB_DEBUG(pScrn->scrnIndex, 3, "psbLVDSCreateResources\n");

    panelfitting_atom = MakeAtom(PANELFITTING_NAME,
				 sizeof(PANELFITTING_NAME) - 1, TRUE);

    pPsb->panelFittingMode = pPsb->noFitting ?
	PSB_PANELFITTING_CENTERED : PSB_PANELFITTING_FIT;

    range[0] = 0;
    range[1] = 2;

    data = pPsb->panelFittingMode;
    err = RRConfigureOutputProperty(output->randr_output,
				    panelfitting_atom, FALSE, TRUE, FALSE, 2,
				    range);
    if (err != 0) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "RRConfigureOutputProperty error, %d\n", err);
    }
    /* Set the current value of the panelfitting property */
    err = RRChangeOutputProperty(output->randr_output, panelfitting_atom,
				 XA_INTEGER, 32, PropModeReplace, 1, &data,
				 FALSE, TRUE);
    if (err != 0) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "RRChangeOutputProperty error, %d\n", err);
    }

    /* Set up the backlight property, which takes effect immediately
     * and accepts values only within the range.
     *
     * XXX: Currently, RandR doesn't verify that properties set are
     * within the range.
     */
    if ((pDevice->blc_data.type == BLC_I2C_TYPE)
	|| (pDevice->blc_data.type == BLC_PWM_TYPE)) {
	backlight_atom = MakeAtom(BACKLIGHT_NAME, sizeof(BACKLIGHT_NAME) - 1,
			      TRUE);

	range[0] = 0;
	range[1] = psbLVDSGetMaxBacklight(pLVDS);
	/* Set the backlight to max for now */
	pLVDS->backlight = psbLVDSGetMaxBacklight(pLVDS);
	data = pLVDS->backlight;

	err = RRConfigureOutputProperty(output->randr_output, backlight_atom,
					FALSE, TRUE, FALSE, 2, range);
	if (err != 0) {
	    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "RRConfigureOutputProperty error, %d\n", err);
	}

	/* Set the current value of the backlight property */
	err = RRChangeOutputProperty(output->randr_output, backlight_atom,
				 XA_INTEGER, 32, PropModeReplace, 1, &data,
				 FALSE, TRUE);
	if (err != 0) {
	    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "RRChangeOutputProperty error, %d\n", err);
	}
    }
#endif /* RANDR_12_INTERFACE */
}

#ifdef RANDR_12_INTERFACE
static Bool
psbLVDSSetProperty(xf86OutputPtr output, Atom property,
		   RRPropertyValuePtr value)
{
    ScrnInfoPtr pScrn = output->scrn;
    PsbLVDSOutputPtr pLVDS =
	containerOf(output->driver_private, PsbLVDSOutputRec, psbOutput);
    PsbPtr pPsb = psbPTR(pScrn);

    PSB_DEBUG(output->scrn->scrnIndex, 3, "psbLVDSSetProperty\n");

    if (property == backlight_atom) {
	INT32 val;

	if (value->type != XA_INTEGER || value->format != 32 ||
	    value->size != 1) {
	    return FALSE;
	}

	val = *(INT32 *) value->data;
	if (val < 0 || val > psbLVDSGetMaxBacklight(pLVDS))
	    return FALSE;

	psbLVDSSetBacklight(pLVDS, val);
	pLVDS->backlight = val;
	PSB_DEBUG(output->scrn->scrnIndex, 3,
		  "psbLVDSSetProperty BLC level %ld", val);
	return TRUE;
    }

    if (property == panelfitting_atom) {
	INT32 val;

	if (value->type != XA_INTEGER || value->format != 32 ||
	    value->size != 1) {
	    return FALSE;
	}

	val = *(INT32 *) value->data;

	pPsb->panelFittingMode = val;
	PSB_DEBUG(output->scrn->scrnIndex, 3,
		  "psbLVDSSetProperty  panelfitting %ld\n", val);

	if (pPsb->panelFittingMode < 2) {
	    xf86CrtcPtr xCrtc = NULL;
	    PsbCrtcPrivatePtr pCrtc;
	    int i;

	    for (i = 0; i < PSB_MAX_CRTCS; i++) {
		pCrtc = pPsb->crtcs[i]->driver_private;
		if (pCrtc->pipe == 1) {
		    xCrtc = pPsb->crtcs[i];
		    break;
		}
	    }
	    if (pCrtc->saved_mode.HDisplay != 0
		&& pCrtc->saved_mode.VDisplay != 0) {
		xCrtc->funcs->mode_set(xCrtc, &pCrtc->saved_mode,
				       &pCrtc->saved_adjusted_mode, pCrtc->x,
				       pCrtc->y);
		psbLVDSModeSet(output, NULL, NULL);
	    }
	}

	return TRUE;
    }

    return TRUE;
}
#endif /* RANDR_12_INTERFACE */

static const xf86OutputFuncsRec psbLVDSOutputFuncs = {
    .create_resources = psbLVDSCreateResources,
    .dpms = psbLVDSDPMS,
    .save = psbLVDSSave,
    .restore = psbLVDSRestore,
    .mode_valid = psbLVDSModeValid,
    .mode_fixup = psbLVDSModeFixup,
    .prepare = psbOutputPrepare,
    .mode_set = psbLVDSModeSet,
    .commit = psbOutputCommit,
    .detect = psbLVDSDetect,
    .get_modes = psbLVDSGetModes,
#ifdef RANDR_12_INTERFACE
    .set_property = psbLVDSSetProperty,
#endif
    .destroy = psbLVDSDestroy
};

static int
parse_acpi_video_lvdsid(void)
{
    DIR *acpi_video;
    struct dirent *d, *td, *device = NULL;
    int ret = 0;
    char *s1, *s2;

#define ACPI_PROC_VIDEO "/proc/acpi/video"

    acpi_video = opendir(ACPI_PROC_VIDEO);

    while (acpi_video && (d = readdir(acpi_video)))
	if (strncmp(".", d->d_name, 1))
	    device = d;

    closedir(acpi_video);

    if (!device)
	return 0;

    s1 = calloc(1, strlen(ACPI_PROC_VIDEO) + strlen(device->d_name) + 1);

    strncpy(s1, ACPI_PROC_VIDEO, strlen(ACPI_PROC_VIDEO));
    strncpy(s1 + strlen(ACPI_PROC_VIDEO), "/", 1);
    strncpy(s1 + strlen(ACPI_PROC_VIDEO) + 1, device->d_name,
	    strlen(device->d_name));

    acpi_video = opendir(s1);

    while (acpi_video && (d = readdir(acpi_video))) {
	DIR *dod;

	if (strncmp(".", d->d_name, 1)) {
	    s2 = calloc(1, strlen(s1) + strlen(d->d_name) + 1);

	    strncpy(s2, s1, strlen(s1));

	    strncpy(s2 + strlen(s1), "/", 1);
	    strncpy(s2 + strlen(s1) + 1, d->d_name, strlen(d->d_name));

	    dod = opendir(s2);
	    while (dod && (td = readdir(dod))) {
		if (!strncmp("info", td->d_name, 4)) {
		    int fd;
		    char *infofile;
		    char buffer[100];
		    char *device_id;
		    unsigned int devid;

		    infofile = calloc(1, strlen(s2) + 5);

		    strncpy(infofile, s2, strlen(s2));
		    strncpy(infofile + strlen(s2), "/", 1);
		    strncpy(infofile + strlen(s2) + 1, "info", 4);

		    fd = open(infofile, O_RDONLY);

		    if (fd > 0) {
			read(fd, buffer, 100);

			device_id = strstr(buffer, "0x");

			sscanf(device_id, "0x%x", &devid);

			if (devid & 0x400)
			    ret = 1;

			close(fd);
		    }

		    free(infofile);
		}
	    }
	    closedir(dod);

	    free(s2);
	}
    }

    closedir(acpi_video);

    return ret;
}

xf86OutputPtr
psbLVDSInit(ScrnInfoPtr pScrn, const char *name)
{
    PsbPtr pPsb = psbPTR(pScrn);
    xf86OutputPtr output;
    PsbLVDSOutputPtr pLVDS;
    DisplayModePtr modes, scan, bios_mode;
    PsbDevicePtr pDevice = psbDevicePTR(psbPTR(pScrn));

    PSB_DEBUG(pScrn->scrnIndex, 3, "psbLVDSInit\n");

    /* Check acpi video file for LVDS capability */
#ifdef __linux__
    if (pPsb->noPanel || (!pPsb->ignoreACPI && !parse_acpi_video_lvdsid())) {
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "No LVDS found (via ACPI)\n");
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "If you do have an LVDS panel, try adding Option \"IgnoreACPI\"\n");

	PSB_WRITE32(PP_CONTROL, PSB_READ32(PP_CONTROL) & ~POWER_TARGET_ON);
#if 0
	while ((PSB_READ32(PP_STATUS) & PP_ON) == PP_ON) ;
#endif
	return NULL;
    }
#endif

    output = xf86OutputCreate(pScrn, &psbLVDSOutputFuncs, name);
    if (!output)
	return NULL;
    pLVDS = xnfcalloc(sizeof(*pLVDS), 1);
    if (!pLVDS) {
	xf86OutputDestroy(output);
	return NULL;
    }
    psbOutputInit(psbDevicePTR(psbPTR(pScrn)), &pLVDS->psbOutput);

    pLVDS->psbOutput.type = PSB_OUTPUT_LVDS;
    pLVDS->psbOutput.refCount = 1;

    output->driver_private = &pLVDS->psbOutput;
    output->subpixel_order = SubPixelHorizontalRGB;
    output->interlaceAllowed = FALSE;
    output->doubleScanAllowed = FALSE;

    /* Set up the LVDS BLC control I2C.  Some panel will  use I2C instead of PWM
     */
    I830I2CInit(pScrn, &pLVDS->blc_d.pI2CBus, GPIOB, "LVDSBLC_B");

    pLVDS->blc_d.DevName = "BLC Control";
    pLVDS->blc_d.SlaveAddr = 0x58;
    pLVDS->blc_d.pI2CBus->HoldTime = 10;	/* 50 khz */
    pLVDS->blc_d.pI2CBus->StartTimeout = 10;
    pLVDS->blc_d.pI2CBus->BitTimeout = 10;
    pLVDS->blc_d.pI2CBus->ByteTimeout = 10;
    pLVDS->blc_d.pI2CBus->AcknTimeout = 10;

    /* Set up the LVDS DDC channel.  Most panels won't support it, but it can
     * be useful if available.
     */
    I830I2CInit(pScrn, &pLVDS->psbOutput.pDDCBus, GPIOC, "LVDSDDC_C");

    pLVDS->blc_d.DriverPrivate.ptr = output;

    if (!xf86I2CDevInit(&pLVDS->blc_d)) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "Failed to initialize %s I2C device\n",
		   pLVDS->blc_d.DevName);
    }
    /* Attempt to get the fixed panel mode from DDC.  Assume that the preferred
     * mode is the right one.
     */
    modes = psbOutputDDCGetModes(output);
    for (scan = modes; scan != NULL; scan = scan->next) {
	if (scan->type & M_T_PREFERRED)
	    break;
    }
    if (scan != NULL) {
	/* Pull our chosen mode out and make it the fixed mode */
	if (modes == scan)
	    modes = modes->next;
	if (scan->prev != NULL)
	    scan->prev = scan->next;
	if (scan->next != NULL)
	    scan->next = scan->prev;
	pLVDS->panelFixedMode = scan;
    }
    /* Delete the mode list */
    while (modes != NULL)
	xf86DeleteMode(&modes, modes);

    /* If we didn't get EDID, try checking if the panel is already turned on.
     * If so, assume that whatever is currently programmed is the correct mode.
     * FIXME: Better method, please.
     */

    if (pLVDS->panelFixedMode == NULL) {
	CARD32 lvds = PSB_READ32(LVDS);
	xf86CrtcRec crtc;	       /*faked */
	PsbCrtcPrivateRec pCrtc;

	memset(&crtc, 0, sizeof(xf86CrtcRec));
	pCrtc.pipe = 1;
	crtc.driver_private = &pCrtc;

	if (lvds & LVDS_PORT_EN) {
	    pLVDS->panelFixedMode = psbCrtcModeGet(pScrn, &crtc);
	    if (pLVDS->panelFixedMode != NULL)
		pLVDS->panelFixedMode->type |= M_T_PREFERRED;
	} else
	    /* Fall through to BIOS mode. */
	    ;
    }
    /* Get the LVDS fixed mode out of the BIOS.  We should support LVDS with
     * the BIOS being unavailable or broken, but lack the configuration options
     * for now.
     */
    bios_mode = i830_bios_get_panel_mode(pScrn, &pLVDS->panelWantsDither);
    if (bios_mode != NULL) {
	if (pLVDS->panelFixedMode != NULL) {
	    if (!xf86ModesEqual(pLVDS->panelFixedMode, bios_mode)) {
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			   "BIOS panel mode data doesn't match probed data, "
			   "continuing with probed.\n");
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "BIOS mode:\n");
		xf86PrintModeline(pScrn->scrnIndex, bios_mode);
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "probed mode:\n");
		xf86PrintModeline(pScrn->scrnIndex, pLVDS->panelFixedMode);
		xfree(bios_mode->name);
		xfree(bios_mode);
	    }
	} else {
	    pLVDS->panelFixedMode = bios_mode;
	}
    } else {
	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "Couldn't detect panel mode.  Disabling panel\n");
	goto disable_exit;
    }

    /* set timer to examine LVDS lidswitch status */
    if (pDevice->OpRegion && pPsb->lidTimer) {
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Lid state timer is enabled!\n");
	pDevice->devicesTimer = TimerSet(NULL, 0, 1000, psbCheckDevicesLidStatusTimer, pLVDS);
    }

    return output;

  disable_exit:
    xf86DestroyI2CBusRec(pLVDS->psbOutput.pDDCBus, TRUE, TRUE);
    pLVDS->psbOutput.pDDCBus = NULL;
    xf86DestroyI2CBusRec(pLVDS->blc_d.pI2CBus, TRUE, TRUE);
    pLVDS->blc_d.pI2CBus = NULL;
    xf86OutputDestroy(output);
    return NULL;
}
