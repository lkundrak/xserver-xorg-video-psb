/**************************************************************************

 Copyright 2006 Dave Airlie <airlied@linux.ie>

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
on the rights to use, copy, modify, merge, publish, distribute, sub
license, and/or sell copies of the Software, and to permit persons to whom
the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
THE COPYRIGHT HOLDERS AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
USE OR OTHER DEALINGS IN THE SOFTWARE.

**************************************************************************/

/** @file
 * SDVO support for i915 and newer chipsets.
 *
 * The SDVO outputs send digital display data out over the PCIE bus to display
 * cards implementing a defined interface.  These cards may have DVI, TV, CRT,
 * or other outputs on them.
 *
 * The system has two SDVO channels, which may be used for SDVO chips on the
 * motherboard, or in the external cards.  The two channels may also be used
 * in a ganged mode to provide higher bandwidth to a single output.  Currently,
 * this code doesn't deal with either ganged mode or more than one SDVO output.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <unistd.h>
#include <psb_reg.h>

#include "xf86.h"
#include "psb_driver.h"
#include "i810_reg.h"
#include "i830.h"
#include "i830_bios.h"
#include "X11/Xatom.h"
#include "i830_sdvo_regs.h"
#include "libmm/mm_defines.h"

#undef DUMP_HDMI_BUFFER

/*Extended EDID modes table index table*/
unsigned long  g_CeShortVideoModes[] = {
                m_ModeUID(640,480,60,0),
                m_ModeUID(720,480,60,0),
                m_ModeUID(720,480,60,0),
                m_ModeUID(1280,720,60,0),
                m_ModeUID(1920,1080,60,1),
                m_ModeUID(720,480,60,1),
                m_ModeUID(720,480,60,1),
                m_ModeUID(720,240,60,0),
                m_ModeUID(720,240,60,0),
                m_ModeUID(2880,480,60,1),
                m_ModeUID(2880,480,60,1),
                m_ModeUID(2880,240,60,0),
                m_ModeUID(2880,240,60,0),
                m_ModeUID(1440,480,60,0),
                m_ModeUID(1440,480,60,0),
                m_ModeUID(1920,1080,60,0),
                m_ModeUID(720,576,50,0),
                m_ModeUID(720,576,50,0),
                m_ModeUID(1280,720,50,0),
                m_ModeUID(1920,1080,50,1),
                m_ModeUID(720,576,50,1),
                m_ModeUID(720,576,50,1),
                m_ModeUID(720,288,50,0),
                m_ModeUID(720,288,50,0),
                m_ModeUID(2880,576,50,1),
                m_ModeUID(2880,576,50,1),
                m_ModeUID(2880,288,50,0),
                m_ModeUID(2880,288,50,0),
                m_ModeUID(1440,576,50,0),
                m_ModeUID(1440,576,50,0),
                m_ModeUID(1920,1080,50,0),
                m_ModeUID(1920,1080,24,0),
                m_ModeUID(1920,1080,25,0),
                m_ModeUID(1920,1080,30,0),
        };
#define TableLen  sizeof(g_CeShortVideoModes) / sizeof(unsigned long)

#define SII_WA 1
#define CH_7312_WA 1

#ifdef SII_WA
#define SII_1362_VEN_ID 0x4
#define SII_1362_DEV_ID 0xAA
#define SII_1362_DEV_REV_ID 0x2

#define SII_VEN_ID 0x4
#define SII_1362A_DEV_ID 0xAA
#define SII_1362A_DEV_REV_ID 0x3

#define SII_1368_DEV_ID 0xAB
#define SII_1368_DEV_REV0_ID 0x00

#define SII_1390_DEV_ID 0xAC
#define SII_1390_DEV_REV0_ID 0x00

#define SII_1362_WA_REG 0x34
#define SII_1362_AUTOZONESWITCH_WA_REG 0x51
#endif	

#ifdef CH_7312_WA
#define CH_7312_VEN_ID        2
#define CH_7312_DEV_ID        0x43
#define CH_7312_DEV_REV_ID    0
#define CH7312_WORKAROUND_REG 0x20
#define CH7312_INIT_WORKAROUND_VALUE 0x02
#define CH7312_FINAL_WORKAROUND_VALUE 0x03
#endif 

#define MAX_VAL 1000

typedef struct _EXTVDATA
{
    CARD32 Value;
    CARD32 Default;
    CARD32 Min;
    CARD32 Max;
    CARD32 Step;		       // arbitrary unit (e.g. pixel, percent) returned during VP_COMMAND_GET
} EXTVDATA, *PEXTVDATA;

typedef struct _sdvo_display_params
{
    EXTVDATA FlickerFilter;	       /* Flicker Filter : for TV onl */
    EXTVDATA AdaptiveFF;	       /* Adaptive Flicker Filter : for TV onl */
    EXTVDATA TwoD_FlickerFilter;       /* 2D Flicker Filter : for TV onl */
    EXTVDATA Brightness;	       /* Brightness : for TV & CRT onl */
    EXTVDATA Contrast;		       /* Contrast : for TV & CRT onl */
    EXTVDATA PositionX;		       /* Horizontal Position : for all device */
    EXTVDATA PositionY;		       /* Vertical Position : for all device */
    /*EXTVDATA    OverScanX;         Horizontal Overscan : for TV onl */
    EXTVDATA DotCrawl;		       /* Dot crawl value : for TV onl */
    EXTVDATA ChromaFilter;	       /* Chroma Filter : for TV onl */
    /* EXTVDATA    OverScanY;        Vertical Overscan : for TV onl */
    EXTVDATA LumaFilter;	       /* Luma Filter : for TV only */
    EXTVDATA Sharpness;		       /* Sharpness : for TV & CRT onl */
    EXTVDATA Saturation;	       /* Saturation : for TV & CRT onl */
    EXTVDATA Hue;		       /* Hue : for TV & CRT onl */
    EXTVDATA Dither;		       /* Dither : For LVDS onl */
} sdvo_display_params;

typedef enum _SDVO_PICTURE_ASPECT_RATIO_T
{
    UAIM_PAR_NO_DATA = 0x00000000,
    UAIM_PAR_4_3 = 0x00000100,
    UAIM_PAR_16_9 = 0x00000200,
    UAIM_PAR_FUTURE = 0x00000300,
    UAIM_PAR_MASK = 0x00000300,
} SDVO_PICTURE_ASPECT_RATIO_T;

typedef enum _SDVO_FORMAT_ASPECT_RATIO_T
{
    UAIM_FAR_NO_DATA = 0x00000000,
    UAIM_FAR_SAME_AS_PAR = 0x00002000,
    UAIM_FAR_4_BY_3_CENTER = 0x00002400,
    UAIM_FAR_16_BY_9_CENTER = 0x00002800,
    UAIM_FAR_14_BY_9_CENTER = 0x00002C00,
    UAIM_FAR_16_BY_9_LETTERBOX_TOP = 0x00000800,
    UAIM_FAR_14_BY_9_LETTERBOX_TOP = 0x00000C00,
    UAIM_FAR_GT_16_BY_9_LETTERBOX_CENTER = 0x00002000,
    UAIM_FAR_4_BY_3_SNP_14_BY_9_CENTER = 0x00003400,	/* With shoot and protect 14:9 cente */
    UAIM_FAR_16_BY_9_SNP_14_BY_9_CENTER = 0x00003800,	/* With shoot and protect 14:9 cente */
    UAIM_FAR_16_BY_9_SNP_4_BY_3_CENTER = 0x00003C00,	/* With shoot and protect 4:3 cente */
    UAIM_FAR_MASK = 0x00003C00,
} SDVO_FORMAT_ASPECT_RATIO_T;

// TV image aspect ratio
typedef enum _CP_IMAGE_ASPECT_RATIO
{
    CP_ASPECT_RATIO_FF_4_BY_3 = 0,
    CP_ASPECT_RATIO_14_BY_9_CENTER = 1,
    CP_ASPECT_RATIO_14_BY_9_TOP = 2,
    CP_ASPECT_RATIO_16_BY_9_CENTER = 3,
    CP_ASPECT_RATIO_16_BY_9_TOP = 4,
    CP_ASPECT_RATIO_GT_16_BY_9_CENTER = 5,
    CP_ASPECT_RATIO_FF_4_BY_3_PROT_CENTER = 6,
    CP_ASPECT_RATIO_FF_16_BY_9_ANAMORPHIC = 7,
} CP_IMAGE_ASPECT_RATIO;

typedef struct _SDVO_ANCILLARY_INFO_T
{
    CP_IMAGE_ASPECT_RATIO AspectRatio;
    CARD32 RedistCtrlFlag;	       /* Redistribution control flag (get and set */
} SDVO_ANCILLARY_INFO_T, *PSDVO_ANCILLARY_INFO_T;

/** SDVO driver private structure. */
typedef struct _PsbSDVOOutputRec
{
    PsbOutputPrivateRec psbOutput;

    /** SDVO device on SDVO I2C bus. */
    I2CDevRec d;

    /** Register for the SDVO device: SDVOB or SDVOC */
    int output_device;

    /** Active outputs controlled by this SDVO output */
    CARD16 active_outputs;

    /**
     * Capabilities of the SDVO device returned by i830_sdvo_get_capabilities()
     */
    struct i830_sdvo_caps caps;

    /** Pixel clock limitations reported by the SDVO device, in kHz */
    int pixel_clock_min, pixel_clock_max;

    /** State for save/restore */
    /** @{ */
    int save_sdvo_mult;
    CARD16 save_active_outputs;
    struct i830_sdvo_dtd save_input_dtd_1, save_input_dtd_2;
    struct i830_sdvo_dtd save_output_dtd[16];
    CARD32 save_in0output;
    CARD32 save_in1output;
    CARD32 save_SDVOX;
    /** @} */
	/**
	* TV out support
	*/
    CARD32 ActiveDevice;	       /* CRT, TV, LVDS, TMDS */
    CARD32 TVStandard;		       /* PAL, NTSC */
    int TVOutput;		       /* S-Video, CVBS,YPbPr,RGB */
    int TVMode;			       /* SDTV/HDTV/SECAM mod */
    CARD32 TVStdBitmask;
    CARD32 dwSDVOHDTVBitMask;
    CARD32 dwSDVOSDTVBitMask;
    CARD8 byInputWiring;
    Bool bGetClk;
    CARD32 dwMaxDotClk;
    CARD32 dwMinDotClk;

    CARD32 dwMaxInDotClk;
    CARD32 dwMinInDotClk;

    CARD32 dwMaxOutDotClk;
    CARD32 dwMinOutDotClk;
    CARD32 dwSupportedEnhancements;
    EXTVDATA OverScanY;		       /* Vertical Overscan : for TV onl */
    EXTVDATA OverScanX;		       /* Horizontal Overscan : for TV onl */
    sdvo_display_params dispParams;
    SDVO_ANCILLARY_INFO_T AncillaryInfo;
	DisplayModePtr currentMode;

} PsbSDVOOutputRec, *PsbSDVOOutputPtr;

/* Define TV mode type */
/* The full set are defined in xf86str.h*/
#define M_T_TV 0x80

#define MODEPREFIX(name) NULL, NULL, name, MODE_OK, M_T_DRIVER
#define MODEPREFIX_PRE(name) NULL, NULL, name, MODE_OK, M_T_PREFERRED|M_T_DRIVER
#define MODESUFFIX       0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,FALSE,FALSE,0,NULL,0,0.0,0.0

#define MODEPREFIX_TV(name) NULL, NULL, name, MODE_OK, M_T_DRIVER|M_T_TV
#define MODEPREFIX_PRE_TV(name) NULL, NULL, name, MODE_OK, M_T_PREFERRED|M_T_DRIVER|M_T_TV

typedef struct _tv_mode_t
{
    /* the following data is detailed mode information as it would be passed to the hardware: */
    DisplayModeRec mode_entry;
    CARD32 dwSupportedSDTVvss;
    CARD32 dwSupportedHDTVvss;
    Bool m_preferred;
    Bool isTVMode;
} tv_mode_t;

static tv_mode_t tv_modes[] = {
    {
     .mode_entry =
     {MODEPREFIX_PRE_TV("800x600"), 0x2625a00 / 1000, 800, 840, 968, 1056, 0,
      600, 601,
      604, 628, 0, V_PHSYNC | V_PVSYNC, MODESUFFIX},
     .dwSupportedSDTVvss = TVSTANDARD_SDTV_ALL,
     .dwSupportedHDTVvss = TVSTANDARD_HDTV_ALL,
     .m_preferred = TRUE,
     .isTVMode = TRUE,
     },
    {
     .mode_entry =
     {MODEPREFIX_TV("1024x768"), 0x3dfd240 / 1000, 1024, 0x418, 0x49f, 0x540,
      0, 768,
      0x303, 0x308, 0x325, 0, V_PHSYNC | V_PVSYNC, MODESUFFIX},
     .dwSupportedSDTVvss = TVSTANDARD_SDTV_ALL,
     .dwSupportedHDTVvss = TVSTANDARD_HDTV_ALL,
     .m_preferred = FALSE,
     .isTVMode = TRUE,
     },
    {
     .mode_entry =
     {MODEPREFIX_TV("720x480"), 0x1978ff0 / 1000, 720, 0x2e1, 0x326, 0x380, 0,
      480,
      0x1f0, 0x1e1, 0x1f1, 0, V_PHSYNC | V_PVSYNC, MODESUFFIX},
     .dwSupportedSDTVvss =
     TVSTANDARD_NTSC_M | TVSTANDARD_NTSC_M_J | TVSTANDARD_NTSC_433,
     .dwSupportedHDTVvss = 0x0,
     .m_preferred = FALSE,
     .isTVMode = TRUE,
     },
    {
     /*Modeline "720x576_SDVO"   0.96 720 756 788 864  576 616 618 700 +vsync  */
     .mode_entry =
     {MODEPREFIX_TV("720x576"), 0x1f25a20 / 1000, 720, 756, 788, 864, 0, 576,
      616,
      618, 700, 0, V_PHSYNC | V_PVSYNC, MODESUFFIX},
     .dwSupportedSDTVvss =
     (TVSTANDARD_PAL_B | TVSTANDARD_PAL_D | TVSTANDARD_PAL_H |
      TVSTANDARD_PAL_I | TVSTANDARD_PAL_N | TVSTANDARD_SECAM_B |
      TVSTANDARD_SECAM_D | TVSTANDARD_SECAM_G | TVSTANDARD_SECAM_H |
      TVSTANDARD_SECAM_K | TVSTANDARD_SECAM_K1 | TVSTANDARD_SECAM_L |
      TVSTANDARD_PAL_G | TVSTANDARD_SECAM_L1),
     .dwSupportedHDTVvss = 0x0,
     .m_preferred = FALSE,
     .isTVMode = TRUE,
     },
    {
     .mode_entry =
     {MODEPREFIX_TV("1280x720@60"), 74250000 / 1000, 1280, 1390, 1430, 1650,
      0,
      720,
      725, 730, 750, 0, V_PHSYNC | V_PVSYNC, MODESUFFIX},
     .dwSupportedSDTVvss = 0x0,
     .dwSupportedHDTVvss = HDTV_SMPTE_296M_720p60,
     .m_preferred = FALSE,
     .isTVMode = TRUE,
     },
    {
     .mode_entry =
     {MODEPREFIX_TV("1280x720@50"), 74250000 / 1000, 1280, 1720, 1759, 1980,
      0,
      720,
      725, 730, 750, 0, V_PHSYNC | V_PVSYNC, MODESUFFIX},
     .dwSupportedSDTVvss = 0x0,
     .dwSupportedHDTVvss = HDTV_SMPTE_296M_720p50,
     .m_preferred = FALSE,
     .isTVMode = TRUE,
     },
    {
     .mode_entry =
     {MODEPREFIX_TV("1920x1080@60"), 148500000 / 1000, 1920, 2008, 2051, 2200,
      0,
      1080,
      1084, 1088, 1124, 0, V_PHSYNC | V_PVSYNC, MODESUFFIX},
     .dwSupportedSDTVvss = 0x0,
     .dwSupportedHDTVvss = HDTV_SMPTE_274M_1080i60,
     .m_preferred = FALSE,
     .isTVMode = TRUE,
     },
};

#define NUM_TV_MODES sizeof(tv_modes) / sizeof (tv_modes[0])

/**
 * Build a mode list containing all of the default modes
 */
DisplayModePtr
i830_sdvo_get_tvmode_from_table(xf86OutputPtr output)
{
    DisplayModePtr head = NULL, prev = NULL, mode;
    int i;
	
    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);

    for (i = 0; i < NUM_TV_MODES; i++) {

	/*DisplayModePtr defMode = &tv_modes[i].mode_entry; */

	if (((pSDVO->TVMode == TVMODE_HDTV) && /*hdtv mode list */
		(tv_modes[i].dwSupportedHDTVvss & TVSTANDARD_HDTV_ALL)) ||
		((pSDVO->TVMode == TVMODE_SDTV) && /*sdtv mode list */
		(tv_modes[i].dwSupportedSDTVvss & TVSTANDARD_SDTV_ALL))) {
	    mode = xalloc(sizeof(DisplayModeRec));
	    if (!mode)
	        continue;
	    memcpy(mode, &tv_modes[i].mode_entry, sizeof(DisplayModeRec));
	    mode->name = xstrdup(tv_modes[i].mode_entry.name);
	    if (!mode->name) {
	        xfree(mode);
	        continue;
	    }
	    xf86SetModeCrtc(mode, 0);
	    mode->prev = prev;
	    mode->next = NULL;
	    if (prev)
	        prev->next = mode;
	    else
	        head = mode;
	    prev = mode;
	}	
    }
    return head;
}

/**
 * Writes the SDVOB or SDVOC with the given value, but always writes both
 * SDVOB and SDVOC to work around apparent hardware issues (according to
 * comments in the BIOS).
 */
static void
i830_sdvo_write_sdvox(xf86OutputPtr output, CARD32 val)
{
    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);
    PsbDevicePtr pDevice = pSDVO->psbOutput.pDevice;
    CARD32 bval = val, cval = val;
    int i;

    if (pSDVO->output_device == SDVOB)
	cval = PSB_READ32(SDVOC);
    else
	bval = PSB_READ32(SDVOB);

    /*
     * Write the registers twice for luck. Sometimes,
     * writing them only once doesn't appear to 'stick'.
     * The BIOS does this too. Yay, magic
     */
    for (i = 0; i < 2; i++) {
	PSB_WRITE32(SDVOB, bval);
	PSB_READ32(SDVOB);
	PSB_WRITE32(SDVOC, cval);
	PSB_READ32(SDVOC);
    }
}

/** Read a single byte from the given address on the SDVO device. */
static Bool
i830_sdvo_read_byte(xf86OutputPtr output, int addr, unsigned char *ch)
{
    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);
    PsbOutputPrivatePtr intel_output = output->driver_private;

    if (!xf86I2CReadByte(&pSDVO->d, addr, ch)) {
	xf86DrvMsg(intel_output->pI2CBus->scrnIndex, X_ERROR,
		   "Unable to read from %s slave 0x%02x.\n",
		   intel_output->pI2CBus->BusName, pSDVO->d.SlaveAddr);
	return FALSE;
    }
    return TRUE;
}

/** Read a single byte from the given address on the SDVO device. */
static Bool
i830_sdvo_read_byte_quiet(xf86OutputPtr output, int addr, unsigned char *ch)
{
    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);

    return xf86I2CReadByte(&pSDVO->d, addr, ch);
}

/** Write a single byte to the given address on the SDVO device. */
static Bool
i830_sdvo_write_byte(xf86OutputPtr output, int addr, unsigned char ch)
{
    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);
    PsbOutputPrivatePtr intel_output = output->driver_private;

    if (!xf86I2CWriteByte(&pSDVO->d, addr, ch)) {
	xf86DrvMsg(intel_output->pI2CBus->scrnIndex, X_ERROR,
		   "Unable to write to %s Slave 0x%02x.\n",
		   intel_output->pI2CBus->BusName, pSDVO->d.SlaveAddr);
	return FALSE;
    }
    return TRUE;
}

#define SDVO_CMD_NAME_ENTRY(cmd) {cmd, #cmd}
/** Mapping of command numbers to names, for debug output */
const static struct _sdvo_cmd_name
{
    CARD8 cmd;
    char *name;
} sdvo_cmd_names[] = {
SDVO_CMD_NAME_ENTRY(SDVO_CMD_RESET),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_DEVICE_CAPS),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_FIRMWARE_REV),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_TRAINED_INPUTS),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_ACTIVE_OUTPUTS),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_ACTIVE_OUTPUTS),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_IN_OUT_MAP),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_IN_OUT_MAP),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_ATTACHED_DISPLAYS),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_HOT_PLUG_SUPPORT),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_ACTIVE_HOT_PLUG),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_ACTIVE_HOT_PLUG),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_INTERRUPT_EVENT_SOURCE),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_TARGET_INPUT),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_TARGET_OUTPUT),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_INPUT_TIMINGS_PART1),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_INPUT_TIMINGS_PART2),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_INPUT_TIMINGS_PART1),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_INPUT_TIMINGS_PART2),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_INPUT_TIMINGS_PART1),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_OUTPUT_TIMINGS_PART1),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_OUTPUT_TIMINGS_PART2),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_OUTPUT_TIMINGS_PART1),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_OUTPUT_TIMINGS_PART2),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_CREATE_PREFERRED_INPUT_TIMING),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_PREFERRED_INPUT_TIMING_PART1),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_PREFERRED_INPUT_TIMING_PART2),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_INPUT_PIXEL_CLOCK_RANGE),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_OUTPUT_PIXEL_CLOCK_RANGE),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_SUPPORTED_CLOCK_RATE_MULTS),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_CLOCK_RATE_MULT),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_CLOCK_RATE_MULT),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_SUPPORTED_TV_FORMATS),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_TV_FORMATS),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_TV_FORMATS),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_SUPPORTED_POWER_STATES),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_ENCODER_POWER_STATE),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_ENCODER_POWER_STATE),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_TV_RESOLUTION_SUPPORT),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_CONTROL_BUS_SWITCH),
	// HDMI Op Code
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_SUPP_ENCODE),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_ENCODE),
        SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_PIXEL_REPLI),
        SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_PIXEL_REPLI),
        SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_COLORIMETRY_CAP),
        SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_COLORIMETRY),
        SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_COLORIMETRY),
        SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_AUDIO_STATE),
        SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_AUDIO_STATE),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_HBUF_INDEX),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_HBUF_INFO),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_HBUF_AV_SPLIT),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_HBUF_AV_SPLIT),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_HBUF_DATA),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_HBUF_DATA),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_HBUF_TXRATE),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_AUDIO_TX_INFO),
};

static const char *cmd_status_names[] = {
    "Power on",
    "Success",
    "Not supported",
    "Invalid arg",
    "Pending",
    "Target not specified",
    "Scaling not supported"
};

static I2CSlaveAddr slaveAddr;

#define SDVO_NAME(dev_priv) ((dev_priv)->output_device == SDVOB ? "SDVO" : "SDVO")

/**
 * Writes out the data given in args (up to 8 bytes), followed by the opcode.
 */
static void
i830_sdvo_write_cmd(xf86OutputPtr output, CARD8 cmd, void *args, int args_len)
{
    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);
    int i;

    if (slaveAddr && slaveAddr != pSDVO->d.SlaveAddr)
	xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
		   "Mismatch slave addr %x != %x\n", slaveAddr,
		   pSDVO->d.SlaveAddr);
	
	PSB_DEBUG(0,2, "%s: W: %02X ", SDVO_NAME(pSDVO), cmd);
	for (i = 0; i < args_len; i++)
			LogWrite(1,"%02X ", ((CARD8 *)args)[i]);
	for (; i < 8; i++)
			LogWrite(1,"   ");

    for (i = 0; i < sizeof(sdvo_cmd_names) / sizeof(sdvo_cmd_names[0]); i++) {
	if (cmd == sdvo_cmd_names[i].cmd) {
	    LogWrite(1, "(i830_%s)\n", sdvo_cmd_names[i].name);
	    break;
	}
    }

    /* send the output regs */
    for (i = 0; i < args_len; i++) {
	i830_sdvo_write_byte(output, SDVO_I2C_ARG_0 - i, ((CARD8 *) args)[i]);
    }
    /* blast the command reg */
    i830_sdvo_write_byte(output, SDVO_I2C_OPCODE, cmd);
}

/**
 * Reads back response_len bytes from the SDVO device, and returns the status.
 */
static CARD8
i830_sdvo_read_response(xf86OutputPtr output, void *response,
			int response_len)
{

    int i;
    CARD8 status;
    CARD8 retry = 50;
    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);
    PsbOutputPrivatePtr intel_output = output->driver_private;

    while (retry--) {
	/* Read the return status */
	i830_sdvo_read_byte(output, SDVO_I2C_CMD_STATUS, &status);

	/* Read the command response */
	for (i = 0; i < response_len; i++) {
	    i830_sdvo_read_byte(output, SDVO_I2C_RETURN_0 + i,
				&((CARD8 *) response)[i]);
	}

	/* Write the SDVO command logging */
	if (1) {
	    xf86DrvMsg(intel_output->pI2CBus->scrnIndex, X_INFO,
		       "%s: R: ", SDVO_NAME(pSDVO));
	    for (i = 0; i < response_len; i++)
		LogWrite(1, "%02X ", ((CARD8 *) response)[i]);
	    for (; i < 8; i++)
		LogWrite(1, "   ");
	    if (status <= SDVO_CMD_STATUS_SCALING_NOT_SUPP) {
		LogWrite(1, "(%s)", cmd_status_names[status]);
	    } else {
		LogWrite(1, "(??? %d)", status);
	    }
	    LogWrite(1, "\n");
	}

	if (status != SDVO_CMD_STATUS_PENDING)
	    return status;

	usleep(50);
    }

    return status;
}

int
i830_sdvo_get_pixel_multiplier(DisplayModePtr pMode)
{
    if (pMode->Clock >= 100000)
	return 1;
    else if (pMode->Clock >= 50000)
	return 2;
    else
	return 4;
}

/* Sets the control bus switch to either point at one of the DDC buses or the
 * PROM.  It resets from the DDC bus back to internal registers at the next I2C
 * STOP.  PROM access is terminated by accessing an internal register.
 */
static void
i830_sdvo_set_control_bus_switch(xf86OutputPtr output, CARD8 target)
{
    i830_sdvo_write_cmd(output, SDVO_CMD_SET_CONTROL_BUS_SWITCH, &target, 1);
}

static Bool
i830_sdvo_set_target_input(xf86OutputPtr output, Bool target_0, Bool target_1)
{
    struct i830_sdvo_set_target_input_args targets = { 0 };
    CARD8 status;

    if (target_0 && target_1)
	return SDVO_CMD_STATUS_NOTSUPP;

    if (target_1)
	targets.target_1 = 1;

    i830_sdvo_write_cmd(output, SDVO_CMD_SET_TARGET_INPUT, &targets,
			sizeof(targets));

    status = i830_sdvo_read_response(output, NULL, 0);

    return (status == SDVO_CMD_STATUS_SUCCESS);
}

/**
 * Return whether each input is trained.
 *
 * This function is making an assumption about the layout of the response,
 * which should be checked against the docs.
 */
static Bool
i830_sdvo_get_trained_inputs(xf86OutputPtr output, Bool * input_1,
			     Bool * input_2)
{
    struct i830_sdvo_get_trained_inputs_response response;
    CARD8 status;

    i830_sdvo_write_cmd(output, SDVO_CMD_GET_TRAINED_INPUTS, NULL, 0);

    status = i830_sdvo_read_response(output, &response, sizeof(response));
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;
    /* Fill up the output values; */
    *input_1 = (response.input0_trained & 0x1);
    *input_2 = (response.input1_trained & 0x2);

    return TRUE;
}

static Bool
i830_sdvo_get_active_outputs(xf86OutputPtr output, CARD16 * outputs)
{
    CARD8 status;

    i830_sdvo_write_cmd(output, SDVO_CMD_GET_ACTIVE_OUTPUTS, NULL, 0);
    status = i830_sdvo_read_response(output, outputs, sizeof(*outputs));

    return (status == SDVO_CMD_STATUS_SUCCESS);
}

static Bool
i830_sdvo_set_active_outputs(xf86OutputPtr output, CARD16 outputs)
{
    CARD8 status;

    i830_sdvo_write_cmd(output, SDVO_CMD_SET_ACTIVE_OUTPUTS, &outputs,
			sizeof(outputs));
    status = i830_sdvo_read_response(output, NULL, 0);

    return (status == SDVO_CMD_STATUS_SUCCESS);
}

static Bool
i830_sdvo_set_encoder_power_state(xf86OutputPtr output, int mode)
{
    CARD8 status;
    CARD8 state;

    switch (mode) {
    case DPMSModeOn:
	state = SDVO_ENCODER_STATE_ON;
	break;
    case DPMSModeStandby:
	state = SDVO_ENCODER_STATE_STANDBY;
	break;
    case DPMSModeSuspend:
	state = SDVO_ENCODER_STATE_SUSPEND;
	break;
    case DPMSModeOff:
	state = SDVO_ENCODER_STATE_OFF;
	break;
    }

    i830_sdvo_write_cmd(output, SDVO_CMD_SET_ENCODER_POWER_STATE, &state,
			sizeof(state));
    status = i830_sdvo_read_response(output, NULL, 0);

    return (status == SDVO_CMD_STATUS_SUCCESS);
}

/**
 * Returns the pixel clock range limits of the current target input in kHz.
 */
static Bool
i830_sdvo_get_input_pixel_clock_range(xf86OutputPtr output, int *clock_min,
				      int *clock_max)
{
    struct i830_sdvo_pixel_clock_range clocks;
    CARD8 status;

    i830_sdvo_write_cmd(output, SDVO_CMD_GET_INPUT_PIXEL_CLOCK_RANGE, NULL,
			0);

    status = i830_sdvo_read_response(output, &clocks, sizeof(clocks));

    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    /* Convert the values from units of 10 kHz to kHz. */
    *clock_min = clocks.min * 10;
    *clock_max = clocks.max * 10;

    return TRUE;
}

static Bool
i830_sdvo_set_target_output(xf86OutputPtr output, CARD16 outputs)
{
    CARD8 status;

    i830_sdvo_write_cmd(output, SDVO_CMD_SET_TARGET_OUTPUT, &outputs,
			sizeof(outputs));

    status = i830_sdvo_read_response(output, NULL, 0);

    return (status == SDVO_CMD_STATUS_SUCCESS);
}

/** Fetches either input or output timings to *dtd, depending on cmd. */
static Bool
i830_sdvo_get_timing(xf86OutputPtr output, CARD8 cmd,
		     struct i830_sdvo_dtd *dtd)
{
    CARD8 status;

    i830_sdvo_write_cmd(output, cmd, NULL, 0);

    status = i830_sdvo_read_response(output, &dtd->part1, sizeof(dtd->part1));
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    i830_sdvo_write_cmd(output, cmd + 1, NULL, 0);

    status = i830_sdvo_read_response(output, &dtd->part2, sizeof(dtd->part2));
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    return TRUE;
}

static Bool
i830_sdvo_get_input_timing(xf86OutputPtr output, struct i830_sdvo_dtd *dtd)
{
    return i830_sdvo_get_timing(output, SDVO_CMD_GET_INPUT_TIMINGS_PART1,
				dtd);
}

static Bool
i830_sdvo_get_output_timing(xf86OutputPtr output, struct i830_sdvo_dtd *dtd)
{
    return i830_sdvo_get_timing(output, SDVO_CMD_GET_OUTPUT_TIMINGS_PART1,
				dtd);
}

/** Sets either input or output timings from *dtd, depending on cmd. */
static Bool
i830_sdvo_set_timing(xf86OutputPtr output, CARD8 cmd,
		     struct i830_sdvo_dtd *dtd)
{
    CARD8 status;

    i830_sdvo_write_cmd(output, cmd, &dtd->part1, sizeof(dtd->part1));
    status = i830_sdvo_read_response(output, NULL, 0);
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    i830_sdvo_write_cmd(output, cmd + 1, &dtd->part2, sizeof(dtd->part2));
    status = i830_sdvo_read_response(output, NULL, 0);
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    return TRUE;
}

static Bool
i830_sdvo_set_input_timing(xf86OutputPtr output, struct i830_sdvo_dtd *dtd)
{
    return i830_sdvo_set_timing(output, SDVO_CMD_SET_INPUT_TIMINGS_PART1,
				dtd);
}

static Bool
i830_sdvo_set_output_timing(xf86OutputPtr output, struct i830_sdvo_dtd *dtd)
{
    return i830_sdvo_set_timing(output, SDVO_CMD_SET_OUTPUT_TIMINGS_PART1,
				dtd);
}

#if 0
static Bool
i830_sdvo_create_preferred_input_timing(xf86OutputPtr output, CARD16 clock,
					CARD16 width, CARD16 height)
{
    struct i830_sdvo_priv *dev_priv = output->dev_priv;
    struct i830_sdvo_preferred_input_timing_args args;

    args.clock = clock;
    args.width = width;
    args.height = height;
    i830_sdvo_write_cmd(output, SDVO_CMD_CREATE_PREFERRED_INPUT_TIMING,
			&args, sizeof(args));
    status = i830_sdvo_read_response(output, NULL, 0);
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    return TRUE;
}

static Bool
i830_sdvo_get_preferred_input_timing(PsbOutputPtr output,
				     struct i830_sdvo_dtd *dtd)
{
    struct i830_sdvo_priv *dev_priv = output->dev_priv;

    i830_sdvo_write_cmd(output, SDVO_CMD_GET_PREFERRED_INPUT_TIMING_PART1,
			NULL, 0);

    status = i830_sdvo_read_response(output, &dtd->part1, sizeof(dtd->part1));
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    i830_sdvo_write_cmd(output, SDVO_CMD_GET_PREFERRED_INPUT_TIMING_PART2,
			NULL, 0);

    status = i830_sdvo_read_response(output, &dtd->part2, sizeof(dtd->part2));
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    return TRUE;
}
#endif

/** Returns the SDVO_CLOCK_RATE_MULT_* for the current clock multiplier */
static int
i830_sdvo_get_clock_rate_mult(xf86OutputPtr output)
{
    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);
    CARD8 response;
    CARD8 status;

    i830_sdvo_write_cmd(output, SDVO_CMD_GET_CLOCK_RATE_MULT, NULL, 0);
    status = i830_sdvo_read_response(output, &response, 1);

    if (status != SDVO_CMD_STATUS_SUCCESS) {
	xf86DrvMsg(pSDVO->d.pI2CBus->scrnIndex, X_ERROR,
		   "Couldn't get SDVO clock rate multiplier\n");
	return SDVO_CLOCK_RATE_MULT_1X;
    } else {
	xf86DrvMsg(pSDVO->d.pI2CBus->scrnIndex, X_INFO,
		   "Current clock rate multiplier: %d\n", response);
    }

    return response;
}

/**
 * Sets the current clock multiplier.
 *
 * This has to match with the settings in the DPLL/SDVO reg when the output
 * is actually turned on.
 */
static Bool
i830_sdvo_set_clock_rate_mult(xf86OutputPtr output, CARD8 val)
{
    CARD8 status;

    i830_sdvo_write_cmd(output, SDVO_CMD_SET_CLOCK_RATE_MULT, &val, 1);
    status = i830_sdvo_read_response(output, NULL, 0);
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    return TRUE;
}

static void
i830_sdvo_map_hdtvstd_bitmask(xf86OutputPtr output)
{
    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);
    switch (pSDVO->TVStandard) {
#if 0

    case HDTV_SMPTE_240M_1080i59:
	*pTVStdBitmask = SDVO_HDTV_STD_240M_1080i59;
	break;

    case HDTV_SMPTE_240M_1080i60:
	*pTVStdBitmask = SDVO_HDTV_STD_240M_1080i60;
	break;

    case HDTV_SMPTE_260M_1080i59:
	*pTVStdBitmask = SDVO_HDTV_STD_260M_1080i59;
	break;

    case HDTV_SMPTE_260M_1080i60:
	*pTVStdBitmask = SDVO_HDTV_STD_260M_1080i60;
	break;
#endif
    case HDTV_SMPTE_274M_1080i50:
	pSDVO->TVStdBitmask = SDVO_HDTV_STD_274M_1080i50;
	break;

    case HDTV_SMPTE_274M_1080i59:
	pSDVO->TVStdBitmask = SDVO_HDTV_STD_274M_1080i59;
	break;

    case HDTV_SMPTE_274M_1080i60:
	pSDVO->TVStdBitmask = SDVO_HDTV_STD_274M_1080i60;
	break;
#if 0
    case HDTV_SMPTE_274M_1080p23:
	*pTVStdBitmask = SDVO_HDTV_STD_274M_1080p23;
	break;

    case HDTV_SMPTE_274M_1080p24:
	*pTVStdBitmask = SDVO_HDTV_STD_274M_1080p24;
	break;

    case HDTV_SMPTE_274M_1080p25:
	*pTVStdBitmask = SDVO_HDTV_STD_274M_1080p25;
	break;

    case HDTV_SMPTE_274M_1080p29:
	*pTVStdBitmask = SDVO_HDTV_STD_274M_1080p29;
	break;

    case HDTV_SMPTE_274M_1080p30:
	*pTVStdBitmask = SDVO_HDTV_STD_274M_1080p30;
	break;

    case HDTV_SMPTE_274M_1080p50:
	*pTVStdBitmask = SDVO_HDTV_STD_274M_1080p50;
	break;

    case HDTV_SMPTE_274M_1080p59:
	*pTVStdBitmask = SDVO_HDTV_STD_274M_1080p59;
	break;
#endif
    case HDTV_SMPTE_274M_1080p60:
	pSDVO->TVStdBitmask = SDVO_HDTV_STD_274M_1080p60;
	break;
#if 0
    case HDTV_SMPTE_295M_1080i50:
	*pTVStdBitmask = SDVO_HDTV_STD_295M_1080i50;
	break;

    case HDTV_SMPTE_295M_1080p50:
	*pTVStdBitmask = SDVO_HDTV_STD_295M_1080p50;
	break;
	break;
#endif
    case HDTV_SMPTE_296M_720p59:
	pSDVO->TVStdBitmask = SDVO_HDTV_STD_296M_720p59;
	break;

    case HDTV_SMPTE_296M_720p60:
	pSDVO->TVStdBitmask = SDVO_HDTV_STD_296M_720p60;
	break;

    case HDTV_SMPTE_296M_720p50:
	pSDVO->TVStdBitmask = SDVO_HDTV_STD_296M_720p50;
	break;

    case HDTV_SMPTE_293M_480p59:
	pSDVO->TVStdBitmask = SDVO_HDTV_STD_293M_480p59;
	break;

    case HDTV_SMPTE_293M_480p60:
	pSDVO->TVStdBitmask = SDVO_HDTV_STD_EIA_7702A_480p60;
	break;

    case HDTV_SMPTE_170M_480i59:
	pSDVO->TVStdBitmask = SDVO_HDTV_STD_170M_480i59;
	break;

    case HDTV_ITURBT601_576i50:
	pSDVO->TVStdBitmask = SDVO_HDTV_STD_ITURBT601_576i50;
	break;

    case HDTV_ITURBT601_576p50:
	pSDVO->TVStdBitmask = SDVO_HDTV_STD_ITURBT601_576p50;
	break;
    default:
	PSB_DEBUG(output->scrn->scrnIndex, 3,
		  "ERROR: Unknown TV Standard!!!\n");
	/*Invalid return 0 */
	pSDVO->TVStdBitmask = 0;
    }

}

static void
i830_sdvo_map_sdtvstd_bitmask(xf86OutputPtr output)
{
    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);
    switch (pSDVO->TVStandard) {
    case TVSTANDARD_NTSC_M:
	pSDVO->TVStdBitmask = SDVO_NTSC_M;
	break;

    case TVSTANDARD_NTSC_M_J:
	pSDVO->TVStdBitmask = SDVO_NTSC_M_J;
	break;

    case TVSTANDARD_NTSC_433:
	pSDVO->TVStdBitmask = SDVO_NTSC_433;
	break;

    case TVSTANDARD_PAL_B:
	pSDVO->TVStdBitmask = SDVO_PAL_B;
	break;

    case TVSTANDARD_PAL_D:
	pSDVO->TVStdBitmask = SDVO_PAL_D;
	break;

    case TVSTANDARD_PAL_G:
	pSDVO->TVStdBitmask = SDVO_PAL_G;
	break;

    case TVSTANDARD_PAL_H:
	pSDVO->TVStdBitmask = SDVO_PAL_H;
	break;

    case TVSTANDARD_PAL_I:
	pSDVO->TVStdBitmask = SDVO_PAL_I;
	break;

    case TVSTANDARD_PAL_M:
	pSDVO->TVStdBitmask = SDVO_PAL_M;
	break;

    case TVSTANDARD_PAL_N:
	pSDVO->TVStdBitmask = SDVO_PAL_N;
	break;

    case TVSTANDARD_PAL_60:
	pSDVO->TVStdBitmask = SDVO_PAL_60;
	break;

    case TVSTANDARD_SECAM_B:
	pSDVO->TVStdBitmask = SDVO_SECAM_B;
	break;

    case TVSTANDARD_SECAM_D:
	pSDVO->TVStdBitmask = SDVO_SECAM_D;
	break;

    case TVSTANDARD_SECAM_G:
	pSDVO->TVStdBitmask = SDVO_SECAM_G;
	break;

    case TVSTANDARD_SECAM_K:
	pSDVO->TVStdBitmask = SDVO_SECAM_K;
	break;

    case TVSTANDARD_SECAM_K1:
	pSDVO->TVStdBitmask = SDVO_SECAM_K1;
	break;

    case TVSTANDARD_SECAM_L:
	pSDVO->TVStdBitmask = SDVO_SECAM_L;
	break;

    case TVSTANDARD_SECAM_L1:
	PSB_DEBUG(output->scrn->scrnIndex, 3,
		  "TVSTANDARD_SECAM_L1 not supported by encoder\n");
	break;

    case TVSTANDARD_SECAM_H:
	PSB_DEBUG(output->scrn->scrnIndex, 3,
		  "TVSTANDARD_SECAM_H not supported by encoder\n");
	break;

    default:
	PSB_DEBUG(output->scrn->scrnIndex, 3, "ERROR: Unknown TV Standard\n");
	/*Invalid return 0 */
	pSDVO->TVStdBitmask = 0;
	break;
    }
	PSB_DEBUG(0, 2, "TVStandard is %x, TVStdBitmask is %x\n",pSDVO->TVStandard,pSDVO->TVStdBitmask);
}

static Bool
i830_sdvo_set_tvoutputs_formats(xf86OutputPtr output)
{
    CARD8 byArgs[6];
    CARD8 status;
    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);

    /* Make all fields of the  args/ret to zero */
    memset(byArgs, 0, sizeof(byArgs));

    if (pSDVO->TVMode & (TVMODE_SDTV)) {
	/* Fill up the arguement value */
	byArgs[0] = (CARD8) (pSDVO->TVStdBitmask & 0xFF);
	byArgs[1] = (CARD8) ((pSDVO->TVStdBitmask >> 8) & 0xFF);
	byArgs[2] = (CARD8) ((pSDVO->TVStdBitmask >> 16) & 0xFF);
    } else {
	/* Fill up the arguement value */
	byArgs[0] = 0;
	byArgs[1] = 0;
	byArgs[2] = (CARD8) ((pSDVO->TVStdBitmask & 0xFF));
	byArgs[3] = (CARD8) ((pSDVO->TVStdBitmask >> 8) & 0xFF);
	byArgs[4] = (CARD8) ((pSDVO->TVStdBitmask >> 16) & 0xFF);
	byArgs[5] = (CARD8) ((pSDVO->TVStdBitmask >> 24) & 0xFF);
    }

    i830_sdvo_write_cmd(output, SDVO_CMD_SET_TV_FORMATS, byArgs, 6);
    status = i830_sdvo_read_response(output, NULL, 0);

    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    return TRUE;

}

static Bool
i830_sdvo_create_preferred_input_timing(xf86OutputPtr output,
					DisplayModePtr mode)
{
    CARD8 byArgs[7];
    CARD8 status;
    CARD32 dwClk;
    CARD32 dwHActive, dwVActive;
    Bool bIsInterlaced, bIsScaled;

    /* Make all fields of the  args/ret to zero */
    memset(byArgs, 0, sizeof(byArgs));

    /* Fill up the arguement values */
    dwHActive = mode->CrtcHDisplay;
    dwVActive = mode->CrtcVDisplay;

    dwClk = mode->Clock * 1000 / 10000;
    byArgs[0] = (CARD8) (dwClk & 0xFF);
    byArgs[1] = (CARD8) ((dwClk >> 8) & 0xFF);

    /* HActive & VActive should not exceed 12 bits each. So check it */
    if ((dwHActive > 0xFFF) || (dwVActive > 0xFFF))
	return FALSE;

    byArgs[2] = (CARD8) (dwHActive & 0xFF);
    byArgs[3] = (CARD8) ((dwHActive >> 8) & 0xF);
    byArgs[4] = (CARD8) (dwVActive & 0xFF);
    byArgs[5] = (CARD8) ((dwVActive >> 8) & 0xF);

    bIsInterlaced = 1;
    bIsScaled = 0;

    byArgs[6] = bIsInterlaced ? 1 : 0;
    byArgs[6] |= bIsScaled ? 2 : 0;

    i830_sdvo_write_cmd(output, SDVO_CMD_CREATE_PREFERRED_INPUT_TIMINGS,
			byArgs, 7);
    status = i830_sdvo_read_response(output, NULL, 0);

    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    return TRUE;

}

static Bool
i830_sdvo_get_preferred_input_timing(xf86OutputPtr output,
				     struct i830_sdvo_dtd *output_dtd)
{
    return i830_sdvo_get_timing(output,
				SDVO_CMD_GET_PREFERRED_INPUT_TIMING_PART1,
				output_dtd);
}

static Bool
i830_sdvo_get_current_inoutmap(xf86OutputPtr output, CARD32 * in0output,
			       CARD32 * in1output)
{
    CARD8 byArgs[4];
    CARD8 status;

    i830_sdvo_write_cmd(output, SDVO_CMD_GET_IN_OUT_MAP, byArgs, 4);
    status = i830_sdvo_read_response(output, NULL, 0);

    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    *in0output = byArgs[0] | byArgs[1] << 8;
    *in1output = byArgs[2] | byArgs[3] << 8;

    return TRUE;
}

static Bool
i830_sdvo_set_current_inoutmap(xf86OutputPtr output, CARD32 in0outputmask,
			       CARD32 in1outputmask)
{
    CARD8 byArgs[4];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byArgs, 0, sizeof(byArgs));

    /* Fill up the arguement values; */
    byArgs[0] = (CARD8) (in0outputmask & 0xFF);
    byArgs[1] = (CARD8) ((in0outputmask >> 8) & 0xFF);
    byArgs[2] = (CARD8) (in1outputmask & 0xFF);
    byArgs[3] = (CARD8) ((in1outputmask >> 8) & 0xFF);
    i830_sdvo_write_cmd(output, SDVO_CMD_SET_IN_OUT_MAP, byArgs, 4);
    status = i830_sdvo_read_response(output, NULL, 0);

    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    return TRUE;

}

static Bool
i830_sdvo_get_input_output_pixelclock_range(xf86OutputPtr output,
					    Bool direction)
{
    CARD8 byRets[4];
    CARD8 status;

    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);
    /* Make all fields of the  args/ret to zero */
    memset(byRets, 0, sizeof(byRets));
    if (direction)		       /* output pixel clock */
	i830_sdvo_write_cmd(output, SDVO_CMD_GET_OUTPUT_PIXEL_CLOCK_RANGE,
			    NULL, 0);
    else
	i830_sdvo_write_cmd(output, SDVO_CMD_GET_INPUT_PIXEL_CLOCK_RANGE,
			    NULL, 0);
    status = i830_sdvo_read_response(output, byRets, 4);

    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    if (direction) {
	/* Fill up the return values. */
	pSDVO->dwMinOutDotClk =
	    (CARD32) byRets[0] | ((CARD32) byRets[1] << 8);
	pSDVO->dwMaxOutDotClk =
	    (CARD32) byRets[2] | ((CARD32) byRets[3] << 8);

	/* Multiply 10000 with the clocks obtained */
	pSDVO->dwMinOutDotClk = (pSDVO->dwMinOutDotClk) * 10000;
	pSDVO->dwMaxOutDotClk = (pSDVO->dwMaxOutDotClk) * 10000;

    } else {
	/* Fill up the return values. */
	pSDVO->dwMinInDotClk = (CARD32) byRets[0] | ((CARD32) byRets[1] << 8);
	pSDVO->dwMaxInDotClk = (CARD32) byRets[2] | ((CARD32) byRets[3] << 8);

	/* Multiply 10000 with the clocks obtained */
	pSDVO->dwMinInDotClk = (pSDVO->dwMinInDotClk) * 10000;
	pSDVO->dwMaxInDotClk = (pSDVO->dwMaxInDotClk) * 10000;
    }
    PSB_DEBUG(output->scrn->scrnIndex, 3, "MinDotClk = 0x%lx\n",
	      pSDVO->dwMinInDotClk);
    PSB_DEBUG(output->scrn->scrnIndex, 3, "MaxDotClk = 0x%lx\n",
	      pSDVO->dwMaxInDotClk);

    return TRUE;

}

static Bool
i830_sdvo_get_supported_tvoutput_formats(xf86OutputPtr output,
					 CARD32 * pTVStdMask,
					 CARD32 * pHDTVStdMask)
{
    CARD8 byRets[6];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byRets, 0, sizeof(byRets));

    /* Send the arguements & SDVO opcode to the h/w */
    i830_sdvo_write_cmd(output, SDVO_CMD_GET_SUPPORTED_TV_FORMATS, NULL, 0);

    status = i830_sdvo_read_response(output, byRets, 6);
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    /* Fill up the return values; */
    *pTVStdMask = (((CARD32) byRets[0]) |
		   ((CARD32) byRets[1] << 8) |
		   ((CARD32) (byRets[2] & 0x7) << 16));

    *pHDTVStdMask = (((CARD32) byRets[2] & 0xF8) |
		     ((CARD32) byRets[3] << 8) |
		     ((CARD32) byRets[4] << 16) | ((CARD32) byRets[5] << 24));
    return TRUE;

}

static Bool
i830_sdvo_get_supported_enhancements(xf86OutputPtr output,
				     CARD32 * psupported_enhancements)
{

    CARD8 status;
    CARD8 byRets[2];
    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);

    /* Make all fields of the  args/ret to zero */
    memset(byRets, 0, sizeof(byRets));

    /* Send the arguements & SDVO opcode to the h/w */
    i830_sdvo_write_cmd(output, SDVO_CMD_GET_SUPPORTED_ENHANCEMENTS, NULL, 0);

    status = i830_sdvo_read_response(output, byRets, 2);
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    pSDVO->dwSupportedEnhancements = *psupported_enhancements =
	((CARD32) byRets[0] | ((CARD32) byRets[1] << 8));
    return TRUE;

}

static Bool
i830_sdvo_reset(xf86OutputPtr output)
{
    CARD8 status;

    i830_sdvo_write_cmd(output, SDVO_CMD_RESET, NULL, 0);

    status = i830_sdvo_read_response(output, NULL, 0);
    if (status != SDVO_CMD_STATUS_POWER_ON)
	return FALSE;

    return TRUE;
}

static Bool
i830_sdvo_get_max_horizontal_overscan(xf86OutputPtr output, CARD32 * pMaxVal,
				      CARD32 * pDefaultVal)
{
    CARD8 byRets[4];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byRets, 0, sizeof(byRets));

    /* Send the arguements & SDVO opcode to the h/w */
    i830_sdvo_write_cmd(output, SDVO_CMD_GET_MAX_HORIZONTAL_OVERSCAN, NULL,
			0);

    status = i830_sdvo_read_response(output, byRets, 4);
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;
    /* Fill up the return values. */
    *pMaxVal = (CARD32) byRets[0] | ((CARD32) byRets[1] << 8);
    *pDefaultVal = (CARD32) byRets[2] | ((CARD32) byRets[3] << 8);
    return TRUE;
}

static Bool
i830_sdvo_get_max_vertical_overscan(xf86OutputPtr output, CARD32 * pMaxVal,
				    CARD32 * pDefaultVal)
{
    CARD8 byRets[4];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byRets, 0, sizeof(byRets));

    /* Send the arguements & SDVO opcode to the h/w */
    i830_sdvo_write_cmd(output, SDVO_CMD_GET_MAX_VERTICAL_OVERSCAN, NULL, 0);

    status = i830_sdvo_read_response(output, byRets, 4);
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;
    /* Fill up the return values. */
    *pMaxVal = (CARD32) byRets[0] | ((CARD32) byRets[1] << 8);
    *pDefaultVal = (CARD32) byRets[2] | ((CARD32) byRets[3] << 8);
    return TRUE;
}

static Bool
i830_sdvo_get_max_horizontal_position(xf86OutputPtr output, CARD32 * pMaxVal,
				      CARD32 * pDefaultVal)
{

    CARD8 byRets[4];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byRets, 0, sizeof(byRets));

    /* Send the arguements & SDVO opcode to the h/w */
    i830_sdvo_write_cmd(output, SDVO_CMD_GET_MAX_HORIZONTAL_POSITION, NULL,
			0);

    status = i830_sdvo_read_response(output, byRets, 4);
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    /* Fill up the return values. */
    *pMaxVal = (CARD32) byRets[0] | ((CARD32) byRets[1] << 8);
    *pDefaultVal = (CARD32) byRets[2] | ((CARD32) byRets[3] << 8);

    return TRUE;
}

static Bool
i830_sdvo_get_max_vertical_position(xf86OutputPtr output,
				    CARD32 * pMaxVal, CARD32 * pDefaultVal)
{

    CARD8 byRets[4];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byRets, 0, sizeof(byRets));

    /* Send the arguements & SDVO opcode to the h/w */
    i830_sdvo_write_cmd(output, SDVO_CMD_GET_MAX_VERTICAL_POSITION, NULL, 0);

    status = i830_sdvo_read_response(output, byRets, 4);
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    /* Fill up the return values. */
    *pMaxVal = (CARD32) byRets[0] | ((CARD32) byRets[1] << 8);
    *pDefaultVal = (CARD32) byRets[2] | ((CARD32) byRets[3] << 8);

    return TRUE;
}

static Bool
i830_sdvo_get_max_flickerfilter(xf86OutputPtr output,
				CARD32 * pMaxVal, CARD32 * pDefaultVal)
{

    CARD8 byRets[4];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byRets, 0, sizeof(byRets));

    /* Send the arguements & SDVO opcode to the h/w */

    i830_sdvo_write_cmd(output, SDVO_CMD_GET_MAX_FLICKER_FILTER, NULL, 0);

    status = i830_sdvo_read_response(output, byRets, 4);
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;
    /* Fill up the return values. */
    *pMaxVal = (CARD32) byRets[0] | ((CARD32) byRets[1] << 8);
    *pDefaultVal = (CARD32) byRets[2] | ((CARD32) byRets[3] << 8);

    return TRUE;
}

static Bool
i830_sdvo_get_max_brightness(xf86OutputPtr output,
			     CARD32 * pMaxVal, CARD32 * pDefaultVal)
{

    CARD8 byRets[4];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byRets, 0, sizeof(byRets));

    /* Send the arguements & SDVO opcode to the h/w */

    i830_sdvo_write_cmd(output, SDVO_CMD_GET_MAX_BRIGHTNESS, NULL, 0);

    status = i830_sdvo_read_response(output, byRets, 4);
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;
    /* Fill up the return values. */
    *pMaxVal = (CARD32) byRets[0] | ((CARD32) byRets[1] << 8);
    *pDefaultVal = (CARD32) byRets[2] | ((CARD32) byRets[3] << 8);

    return TRUE;
}

static Bool
i830_sdvo_get_max_contrast(xf86OutputPtr output,
			   CARD32 * pMaxVal, CARD32 * pDefaultVal)
{

    CARD8 byRets[4];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byRets, 0, sizeof(byRets));

    /* Send the arguements & SDVO opcode to the h/w */

    i830_sdvo_write_cmd(output, SDVO_CMD_GET_MAX_CONTRAST, NULL, 0);

    status = i830_sdvo_read_response(output, byRets, 4);
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;
    /* Fill up the return values. */
    *pMaxVal = (CARD32) byRets[0] | ((CARD32) byRets[1] << 8);
    *pDefaultVal = (CARD32) byRets[2] | ((CARD32) byRets[3] << 8);

    return TRUE;
}

static Bool
i830_sdvo_get_max_sharpness(xf86OutputPtr output,
			    CARD32 * pMaxVal, CARD32 * pDefaultVal)
{

    CARD8 byRets[4];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byRets, 0, sizeof(byRets));

    /* Send the arguements & SDVO opcode to the h/w */

    i830_sdvo_write_cmd(output, SDVO_CMD_GET_MAX_SHARPNESS, NULL, 0);

    status = i830_sdvo_read_response(output, byRets, 4);
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    /* Fill up the return values. */
    *pMaxVal = (CARD32) byRets[0] | ((CARD32) byRets[1] << 8);
    *pDefaultVal = (CARD32) byRets[2] | ((CARD32) byRets[3] << 8);

    return TRUE;
}

static Bool
i830_sdvo_get_max_hue(xf86OutputPtr output,
		      CARD32 * pMaxVal, CARD32 * pDefaultVal)
{
    CARD8 byRets[4];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byRets, 0, sizeof(byRets));

    /* Send the arguements & SDVO opcode to the h/w */
    i830_sdvo_write_cmd(output, SDVO_CMD_GET_MAX_HUE, NULL, 0);

    status = i830_sdvo_read_response(output, byRets, 4);
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    /* Fill up the return values. */
    *pMaxVal = (CARD32) byRets[0] | ((CARD32) byRets[1] << 8);
    *pDefaultVal = (CARD32) byRets[2] | ((CARD32) byRets[3] << 8);

    return TRUE;
}

static Bool
i830_sdvo_get_max_saturation(xf86OutputPtr output,
			     CARD32 * pMaxVal, CARD32 * pDefaultVal)
{

    CARD8 byRets[4];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byRets, 0, sizeof(byRets));

    /* Send the arguements & SDVO opcode to the h/w */
    i830_sdvo_write_cmd(output, SDVO_CMD_GET_MAX_SATURATION, NULL, 0);

    status = i830_sdvo_read_response(output, byRets, 4);
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    /* Fill up the return values. */
    *pMaxVal = (CARD32) byRets[0] | ((CARD32) byRets[1] << 8);
    *pDefaultVal = (CARD32) byRets[2] | ((CARD32) byRets[3] << 8);

    return TRUE;
}

static Bool
i830_sdvo_get_max_adaptive_flickerfilter(xf86OutputPtr output,
					 CARD32 * pMaxVal,
					 CARD32 * pDefaultVal)
{
    CARD8 byRets[4];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byRets, 0, sizeof(byRets));

    /* Send the arguements & SDVO opcode to the h/w */
    i830_sdvo_write_cmd(output, SDVO_CMD_GET_MAX_ADAPTIVE_FLICKER_FILTER,
			NULL, 0);
    status = i830_sdvo_read_response(output, byRets, 4);
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    /* Fill up the return values. */
    *pMaxVal = (CARD32) byRets[0] | ((CARD32) byRets[1] << 8);
    *pDefaultVal = (CARD32) byRets[2] | ((CARD32) byRets[3] << 8);

    return TRUE;
}

static Bool
i830_sdvo_get_max_lumafilter(xf86OutputPtr output,
			     CARD32 * pMaxVal, CARD32 * pDefaultVal)
{

    CARD8 byRets[4];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byRets, 0, sizeof(byRets));

    /* Send the arguements & SDVO opcode to the h/w */
    i830_sdvo_write_cmd(output, SDVO_CMD_GET_MAX_TV_LUMA_FILTER, NULL, 0);
    status = i830_sdvo_read_response(output, byRets, 4);
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    /* Fill up the return values. */
    *pMaxVal = (CARD32) byRets[0] | ((CARD32) byRets[1] << 8);
    *pDefaultVal = (CARD32) byRets[2] | ((CARD32) byRets[3] << 8);

    return TRUE;
}

static Bool
i830_sdvo_get_max_chromafilter(xf86OutputPtr output,
			       CARD32 * pMaxVal, CARD32 * pDefaultVal)
{

    CARD8 byRets[4];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byRets, 0, sizeof(byRets));

    /* Send the arguements & SDVO opcode to the h/w */
    i830_sdvo_write_cmd(output, SDVO_CMD_GET_MAX_TV_CHROMA_FILTER, NULL, 0);
    status = i830_sdvo_read_response(output, byRets, 4);
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    /* Fill up the return values. */
    *pMaxVal = (CARD32) byRets[0] | ((CARD32) byRets[1] << 8);
    *pDefaultVal = (CARD32) byRets[2] | ((CARD32) byRets[3] << 8);

    return TRUE;
}

static Bool
i830_sdvo_get_dotcrawl(xf86OutputPtr output,
		       CARD32 * pCurrentVal, CARD32 * pDefaultVal)
{

    CARD8 byRets[2];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byRets, 0, sizeof(byRets));

    /* Send the arguements & SDVO opcode to the h/w */

    i830_sdvo_write_cmd(output, SDVO_CMD_GET_DOT_CRAWL, NULL, 0);
    status = i830_sdvo_read_response(output, byRets, 2);
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    /* Tibet issue 1603772: Dot crawl do not persist after reboot/Hibernate */
    /* Details : Bit0 is considered as DotCrawl Max value. But according to EDS, Bit0 */
    /*           represents the Current DotCrawl value. */
    /* Fix     : The current value is updated with Bit0. */

    /* Fill up the return values. */
    *pCurrentVal = (CARD32) (byRets[0] & 0x1);
    *pDefaultVal = (CARD32) ((byRets[0] >> 1) & 0x1);
    return TRUE;
}

static Bool
i830_sdvo_get_max_2D_flickerfilter(xf86OutputPtr output,
				   CARD32 * pMaxVal, CARD32 * pDefaultVal)
{

    CARD8 byRets[4];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byRets, 0, sizeof(byRets));

    /* Send the arguements & SDVO opcode to the h/w */

    i830_sdvo_write_cmd(output, SDVO_CMD_GET_MAX_2D_FLICKER_FILTER, NULL, 0);
    status = i830_sdvo_read_response(output, byRets, 4);
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    /* Fill up the return values. */
    *pMaxVal = (CARD32) byRets[0] | ((CARD32) byRets[1] << 8);
    *pDefaultVal = (CARD32) byRets[2] | ((CARD32) byRets[3] << 8);

    return TRUE;
}

static Bool
i830_sdvo_set_horizontal_overscan(xf86OutputPtr output, CARD32 dwVal)
{

    CARD8 byArgs[2];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byArgs, 0, sizeof(byArgs));

    /* Fill up the arguement value */
    byArgs[0] = (CARD8) (dwVal & 0xFF);
    byArgs[1] = (CARD8) ((dwVal >> 8) & 0xFF);

    /* Send the arguements & SDVO opcode to the h/w */

    i830_sdvo_write_cmd(output, SDVO_CMD_SET_HORIZONTAL_OVERSCAN, byArgs, 2);
    status = i830_sdvo_read_response(output, NULL, 0);

    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;
    return TRUE;
}

static Bool
i830_sdvo_set_vertical_overscan(xf86OutputPtr output, CARD32 dwVal)
{

    CARD8 byArgs[2];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byArgs, 0, sizeof(byArgs));

    /* Fill up the arguement value */
    byArgs[0] = (CARD8) (dwVal & 0xFF);
    byArgs[1] = (CARD8) ((dwVal >> 8) & 0xFF);

    /* Send the arguements & SDVO opcode to the h/w */

    i830_sdvo_write_cmd(output, SDVO_CMD_SET_VERTICAL_OVERSCAN, byArgs, 2);
    status = i830_sdvo_read_response(output, NULL, 0);

    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;
    return TRUE;
}

static Bool
i830_sdvo_set_horizontal_position(xf86OutputPtr output, CARD32 dwVal)
{

    CARD8 byArgs[2];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byArgs, 0, sizeof(byArgs));

    /* Fill up the arguement value */
    byArgs[0] = (CARD8) (dwVal & 0xFF);
    byArgs[1] = (CARD8) ((dwVal >> 8) & 0xFF);

    /* Send the arguements & SDVO opcode to the h/w */

    i830_sdvo_write_cmd(output, SDVO_CMD_SET_HORIZONTAL_POSITION, byArgs, 2);
    status = i830_sdvo_read_response(output, NULL, 0);

    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    return TRUE;
}

static Bool
i830_sdvo_set_vertical_position(xf86OutputPtr output, CARD32 dwVal)
{

    CARD8 byArgs[2];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byArgs, 0, sizeof(byArgs));

    /* Fill up the arguement value */
    byArgs[0] = (CARD8) (dwVal & 0xFF);
    byArgs[1] = (CARD8) ((dwVal >> 8) & 0xFF);

    /* Send the arguements & SDVO opcode to the h/w */
    i830_sdvo_write_cmd(output, SDVO_CMD_SET_VERTICAL_POSITION, byArgs, 2);
    status = i830_sdvo_read_response(output, NULL, 0);

    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    return TRUE;

}

static Bool
i830_sdvo_set_flickerilter(xf86OutputPtr output, CARD32 dwVal)
{

    CARD8 byArgs[2];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byArgs, 0, sizeof(byArgs));

    /* Fill up the arguement value */
    byArgs[0] = (CARD8) (dwVal & 0xFF);
    byArgs[1] = (CARD8) ((dwVal >> 8) & 0xFF);

    /* Send the arguements & SDVO opcode to the h/w */
    i830_sdvo_write_cmd(output, SDVO_CMD_SET_FLICKER_FILTER, byArgs, 2);
    status = i830_sdvo_read_response(output, NULL, 0);

    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    return TRUE;
}

static Bool
i830_sdvo_set_brightness(xf86OutputPtr output, CARD32 dwVal)
{

    CARD8 byArgs[2];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byArgs, 0, sizeof(byArgs));

    /* Fill up the arguement value */
    byArgs[0] = (CARD8) (dwVal & 0xFF);
    byArgs[1] = (CARD8) ((dwVal >> 8) & 0xFF);

    /* Send the arguements & SDVO opcode to the h/w */
    i830_sdvo_write_cmd(output, SDVO_CMD_SET_BRIGHTNESS, byArgs, 2);
    status = i830_sdvo_read_response(output, NULL, 0);

    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    return TRUE;
}

static Bool
i830_sdvo_set_contrast(xf86OutputPtr output, CARD32 dwVal)
{

    CARD8 byArgs[2];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byArgs, 0, sizeof(byArgs));
    /* Fill up the arguement value */
    byArgs[0] = (CARD8) (dwVal & 0xFF);
    byArgs[1] = (CARD8) ((dwVal >> 8) & 0xFF);

    /* Send the arguements & SDVO opcode to the h/w */
    i830_sdvo_write_cmd(output, SDVO_CMD_SET_CONTRAST, byArgs, 2);
    status = i830_sdvo_read_response(output, NULL, 0);

    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    return TRUE;
}

static Bool
i830_sdvo_set_sharpness(xf86OutputPtr output, CARD32 dwVal)
{

    CARD8 byArgs[2];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byArgs, 0, sizeof(byArgs));

    /* Fill up the arguement value */
    byArgs[0] = (CARD8) (dwVal & 0xFF);
    byArgs[1] = (CARD8) ((dwVal >> 8) & 0xFF);

    /* Send the arguements & SDVO opcode to the h/w */
    i830_sdvo_write_cmd(output, SDVO_CMD_SET_SHARPNESS, byArgs, 2);
    status = i830_sdvo_read_response(output, NULL, 0);

    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    return TRUE;
}

static Bool
i830_sdvo_set_hue(xf86OutputPtr output, CARD32 dwVal)
{

    CARD8 byArgs[2];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byArgs, 0, sizeof(byArgs));

    /* Fill up the arguement value */
    byArgs[0] = (CARD8) (dwVal & 0xFF);
    byArgs[1] = (CARD8) ((dwVal >> 8) & 0xFF);

    /* Send the arguements & SDVO opcode to the h/w */

    i830_sdvo_write_cmd(output, SDVO_CMD_SET_HUE, byArgs, 2);
    status = i830_sdvo_read_response(output, NULL, 0);

    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    return TRUE;
}

static Bool
i830_sdvo_set_saturation(xf86OutputPtr output, CARD32 dwVal)
{

    CARD8 byArgs[2];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byArgs, 0, sizeof(byArgs));

    /* Fill up the arguement value */
    byArgs[0] = (CARD8) (dwVal & 0xFF);
    byArgs[1] = (CARD8) ((dwVal >> 8) & 0xFF);

    /* Send the arguements & SDVO opcode to the h/w */
    i830_sdvo_write_cmd(output, SDVO_CMD_SET_SATURATION, byArgs, 2);
    status = i830_sdvo_read_response(output, NULL, 0);

    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    return TRUE;
}

static Bool
i830_sdvo_set_adaptive_flickerfilter(xf86OutputPtr output, CARD32 dwVal)
{
    CARD8 byArgs[2];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byArgs, 0, sizeof(byArgs));

    /* Fill up the arguement value */
    byArgs[0] = (CARD8) (dwVal & 0xFF);
    byArgs[1] = (CARD8) ((dwVal >> 8) & 0xFF);

    /* Send the arguements & SDVO opcode to the h/w */

    i830_sdvo_write_cmd(output, SDVO_CMD_SET_ADAPTIVE_FLICKER_FILTER, byArgs,
			2);
    status = i830_sdvo_read_response(output, NULL, 0);

    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    return TRUE;

}

static Bool
i830_sdvo_set_lumafilter(xf86OutputPtr output, CARD32 dwVal)
{
    CARD8 byArgs[2];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byArgs, 0, sizeof(byArgs));

    /* Fill up the arguement value */
    byArgs[0] = (CARD8) (dwVal & 0xFF);
    byArgs[1] = (CARD8) ((dwVal >> 8) & 0xFF);

    /* Send the arguements & SDVO opcode to the h/w */

    i830_sdvo_write_cmd(output, SDVO_CMD_SET_TV_LUMA_FILTER, byArgs, 2);
    status = i830_sdvo_read_response(output, NULL, 0);

    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    return TRUE;
}

static Bool
i830_sdvo_set_chromafilter(xf86OutputPtr output, CARD32 dwVal)
{

    CARD8 byArgs[2];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byArgs, 0, sizeof(byArgs));

    /* Fill up the arguement value */
    byArgs[0] = (CARD8) (dwVal & 0xFF);
    byArgs[1] = (CARD8) ((dwVal >> 8) & 0xFF);

    /* Send the arguements & SDVO opcode to the h/w */

    i830_sdvo_write_cmd(output, SDVO_CMD_SET_TV_CHROMA_FILTER, byArgs, 2);
    status = i830_sdvo_read_response(output, NULL, 0);

    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    return TRUE;
}

static Bool
i830_sdvo_set_dotcrawl(xf86OutputPtr output, CARD32 dwVal)
{

    CARD8 byArgs[2];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byArgs, 0, sizeof(byArgs));

    /* Fill up the arguement value */
    byArgs[0] = (CARD8) (dwVal & 0xFF);
    byArgs[1] = (CARD8) ((dwVal >> 8) & 0xFF);

    /* Send the arguements & SDVO opcode to the h/w */

    i830_sdvo_write_cmd(output, SDVO_CMD_SET_DOT_CRAWL, byArgs, 2);
    status = i830_sdvo_read_response(output, NULL, 0);
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    return TRUE;
}

static Bool
i830_sdvo_set_2D_flickerfilter(xf86OutputPtr output, CARD32 dwVal)
{

    CARD8 byArgs[2];
    CARD8 status;

    /* Make all fields of the  args/ret to zero */
    memset(byArgs, 0, sizeof(byArgs));

    /* Fill up the arguement value */
    byArgs[0] = (CARD8) (dwVal & 0xFF);
    byArgs[1] = (CARD8) ((dwVal >> 8) & 0xFF);

    /* Send the arguements & SDVO opcode to the h/w */

    i830_sdvo_write_cmd(output, SDVO_CMD_SET_2D_FLICKER_FILTER, byArgs, 2);
    status = i830_sdvo_read_response(output, NULL, 0);

    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    return TRUE;
}

static Bool
i830_tv_mode_find(xf86OutputPtr output, DisplayModePtr pMode)
{
    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);
    PSB_DEBUG(output->scrn->scrnIndex, 3, "i830_tv_mode_find,0x%lx\n",
	      pSDVO->TVStandard);
    Bool find = FALSE;
    int i;

    for (i = 0; i < NUM_TV_MODES; i++) {
	const tv_mode_t *tv_mode = &tv_modes[i];

	if (xf86nameCompare(tv_mode->mode_entry.name, pMode->name) == 0
	    && (pMode->type & M_T_TV)) {
	    find = TRUE;
	    break;
	}
    }
    return find;
}

static Bool
i830_translate_dtd2timing(DisplayModePtr pTimingInfo,
			  struct i830_sdvo_dtd *pDTD)
{

    CARD32 dwHBLHigh = 0;
    CARD32 dwVBLHigh = 0;
    CARD32 dwHSHigh1 = 0;
    CARD32 dwHSHigh2 = 0;
    CARD32 dwVSHigh1 = 0;
    CARD32 dwVSHigh2 = 0;
    CARD32 dwVPWLow = 0;
    Bool status = FALSE;

    if ((pDTD == NULL) || (pTimingInfo == NULL)) {
	return status;
    }

    pTimingInfo->Clock = pDTD->part1.clock * 10000 / 1000;	/*fix me if i am wrong */

    pTimingInfo->HDisplay = pTimingInfo->CrtcHDisplay =
	(CARD32) pDTD->part1.
	h_active | ((CARD32) (pDTD->part1.h_high & 0xF0) << 4);

    pTimingInfo->VDisplay = pTimingInfo->CrtcVDisplay =
	(CARD32) pDTD->part1.
	v_active | ((CARD32) (pDTD->part1.v_high & 0xF0) << 4);

    pTimingInfo->CrtcHBlankStart = pTimingInfo->CrtcHDisplay;

    /* Horizontal Total = Horizontal Active + Horizontal Blanking */
    dwHBLHigh = (CARD32) (pDTD->part1.h_high & 0x0F);
    pTimingInfo->HTotal = pTimingInfo->CrtcHTotal =
	pTimingInfo->CrtcHDisplay + (CARD32) pDTD->part1.h_blank +
	(dwHBLHigh << 8);

    pTimingInfo->CrtcHBlankEnd = pTimingInfo->CrtcHTotal - 1;

    /* Vertical Total = Vertical Active + Vertical Blanking */
    dwVBLHigh = (CARD32) (pDTD->part1.v_high & 0x0F);
    pTimingInfo->VTotal = pTimingInfo->CrtcVTotal =
	pTimingInfo->CrtcVDisplay + (CARD32) pDTD->part1.v_blank +
	(dwVBLHigh << 8);
    pTimingInfo->CrtcVBlankStart = pTimingInfo->CrtcVDisplay;
    pTimingInfo->CrtcVBlankEnd = pTimingInfo->CrtcVTotal - 1;

    /* Horz Sync Start = Horz Blank Start + Horz Sync Offset */
    dwHSHigh1 = (CARD32) (pDTD->part2.sync_off_width_high & 0xC0);
    pTimingInfo->HSyncStart = pTimingInfo->CrtcHSyncStart =
	pTimingInfo->CrtcHBlankStart + (CARD32) pDTD->part2.h_sync_off +
	(dwHSHigh1 << 2);

    /* Horz Sync End = Horz Sync Start + Horz Sync Pulse Width */
    dwHSHigh2 = (CARD32) (pDTD->part2.sync_off_width_high & 0x30);
    pTimingInfo->HSyncEnd = pTimingInfo->CrtcHSyncEnd =
	pTimingInfo->CrtcHSyncStart + (CARD32) pDTD->part2.h_sync_width +
	(dwHSHigh2 << 4) - 1;

    /* Vert Sync Start = Vert Blank Start + Vert Sync Offset */
    dwVSHigh1 = (CARD32) (pDTD->part2.sync_off_width_high & 0x0C);
    dwVPWLow = (CARD32) (pDTD->part2.v_sync_off_width & 0xF0);

    pTimingInfo->VSyncStart = pTimingInfo->CrtcVSyncStart =
	pTimingInfo->CrtcVBlankStart + (dwVPWLow >> 4) + (dwVSHigh1 << 2);

    /* Vert Sync End = Vert Sync Start + Vert Sync Pulse Width */
    dwVSHigh2 = (CARD32) (pDTD->part2.sync_off_width_high & 0x03);
    pTimingInfo->VSyncEnd = pTimingInfo->CrtcVSyncEnd =
	pTimingInfo->CrtcVSyncStart +
	(CARD32) (pDTD->part2.v_sync_off_width & 0x0F) + (dwVSHigh2 << 4) - 1;

    /* Fillup flags */
#if 0
    if (pDTD->ucDTDFlags & BIT7)
	pTimingInfo->flFlags.bInterlaced = 1;
    else
	pTimingInfo->flFlags.bInterlaced = 0;

    /* The sync polarity definition of DCTIMING_INFO is reversed from that of DTD's */
    if (pDTD->ucDTDFlags & BIT1)
	pTimingInfo->flFlags.bHSyncPolarity = 0;	/* set positive */
    else
	pTimingInfo->flFlags.bHSyncPolarity = 1;	/* set negative */

    if (pDTD->ucDTDFlags & BIT2)
	pTimingInfo->flFlags.bVSyncPolarity = 0;	/* set positive */
    else
	pTimingInfo->flFlags.bVSyncPolarity = 1;	/* set negative */

    if (pDTD->ucSDVOFlags & BIT7)
	pTimingInfo->flFlags.bStall = TRUE;
    else
	pTimingInfo->flFlags.bStall = FALSE;

    /* Check for SDVO scaling bits Bits[5:4] */
    if (pDTD->ucSDVOFlags & 0x30) {
	if (pDTD->ucSDVOFlags & 0x10)
	    /* Bit[5:4] = [0:1], text scaling */
	    pTimingInfo->flFlags.ucStretching = STRETCH_FLAGS_TEXT_MODES;
	else
	    /* Bit[5:4] = [1:0], Gfx scaling */
	    pTimingInfo->flFlags.ucStretching = STRETCH_FLAGS_GFX_MODES;
    } else {
	/* Bit[5:4] = [0:0], No scaling */
	pTimingInfo->flFlags.ucStretching = STRETCH_FLAGS_DONTUPDATE;
    }
#endif
    status = TRUE;

    return status;
}

static void
i830_translate_timing2dtd(DisplayModePtr mode, struct i830_sdvo_dtd *dtd)
{
    CARD16 width, height;
    CARD16 h_blank_len, h_sync_len, v_blank_len, v_sync_len;
    CARD16 h_sync_offset, v_sync_offset;

    width = mode->CrtcHDisplay;
    height = mode->CrtcVDisplay;

    /* do some mode translations */
    h_blank_len = mode->CrtcHBlankEnd - mode->CrtcHBlankStart;
    h_sync_len = mode->CrtcHSyncEnd - mode->CrtcHSyncStart;

    v_blank_len = mode->CrtcVBlankEnd - mode->CrtcVBlankStart;
    v_sync_len = mode->CrtcVSyncEnd - mode->CrtcVSyncStart;

    h_sync_offset = mode->CrtcHSyncStart - mode->CrtcHBlankStart;
    v_sync_offset = mode->CrtcVSyncStart - mode->CrtcVBlankStart;

    dtd->part1.clock = mode->Clock * 1000 / 10000;	/*xiaolin, fixme, do i need to by 1k hz */
    dtd->part1.h_active = width & 0xff;
    dtd->part1.h_blank = h_blank_len & 0xff;
    dtd->part1.h_high = (((width >> 8) & 0xf) << 4) |
	((h_blank_len >> 8) & 0xf);
    dtd->part1.v_active = height & 0xff;
    dtd->part1.v_blank = v_blank_len & 0xff;
    dtd->part1.v_high = (((height >> 8) & 0xf) << 4) |
	((v_blank_len >> 8) & 0xf);

    dtd->part2.h_sync_off = h_sync_offset;
    dtd->part2.h_sync_width = h_sync_len & 0xff;
    dtd->part2.v_sync_off_width = ((v_sync_offset & 0xf) << 4 |
				   (v_sync_len & 0xf));
    dtd->part2.sync_off_width_high = ((h_sync_offset & 0x300) >> 2) |
	((h_sync_len & 0x300) >> 4) | ((v_sync_offset & 0x30) >> 2) |
	((v_sync_len & 0x30) >> 4);

    dtd->part2.dtd_flags = 0x18;
    if (mode->Flags & V_PHSYNC)
	dtd->part2.dtd_flags |= 0x2;
    if (mode->Flags & V_PVSYNC)
	dtd->part2.dtd_flags |= 0x4;

    dtd->part2.sdvo_flags = 0;
    dtd->part2.v_sync_off_high = v_sync_offset & 0xc0;
    dtd->part2.reserved = 0;

}

static Bool
i830_tv_program_display_params(xf86OutputPtr output)
{
    CARD8 status;
    CARD32 dwMaxVal = 0;
    CARD32 dwDefaultVal = 0;
    CARD32 dwCurrentVal = 0;

    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);

    /* X & Y Positions */

    /* Horizontal postition */
    if (pSDVO->dwSupportedEnhancements & SDVO_HORIZONTAL_POSITION) {
	status =
	    i830_sdvo_get_max_horizontal_position(output, &dwMaxVal,
						  &dwDefaultVal);

	if (status) {
	    /*Tibet issue 1596943: After changing mode from 8x6 to 10x7 open CUI and press Restore Defaults */
	    /*Position changes. */

	    /* Tibet:1629992 : can't keep previous TV setting status if re-boot system after TV setting(screen position & size) of CUI */
	    /* Fix : compare whether current postion is greater than max value and then assign the default value. Earlier the check was */
	    /*       against the pAim->PositionX.Max value to dwMaxVal. When we boot the PositionX.Max value is 0 and so after every reboot, */
	    /*       position is set to default. */

	    if (pSDVO->dispParams.PositionX.Value > dwMaxVal)
		pSDVO->dispParams.PositionX.Value = dwDefaultVal;

	    status =
		i830_sdvo_set_horizontal_position(output,
						  pSDVO->dispParams.PositionX.
						  Value);

	    if (!status)
		return status;

	    pSDVO->dispParams.PositionX.Max = dwMaxVal;
	    pSDVO->dispParams.PositionX.Min = 0;
	    pSDVO->dispParams.PositionX.Default = dwDefaultVal;
	    pSDVO->dispParams.PositionX.Step = 1;
	} else {
	    return status;
	}
    }

    /* Vertical position */
    if (pSDVO->dwSupportedEnhancements & SDVO_VERTICAL_POSITION) {
	status =
	    i830_sdvo_get_max_vertical_position(output, &dwMaxVal,
						&dwDefaultVal);

	if (status) {

	    /*Tibet issue 1596943: After changing mode from 8x6 to 10x7 open CUI and press Restore Defaults */
	    /*Position changes. */
	    /*currently if we are out of range get back to default */

	    /* Tibet:1629992 : can't keep previous TV setting status if re-boot system after TV setting(screen position & size) of CUI */
	    /* Fix : compare whether current postion is greater than max value and then assign the default value. Earlier the check was */
	    /*       against the pAim->PositionY.Max  value to dwMaxVal. When we boot the PositionX.Max value is 0 and so after every reboot, */
	    /*       position is set to default. */

	    if (pSDVO->dispParams.PositionY.Value > dwMaxVal)
		pSDVO->dispParams.PositionY.Value = dwDefaultVal;

	    status =
		i830_sdvo_set_vertical_position(output,
						pSDVO->dispParams.PositionY.
						Value);
	    if (!status)
		return status;

	    pSDVO->dispParams.PositionY.Max = dwMaxVal;
	    pSDVO->dispParams.PositionY.Min = 0;
	    pSDVO->dispParams.PositionY.Default = dwDefaultVal;
	    pSDVO->dispParams.PositionY.Step = 1;
	} else {
	    return status;
	}
    }

    /* Flicker Filter */
    if (pSDVO->dwSupportedEnhancements & SDVO_FLICKER_FILTER) {
	status =
	    i830_sdvo_get_max_flickerfilter(output, &dwMaxVal, &dwDefaultVal);

	if (status) {
	    /*currently if we are out of range get back to default */
	    if (pSDVO->dispParams.FlickerFilter.Value > dwMaxVal)
		pSDVO->dispParams.FlickerFilter.Value = dwDefaultVal;

	    status =
		i830_sdvo_set_flickerilter(output,
					   pSDVO->dispParams.FlickerFilter.
					   Value);
	    if (!status)
		return status;

	    pSDVO->dispParams.FlickerFilter.Max = dwMaxVal;
	    pSDVO->dispParams.FlickerFilter.Min = 0;
	    pSDVO->dispParams.FlickerFilter.Default = dwDefaultVal;
	    pSDVO->dispParams.FlickerFilter.Step = 1;
	} else {
	    return status;
	}
    }

    /* Brightness */
    if (pSDVO->dwSupportedEnhancements & SDVO_BRIGHTNESS) {

	status =
	    i830_sdvo_get_max_brightness(output, &dwMaxVal, &dwDefaultVal);

	if (status) {
	    /*check whether the value is beyond the max value, min value as per EDS is always 0 so */
	    /*no need to check it. */
	    if (pSDVO->dispParams.Brightness.Value > dwMaxVal)
		pSDVO->dispParams.Brightness.Value = dwDefaultVal;

	    /* Program the device */
	    status =
		i830_sdvo_set_brightness(output,
					 pSDVO->dispParams.Brightness.Value);
	    if (!status)
		return status;

	    pSDVO->dispParams.Brightness.Max = dwMaxVal;
	    pSDVO->dispParams.Brightness.Min = 0;
	    pSDVO->dispParams.Brightness.Default = dwDefaultVal;
	    pSDVO->dispParams.Brightness.Step = 1;
	} else {
	    return status;
	}

    }

    /* Contrast */
    if (pSDVO->dwSupportedEnhancements & SDVO_CONTRAST) {

	status = i830_sdvo_get_max_contrast(output, &dwMaxVal, &dwDefaultVal);

	if (status) {
	    /*check whether the value is beyond the max value, min value as per EDS is always 0 so */
	    /*no need to check it. */
	    if (pSDVO->dispParams.Contrast.Value > dwMaxVal)
		pSDVO->dispParams.Contrast.Value = dwDefaultVal;

	    /* Program the device */
	    status =
		i830_sdvo_set_contrast(output,
				       pSDVO->dispParams.Contrast.Value);
	    if (!status)
		return status;

	    pSDVO->dispParams.Contrast.Max = dwMaxVal;
	    pSDVO->dispParams.Contrast.Min = 0;
	    pSDVO->dispParams.Contrast.Default = dwDefaultVal;

	    pSDVO->dispParams.Contrast.Step = 1;

	} else {
	    return status;
	}
    }

    /* Sharpness */
    if (pSDVO->dwSupportedEnhancements & SDVO_SHARPNESS) {

	status =
	    i830_sdvo_get_max_sharpness(output, &dwMaxVal, &dwDefaultVal);

	if (status) {
	    /*check whether the value is beyond the max value, min value as per EDS is always 0 so */
	    /*no need to check it. */
	    if (pSDVO->dispParams.Sharpness.Value > dwMaxVal)
		pSDVO->dispParams.Sharpness.Value = dwDefaultVal;

	    /* Program the device */
	    status =
		i830_sdvo_set_sharpness(output,
					pSDVO->dispParams.Sharpness.Value);
	    if (!status)
		return status;
	    pSDVO->dispParams.Sharpness.Max = dwMaxVal;
	    pSDVO->dispParams.Sharpness.Min = 0;
	    pSDVO->dispParams.Sharpness.Default = dwDefaultVal;

	    pSDVO->dispParams.Sharpness.Step = 1;
	} else {
	    return status;
	}
    }

    /* Hue */
    if (pSDVO->dwSupportedEnhancements & SDVO_HUE) {

	status = i830_sdvo_get_max_hue(output, &dwMaxVal, &dwDefaultVal);

	if (status) {
	    /*check whether the value is beyond the max value, min value as per EDS is always 0 so */
	    /*no need to check it. */
	    if (pSDVO->dispParams.Hue.Value > dwMaxVal)
		pSDVO->dispParams.Hue.Value = dwDefaultVal;

	    /* Program the device */
	    status = i830_sdvo_set_hue(output, pSDVO->dispParams.Hue.Value);
	    if (!status)
		return status;

	    pSDVO->dispParams.Hue.Max = dwMaxVal;
	    pSDVO->dispParams.Hue.Min = 0;
	    pSDVO->dispParams.Hue.Default = dwDefaultVal;

	    pSDVO->dispParams.Hue.Step = 1;

	} else {
	    return status;
	}
    }

    /* Saturation */
    if (pSDVO->dwSupportedEnhancements & SDVO_SATURATION) {
	status =
	    i830_sdvo_get_max_saturation(output, &dwMaxVal, &dwDefaultVal);

	if (status) {
	    /*check whether the value is beyond the max value, min value as per EDS is always 0 so */
	    /*no need to check it. */
	    if (pSDVO->dispParams.Saturation.Value > dwMaxVal)
		pSDVO->dispParams.Saturation.Value = dwDefaultVal;

	    /* Program the device */
	    status =
		i830_sdvo_set_saturation(output,
					 pSDVO->dispParams.Saturation.Value);
	    if (!status)
		return status;

	    pSDVO->dispParams.Saturation.Max = dwMaxVal;
	    pSDVO->dispParams.Saturation.Min = 0;
	    pSDVO->dispParams.Saturation.Default = dwDefaultVal;
	    pSDVO->dispParams.Saturation.Step = 1;
	} else {
	    return status;
	}

    }

    /* Adaptive Flicker filter */
    if (pSDVO->dwSupportedEnhancements & SDVO_ADAPTIVE_FLICKER_FILTER) {
	status =
	    i830_sdvo_get_max_adaptive_flickerfilter(output, &dwMaxVal,
						     &dwDefaultVal);

	if (status) {
	    /*check whether the value is beyond the max value, min value as per EDS is always 0 so */
	    /*no need to check it. */
	    if (pSDVO->dispParams.AdaptiveFF.Value > dwMaxVal)
		pSDVO->dispParams.AdaptiveFF.Value = dwDefaultVal;

	    status =
		i830_sdvo_set_adaptive_flickerfilter(output,
						     pSDVO->dispParams.
						     AdaptiveFF.Value);
	    if (!status)
		return status;

	    pSDVO->dispParams.AdaptiveFF.Max = dwMaxVal;
	    pSDVO->dispParams.AdaptiveFF.Min = 0;
	    pSDVO->dispParams.AdaptiveFF.Default = dwDefaultVal;
	    pSDVO->dispParams.AdaptiveFF.Step = 1;
	} else {
	    return status;
	}
    }

    /* 2D Flicker filter */
    if (pSDVO->dwSupportedEnhancements & SDVO_2D_FLICKER_FILTER) {

	status =
	    i830_sdvo_get_max_2D_flickerfilter(output, &dwMaxVal,
					       &dwDefaultVal);

	if (status) {
	    /*check whether the value is beyond the max value, min value as per EDS is always 0 so */
	    /*no need to check it. */
	    if (pSDVO->dispParams.TwoD_FlickerFilter.Value > dwMaxVal)
		pSDVO->dispParams.TwoD_FlickerFilter.Value = dwDefaultVal;

	    status =
		i830_sdvo_set_2D_flickerfilter(output,
					       pSDVO->dispParams.
					       TwoD_FlickerFilter.Value);
	    if (!status)
		return status;

	    pSDVO->dispParams.TwoD_FlickerFilter.Max = dwMaxVal;
	    pSDVO->dispParams.TwoD_FlickerFilter.Min = 0;
	    pSDVO->dispParams.TwoD_FlickerFilter.Default = dwDefaultVal;
	    pSDVO->dispParams.TwoD_FlickerFilter.Step = 1;
	} else {
	    return status;
	}
    }

    /* Luma Filter */
    if (pSDVO->dwSupportedEnhancements & SDVO_TV_MAX_LUMA_FILTER) {
	status =
	    i830_sdvo_get_max_lumafilter(output, &dwMaxVal, &dwDefaultVal);

	if (status) {
	    /*check whether the value is beyond the max value, min value as per EDS is always 0 so */
	    /*no need to check it. */
	    if (pSDVO->dispParams.LumaFilter.Value > dwMaxVal)
		pSDVO->dispParams.LumaFilter.Value = dwDefaultVal;

	    /* Program the device */
	    status =
		i830_sdvo_set_lumafilter(output,
					 pSDVO->dispParams.LumaFilter.Value);
	    if (!status)
		return status;

	    pSDVO->dispParams.LumaFilter.Max = dwMaxVal;
	    pSDVO->dispParams.LumaFilter.Min = 0;
	    pSDVO->dispParams.LumaFilter.Default = dwDefaultVal;
	    pSDVO->dispParams.LumaFilter.Step = 1;

	} else {
	    return status;
	}

    }

    /* Chroma Filter */
    if (pSDVO->dwSupportedEnhancements & SDVO_MAX_TV_CHROMA_FILTER) {

	status =
	    i830_sdvo_get_max_chromafilter(output, &dwMaxVal, &dwDefaultVal);

	if (status) {
	    /*check whether the value is beyond the max value, min value as per EDS is always 0 so */
	    /*no need to check it. */
	    if (pSDVO->dispParams.ChromaFilter.Value > dwMaxVal)
		pSDVO->dispParams.ChromaFilter.Value = dwDefaultVal;

	    /* Program the device */
	    status =
		i830_sdvo_set_chromafilter(output,
					   pSDVO->dispParams.ChromaFilter.
					   Value);
	    if (!status)
		return status;

	    pSDVO->dispParams.ChromaFilter.Max = dwMaxVal;
	    pSDVO->dispParams.ChromaFilter.Min = 0;
	    pSDVO->dispParams.ChromaFilter.Default = dwDefaultVal;
	    pSDVO->dispParams.ChromaFilter.Step = 1;
	} else {
	    return status;
	}

    }

    /* Dot Crawl */
    if (pSDVO->dwSupportedEnhancements & SDVO_DOT_CRAWL) {
	status = i830_sdvo_get_dotcrawl(output, &dwCurrentVal, &dwDefaultVal);

	if (status) {

	    dwMaxVal = 1;
	    /*check whether the value is beyond the max value, min value as per EDS is always 0 so */
	    /*no need to check it. */

	    /* Tibet issue 1603772: Dot crawl do not persist after reboot/Hibernate */
	    /* Details : "Dotcrawl.value" is compared with "dwDefaultVal". Since */
	    /*            dwDefaultVal is always 0, dotCrawl value is always set to 0. */
	    /* Fix     : Compare the current dotCrawl value with dwMaxValue. */

	    if (pSDVO->dispParams.DotCrawl.Value > dwMaxVal)

		pSDVO->dispParams.DotCrawl.Value = dwMaxVal;

	    status =
		i830_sdvo_set_dotcrawl(output,
				       pSDVO->dispParams.DotCrawl.Value);
	    if (!status)
		return status;

	    pSDVO->dispParams.DotCrawl.Max = dwMaxVal;
	    pSDVO->dispParams.DotCrawl.Min = 0;
	    pSDVO->dispParams.DotCrawl.Default = dwMaxVal;
	    pSDVO->dispParams.DotCrawl.Step = 1;
	} else {
	    return status;
	}
    }

    return TRUE;
}

static Bool
i830_tv_set_overscan_parameters(xf86OutputPtr output)
{
    CARD8 status;

    CARD32 dwDefaultVal = 0;
    CARD32 dwMaxVal = 0;
    CARD32 dwPercentageValue = 0;
    CARD32 dwDefOverscanXValue = 0;
    CARD32 dwDefOverscanYValue = 0;
    CARD32 dwOverscanValue = 0;
    CARD32 dwSupportedEnhancements;
    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);

    /* Get supported picture enhancements */
    status =
	i830_sdvo_get_supported_enhancements(output,
					     &dwSupportedEnhancements);
    if (!status)
	return status;

    /* Horizontal Overscan */
    if (dwSupportedEnhancements & SDVO_HORIZONTAL_OVERSCAN) {
	status =
	    i830_sdvo_get_max_horizontal_overscan(output, &dwMaxVal,
						  &dwDefaultVal);
	if (!status)
	    return status;

	/*Calculate the default value in terms of percentage */
	dwDefOverscanXValue = ((dwDefaultVal * 100) / dwMaxVal);

	/*Calculate the default value in 0-1000 range */
	dwDefOverscanXValue = (dwDefOverscanXValue * 10);

	/*Overscan is in the range of 0 to 10000 as per MS spec */
	if (pSDVO->OverScanX.Value > MAX_VAL)
	    pSDVO->OverScanX.Value = dwDefOverscanXValue;

	/*Calculate the percentage(0-100%) of the overscan value */
	dwPercentageValue = (pSDVO->OverScanX.Value * 100) / 1000;

	/* Now map the % value to absolute value to be programed to the encoder */
	dwOverscanValue = (dwMaxVal * dwPercentageValue) / 100;

	status = i830_sdvo_set_horizontal_overscan(output, dwOverscanValue);
	if (!status)
	    return status;

	pSDVO->OverScanX.Max = 1000;
	pSDVO->OverScanX.Min = 0;
	pSDVO->OverScanX.Default = dwDefOverscanXValue;
	pSDVO->OverScanX.Step = 20;
    }

    /* Horizontal Overscan */
    /* vertical Overscan */
    if (dwSupportedEnhancements & SDVO_VERTICAL_OVERSCAN) {
	status =
	    i830_sdvo_get_max_vertical_overscan(output, &dwMaxVal,
						&dwDefaultVal);
	if (!status)
	    return status;

	/*Calculate the default value in terms of percentage */
	dwDefOverscanYValue = ((dwDefaultVal * 100) / dwMaxVal);

	/*Calculate the default value in 0-1000 range */
	dwDefOverscanYValue = (dwDefOverscanYValue * 10);

	/*Overscan is in the range of 0 to 10000 as per MS spec */
	if (pSDVO->OverScanY.Value > MAX_VAL)
	    pSDVO->OverScanY.Value = dwDefOverscanYValue;

	/*Calculate the percentage(0-100%) of the overscan value */
	dwPercentageValue = (pSDVO->OverScanY.Value * 100) / 1000;

	/* Now map the % value to absolute value to be programed to the encoder */
	dwOverscanValue = (dwMaxVal * dwPercentageValue) / 100;

	status = i830_sdvo_set_vertical_overscan(output, dwOverscanValue);
	if (!status)
	    return status;

	pSDVO->OverScanY.Max = 1000;
	pSDVO->OverScanY.Min = 0;
	pSDVO->OverScanY.Default = dwDefOverscanYValue;
	pSDVO->OverScanY.Step = 20;

    }
    /* vertical Overscan */
    return TRUE;
}

void
i830_sdvo_tv_settiming(xf86OutputPtr output, DisplayModePtr mode,
		       DisplayModePtr adjusted_mode)
{

    ScrnInfoPtr pScrn = output->scrn;
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);

    int pipe = 0;
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
    intel_clock_t clock;
    CARD32 dpll = 0, fp = 0, dspcntr, pipeconf;
    Bool ok, is_sdvo = FALSE;
    int centerX = 0, centerY = 0;
    CARD32 ulPortMultiplier, ulTemp, ulDotClock;
    int sdvo_pixel_multiply;

    /* Set up some convenient bools for what outputs are connected to
     * our pipe, used in DPLL setup.
     */
    is_sdvo = TRUE;
    ok = TRUE;
    ulDotClock = mode->Clock * 1000 / 1000;	/*xiaolin, fixme, do i need to by 1k hz */
    for (ulPortMultiplier = 1; ulPortMultiplier <= 5; ulPortMultiplier++) {
	ulTemp = ulDotClock * ulPortMultiplier;
	if ((ulTemp >= 100000) && (ulTemp <= 200000)) {
	    if ((ulPortMultiplier == 3) || (ulPortMultiplier == 5))
		continue;
	    else
		break;
	}
    }
    /* ulPortMultiplier is 2, dotclok is 1babc, fall into the first one case */
    /* add two to each m and n value -- optimizes (slightly) the search algo. */
    CARD32 dotclock = ulPortMultiplier * (mode->Clock * 1000) / 1000;

    if ((dotclock >= 100000) && (dotclock < 140500)) {
	clock.p1 = 0x2;
	clock.p2 = 0x00;
	clock.n = 0x3;
	clock.m1 = 0x10;
	clock.m2 = 0x8;
    } else if ((dotclock >= 140500) && (dotclock <= 200000)) {
	clock.p1 = 0x1;
	/*CG was using 0x10 from spreadsheet it should be 0 */
	/*pClock_Data->Clk_P2 = 0x10; */
	clock.p2 = 0x00;
	clock.n = 0x6;
	clock.m1 = 0xC;
	clock.m2 = 0x8;
    } else
	ok = FALSE;

    if (!ok)
	FatalError("Couldn't find PLL settings for mode!\n");

    fp = clock.n << 16 | clock.m1 << 8 | clock.m2;

    dpll = DPLL_VGA_MODE_DIS | DPLL_CLOCK_PHASE_9;

    dpll |= DPLLB_MODE_DAC_SERIAL;

    sdvo_pixel_multiply = ulPortMultiplier;
    dpll |= DPLL_DVO_HIGH_SPEED;
    dpll |= (sdvo_pixel_multiply - 1) << SDVO_MULTIPLIER_SHIFT_HIRES;

    /* compute bitmask from p1 value */
    dpll |= (clock.p1 << 16);
    dpll |= (clock.p2 << 24);

    dpll |= PLL_REF_INPUT_TVCLKINBC;

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
	if (mode->Clock * 1000 > (pDevice->CoreClock * 1000) * 9 / 10)	/*xiaolin, fixme, do i need to by 1k hz */
	    pipeconf |= PIPEACONF_DOUBLE_WIDE;
	else
	    pipeconf &= ~PIPEACONF_DOUBLE_WIDE;
    }
    dspcntr |= DISPLAY_PLANE_ENABLE;
    pipeconf |= PIPEACONF_ENABLE;
    dpll |= DPLL_VCO_ENABLE;

    /* Disable the panel fitter if it was on our pipe */
    if (psbPanelFitterPipe(PSB_READ32(PFIT_CONTROL)) == pipe)
	PSB_WRITE32(PFIT_CONTROL, 0);

    psbPrintPll(pScrn->scrnIndex, "chosen", &clock);
    PSB_DEBUG(pScrn->scrnIndex, 3, "clock regs: 0x%08x, 0x%08x\n", (int)dpll,
	      (int)fp);

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

    PSB_WRITE32(htot_reg, (mode->CrtcHDisplay - 1) |
		((mode->CrtcHTotal - 1) << 16));
    PSB_WRITE32(hblank_reg, (mode->CrtcHBlankStart - 1) |
		((mode->CrtcHBlankEnd - 1) << 16));
    PSB_WRITE32(hsync_reg, (mode->CrtcHSyncStart - 1) |
		((mode->CrtcHSyncEnd - 1) << 16));
    PSB_WRITE32(vtot_reg, (mode->CrtcVDisplay - 1) |
		((mode->CrtcVTotal - 1) << 16));
    PSB_WRITE32(vblank_reg, (mode->CrtcVBlankStart - 1) |
		((mode->CrtcVBlankEnd - 1) << 16));
    PSB_WRITE32(vsync_reg, (mode->CrtcVSyncStart - 1) |
		((mode->CrtcVSyncEnd - 1) << 16));

    if (0 && pPsb->panelFittingMode != PSB_PANELFITTING_FIT) {

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
#if 0
    Offset = 0;
    Start = 0;

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
	Start = pCrtc->rotate->offset;
	Offset = 0;
    } else {
	PSB_DEBUG(pScrn->scrnIndex, 3, "not Rotated base\n");
	Offset = ((0 * pScrn->displayWidth + 0) * pPsb->front->cpp);
	Start = pPsb->front->offset;
    }

    PSB_DEBUG(pScrn->scrnIndex, 3, "Update stride as Resize\n");

    /* Update stride as Resize can cause the pitch to re-adjust */
    PSB_WRITE32(dspstride_reg, (crtc->rotatedData != NULL) ?
		(pCrtc->rotate->stride) : (pPsb->front->stride));

    PSB_WRITE32(dspbase, Start + Offset);
    (void)PSB_READ32(dspbase);

    PSB_DEBUG(pScrn->scrnIndex, 3, "Update stride as Resize\n");
    Offset = 0;
    Start = 0;

    Update stride as Resize can cause the pitch to re - adjust
	PSB_WRITE32(dspstride_reg, pPsb->front->stride);

    PSB_WRITE32(dspbase, Start + Offset);
    (void)PSB_READ32(dspbase);
#endif
    psbWaitForVblank(pScrn);

}

static Bool
i830_sdvo_mode_fixup(xf86OutputPtr output, DisplayModePtr mode,
		     DisplayModePtr adjusted_mode)
{
    /* Make the CRTC code factor in the SDVO pixel multiplier.  The SDVO
     * device will be told of the multiplier during mode_set.
     */
    PSB_DEBUG(output->scrn->scrnIndex, 3,
	      "xxi830_sdvo_mode_fixup,mode name is %s\n", mode->name);
    adjusted_mode->Clock *= i830_sdvo_get_pixel_multiplier(mode);

    return TRUE;
}

void i830_sdvo_set_iomap(xf86OutputPtr output);
Bool i830_tv_mode_check_support(xf86OutputPtr output, DisplayModePtr pMode);

static void i830_sdvo_dump_hdmi_buf(xf86OutputPtr output, uint8_t hdmiBufIndex)
{
    int i, j;
    uint8_t set_buf_index[2];
    uint8_t av_split;
    uint8_t buf_size;
    uint8_t buf[48];
    uint8_t *pos;

    i830_sdvo_write_cmd(output, SDVO_CMD_GET_HBUF_AV_SPLIT, NULL, 0);
    i830_sdvo_read_response(output, &av_split, 1);

    set_buf_index[0] = hdmiBufIndex; set_buf_index[1] = 0;
    i830_sdvo_write_cmd(output, SDVO_CMD_SET_HBUF_INDEX, &set_buf_index, 1);

    i830_sdvo_write_cmd(output, SDVO_CMD_GET_HBUF_INFO, NULL, 0);
    i830_sdvo_read_response(output, &buf_size, 1);
    pos = buf;

    PSB_DEBUG(output->scrn->scrnIndex, 3, 
        "\nHDMI BUFFER (%d 0), buf_size = %d\n", hdmiBufIndex, buf_size);

    for (j = 0; j <= buf_size; j += 8) {
        i830_sdvo_write_cmd(output, SDVO_CMD_GET_HBUF_DATA, NULL, 0);
        i830_sdvo_read_response(output, pos, 8);
        pos += 8;
    }

    int k;
    for (k = 0; k < buf_size; k++)
        PSB_DEBUG(output->scrn->scrnIndex, 3, "HDMI BUFFER (%d 0): buf[%d] = 0x%x\n", hdmiBufIndex, k, buf[k]);
/*
    for (i = 0; i <= av_split; i++) {
        set_buf_index[0] = i; set_buf_index[1] = 0;
        i830_sdvo_write_cmd(output, SDVO_CMD_SET_HBUF_INDEX, &set_buf_index, 1);

        i830_sdvo_write_cmd(output, SDVO_CMD_GET_HBUF_INFO, NULL, 0);
        i830_sdvo_read_response(output, &buf_size, 1);
        pos = buf;

        for (j = 0; j <= buf_size; j += 8) {
            i830_sdvo_write_cmd(output, SDVO_CMD_GET_HBUF_DATA, NULL, 0);
            i830_sdvo_read_response(output, pos, 8);
            pos += 8;
        }
    }
*/
}

static void i830_sdvo_set_hdmi_buf(xf86OutputPtr output, int index,
                                uint8_t *data, int8_t size, uint8_t tx_rate)
{
    uint8_t set_buf_index[2];

    set_buf_index[0] = index;
    set_buf_index[1] = 0;

    i830_sdvo_write_cmd(output, SDVO_CMD_SET_HBUF_INDEX, set_buf_index, 2);

    for (; size > 0; size -= 8) {
        i830_sdvo_write_cmd(output, SDVO_CMD_SET_HBUF_DATA, data, 8);
        data += 8;
    }

    //i830_sdvo_write_cmd(output, SDVO_CMD_SET_HBUF_TXRATE, &tx_rate, 1);
}

static uint8_t i830_sdvo_calc_hbuf_csum(uint8_t *data, uint8_t size)
{
    uint8_t csum = 0;
    int i;

    for (i = 0; i < size; i++)
        csum += data[i];

    return 0x100 - csum;
}

static unsigned char i830_sdvo_get_aspect_ratio(int xRes, int yRes)
{
    unsigned char aspectRatio = 0xFF;

    if(yRes == xRes * 10 / 16)
    {
        aspectRatio = 0x00;/* 16:10 aspect ratio for EDID 1.3*/
    }
    else if(yRes == xRes * 3 / 4)
    {
        aspectRatio = 0x01;/* 4:3 aspect ratio*/
    }
    else if(yRes == xRes * 4 / 5)/* 5:4 aspect ratio*/
    {
        aspectRatio = 0x02;
    }
    else if(yRes == xRes * 9 / 16)/* 16:9 aspect ratio*/
    {
        aspectRatio = 0x03;
    }
    else if(yRes == xRes)
    {
        aspectRatio = 0x00; /*1:1 aspect ratio for EDID prior to EDID 1.3*/
    }

    return aspectRatio;
}

static Bool i830_sdvo_set_colorimetry(xf86OutputPtr output, DisplayModePtr mode)
{
    uint8_t status;
    uint8_t colorimetry = SDVO_COLORIMETRY_RGB256;

    unsigned long  xRes, yRes, rRate;
    unsigned long  hTotal, vTotal, pixelClock;
    unsigned long  currentModeUID;
    int interlaced = mode->Flags & 0x80;

    int i;
    for (i = 0; i < TableLen; i++)
    {
        g_SupportedCeShortVideoModes[i].ceIndex = i+1;
        g_SupportedCeShortVideoModes[i].modeUID = g_CeShortVideoModes[i];
    }

    xRes = mode->HDisplay;
    yRes = mode->VDisplay;

    if (interlaced)
        yRes = yRes * 2;

    pixelClock = mode->Clock * 1000;

    if ((pixelClock == 0) || (xRes == 0) || (yRes == 0))
        return FALSE;

    hTotal = mode->HTotal;
    vTotal = mode->VTotal;
    rRate = (pixelClock + hTotal * vTotal / 2) / (hTotal * vTotal);
    if (interlaced)
        rRate = rRate * 2;

    currentModeUID = m_ModeUID (xRes, yRes, rRate, interlaced);

    for (i = 0; i < TableLen; i++)
    {
        if (currentModeUID == g_SupportedCeShortVideoModes[i].modeUID)
            if ((mode->HDisplay != 640) || (mode->VDisplay != 480))
                colorimetry = SDVO_COLORIMETRY_RGB220;
    }

    i830_sdvo_write_cmd(output, SDVO_CMD_SET_COLORIMETRY, &colorimetry, 1);
    status = i830_sdvo_read_response(output, NULL, 0);

    return (status == SDVO_CMD_STATUS_SUCCESS);
}

static Bool i830_sdvo_set_pixel_repli(xf86OutputPtr output, uint8_t repli)
{
    uint8_t status;

    i830_sdvo_write_cmd(output, SDVO_CMD_SET_PIXEL_REPLI, &repli, 1);
    status = i830_sdvo_read_response(output, NULL, 0);

    return (status == SDVO_CMD_STATUS_SUCCESS);
}

static void PBDCSdvoEncoderDFP_PruneAudioModes(xf86OutputPtr output, 
    struct dip_eld_infoframe *pEld, int ulHFreq, int ulHBlank)
{
	int ulNPCalc[2] = {0, 0};
	int ulNPLpossible[2] = {0, 0};
	int ulMaxBitRate2[2] = {0, 0};
	int ulMaxBitRate8[2] = {0, 0};
	DEVICE_AUDIO_CAPS AudioCaps;
	uint8_t ucMaxChSupport = 0;
	int ulRefFreq = 0;
	struct LPCM_CAD *plCad = NULL;
	uint8_t i = 0;
	uint8_t j = 0;
	uint8_t audioCaps[3];

	i830_sdvo_write_cmd(output, SDVO_CMD_GET_AUDIO_TX_INFO, NULL, 0);
	i830_sdvo_read_response(output, audioCaps, 1);
	
	memset(&AudioCaps, 0, sizeof(PDEVICE_AUDIO_CAPS));

	AudioCaps.NPLDesign = audioCaps[0];
	AudioCaps.K0 = audioCaps[1];
	AudioCaps.K1 = audioCaps[2];

	/* Update HDCP, RPTR bits in ELD*/
	//pEld->ucHDCP = (U8)AudioCaps.bIsHDCP;
	//pEld->ucRPTR = (U8)AudioCaps.bIsRPTR;

	/* Number of packets per line*/
	ulNPCalc[0] = ((AudioCaps.PR * ulHBlank) - AudioCaps.K0) / 32;
	ulNPCalc[1] = ((AudioCaps.PR * ulHBlank) - AudioCaps.K1) / 32;

	/* Actual number of packets possible within given HBLANK*/
	ulNPLpossible[0] = min(ulNPCalc[0], AudioCaps.NPLDesign);
	ulNPLpossible[1] = min(ulNPCalc[1], AudioCaps.NPLDesign);

	/* For 2 channel PCM, max bit rate transmitter can support*/
	ulMaxBitRate2[0] = (ulHFreq * ulNPLpossible[0] * 4) - /*1536*/1500;
	ulMaxBitRate2[1] = (ulHFreq * ulNPLpossible[1] * 4) - /*1536*/1500;

	/* For 3-8 channel PCM, max bit rate transmitter can support*/
	ulMaxBitRate8[0] = (ulHFreq * ulNPLpossible[0] * 1) - /*1536*/1500;
	ulMaxBitRate8[1] = (ulHFreq * ulNPLpossible[1] * 1) - /*1536*/1500;

	for (i = 0; i < 2; ++i) /* for both CP on and off*/
	{
		ucMaxChSupport = 0;

		plCad = (struct LPCM_CAD *)&pEld->uc48CAD; /* byte 7*/
		ulRefFreq = 48000;

		/* Iterate 3 times to fill byte #7, #8, #9*/
		/* plCad moves from uc48CAD->uc96CAD->uc192CAD in ELD*/
		for (j = 0; j < 3; j++, plCad++)
		{
			if (sizeof(plCad) == 0)
				continue;

			ucMaxChSupport = 0;

			/* ulRefFreq changes from 48khz->96Khz->192kHz*/
			ulRefFreq = ulRefFreq * (1<<j);

			if (ulMaxBitRate8[i] >= ulRefFreq)
			{
				ucMaxChSupport = 7; /* 8-channel support*/
			}
			else if (ulMaxBitRate2[i] >= ulRefFreq)
			{
				ucMaxChSupport = 1; /* 2-channel support*/
			}

			/* min(maxchannel computed after pruning, maxchannel supported by monitor)*/
			ucMaxChSupport = min(ucMaxChSupport, plCad->ucMaxCh_CPOf);

			if (i)
				plCad->ucMaxCh_CPOn = ucMaxChSupport;
			else
				plCad->ucMaxCh_CPOf = ucMaxChSupport;
		}
	}
}

static void i830_sdvo_set_eld_infoframe(xf86OutputPtr output, DisplayModePtr mode)
{
    U8 *pData;
    U8 i, j;
    int sizeOfCEADataBlock = 0;
    U8 *pDataBlock = 0;
    int dwNumOfBytes = 0;
    U8 ucDataBlockTag = 0;
    PCEA_861B_ADB pADB = 0;
    PEDID_1_X pEdid = 0;
    PCE_EDID pCeEdid = 0;
    U8 ucELDSize = 0;
    PEDID_DTD pDTD = 0;

    struct dip_eld_infoframe eld_if;
    memset(&eld_if, 0, sizeof(eld_if));

    eld_if.ucCEA_ver = CEA_VERSION;
    eld_if.ucELD_ver = ELD_VERSION;
    
    PsbOutputPrivatePtr intel_output = output->driver_private;
    xf86MonPtr edid_mon = xf86OutputGetEDID(output, intel_output->pDDCBus);
    if (!edid_mon)
    {
        PSB_DEBUG(output->scrn->scrnIndex, 3, "Could not get edid_mon\n");
        return;
    }

    if (edid_mon && edid_mon->no_sections) 
    {
        unsigned char *basicEDIDData = psbDDCRead_DDC2(output->scrn->scrnIndex, intel_output->pDDCBus, 0, 128);
        unsigned char *ceEDIDData = psbDDCRead_DDC2(output->scrn->scrnIndex, intel_output->pDDCBus, 128, 128);

        if ((!basicEDIDData) || (!ceEDIDData))
            return;

        pEdid = (PEDID_1_X)basicEDIDData;
        pCeEdid = (PCE_EDID)ceEDIDData;

	ucELDSize = BASE_ELD_SIZE;

	// Fill Manufacturer name and Product code
	eld_if.usManufacturerName = (((U16)(pEdid->ProductID[1]) << 8) | (U16)(pEdid->ProductID[0]));
	eld_if.usProductCode = (((U16)(pEdid->ProductID[3]) << 8) | (U16)(pEdid->ProductID[2]));

	// TODO: Should we update HDCP, RPTR and 48CAD in ELD data?
	PBDCSdvoEncoderDFP_PruneAudioModes(output, &eld_if, mode->Clock * 1000 / mode->HTotal, mode->HTotal - mode->HDisplay);

	/* skip the first DTD*/
	pDTD = &pEdid->DTD[1];

	/* Search through DTD blocks, looking for monitor name*/
	for (i = 0; i < NUM_DTD_BLOCK - 1; ++i, ++pDTD)
	{
		/* Set a U8 pointer to DTD data*/
		pData = (U8*)pDTD;

		/* Check the Flag (the first two bytes) to determine*/
		/* if this block is used as descriptor*/
		if (pData[0] == 0x00 && pData[1] == 0x00)
		{
			/* And now check Data Type Tag within this descriptor*/
			/* Tag = 0xFC, then monitor name stored as ASCII*/
			if (pData[3] == 0xFC)
			{
				/* Copy monitor name*/
				for (j = 0; (j < 13) && (pData[j+5] != 0x0A); ++j)
				{
					eld_if.ucData[j] = pData[j+5];
				}
				eld_if.ucMNL = (j%7) + 1;
				ucELDSize += j;
				break;
			}
		}
	}

	/* Set a pointer in ELD packet after monitor name*/
	pData = (U8*)&(eld_if.ucData[j]);

	/* Now pull out data from CEA Extension EDID*/

	/* If Offset <= 4, we will not have CEA DataBlocks*/
	if (pCeEdid->ucDTDOffset > CEA_EDID_HEADER_SIZE)
	{
		sizeOfCEADataBlock = pCeEdid->ucDTDOffset - CEA_EDID_HEADER_SIZE;

		pDataBlock = (U8*)pCeEdid;

		/* skip header (first 4 bytes) in CEA EDID Timing Extension*/
		/* and set pointer to start of DataBlocks collection*/
		pDataBlock += CEA_EDID_HEADER_SIZE;

		while (sizeOfCEADataBlock > 0)
		{
			/* Get the Size of CEA DataBlock in bytes and TAG*/
			dwNumOfBytes = *pDataBlock & CEA_DATABLOCK_LENGTH_MASK;
			ucDataBlockTag = (*pDataBlock & CEA_DATABLOCK_TAG_MASK) >> 5;

			switch (ucDataBlockTag)
			{
				case CEA_AUDIO_DATABLOCK:
					/* move beyond tag/length byte*/
					++pDataBlock;
					for (i = 0; i < (dwNumOfBytes / 3); ++i, pDataBlock += 3)
					{
						pADB = (PCEA_861B_ADB)pDataBlock;
						switch(pADB->ucAudioFormatCode)
						{
							/* uncompressed audio (Linear PCM)*/
							case AUDIO_LPCM:
								if (pADB->uc48kHz)
								{
									eld_if.uc48CAD.ucMaxCh_CPOn = pADB->ucMaxChannels;
									eld_if.uc48CAD.ucMaxCh_CPOf = pADB->ucMaxChannels;
									eld_if.uc48CAD.uc20Bit = pADB->uc20Bit;
									eld_if.uc48CAD.uc24Bit = pADB->uc24Bit;
								}
								if (pADB->uc96kHz)
								{
									eld_if.uc96CAD.ucMaxCh_CPOn = pADB->ucMaxChannels;
									eld_if.uc96CAD.ucMaxCh_CPOf = pADB->ucMaxChannels;
									eld_if.uc96CAD.uc20Bit = pADB->uc20Bit;
									eld_if.uc96CAD.uc24Bit = pADB->uc24Bit;
                                
									/* if both 96kHz & 88.2kHz are supported*/
									/* then indicate 44.1kHz multiples support*/
									if (pADB->uc88kHz)
										eld_if.uc44MS = 1;
								}
								if (pADB->uc192kHz)
								{
									eld_if.uc192CAD.ucMaxCh_CPOn = pADB->ucMaxChannels;
									eld_if.uc192CAD.ucMaxCh_CPOf = pADB->ucMaxChannels;
									eld_if.uc192CAD.uc20Bit = pADB->uc20Bit;
									eld_if.uc192CAD.uc24Bit = pADB->uc24Bit;
                                
									/* if both 192kHz & 176kHz are supported*/
									/* then indicate 44.1kHz multiples support*/
									if (pADB->uc176kHz)
										eld_if.uc44MS = 1;
								}
								if ((pADB->uc88kHz) && (pADB->uc96kHz) && (pADB->uc192kHz))
								{
									if (!pADB->uc176kHz)
										eld_if.uc44MS = 0;
								}
								break;

							/* compressed audio*/
							case AUDIO_AC3:
							case AUDIO_MPEG1:
							case AUDIO_MP3:
							case AUDIO_MPEG2:
							case AUDIO_AAC:
							case AUDIO_DTS:
							case AUDIO_ATRAC:
								memcpy(pData, pDataBlock, 3);
								/* move pointer in ELD*/
								pData += 3;
								/* update ELD size*/
								ucELDSize += 3;
								/* update SADC field*/
								eld_if.ucSADC += 1;
								break;
						}
					}
					break;

				case CEA_VENDOR_DATABLOCK:
					/* audio wants data from 6th byte of VSDB onwards*/
					//Sighting 94842
					++pDataBlock;
					if (dwNumOfBytes > 5)
					{
						for (i=5, j=0; i < dwNumOfBytes; ++i,j++)
						{
							pData[j] = pDataBlock[i];
						}
						/* update VSDBL field*/
						eld_if.ucVSDBL = j;
						/* move pointer in ELD*/
						pData += j;
						/* update ELD size*/
						ucELDSize += (U8)j;
					}
					/* move pointer to next CEA Datablock*/
					pDataBlock += dwNumOfBytes;
					break;                 

				case CEA_SPEAKER_DATABLOCK:
					memcpy(eld_if.ucSAB, ++pDataBlock, dwNumOfBytes);
					/* move pointer to next CEA Datablock*/
					pDataBlock += dwNumOfBytes;
					break;                       

				default:
					/* Move pointer to next CEA DataBlock*/
					pDataBlock += (dwNumOfBytes + 1);
			}
			/* Decrement size of CEA DataBlock*/
			sizeOfCEADataBlock -= (dwNumOfBytes + 1);
		}	
	}
    }

    pDataBlock = (U8*)pCeEdid;
	
    if((*(pDataBlock + CEA_EXTENSION_BLOCK_BYTE_3) & BASIC_AUDIO_SUPPORTED) && (*(eld_if.ucSAB) == 0))
    {
        (*(eld_if.ucSAB)) = FL_AND_FR_SPEAKERS_CONNECTED;
    }

    i830_sdvo_set_hdmi_buf(output, ELD_INDEX, (uint8_t *)&eld_if, ucELDSize, SDVO_HBUF_TX_VSYNC);
}

static void i830_sdvo_set_avi_infoframe(xf86OutputPtr output,
                                        DisplayModePtr mode)
{
    struct dip_avi_infoframe avi_if;
    memset(&avi_if, 0, sizeof(avi_if));

    avi_if.type = DIP_TYPE_AVI;
    avi_if.version = DIP_VERSION_AVI;
    avi_if.len = DIP_LEN_AVI;

    /* Packet Byte #1 */
    avi_if.S = AVI_SCAN_NODATA;
    avi_if.B = AVI_BAR_INVALID;
    avi_if.A = AVI_AFI_INVALID;
    avi_if.Y = AVI_RGB_MODE;

    /* Packet Byte #2 */
    avi_if.R = AVI_AFAR_SAME;

    unsigned char aspectRatio = i830_sdvo_get_aspect_ratio(mode->HDisplay*2, mode->VDisplay*2);
    if (aspectRatio != 0xFF)
    {
        switch (aspectRatio)
        {
            case EDID_STD_ASPECT_RATIO_4_3:
                avi_if.M = AVI_PAR_4_3;
                break;

            case EDID_STD_ASPECT_RATIO_16_9:
                avi_if.M = AVI_PAR_16_9;
                break;

            default:
                avi_if.M = AVI_PAR_NODATA;
                break;
        }
    }

    avi_if.C = AVI_COLOR_NODATA; 
    if (((mode->HDisplay == 720) && (mode->VDisplay == 480)) 
        || ((mode->HDisplay == 720) && (mode->VDisplay == 576)))
    {
        avi_if.C = AVI_COLOR_ITU601; 
    }
    else if (((mode->HDisplay == 1280) && (mode->VDisplay == 720))
            || ((mode->HDisplay == 1920) && (mode->VDisplay == 1080)))
    {
        avi_if.C = AVI_COLOR_ITU709; 
    }

    /* Packet Byte #3 */
    avi_if.SC = AVI_SCALING_NODATA;
    avi_if.Q = 0;
    avi_if.EC = 0;
    avi_if.ITC = 0;

    /* Packet Byte #4 */
    avi_if.VIC = 0;
    int i;
    for (i = 0; i < TableLen; i++)
    {
        g_SupportedCeShortVideoModes[i].ceIndex = i+1;
        g_SupportedCeShortVideoModes[i].modeUID = g_CeShortVideoModes[i];
    }

    unsigned long  xRes, yRes, rRate;
    unsigned long  hTotal, vTotal, pixelClock;
    unsigned long  currentModeUID;
    int interlaced = mode->Flags & 0x80;

    xRes = mode->HDisplay;
    yRes = mode->VDisplay;

    if (interlaced)
        yRes = yRes * 2;

    pixelClock = mode->Clock * 1000;

    if ((pixelClock == 0) || (xRes == 0) || (yRes == 0))
        return;

    hTotal = mode->HTotal;
    vTotal = mode->VTotal;
    rRate = (pixelClock + hTotal * vTotal / 2) / (hTotal * vTotal);
    if (interlaced)
        rRate = rRate * 2;

    currentModeUID = m_ModeUID (xRes, yRes, rRate, interlaced);

    for (i = 0; i < TableLen; i++)
    {
        if (currentModeUID == g_SupportedCeShortVideoModes[i].modeUID)
            avi_if.VIC = g_SupportedCeShortVideoModes[i].ceIndex;
    }

    /* Packet Byte #5 */
    avi_if.PR = HDMI_PR_ONE;

    avi_if.checksum = i830_sdvo_calc_hbuf_csum((uint8_t *)&avi_if,
                                        4 + avi_if.len);
    i830_sdvo_set_hdmi_buf(output, AVI_INDEX, (uint8_t *)&avi_if, 4 + avi_if.len,
                        SDVO_HBUF_TX_VSYNC);
}

static void i830_sdvo_set_spd_infoframe(xf86OutputPtr output)
{
    struct dip_spd_infoframe spd_if;
    memset(&spd_if, 0, sizeof(spd_if));
    spd_if.type = DIP_TYPE_SPD;
    spd_if.version = DIP_VERSION_SPD;
    spd_if.len = DIP_LEN_SPD;
    
    memset(spd_if.name, 0, 8);
    memset(spd_if.desc, 0, 16);

    spd_if.SDI = SPD_SRC_PC;

    spd_if.checksum = i830_sdvo_calc_hbuf_csum((uint8_t *)&spd_if,
                                        4 + spd_if.len);

    i830_sdvo_set_hdmi_buf(output, SPD_INDEX, (uint8_t *)&spd_if, 4 + spd_if.len,
                        SDVO_HBUF_TX_VSYNC);
}

static void i830_sdvo_set_audio_infoframe(xf86OutputPtr output)
{
    struct dip_audio_infoframe audio_if;
    memset(&audio_if, 0, sizeof(audio_if));
    audio_if.type = DIP_TYPE_AUDIO;
    audio_if.version = DIP_VERSION_AUDIO;
    audio_if.len = DIP_LEN_AUDIO;

    audio_if.checksum = i830_sdvo_calc_hbuf_csum((uint8_t *)&audio_if,
                                        4 + audio_if.len);

    int av_split_num;
    i830_sdvo_write_cmd(output, SDVO_CMD_GET_HBUF_AV_SPLIT, NULL, 0);
    i830_sdvo_read_response(output, &av_split_num, 1);

    if (av_split_num <= 3)
    {
        av_split_num = 4;
        i830_sdvo_write_cmd(output, SDVO_CMD_SET_HBUF_AV_SPLIT, &av_split_num, 1);
    }

    i830_sdvo_set_hdmi_buf(output, AUDIO_INDEX, (uint8_t *)&audio_if, 4 + audio_if.len,
                        SDVO_HBUF_TX_VSYNC);
}

#if 0
static void
i830_sdvo_set_eld(xf86OutputPtr output /*,eld data */)
{
    uint8_t eld_pd;
    eld_pd = SDVO_HDMI_ELD_AND_PD;
        
    // just use default eld for now (which is all zeros)
    i830_sdvo_write_cmd(output, SDVO_CMD_SET_AUDIO_STATE, &eld_pd, 1);
}
#endif

static void i830_sdvo_send_hdmi_info_frames(xf86OutputPtr output, DisplayModePtr mode)
{
	//i830_sdvo_set_eld(output); //set eld, eld created during edid parsing.

	i830_sdvo_set_colorimetry(output, mode);
	i830_sdvo_set_pixel_repli(output, HDMI_PR_ONE);

	i830_sdvo_set_eld_infoframe(output, mode); //set eld, eld created during edid parsing.
	i830_sdvo_set_avi_infoframe(output, mode);
	i830_sdvo_set_spd_infoframe(output);
        
	//i830_sdvo_set_audio_infoframe(output);
}

static void i830_sdvo_set_audio_state(xf86OutputPtr output, uint8_t data, uint8_t mask)
{
    CARD8 status = SDVO_CMD_STATUS_SUCCESS;
    CARD8 audioState = 0;

    i830_sdvo_write_cmd(output, SDVO_CMD_GET_AUDIO_STATE, NULL, 0);
    status = i830_sdvo_read_response(output, &audioState, 1);
    if (status != SDVO_CMD_STATUS_SUCCESS)
        return;

    audioState &= ~mask;
    audioState |= (data & mask);

    i830_sdvo_write_cmd(output, SDVO_CMD_SET_AUDIO_STATE, &audioState, 1);

    return;
}

static void i830_sdvo_enable_disable_hdmi_info_frame(xf86OutputPtr output, int index, Bool hdmiIFEnable)
{
    CARD8 status = SDVO_CMD_STATUS_SUCCESS;
    uint8_t set_buf_index[2];
    set_buf_index[0] = index;
    set_buf_index[1] = 0;
    uint8_t tx_rate = hdmiIFEnable ? SDVO_HBUF_TX_VSYNC : SDVO_HBUF_TX_DISABLED;

    i830_sdvo_write_cmd(output, SDVO_CMD_SET_HBUF_INDEX, set_buf_index, 2);
    i830_sdvo_write_cmd(output, SDVO_CMD_SET_HBUF_TXRATE, &tx_rate, 1);

    return;
}

static void
i830_sdvo_mode_set(xf86OutputPtr output, DisplayModePtr mode,
		   DisplayModePtr adjusted_mode)
{
    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);
    PsbDevicePtr pDevice = pSDVO->psbOutput.pDevice;
    xf86CrtcPtr crtc = output->crtc;
    PsbCrtcPrivatePtr intel_crtc = crtc->driver_private;
    CARD32 sdvox;
    int sdvo_pixel_multiply;

    struct i830_sdvo_dtd output_dtd;
    CARD16 no_outputs;
    Bool success;


    no_outputs = 0;
    if (!mode)
	return;

    PSB_DEBUG(output->scrn->scrnIndex, 3, "xxi830_sdvo_mode_set\n");
    PSB_DEBUG(output->scrn->scrnIndex, 3,
	      "i830_sdvo_mode_set,active_outputs=0x%x,mode_name=%s\n",
	      pSDVO->active_outputs, mode->name);

    pSDVO->currentMode = mode;
    if (pSDVO->ActiveDevice == SDVO_DEVICE_TV) {
	//mode = i830_sdvo_get_tvmode_from_table();
	//mode = &tv_modes[3].mode_entry; /* to forece 800 x 600 d */
	/* check if this mode can be supported or not */
	// mode = adjusted_mode = &tv_modes[0].mode_entry; /* FIXME, test*/

	if (!i830_tv_mode_check_support(output, mode)) {
	    PSB_DEBUG(output->scrn->scrnIndex, 3,
		      "mode setting failed, use the forced mode\n");
	    mode = &tv_modes[0].mode_entry;
	    xf86SetModeCrtc(mode, 0);
	}
    }
    /* Change the encoding Type for HDMI Jamesx */
    if ((((PsbOutputPrivatePtr) (output->driver_private))->isHDMI_Device) 
        && (((PsbOutputPrivatePtr) (output->driver_private))->isHDMI_Monitor)) {
	// Reset PD and ELD valid bits, and disable transmission of CEA-861B InfoFrames
	i830_sdvo_set_audio_state(output, 0, (HDMI_PRESENCE_DETECT | HDMI_ELD_VALID));
	i830_sdvo_enable_disable_hdmi_info_frame(output, AVI_INDEX, FALSE);
	i830_sdvo_enable_disable_hdmi_info_frame(output, SPD_INDEX, FALSE);
	//i830_sdvo_enable_disable_hdmi_info_frame(output, AUDIO_INDEX, FALSE);
    }
#ifdef SII_WA
    {
	CARD8 vendor_id = pSDVO->caps.vendor_id;
	CARD8 device_id = pSDVO->caps.device_id;
	CARD8 device_rev_id = pSDVO->caps.device_rev_id;

	if ((vendor_id == SII_1362_VEN_ID) && (((device_id == SII_1362_DEV_ID)
						&& (device_rev_id ==
						    SII_1362_DEV_REV_ID))
					       ||
					       ((device_id == SII_1368_DEV_ID)
						&& (device_rev_id ==
						    SII_1368_DEV_REV0_ID))
					       ||
					       ((device_id == SII_1390_DEV_ID)
						&& (device_rev_id ==
						    SII_1390_DEV_REV0_ID)))) {
	    PSB_DEBUG(output->scrn->scrnIndex, 3,
		      "Need to do WA for Silicon Image 1362/1364/1368/1390 here.\n"
		      "The clock is %d\n", mode->Clock);
	    if ((mode->Clock >= 100000) && (mode->Clock < 200000)) {
		i830_sdvo_write_byte(output, SII_1362_AUTOZONESWITCH_WA_REG,
				     0x4D);
	    } else if ((mode->Clock < 100000) && (mode->Clock >= 24000)) {
		i830_sdvo_write_byte(output, SII_1362_AUTOZONESWITCH_WA_REG,
				     0x49);
	    }
	}
    }
#endif

    /* disable and enable the display output */
    i830_sdvo_set_target_output(output, 0);
    //i830_sdvo_set_active_outputs(output, pSDVO->active_outputs);
    memset(&output_dtd, 0, sizeof(struct i830_sdvo_dtd));
    /* check if this mode can be supported or not */

    i830_translate_timing2dtd(mode, &output_dtd);
    /* set the target input & output first */
    /* Set the input timing to the screen. Assume always input 0. */
    i830_sdvo_set_target_input(output, TRUE, FALSE);
    i830_sdvo_set_target_output(output, pSDVO->active_outputs);
    /* Set output timing (in DTD) */
    i830_sdvo_set_output_timing(output, &output_dtd);
    if (pSDVO->ActiveDevice == SDVO_DEVICE_TV) {
	i830_tv_set_overscan_parameters(output);
	/* Set TV standard */
	if (pSDVO->TVMode == TVMODE_HDTV)
	    i830_sdvo_map_hdtvstd_bitmask(output);
	else
	    i830_sdvo_map_sdtvstd_bitmask(output);
	/* Set TV format */
	i830_sdvo_set_tvoutputs_formats(output);
	/* We would like to use i830_sdvo_create_preferred_input_timing() to
	 * provide the device with a timing it can support, if it supports that
	 * feature.  However, presumably we would need to adjust the CRTC to output
	 * the preferred timing, and we don't support that currently.
	 */
	success = i830_sdvo_create_preferred_input_timing(output, mode);
	if (success) {
	    i830_sdvo_get_preferred_input_timing(output, &output_dtd);
	    /*
	     * output_dtd.part1.clock = 0x1623;
	     * output_dtd.part1.h_active = 0x20;
	     * output_dtd.part1.h_blank = 0xe7;
	     * output_dtd.part1.h_high = 0x31;
	     * output_dtd.part1.v_active = 0x58;
	     * output_dtd.part1.v_blank = 0x8c;
	     * output_dtd.part1.v_high = 0x20;
	     *
	     * output_dtd.part2.h_sync_off = 0x7a;
	     * output_dtd.part2.h_sync_width = 0x20;
	     * output_dtd.part2.v_sync_off_width = 0xf2;
	     * output_dtd.part2.sync_off_width_high = 0x08;
	     * output_dtd.part2.dtd_flags = 0x16;
	     * output_dtd.part2.sdvo_flags  = 0x0;
	     * output_dtd.part2.v_sync_off_high = 0x0;
	     * output_dtd.part2.reserved = 0x0;
	     */

	}
	/* Set the overscan values now as input timing is dependent on overscan values */

    }

    /* Set input timing (in DTD) */
    i830_sdvo_set_input_timing(output, &output_dtd);

    /* Picture Enhancements should be programmed only after setting the timing to have the effect */
    if (pSDVO->ActiveDevice == SDVO_DEVICE_TV) {
	i830_tv_program_display_params(output);
	/* translate dtd 2 timing */
	i830_translate_dtd2timing(mode, &output_dtd);
	/* Program clock rate multiplier, 2x,clock is = 0x360b730 */
	if ((mode->Clock * 1000 >= 24000000)
	    && (mode->Clock * 1000 < 50000000)) {
	    i830_sdvo_set_clock_rate_mult(output, SDVO_CLOCK_RATE_MULT_4X);
	} else if ((mode->Clock * 1000 >= 50000000)
		   && (mode->Clock * 1000 < 100000000)) {
	    i830_sdvo_set_clock_rate_mult(output, SDVO_CLOCK_RATE_MULT_2X);
	} else if ((mode->Clock * 1000 >= 100000000)
		   && (mode->Clock * 1000 < 200000000)) {
	    i830_sdvo_set_clock_rate_mult(output, SDVO_CLOCK_RATE_MULT_1X);
	} else
	    PSB_DEBUG(output->scrn->scrnIndex, 3,
		      "i830_sdvo_set_clock_rate is failed\n");

	i830_sdvo_tv_settiming(output, mode, adjusted_mode);
	mode = pSDVO->currentMode;
    }

    else {			       /* Program clock rate multiplier for non TV */
	switch (i830_sdvo_get_pixel_multiplier(mode)) {
	case 1:
	    i830_sdvo_set_clock_rate_mult(output, SDVO_CLOCK_RATE_MULT_1X);
	    break;
	case 2:
	    i830_sdvo_set_clock_rate_mult(output, SDVO_CLOCK_RATE_MULT_2X);
	    break;
	case 4:
	    i830_sdvo_set_clock_rate_mult(output, SDVO_CLOCK_RATE_MULT_4X);
	    break;
	}
    }
    /* Set the SDVO control regs. */
#if 0
    if (IS_I965GM(pI830)) {
	sdvox = SDVO_BORDER_ENABLE;
    } else
#endif
    {
	sdvox = PSB_READ32(pSDVO->output_device);

	switch (pSDVO->output_device) {
	case SDVOB:
	    sdvox &= SDVOB_PRESERVE_MASK;
	    break;
	case SDVOC:
	    sdvox &= SDVOC_PRESERVE_MASK;
	    break;
	}
	sdvox |= (9 << 19) | SDVO_BORDER_ENABLE;
    }

    if (intel_crtc->pipe == 1)
	sdvox |= SDVO_PIPE_B_SELECT;

    sdvo_pixel_multiply = i830_sdvo_get_pixel_multiplier(mode);
    if (1) {
	/* done in crtc_mode_set as it lives inside the dpll register */
    } else {
	sdvox |= (sdvo_pixel_multiply - 1) << SDVO_PORT_MULTIPLY_SHIFT;
    }

    i830_sdvo_write_sdvox(output, sdvox);

#ifdef CH_7312_WA
    if ((pSDVO->caps.device_id == CH_7312_DEV_ID) &&	/*Chrontel 7312 DeviceID */
	(pSDVO->caps.vendor_id == CH_7312_VEN_ID) &&	/*Chrontel 7312 VendorID */
	(pSDVO->caps.device_rev_id == CH_7312_DEV_REV_ID)) {
	/* Write the init value now to the workaround register */
	i830_sdvo_write_byte(output, CH7312_WORKAROUND_REG,
			     CH7312_INIT_WORKAROUND_VALUE);

	/*Write the second value now in the same register */
	i830_sdvo_write_byte(output, CH7312_WORKAROUND_REG,
			     CH7312_FINAL_WORKAROUND_VALUE);
    }
#endif

    i830_sdvo_set_iomap(output);

    // Transmit HDMI buffer data
    if (((PsbOutputPrivatePtr) (output->driver_private))->isHDMI_Device) {
	CARD8 target =
	    ((PsbOutputPrivatePtr) (output->driver_private))->isHDMI_Monitor;
	PSB_DEBUG(output->scrn->scrnIndex, 3,
		  "HDMI Set Enconding mode to %d\n", target);

	i830_sdvo_write_cmd(output,
			    SDVO_CMD_SET_ENCODE,
			    &target, 1);

	if (((PsbOutputPrivatePtr) (output->driver_private))->isHDMI_Monitor) {
            i830_sdvo_send_hdmi_info_frames(output, mode);

            // Reinstall PD and ELD valid bits, and enable transmission of CEA-861B InfoFrames
            i830_sdvo_set_audio_state(output, (HDMI_PRESENCE_DETECT | HDMI_ELD_VALID), (HDMI_PRESENCE_DETECT | HDMI_ELD_VALID));
            i830_sdvo_enable_disable_hdmi_info_frame(output, AVI_INDEX, TRUE);
            i830_sdvo_enable_disable_hdmi_info_frame(output, SPD_INDEX, TRUE);
            //i830_sdvo_enable_disable_hdmi_info_frame(output, AUDIO_INDEX, TRUE);
#ifdef DUMP_HDMI_BUFFER
            i830_sdvo_dump_hdmi_buf(output, ELD_INDEX);
            i830_sdvo_dump_hdmi_buf(output, AVI_INDEX);
            i830_sdvo_dump_hdmi_buf(output, SPD_INDEX);
            //i830_sdvo_dump_hdmi_buf(output, AUDIO_INDEX);
#endif
	}
    }
}

static void
i830_sdvo_dpms(xf86OutputPtr output, int mode)
{
    ScrnInfoPtr pScrn = output->scrn;
    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);
    PsbDevicePtr pDevice = pSDVO->psbOutput.pDevice;
    CARD32 temp;

    if (pSDVO->psbOutput.pScrn != output->scrn)
	return;

    PSB_DEBUG(pScrn->scrnIndex, 3, "xxi830_sdvo_dpms %d, active_outputs=%x\n",
	      mode, pSDVO->active_outputs);

    if (mode != DPMSModeOn) {
	i830_sdvo_set_active_outputs(output, pSDVO->active_outputs);	//change 0 to
	if (0)
	    i830_sdvo_set_encoder_power_state(output, mode);
	if (mode == DPMSModeOff) {
	    temp = PSB_READ32(pSDVO->output_device);
	    if ((temp & SDVO_ENABLE) != 0) {
		i830_sdvo_write_sdvox(output, temp & ~SDVO_ENABLE);
	    }
	}
    } else {			       /* DPMSMode On */
	Bool input1, input2;
	int i;
	CARD8 status;

	temp = PSB_READ32(pSDVO->output_device);
	if ((temp & SDVO_ENABLE) == 0)
	    i830_sdvo_write_sdvox(output, temp | SDVO_ENABLE);
	for (i = 0; i < 2; i++)
	    psbWaitForVblank(pScrn);

	status = i830_sdvo_get_trained_inputs(output, &input1, &input2);

	/* Warn if the device reported failure to sync. */
	if (status == SDVO_CMD_STATUS_SUCCESS && !(input1 || input2)) {
	    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		       "First %s output reported failure to sync or input is not trainded!!!\n",
		       SDVO_NAME(pSDVO));
	}

	if (((PsbOutputPrivatePtr) (output->driver_private))->isHDMI_Device && ((PsbOutputPrivatePtr) (output->driver_private))->isHDMI_Monitor) {
            // Reinstall PD and ELD valid bits, and enable transmission of CEA-861B InfoFrames
            i830_sdvo_set_audio_state(output, (HDMI_PRESENCE_DETECT | HDMI_ELD_VALID), (HDMI_PRESENCE_DETECT | HDMI_ELD_VALID));
            i830_sdvo_enable_disable_hdmi_info_frame(output, AVI_INDEX, TRUE);
            i830_sdvo_enable_disable_hdmi_info_frame(output, SPD_INDEX, TRUE);
	}

	if (0)
	    i830_sdvo_set_encoder_power_state(output, mode);
	i830_sdvo_set_active_outputs(output, pSDVO->active_outputs);
    }
}

static void
i830_sdvo_save(xf86OutputPtr output)
{
    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);
    PsbDevicePtr pDevice = pSDVO->psbOutput.pDevice;

    PSB_DEBUG(output->scrn->scrnIndex, 3, "xxi830_sdvo_save\n");
    pSDVO->save_sdvo_mult = i830_sdvo_get_clock_rate_mult(output);
    i830_sdvo_get_active_outputs(output, &pSDVO->save_active_outputs);
    PSB_DEBUG(output->scrn->scrnIndex, 3, " --save_active_outputs is %x\n",
	      pSDVO->save_active_outputs);

    if (pSDVO->caps.sdvo_inputs_mask & 0x1) {
	i830_sdvo_set_target_input(output, TRUE, FALSE);
	i830_sdvo_get_input_timing(output, &pSDVO->save_input_dtd_1);
    }

    if (pSDVO->caps.sdvo_inputs_mask & 0x2) {
	i830_sdvo_set_target_input(output, FALSE, TRUE);
	i830_sdvo_get_input_timing(output, &pSDVO->save_input_dtd_2);
    }

    i830_sdvo_set_target_output(output, pSDVO->active_outputs);
    i830_sdvo_get_output_timing(output,
				&pSDVO->save_output_dtd[pSDVO->
							active_outputs]);

    i830_sdvo_get_current_inoutmap(output, &pSDVO->save_in0output,
				   &pSDVO->save_in1output);

#if 0

    for (o = SDVO_OUTPUT_FIRST; o <= SDVO_OUTPUT_LAST; o++) {
	CARD16 this_output = (1 << o);

	if (pSDVO->caps.output_flags & this_output) {
	    i830_sdvo_set_target_output(output, this_output);
	    i830_sdvo_get_output_timing(output, &pSDVO->save_output_dtd[o]);
	}
    }
#endif

    pSDVO->save_SDVOX = PSB_READ32(pSDVO->output_device);
}

static void
i830_sdvo_restore(xf86OutputPtr output)
{
    ScrnInfoPtr pScrn = output->scrn;
    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);
    int i;
    Bool input1, input2;
    CARD8 status;

    PSB_DEBUG(output->scrn->scrnIndex, 3, "xxi830_sdvo_restore\n");
#if 0
    i830_sdvo_reset(output);
#endif
    i830_sdvo_set_active_outputs(output, 0);
    i830_sdvo_set_target_output(output, pSDVO->save_active_outputs);
    i830_sdvo_set_output_timing(output,
				&pSDVO->save_output_dtd[pSDVO->
							save_active_outputs]);
#if 0
    for (o = SDVO_OUTPUT_FIRST; o <= SDVO_OUTPUT_LAST; o++) {
	this_output = (1 << o);

	if (pSDVO->caps.output_flags & this_output) {
	    i830_sdvo_set_target_output(output, this_output);
	    i830_sdvo_set_output_timing(output, &pSDVO->save_output_dtd[o]);
	}
    }
#endif

    if (pSDVO->caps.sdvo_inputs_mask & 0x1) {
	i830_sdvo_set_target_input(output, TRUE, FALSE);
	i830_sdvo_set_input_timing(output, &pSDVO->save_input_dtd_1);
    }

    if (pSDVO->caps.sdvo_inputs_mask & 0x2) {
	i830_sdvo_set_target_input(output, FALSE, TRUE);
	i830_sdvo_set_input_timing(output, &pSDVO->save_input_dtd_2);
    }

    i830_sdvo_set_clock_rate_mult(output, pSDVO->save_sdvo_mult);

    i830_sdvo_write_sdvox(output, pSDVO->save_SDVOX);

    if (pSDVO->save_SDVOX & SDVO_ENABLE) {
	for (i = 0; i < 2; i++)
	    psbWaitForVblank(pScrn);

	status = i830_sdvo_get_trained_inputs(output, &input1, &input2);
	if (status == SDVO_CMD_STATUS_SUCCESS && !input1)
	    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		       "First %s output reported failure to sync\n",
		       SDVO_NAME(pSDVO));
    }
#if 0
    i830_sdvo_set_current_inoutmap(output, pSDVO->save_in0output,
				   pSDVO->save_in1output);
#endif

    i830_sdvo_set_active_outputs(output, pSDVO->save_active_outputs);
}

static Bool
i830_tv_set_target_io(xf86OutputPtr output)
{
    Bool status;
    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);
    status = i830_sdvo_set_target_input(output, TRUE, FALSE);
    if (status)
	status = i830_sdvo_set_target_output(output, pSDVO->active_outputs);

    return status;
}

static Bool
i830_tv_get_max_min_dotclock(xf86OutputPtr output)
{
    CARD32 dwMaxClkRateMul = 1;
    CARD32 dwMinClkRateMul = 1;
    CARD8 status;

    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);

    /* Set Target Input/Outputs */
    status = i830_tv_set_target_io(output);
    if (!status) {
	PSB_DEBUG(output->scrn->scrnIndex, 3,
		  "SetTargetIO function FAILED!!! \n");
	return status;
    }

    /* Get the clock rate multiplies supported by the encoder */
    dwMinClkRateMul = 1;
#if 0
    /* why we need do this, some time, tv can't bring up for the wrong setting in the last time */
    dwClkRateMulMask = i830_sdvo_get_clock_rate_mult(output);

    /* Find the minimum clock rate multiplier supported */

    if (dwClkRateMulMask & SDVO_CLOCK_RATE_MULT_1X)
	dwMinClkRateMul = 1;
    else if (dwClkRateMulMask & SDVO_CLOCK_RATE_MULT_2X)
	dwMinClkRateMul = 2;
    else if (dwClkRateMulMask & SDVO_CLOCK_RATE_MULT_3X)
	dwMinClkRateMul = 3;
    else if (dwClkRateMulMask & SDVO_CLOCK_RATE_MULT_4X)
	dwMinClkRateMul = 4;
    else if (dwClkRateMulMask & SDVO_CLOCK_RATE_MULT_5X)
	dwMinClkRateMul = 5;
    else
	return FALSE;
#endif
    /* Get the min and max input Dot Clock supported by the encoder */
    status = i830_sdvo_get_input_output_pixelclock_range(output, FALSE);	/* input */

    if (!status) {
	PSB_DEBUG(output->scrn->scrnIndex, 3,
		  "SDVOGetInputPixelClockRange() FAILED!!! \n");
	return status;
    }

    /* Get the min and max output Dot Clock supported by the encoder */
    status = i830_sdvo_get_input_output_pixelclock_range(output, TRUE);	/* output */

    if (!status) {
	PSB_DEBUG(output->scrn->scrnIndex, 3,
		  "SDVOGetOutputPixelClockRange() FAILED!!! \n");
	return status;
    }

    /* Maximum Dot Clock supported should be the minimum of the maximum */
    /* dot clock supported by the encoder & the SDVO bus clock rate */
    pSDVO->dwMaxDotClk =
	((pSDVO->dwMaxInDotClk * dwMaxClkRateMul) <
	 (pSDVO->dwMaxOutDotClk)) ? (pSDVO->dwMaxInDotClk *
				     dwMaxClkRateMul) : (pSDVO->
							 dwMaxOutDotClk);

    /* Minimum Dot Clock supported should be the maximum of the minimum */
    /* dot clocks supported by the input & output */
    pSDVO->dwMinDotClk =
	((pSDVO->dwMinInDotClk * dwMinClkRateMul) >
	 (pSDVO->dwMinOutDotClk)) ? (pSDVO->dwMinInDotClk *
				     dwMinClkRateMul) : (pSDVO->
							 dwMinOutDotClk);

    PSB_DEBUG(output->scrn->scrnIndex, 3,
	      "leave, i830_tv_get_max_min_dotclock() !!! \n");

    return TRUE;

}

Bool
i830_tv_mode_check_support(xf86OutputPtr output, DisplayModePtr pMode)
{
    CARD32 dwDotClk = 0;
    Bool status;
    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);

    dwDotClk = pMode->Clock * 1000;

    /*TODO:  Need to fix this from SoftBios side........ */
    if (pSDVO->TVMode == TVMODE_HDTV) {
	if (((pMode->HDisplay == 1920) && (pMode->VDisplay == 1080)) ||
	    ((pMode->HDisplay == 1864) && (pMode->VDisplay == 1050)) ||
	    ((pMode->HDisplay == 1704) && (pMode->VDisplay == 960)) ||
	    ((pMode->HDisplay == 640) && (pMode->VDisplay == 448)))
	    return TRUE;
    }

    if (pSDVO->bGetClk) {
	status = i830_tv_get_max_min_dotclock(output);
	if (!status) {
	    PSB_DEBUG(output->scrn->scrnIndex, 3,
		      "get max min dotclok failed\n");
	    return status;
	}
	pSDVO->bGetClk = FALSE;
    }

    /* Check the Dot clock first. If the requested Dot Clock should fall */
    /* in the supported range for the mode to be supported */
    if ((dwDotClk <= pSDVO->dwMinDotClk) || (dwDotClk >= pSDVO->dwMaxDotClk)) {
	PSB_DEBUG(output->scrn->scrnIndex, 3,
		  "dwDotClk value is out of range\n");
	/*TODO: now consider VBT add and Remove mode. */
	/* This mode can't be supported */
	return FALSE;
    }
    PSB_DEBUG(output->scrn->scrnIndex, 3,
	      "i830_tv_mode_check_support leave\n");
    return TRUE;

}
static int
i830_sdvo_mode_valid(xf86OutputPtr output, DisplayModePtr pMode)
{
    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);
    Bool status = TRUE;

    PSB_DEBUG(output->scrn->scrnIndex, 3, "xxi830_sdvo_mode_valid\n");
    if (pSDVO->ActiveDevice == SDVO_DEVICE_TV) {	/*FIXME,disable this path */
	status = i830_tv_mode_check_support(output, pMode);
	if (status) {
	    if (i830_tv_mode_find(output, pMode)) {
		PSB_DEBUG(output->scrn->scrnIndex, 3, "%s is ok\n",
			  pMode->name);
		return MODE_OK;
	    } else
		return MODE_CLOCK_RANGE;
	} else {
	    PSB_DEBUG(output->scrn->scrnIndex, 3, "%s is failed\n",
		      pMode->name);
	    return MODE_CLOCK_RANGE;
	}
    } else {
	if (pMode->Flags & V_DBLSCAN)
	    return MODE_NO_DBLESCAN;

	if (pMode->Clock > pSDVO->pixel_clock_max)
	    return MODE_CLOCK_HIGH;

	if (pMode->Clock < pSDVO->pixel_clock_min)
	    return MODE_CLOCK_LOW;
	if (pMode->Flags & V_INTERLACE)
	    return MODE_NO_INTERLACE;

	return MODE_OK;
    }
}

static Bool
i830_sdvo_get_capabilities(xf86OutputPtr output, struct i830_sdvo_caps *caps)
{
    CARD8 status;

    i830_sdvo_write_cmd(output, SDVO_CMD_GET_DEVICE_CAPS, NULL, 0);
    status = i830_sdvo_read_response(output, caps, sizeof(*caps));
    if (status != SDVO_CMD_STATUS_SUCCESS)
	return FALSE;

    return TRUE;
}

/** Forces the device over to the real I2C bus and uses its GetByte */
static Bool
i830_sdvo_ddc_i2c_get_byte(I2CDevPtr d, I2CByte * data, Bool last)
{
    xf86OutputPtr output = d->pI2CBus->DriverPrivate.ptr;
    PsbOutputPrivatePtr intel_output = output->driver_private;
    I2CBusPtr i2cbus = intel_output->pI2CBus, savebus;
    Bool ret;

    savebus = d->pI2CBus;
    d->pI2CBus = i2cbus;
    ret = i2cbus->I2CGetByte(d, data, last);
    d->pI2CBus = savebus;

    return ret;
}

/** Forces the device over to the real I2C bus and uses its PutByte */
static Bool
i830_sdvo_ddc_i2c_put_byte(I2CDevPtr d, I2CByte c)
{
    xf86OutputPtr output = d->pI2CBus->DriverPrivate.ptr;
    PsbOutputPrivatePtr intel_output = output->driver_private;
    I2CBusPtr i2cbus = intel_output->pI2CBus, savebus;
    Bool ret;

    savebus = d->pI2CBus;
    d->pI2CBus = i2cbus;
    ret = i2cbus->I2CPutByte(d, c);
    d->pI2CBus = savebus;

    return ret;
}

/**
 * Sets the control bus over to DDC before sending the start on the real I2C
 * bus.
 *
 * The control bus will flip back at the stop following the start executed
 * here.
 */
static Bool
i830_sdvo_ddc_i2c_start(I2CBusPtr b, int timeout)
{
    xf86OutputPtr output = b->DriverPrivate.ptr;
    PsbOutputPrivatePtr intel_output = output->driver_private;
    I2CBusPtr i2cbus = intel_output->pI2CBus;

    i830_sdvo_set_control_bus_switch(output, SDVO_CONTROL_BUS_DDC2);
    return i2cbus->I2CStart(i2cbus, timeout);
}

/** Forces the device over to the real SDVO bus and sends a stop to it. */
static void
i830_sdvo_ddc_i2c_stop(I2CDevPtr d)
{
    xf86OutputPtr output = d->pI2CBus->DriverPrivate.ptr;
    PsbOutputPrivatePtr intel_output = output->driver_private;
    I2CBusPtr i2cbus = intel_output->pI2CBus, savebus;

    savebus = d->pI2CBus;
    d->pI2CBus = i2cbus;
    i2cbus->I2CStop(d);
    d->pI2CBus = savebus;
}

/**
 * Mirrors xf86i2c I2CAddress, using the bus's (wrapped) methods rather than
 * the default methods.
 *
 * This ensures that our start commands always get wrapped with control bus
 * switches.  xf86i2c should probably be fixed to do this.
 */
static Bool
i830_sdvo_ddc_i2c_address(I2CDevPtr d, I2CSlaveAddr addr)
{
    if (d->pI2CBus->I2CStart(d->pI2CBus, d->StartTimeout)) {
	if (d->pI2CBus->I2CPutByte(d, addr & 0xFF)) {
	    if ((addr & 0xF8) != 0xF0 && (addr & 0xFE) != 0x00)
		return TRUE;

	    if (d->pI2CBus->I2CPutByte(d, (addr >> 8) & 0xFF))
		return TRUE;
	}

	d->pI2CBus->I2CStop(d);
    }

    return FALSE;
}

static void
i830_sdvo_dump_cmd(xf86OutputPtr output, int opcode)
{
    CARD8 response[8];

    i830_sdvo_write_cmd(output, opcode, NULL, 0);
    i830_sdvo_read_response(output, response, 8);
}

static void
i830_sdvo_dump_device(xf86OutputPtr output)
{
    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);

    ErrorF("Dump %s\n", pSDVO->d.DevName);
    i830_sdvo_dump_cmd(output, SDVO_CMD_GET_DEVICE_CAPS);
    i830_sdvo_dump_cmd(output, SDVO_CMD_GET_FIRMWARE_REV);
    i830_sdvo_dump_cmd(output, SDVO_CMD_GET_TRAINED_INPUTS);
    i830_sdvo_dump_cmd(output, SDVO_CMD_GET_ACTIVE_OUTPUTS);
    i830_sdvo_dump_cmd(output, SDVO_CMD_GET_IN_OUT_MAP);
    i830_sdvo_dump_cmd(output, SDVO_CMD_GET_ATTACHED_DISPLAYS);
    i830_sdvo_dump_cmd(output, SDVO_CMD_GET_HOT_PLUG_SUPPORT);
    i830_sdvo_dump_cmd(output, SDVO_CMD_GET_ACTIVE_HOT_PLUG);
    i830_sdvo_dump_cmd(output, SDVO_CMD_GET_INTERRUPT_EVENT_SOURCE);
    i830_sdvo_dump_cmd(output, SDVO_CMD_GET_INPUT_TIMINGS_PART1);
    i830_sdvo_dump_cmd(output, SDVO_CMD_GET_INPUT_TIMINGS_PART2);
    i830_sdvo_dump_cmd(output, SDVO_CMD_GET_OUTPUT_TIMINGS_PART1);
    i830_sdvo_dump_cmd(output, SDVO_CMD_GET_OUTPUT_TIMINGS_PART2);
    i830_sdvo_dump_cmd(output, SDVO_CMD_GET_PREFERRED_INPUT_TIMING_PART1);
    i830_sdvo_dump_cmd(output, SDVO_CMD_GET_PREFERRED_INPUT_TIMING_PART2);
    i830_sdvo_dump_cmd(output, SDVO_CMD_GET_INPUT_PIXEL_CLOCK_RANGE);
    i830_sdvo_dump_cmd(output, SDVO_CMD_GET_OUTPUT_PIXEL_CLOCK_RANGE);
    i830_sdvo_dump_cmd(output, SDVO_CMD_GET_SUPPORTED_CLOCK_RATE_MULTS);
    i830_sdvo_dump_cmd(output, SDVO_CMD_GET_CLOCK_RATE_MULT);
    i830_sdvo_dump_cmd(output, SDVO_CMD_GET_SUPPORTED_TV_FORMATS);
    i830_sdvo_dump_cmd(output, SDVO_CMD_GET_TV_FORMATS);
}

void
i830_sdvo_dump(ScrnInfoPtr pScrn)
{
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    int i;

    for (i = 0; i < xf86_config->num_output; i++) {
	xf86OutputPtr output = xf86_config->output[i];
	PsbOutputPrivatePtr intel_output = output->driver_private;

	if (intel_output->type == PSB_OUTPUT_SDVO)
	    i830_sdvo_dump_device(output);
    }
}

void
i830_tv_sdvobitmask_map_sdtv(CARD32 dwTVStdBitmask, CARD32 * pTVStandard)
{
    if (dwTVStdBitmask & SDVO_NTSC_M)
	*pTVStandard |= TVSTANDARD_NTSC_M;

    if (dwTVStdBitmask & SDVO_NTSC_M_J)
	*pTVStandard |= TVSTANDARD_NTSC_M_J;

    if (dwTVStdBitmask & SDVO_NTSC_433)
	*pTVStandard |= TVSTANDARD_NTSC_433;

    if (dwTVStdBitmask & SDVO_PAL_B)
	*pTVStandard |= TVSTANDARD_PAL_B;

    if (dwTVStdBitmask & SDVO_PAL_D)
	*pTVStandard |= TVSTANDARD_PAL_D;

    if (dwTVStdBitmask & SDVO_PAL_G)
	*pTVStandard |= TVSTANDARD_PAL_G;

    if (dwTVStdBitmask & SDVO_PAL_H)
	*pTVStandard |= TVSTANDARD_PAL_H;

    if (dwTVStdBitmask & SDVO_PAL_I)
	*pTVStandard |= TVSTANDARD_PAL_I;

    if (dwTVStdBitmask & SDVO_PAL_M)
	*pTVStandard |= TVSTANDARD_PAL_M;

    if (dwTVStdBitmask & SDVO_PAL_N)
	*pTVStandard |= TVSTANDARD_PAL_N;

    if (dwTVStdBitmask & SDVO_PAL_60)
	*pTVStandard |= TVSTANDARD_PAL_60;

    if (dwTVStdBitmask & SDVO_SECAM_B)
	*pTVStandard |= TVSTANDARD_SECAM_B;

    if (dwTVStdBitmask & SDVO_SECAM_D)
	*pTVStandard |= TVSTANDARD_SECAM_D;

    if (dwTVStdBitmask & SDVO_SECAM_G)
	*pTVStandard |= TVSTANDARD_SECAM_G;

    if (dwTVStdBitmask & SDVO_SECAM_K)
	*pTVStandard |= TVSTANDARD_SECAM_K;

    if (dwTVStdBitmask & SDVO_SECAM_K1)
	*pTVStandard |= TVSTANDARD_SECAM_K1;

    if (dwTVStdBitmask & SDVO_SECAM_L)
	*pTVStandard |= TVSTANDARD_SECAM_L;

}

void
i830_tv_sdvobitmask_map_hdtv(CARD32 dwTVStdBitmask, CARD32 * pTVStandard)
{

    if (dwTVStdBitmask & SDVO_HDTV_STD_170M_480i59)
	*pTVStandard |= HDTV_SMPTE_170M_480i59;

    if (dwTVStdBitmask & SDVO_HDTV_STD_EIA_7702A_480p60)
	*pTVStandard |= HDTV_SMPTE_293M_480p60;

    if (dwTVStdBitmask & SDVO_HDTV_STD_293M_480p59)
	*pTVStandard |= HDTV_SMPTE_293M_480p59;

    if (dwTVStdBitmask & SDVO_HDTV_STD_ITURBT601_576i50)
	*pTVStandard |= HDTV_ITURBT601_576i50;

    if (dwTVStdBitmask & SDVO_HDTV_STD_ITURBT601_576p50)
	*pTVStandard |= HDTV_ITURBT601_576p50;

    if (dwTVStdBitmask & SDVO_HDTV_STD_296M_720p50)
	*pTVStandard |= HDTV_SMPTE_296M_720p50;

    if (dwTVStdBitmask & SDVO_HDTV_STD_296M_720p59)
	*pTVStandard |= HDTV_SMPTE_296M_720p59;

    if (dwTVStdBitmask & SDVO_HDTV_STD_296M_720p60)
	*pTVStandard |= HDTV_SMPTE_296M_720p60;

    if (dwTVStdBitmask & SDVO_HDTV_STD_274M_1080i50)
	*pTVStandard |= HDTV_SMPTE_274M_1080i50;

    if (dwTVStdBitmask & SDVO_HDTV_STD_274M_1080i59)
	*pTVStandard |= HDTV_SMPTE_274M_1080i59;

    if (dwTVStdBitmask & SDVO_HDTV_STD_274M_1080i60)
	*pTVStandard |= HDTV_SMPTE_274M_1080i60;

    if (dwTVStdBitmask & SDVO_HDTV_STD_274M_1080p60)
	*pTVStandard |= HDTV_SMPTE_274M_1080p60;
}

void
i830_tv_get_default_params(xf86OutputPtr output)
{
    CARD32 dwSupportedSDTVBitMask = 0;
    CARD32 dwSupportedHDTVBitMask = 0;

    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);

    /* Get supported TV Standard */
    i830_sdvo_get_supported_tvoutput_formats(output, &dwSupportedSDTVBitMask,
					     &dwSupportedHDTVBitMask);

    pSDVO->dwSDVOSDTVBitMask = dwSupportedSDTVBitMask;
    pSDVO->dwSDVOHDTVBitMask = dwSupportedHDTVBitMask;

}

void
i830_sdvo_set_iomap(xf86OutputPtr output)
{
    CARD32 dwCurrentSDVOIn0 = 0;
    CARD32 dwCurrentSDVOIn1 = 0;
    CARD32 dwDevMask = 0;

    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);

    /* Please DO NOT change the following code. */
    /* SDVOB_IN0 or SDVOB_IN1 ==> sdvo_in0 */
    /* SDVOC_IN0 or SDVOC_IN1 ==> sdvo_in1 */
    if (pSDVO->byInputWiring & (SDVOB_IN0 | SDVOC_IN0)) {
	switch (pSDVO->ActiveDevice) {
	case SDVO_DEVICE_LVDS:
	    dwDevMask = SDVO_OUTPUT_LVDS0 | SDVO_OUTPUT_LVDS1;
	    break;

	case SDVO_DEVICE_TMDS:
	    dwDevMask = SDVO_OUTPUT_TMDS0 | SDVO_OUTPUT_TMDS1;
	    break;

	case SDVO_DEVICE_TV:
	    dwDevMask =
		SDVO_OUTPUT_YPRPB0 | SDVO_OUTPUT_SVID0 | SDVO_OUTPUT_CVBS0 |
		SDVO_OUTPUT_YPRPB1 | SDVO_OUTPUT_SVID1 | SDVO_OUTPUT_CVBS1 |
		SDVO_OUTPUT_SCART0 | SDVO_OUTPUT_SCART1;
	    break;

	case SDVO_DEVICE_CRT:
	    dwDevMask = SDVO_OUTPUT_RGB0 | SDVO_OUTPUT_RGB1;
	    break;
	}
	dwCurrentSDVOIn0 = (pSDVO->active_outputs & dwDevMask);
    } else if (pSDVO->byInputWiring & (SDVOB_IN1 | SDVOC_IN1)) {
	switch (pSDVO->ActiveDevice) {
	case SDVO_DEVICE_LVDS:
	    dwDevMask = SDVO_OUTPUT_LVDS0 | SDVO_OUTPUT_LVDS1;
	    break;

	case SDVO_DEVICE_TMDS:
	    dwDevMask = SDVO_OUTPUT_TMDS0 | SDVO_OUTPUT_TMDS1;
	    break;

	case SDVO_DEVICE_TV:
	    dwDevMask =
		SDVO_OUTPUT_YPRPB0 | SDVO_OUTPUT_SVID0 | SDVO_OUTPUT_CVBS0 |
		SDVO_OUTPUT_YPRPB1 | SDVO_OUTPUT_SVID1 | SDVO_OUTPUT_CVBS1 |
		SDVO_OUTPUT_SCART0 | SDVO_OUTPUT_SCART1;
	    break;

	case SDVO_DEVICE_CRT:
	    dwDevMask = SDVO_OUTPUT_RGB0 | SDVO_OUTPUT_RGB1;
	    break;
	}
	dwCurrentSDVOIn1 = (pSDVO->active_outputs & dwDevMask);
    }

    i830_sdvo_set_current_inoutmap(output, dwCurrentSDVOIn0,
				   dwCurrentSDVOIn1);
}

/**
 * Asks the SDVO device if any displays are currently connected.
 *
 * This interface will need to be augmented, since we could potentially have
 * multiple displays connected, and the caller will also probably want to know
 * what type of display is connected.  But this is enough for the moment.
 *
 * Takes 14ms on average on my i945G.
 */
static xf86OutputStatus
i830_sdvo_detect(xf86OutputPtr output)
{
    CARD8 response[2];
    CARD8 status;
    int count = 5;
    char deviceName[256];
    char *name_suffix;
    char *name_prefix;
    PsbOutputPrivatePtr intel_output;

    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);

    PsbOutputPrivatePtr pOutput =
	(PsbOutputPrivatePtr) output->driver_private;

	intel_output = &pSDVO->psbOutput;

    PSB_DEBUG(output->scrn->scrnIndex, 2, "xxi830_sdvo_detect %d\n",
	      pOutput->pScrn == output->scrn);

    // Set active hot-plug OpCode.
    uint8_t state0;
    uint8_t stathot;
    uint8_t reshot[2];
    uint8_t byArgs0[2];
    uint32_t pOutput1;

    i830_sdvo_write_cmd(output, SDVO_CMD_GET_ACTIVE_HOT_PLUG, NULL, 0);
    state0 = i830_sdvo_read_response(output, byArgs0, 2);

    pOutput1 = (uint32_t)byArgs0[1];
    pOutput1 = (pOutput1 << 8);
    pOutput1 |= (uint32_t)byArgs0[0];

    pOutput1 = pOutput1 | (0x1 & 0xFFFF);

    byArgs0[0] = (CARD8)(pOutput1 & 0xFF);
    byArgs0[1] = (CARD8)((pOutput1 >> 8) & 0xFF); 
    i830_sdvo_write_cmd(output, SDVO_CMD_SET_ACTIVE_HOT_PLUG, byArgs0, 2);
    i830_sdvo_write_cmd(output, SDVO_CMD_GET_ACTIVE_HOT_PLUG, NULL, 0);

    if (pOutput->pScrn != output->scrn)
	return XF86OutputStatusDisconnected;

    i830_sdvo_dpms(output, DPMSModeOn);
 
    if (!i830_sdvo_get_capabilities(output, &pSDVO->caps)) {
        /*No SDVO support, power down the pipe */
        i830_sdvo_dpms(output, DPMSModeOff);
        xf86DrvMsg(intel_output->pI2CBus->scrnIndex, X_ERROR,
                   ": No SDVO detected\n");
        return XF86OutputStatusDisconnected;
    }

    while (count--) {
	i830_sdvo_write_cmd(output, SDVO_CMD_GET_ATTACHED_DISPLAYS, NULL, 0);
	status = i830_sdvo_read_response(output, &response, 2);

	if (status == SDVO_CMD_STATUS_PENDING) {
	    i830_sdvo_reset(output);
	    continue;
	}

        if ((status != SDVO_CMD_STATUS_SUCCESS) || (response[0] == 0 && response[1] == 0)) {
            usleep(500);
            continue;
        } else
            break;
    }

#if 0
	if (status != SDVO_CMD_STATUS_SUCCESS) {
        /*No SDVO display device attached */
		pSDVO->ActiveDevice = SDVO_DEVICE_NONE;
		return XF86OutputStatusDisconnected;
	}
#endif
    if (response[0] != 0 || response[1] != 0) {
	/*Check what device types are connected to the hardware CRT/HDTV/S-Video/Composite */
	/*in case of CRT and multiple TV's attached give preference in the order mentioned below */
	/* 1. RGB */
	/* 2. HDTV */
	/* 3. S-Video */
	/* 4. composite */
	if (pSDVO->caps.output_flags & SDVO_OUTPUT_TMDS0) {
	    pSDVO->active_outputs = SDVO_OUTPUT_TMDS0;
	    output->subpixel_order = SubPixelHorizontalRGB;
	    name_prefix = "TMDS";
	    pSDVO->ActiveDevice = SDVO_DEVICE_TMDS;
	} else if (pSDVO->caps.output_flags & SDVO_OUTPUT_TMDS1) {
	    pSDVO->active_outputs = SDVO_OUTPUT_TMDS1;
	    output->subpixel_order = SubPixelHorizontalRGB;
	    name_prefix = "TMDS";
	    pSDVO->ActiveDevice = SDVO_DEVICE_TMDS;
	} else if (response[0] & SDVO_OUTPUT_RGB0) {
	    pSDVO->active_outputs = SDVO_OUTPUT_RGB0;
	    output->subpixel_order = SubPixelHorizontalRGB;
	    name_prefix = "RGB0";
	    pSDVO->ActiveDevice = SDVO_DEVICE_CRT;
	} else if ((response[1] << 8 | response[0]) & SDVO_OUTPUT_RGB1) {
	    pSDVO->active_outputs = SDVO_OUTPUT_RGB1;
	    output->subpixel_order = SubPixelHorizontalRGB;
	    name_prefix = "RGB1";
	    pSDVO->ActiveDevice = SDVO_DEVICE_CRT;
	} else if (response[0] & SDVO_OUTPUT_YPRPB0) {
	    pSDVO->active_outputs = SDVO_OUTPUT_YPRPB0;
	} else if ((response[1] << 8 | response[0]) & SDVO_OUTPUT_YPRPB1) {
	    pSDVO->active_outputs = SDVO_OUTPUT_YPRPB1;
	}
	/* SCART is given Second preference */
	else if (response[0] & SDVO_OUTPUT_SCART0) {
	    pSDVO->active_outputs = SDVO_OUTPUT_SCART0;

	} else if ((response[1] << 8 | response[0]) & SDVO_OUTPUT_SCART1) {
	    pSDVO->active_outputs = SDVO_OUTPUT_SCART1;
	}
	/* if S-Video type TV is connected along with Composite type TV give preference to S-Video */
	else if (response[0] & SDVO_OUTPUT_SVID0) {
	    pSDVO->active_outputs = SDVO_OUTPUT_SVID0;

	} else if ((response[1] << 8 | response[0]) & SDVO_OUTPUT_SVID1) {
	    pSDVO->active_outputs = SDVO_OUTPUT_SVID1;
	}
	/* Composite is given least preference */
	else if (response[0] & SDVO_OUTPUT_CVBS0) {
	    pSDVO->active_outputs = SDVO_OUTPUT_CVBS0;
	} else if ((response[1] << 8 | response[0]) & SDVO_OUTPUT_CVBS1) {
	    pSDVO->active_outputs = SDVO_OUTPUT_CVBS1;
	} else {
	    PSB_DEBUG(output->scrn->scrnIndex, 2, "no display attached\n");
	    unsigned char bytes[2];

	    memcpy(bytes, &pSDVO->caps.output_flags, 2);
	    xf86DrvMsg(0, X_ERROR,
		       "%s: No active TMDS or RGB outputs (0x%02x%02x) 0x%08x\n",
		       SDVO_NAME(pSDVO), bytes[0], bytes[1],
		       pSDVO->caps.output_flags);
	    name_prefix = "Unknown";
	}

	/* init para for TV connector */
	if (pSDVO->active_outputs & SDVO_OUTPUT_TV0) {
	    output->subpixel_order = SubPixelHorizontalRGB;
	    name_prefix = "TV0";
	    /* Init TV mode setting para */
	    pSDVO->ActiveDevice = SDVO_DEVICE_TV;
	    pSDVO->bGetClk = TRUE;
		
            if (pSDVO->active_outputs == SDVO_OUTPUT_YPRPB0 ||
                                         pSDVO->active_outputs == SDVO_OUTPUT_YPRPB1) {
                if(!pSDVO->TVStandard)
                   pSDVO->TVStandard = HDTV_SMPTE_274M_1080i60;
                pSDVO->TVMode = TVMODE_HDTV;
            } else {
                if(!pSDVO->TVStandard)
                   pSDVO->TVStandard = TVSTANDARD_NTSC_M;
                pSDVO->TVMode = TVMODE_SDTV;
            }
			
	    intel_output->pDevice->TVEnabled = TRUE;
		
		i830_tv_get_default_params(output);
	    /*Init Display parameter for TV */
	    pSDVO->OverScanX.Value = 0xffffffff;
	    pSDVO->OverScanY.Value = 0xffffffff;
	    pSDVO->dispParams.Brightness.Value = 0x80;
	    pSDVO->dispParams.FlickerFilter.Value = 0xffffffff;
	    pSDVO->dispParams.AdaptiveFF.Value = 7;
	    pSDVO->dispParams.TwoD_FlickerFilter.Value = 0xffffffff;
	    pSDVO->dispParams.Contrast.Value = 0x40;
	    pSDVO->dispParams.PositionX.Value = 0x200;
	    pSDVO->dispParams.PositionY.Value = 0x200;
	    pSDVO->dispParams.DotCrawl.Value = 1;
	    pSDVO->dispParams.ChromaFilter.Value = 1;
	    pSDVO->dispParams.LumaFilter.Value = 2;
	    pSDVO->dispParams.Sharpness.Value = 4;
	    pSDVO->dispParams.Saturation.Value = 0x45;
	    pSDVO->dispParams.Hue.Value = 0x40;
	    pSDVO->dispParams.Dither.Value = 0;
	}
	if (pSDVO->output_device == SDVOB) {
	    name_suffix = "-1";
	} else {
	    name_suffix = "-2";
	}

	strcpy(deviceName, name_prefix);
	strcat(deviceName, name_suffix);

	if(output->name && (memcmp(output->name,deviceName,strlen(deviceName)) != 0)){
	    PSB_DEBUG(output->scrn->scrnIndex, 2,
		"change the output name to %s\n", deviceName);
	    if (!xf86OutputRename(output, deviceName)) {
		xf86OutputDestroy(output);
		return XF86OutputStatusDisconnected;
	    }

#ifdef RANDR_12_INTERFACE
	    if (output->randr_output){ 
		int nameLength = strlen(deviceName);
		RROutputPtr randr_output = output->randr_output;	
		char *name = xalloc(nameLength + 1);
		PSB_DEBUG(output->scrn->scrnIndex, 2,
		    "change the rroutput name to %s\n", deviceName);
		if(name){
		    if(randr_output->name != (char *) (randr_output + 1))
			xfree(randr_output->name);	
		    randr_output->name = name;
		    randr_output->nameLength = nameLength;
		    memcpy(randr_output->name, deviceName, nameLength);
		    randr_output->name[nameLength] = '\0';
		} else {
		    PSB_DEBUG(output->scrn->scrnIndex, 2,
			"change the rroutput name to %s failed, keep to use the original name\n", deviceName);
		}
	    }
#endif
	}

    /* Check whether the device is HDMI supported. Jamesx */
    if (pSDVO->ActiveDevice ==  SDVO_DEVICE_TMDS) {
        CARD8 status;
        CARD8 byargs[2];

        byargs[0] = 0;
        byargs[1] = 0;

        i830_sdvo_write_cmd(output, SDVO_CMD_GET_SUPP_ENCODE, NULL, 0);

        intel_output->isHDMI_Device = 0;        //initial the value to 0
        status = i830_sdvo_read_response(output, byargs, 2);
        if (status != SDVO_CMD_STATUS_SUCCESS)
            PSB_DEBUG(output->scrn->scrnIndex, 3,
                        "psbSDVOInit: check HDMI device fail, no HDMI device\n");
        else {
            intel_output->isHDMI_Device = byargs[1];
            PSB_DEBUG(output->scrn->scrnIndex, 3,
                      "psbSDVOInit: HDMI device value is %d\n",
                      intel_output->isHDMI_Device);
        }
        intel_output->isHDMI_Monitor = 0;       //Will check the monitor HDMI supporting later
    }


	i830_sdvo_set_iomap(output);
	PSB_DEBUG(output->scrn->scrnIndex, 2,
		  "get attached displays=0x%x,0x%x,connectedouputs=0x%x\n",
		  response[0], response[1], pSDVO->active_outputs);
	
	return XF86OutputStatusConnected;

    } else {
        /*No SDVO display device attached */
        i830_sdvo_dpms(output, DPMSModeOff);
	pSDVO->ActiveDevice = SDVO_DEVICE_NONE;
	return XF86OutputStatusDisconnected;
    }
}

static DisplayModePtr
i830_sdvo_get_modes(xf86OutputPtr output)
{
    ScrnInfoPtr pScrn = output->scrn;
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    DisplayModePtr modes;
    xf86OutputPtr crt;

    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);

    PSB_DEBUG(output->scrn->scrnIndex, 3, "xxi830_sdvo_get_modes\n");
    if (pSDVO->ActiveDevice == SDVO_DEVICE_CRT
	|| pSDVO->ActiveDevice == SDVO_DEVICE_TMDS) {
	PSB_DEBUG(output->scrn->scrnIndex, 3,
		  "SDVO_DEVICE_CRT || SDVO_DEVICE_TMDS\n");
	modes = psbOutputDDCGetModes(output);

	/* Remove unsported modes: 832x624, 720x400, 640x480@67 Jamesx */
	if (1) {
	    /* Check whether the Monitor is HDMI supported Jamesx */
	    PsbOutputPrivatePtr intel_output = output->driver_private;
	    xf86MonPtr edid_mon = NULL;

	    PSB_DEBUG(output->scrn->scrnIndex, 3, "Try to get edid_mon\n");
	    edid_mon = xf86OutputGetEDID(output, intel_output->pDDCBus);
	    if (!edid_mon)
		PSB_DEBUG(output->scrn->scrnIndex, 3,
			  "Could not get edid_mon\n");

	    if (edid_mon && edid_mon->no_sections) {
		unsigned char *data = psbDDCRead_DDC2(output->scrn->scrnIndex,
						      intel_output->pDDCBus,
						      128, 128);

                if (data) {
                    Edid_AddCECompatibleModes(data, modes);
                }

		int sizeofdata = 0;
		int numOfBytes = 0;

		if (data) {
		    if ((*data++ == 0x2) /*CEA_EXT_TAG */ &&(*data <=
							     0x03
							     /*CEA_EXT_SUPPORTED_VERSION */
			)) {
			unsigned int IEEERegNum;

			data++;	       //skip the version byte
			sizeofdata = *data++;
			data++;	       //skip the capability byte
			PSB_DEBUG(output->scrn->scrnIndex, 3,
				  "the extension data size is %d\n",
				  sizeofdata);
			sizeofdata -= 4;	//Payload length = sizeofdata - header size
			while (sizeofdata > 0) {
			    numOfBytes = (*data) & 0x1F	/*CEA_DATABLOCK_LENGTH_MASK */;
			    PSB_DEBUG(output->scrn->scrnIndex, 3,
				      "the block size: numOfBytes is %d\n",
				      numOfBytes);
                            
			    if ((*data & 0xE0 /*CEA_DATABLOCK_TAG_MASK */ ) >>
				5 == 0x3 /*CEA_VENDOR_DATABLOCK */ ) {
				data++;	//skip the tag byte
				IEEERegNum = (unsigned int)(*data);
				IEEERegNum |=
				    (unsigned int)(*(data + 1)) << 8;
				IEEERegNum |=
				    (unsigned int)(*(data + 2)) << 16;
				PSB_DEBUG(output->scrn->scrnIndex, 3,
					  "the IEEERegNum is %08x\n",
					  IEEERegNum);
				if (IEEERegNum ==
				    0x00000c03 /*CEA_HDMI_IEEE_REG_ID */ ) {
				    intel_output->isHDMI_Monitor = 1;
				    break;
				}
			    }
			    data += (numOfBytes + 1);
			    sizeofdata -= (numOfBytes + 1);
			}
		    }
		}
	    }

	    if (intel_output->isHDMI_Device && intel_output->isHDMI_Monitor) {
		DisplayModePtr mode = modes;

		while (mode) {
		    if (((mode->Clock == 30240) && (mode->HDisplay == 640)
			 && (mode->VDisplay == 480))
			|| ((mode->HDisplay == 720)
			    && (mode->VDisplay == 400))
			|| ((mode->HDisplay == 1920)
			    && (mode->VDisplay == 1200))
			|| ((mode->HDisplay == 832)
			    && (mode->VDisplay == 624))) {
			PSB_DEBUG(output->scrn->scrnIndex, 3,
				  "i830_sdvo_get_modes: remove the unsupported HDMI mode:\n");
			PSB_DEBUG(output->scrn->scrnIndex, 3,
				  "the Resolution is %dx%d@%d\n",
				  mode->HDisplay, mode->VDisplay,
				  mode->Clock);
			//xf86DeleteMode(modes, temp_mode);
			mode->status = MODE_BAD;
		    }
		    mode = mode->next;
		}

	    }

	}

	if (modes != NULL)
	    return modes;

	/* Mac mini hack.  On this device, I get DDC through the analog, which
	 * load-detects as disconnected.  I fail to DDC through the SDVO DDC,
	 * but it does load-detect as connected.  So, just steal the DDC bits from
	 * analog when we fail at finding it the right way.
	 */
	crt = xf86_config->output[0];
	if (crt->funcs->detect(crt) == XF86OutputStatusDisconnected) {
	    return crt->funcs->get_modes(crt);
	}

	return NULL;
    } else if (pSDVO->ActiveDevice == SDVO_DEVICE_TV) {
	PSB_DEBUG(output->scrn->scrnIndex, 3, "SDVO_DEVICE_TV\n");
	modes = i830_sdvo_get_tvmode_from_table(output);
	if (modes != NULL)
	    return modes;
	return NULL;
    } else {
	PSB_DEBUG(output->scrn->scrnIndex, 3, "other device, no mode get\n");
	return NULL;
    }

}

static void
i830_sdvo_destroy(xf86OutputPtr output)
{
    PSB_DEBUG(output->scrn->scrnIndex, 3, "xxi830_sdvo_destroy\n");
    PsbOutputPrivatePtr intel_output = output->driver_private;

    if (intel_output && (--intel_output->refCount == 0)) {
	PsbSDVOOutputPtr pSDVO =
	    containerOf(output->driver_private, PsbSDVOOutputRec,
			psbOutput);

	xf86DestroyI2CBusRec(intel_output->pDDCBus, FALSE, FALSE);
	xf86DestroyI2CDevRec(&pSDVO->d, FALSE);
	xf86DestroyI2CBusRec(pSDVO->d.pI2CBus, TRUE, TRUE);
	xfree(intel_output);
    }
}

#ifdef RANDR_12_INTERFACE

typedef struct
{
    char *name;
    CARD32 TVStandard;
} tv_format_t;

const static tv_format_t tv_formats[] = {
    {
     .name = "NTSC_M",
     .TVStandard = TVSTANDARD_NTSC_M,
     },
    {
     .name = "NTSC_M_J",
     .TVStandard = TVSTANDARD_NTSC_M_J,
     },
    {
     .name = "PAL_B",
     .TVStandard = TVSTANDARD_PAL_B,
     },
    {
     .name = "PAL_D",
     .TVStandard = TVSTANDARD_PAL_D,
     },
    {
     .name = "PAL_H",
     .TVStandard = TVSTANDARD_PAL_H,
     },
    {
     .name = "PAL_I",
     .TVStandard = TVSTANDARD_PAL_I,
     },
    {
     .name = "PAL_M",
     .TVStandard = TVSTANDARD_PAL_M,
     },
    {
     .name = "PAL_N",
     .TVStandard = TVSTANDARD_PAL_N,
     },
    {
     .name = "SECAM_B",
     .TVStandard = TVSTANDARD_SECAM_B,
     },
    {
     .name = "SECAM_D",
     .TVStandard = TVSTANDARD_SECAM_D,
     },
    {
     .name = "SECAM_G",
     .TVStandard = TVSTANDARD_SECAM_G,
     },
    {
     .name = "SECAM_H",
     .TVStandard = TVSTANDARD_SECAM_H,
     },
    {
     .name = "SECAM_K",
     .TVStandard = TVSTANDARD_SECAM_K,
     },
    {
     .name = "SECAM_K1",
     .TVStandard = TVSTANDARD_SECAM_K1,
     },
    {
     .name = "SECAM_L",
     .TVStandard = TVSTANDARD_SECAM_L,
     },
    {
     .name = "PAL_G",
     .TVStandard = TVSTANDARD_PAL_G,
     },
    {
     .name = "PAL_60",
     .TVStandard = TVSTANDARD_PAL_60,
     },
};

#define NUM_TV_FORMATS sizeof(tv_formats)/sizeof (tv_formats[0])

#define TV_FORMAT_NAME	"TV_FORMAT"
static Atom tv_format_atom;
static Atom tv_format_name_atoms[NUM_TV_FORMATS];

#define TV_HUE_NAME	"TV_HUE"
static Atom tv_hue_atom;

#define TV_BRIGHTNESS_NAME	"TV_BRIGHTNESS"
static Atom tv_brightness_atom;

#define TV_CONTRAST_NAME	"TV_CONTRAST"
static Atom tv_contrast_atom;

#define TV_OVERSCAN_X_NAME	"TV_OVERSCAN_X"
static Atom tv_overscan_x_atom;

#define TV_OVERSCAN_Y_NAME	"TV_OVERSCAN_Y"
static Atom tv_overscan_y_atom;

#endif /* RANDR_12_INTERFACE */

static void
i830_sdvo_create_resources(xf86OutputPtr output)
{
#ifdef RANDR_12_INTERFACE
    ScrnInfoPtr pScrn = output->scrn;

    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);
    INT32 range[2];
    int data, err;
    int i;

    i830_tv_get_default_params(output);
    i830_tv_set_overscan_parameters(output);
    i830_tv_program_display_params(output);

    /* Set up the tv format property, which takes effect immediately
     * and accepts values only within the range.
     *
     * XXX: Currently, RandR doesn't verify that properties set are
     * within the range.
     */
    tv_format_atom = MakeAtom(TV_FORMAT_NAME, sizeof(TV_FORMAT_NAME) - 1,
			      TRUE);

    for (i = 0; i < NUM_TV_FORMATS; i++)
	tv_format_name_atoms[i] = MakeAtom(tv_formats[i].name,
					   strlen(tv_formats[i].name), TRUE);

    err = RRConfigureOutputProperty(output->randr_output, tv_format_atom,
				    TRUE, FALSE, FALSE,
				    NUM_TV_FORMATS,
				    (INT32 *) tv_format_name_atoms);

    if (err != 0) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "RRConfigureOutputProperty error, %d\n", err);
    }

    /* Set the current value of the tv_format property */
    for (i = 0; i < NUM_TV_FORMATS; i++) {
	if (pSDVO->TVStandard == tv_formats[i].TVStandard)
	    break;
    }

    if (i >= NUM_TV_FORMATS)
	i = 0;
    err = RRChangeOutputProperty(output->randr_output, tv_format_atom,
				 XA_ATOM, 32, PropModeReplace, 1,
				 &tv_format_name_atoms[i], FALSE, TRUE);

    if (err != 0) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "RRChangeOutputProperty error, %d\n", err);
    }

    /* Set up the tv hue property, which takes effect immediately
     * and accepts values only within the range.
     *
     * XXX: Currently, RandR doesn't verify that properties set are
     * within the range.
     */
    tv_hue_atom = MakeAtom(TV_HUE_NAME, sizeof(TV_HUE_NAME) - 1, TRUE);

    range[0] = pSDVO->dispParams.Hue.Min;
    range[1] = pSDVO->dispParams.Hue.Max;
    /* Set the tv hue  to default for now */
    data = pSDVO->dispParams.Hue.Default;

    err = RRConfigureOutputProperty(output->randr_output, tv_hue_atom,
				    FALSE, TRUE, FALSE, 2, range);
    if (err != 0) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "RRConfigureOutputProperty error, %d\n", err);
    }

    /* Set the current value of the tv hue property */
    err = RRChangeOutputProperty(output->randr_output, tv_hue_atom,
				 XA_INTEGER, 32, PropModeReplace, 1, &data,
				 FALSE, TRUE);
    if (err != 0) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "RRChangeOutputProperty error, %d\n", err);
    }

    /* Set up the tv brightness property, which takes effect immediately
     * and accepts values only within the range.
     *
     * XXX: Currently, RandR doesn't verify that properties set are
     * within the range.
     */
    tv_brightness_atom =
	MakeAtom(TV_BRIGHTNESS_NAME, sizeof(TV_BRIGHTNESS_NAME) - 1, TRUE);

    range[0] = pSDVO->dispParams.Brightness.Min;
    range[1] = pSDVO->dispParams.Brightness.Max;
    /* Set the tv brightness to default for now */
    data = pSDVO->dispParams.Brightness.Default;

    err = RRConfigureOutputProperty(output->randr_output, tv_brightness_atom,
				    FALSE, TRUE, FALSE, 2, range);
    if (err != 0) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "RRConfigureOutputProperty error, %d\n", err);
    }

    /* Set the current value of the tv brightness property */
    err = RRChangeOutputProperty(output->randr_output, tv_brightness_atom,
				 XA_INTEGER, 32, PropModeReplace, 1, &data,
				 FALSE, TRUE);
    if (err != 0) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "RRChangeOutputProperty error, %d\n", err);
    }

    /* Set up the tv contrast property, which takes effect immediately
     * and accepts values only within the range.
     *
     * XXX: Currently, RandR doesn't verify that properties set are
     * within the range.
     */
    tv_contrast_atom =
	MakeAtom(TV_CONTRAST_NAME, sizeof(TV_CONTRAST_NAME) - 1, TRUE);

    range[0] = pSDVO->dispParams.Contrast.Min;
    range[1] = pSDVO->dispParams.Contrast.Max;
    /* Set the tv contrast to default for now */
    data = pSDVO->dispParams.Contrast.Default;

    err = RRConfigureOutputProperty(output->randr_output, tv_contrast_atom,
				    FALSE, TRUE, FALSE, 2, range);
    if (err != 0) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "RRConfigureOutputProperty error, %d\n", err);
    }
    /* Set the current value of the tv contrast property */
    err = RRChangeOutputProperty(output->randr_output, tv_contrast_atom,
				 XA_INTEGER, 32, PropModeReplace, 1, &data,
				 FALSE, TRUE);
    if (err != 0) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "RRChangeOutputProperty error, %d\n", err);
    }

    /* Set up the tv overscan x property, which takes effect immediately
     * and accepts values only within the range.
     *
     * XXX: Currently, RandR doesn't verify that properties set are
     * within the range.
     */
    tv_overscan_x_atom =
	MakeAtom(TV_OVERSCAN_X_NAME, sizeof(TV_OVERSCAN_X_NAME) - 1, TRUE);

    range[0] = pSDVO->OverScanX.Min;
    range[1] = pSDVO->OverScanX.Max;
    /* Set the tv contrast to default for now */
    data = pSDVO->OverScanX.Default;

    err = RRConfigureOutputProperty(output->randr_output, tv_overscan_x_atom,
				    FALSE, TRUE, FALSE, 2, range);
    if (err != 0) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "RRConfigureOutputProperty error, %d\n", err);
    }

    /* Set the current value of the tv overscan x property */
    err = RRChangeOutputProperty(output->randr_output, tv_overscan_x_atom,
				 XA_INTEGER, 32, PropModeReplace, 1, &data,
				 FALSE, TRUE);
    if (err != 0) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "RRChangeOutputProperty error, %d\n", err);
    }

    /* Set up the tv overscan y property, which takes effect immediately
     * and accepts values only within the range.
     *
     * XXX: Currently, RandR doesn't verify that properties set are
     * within the range.
     */
    tv_overscan_y_atom =
	MakeAtom(TV_OVERSCAN_Y_NAME, sizeof(TV_OVERSCAN_Y_NAME) - 1, TRUE);

    range[0] = pSDVO->OverScanY.Min;
    range[1] = pSDVO->OverScanY.Max;
    /* Set the tv contrast to default for now */
    data = pSDVO->OverScanY.Default;

    err = RRConfigureOutputProperty(output->randr_output, tv_overscan_y_atom,
				    FALSE, TRUE, FALSE, 2, range);
    if (err != 0) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "RRConfigureOutputProperty error, %d\n", err);
    }
    /* Set the current value of the tv overscan y property */
    err = RRChangeOutputProperty(output->randr_output, tv_overscan_y_atom,
				 XA_INTEGER, 32, PropModeReplace, 1, &data,
				 FALSE, TRUE);
    if (err != 0) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "RRChangeOutputProperty error, %d\n", err);
    }
#endif /* RANDR_12_INTERFACE */
}

#ifdef RANDR_12_INTERFACE
static Bool
i830_sdvo_set_property(xf86OutputPtr output, Atom property,
		       RRPropertyValuePtr value)
{
    ScrnInfoPtr pScrn = output->scrn;
    PsbSDVOOutputPtr pSDVO =
	containerOf(output->driver_private, PsbSDVOOutputRec, psbOutput);
    CARD8 status;

    if (property == tv_format_atom) {
	Atom atom;
	char *name;
	char *val;
	int i;

	if (value->type != XA_ATOM || value->format != 32 || value->size != 1) {
	    return FALSE;
	}

	memcpy(&atom, value->data, 4);
	name = NameForAtom(atom);

	val = xalloc(strlen(name) + 1);
	if (!val)
	    return FALSE;
	strcpy(val, name);

	for (i = 0; i < NUM_TV_FORMATS; i++) {
	    const tv_format_t *tv_format = &tv_formats[i];

	    if (xf86nameCompare(val, tv_format->name) == 0)
		break;
	}

	if (i >= NUM_TV_FORMATS) {
	    xfree(val);
	    return FALSE;
	}

	pSDVO->TVStandard = tv_formats[i].TVStandard;
	if (pSDVO->TVMode == TVMODE_HDTV)
	    i830_sdvo_map_hdtvstd_bitmask(output);
	else
	    i830_sdvo_map_sdtvstd_bitmask(output);
	/* Set TV format */
	//i830_sdvo_set_tvoutputs_formats(output);
        if(pSDVO->ActiveDevice != SDVO_DEVICE_NONE)
            i830_sdvo_mode_set(output,pSDVO->currentMode,pSDVO->currentMode);

	PSB_DEBUG(output->scrn->scrnIndex, 3,
		  "i830_sdvo_set_tvoutputs_formats, format is %s\n", val);
	xfree(val);
	return TRUE;
    }

    if (property == tv_hue_atom) {
	INT32 val;

	if (value->type != XA_INTEGER || value->format != 32 ||
	    value->size != 1) {
	    return FALSE;
	}

	val = *(INT32 *) value->data;
	if (val < pSDVO->dispParams.Hue.Min
	    || val > pSDVO->dispParams.Hue.Max)
	    return FALSE;

	status = i830_sdvo_set_hue(output, val);
	pSDVO->dispParams.Hue.Value = val;
	PSB_DEBUG(output->scrn->scrnIndex, 3,
		  "i830_sdvo_set_hue, hue is %ld\n", val);
	return TRUE;
    }

    if (property == tv_brightness_atom) {
	INT32 val;

	if (value->type != XA_INTEGER || value->format != 32 ||
	    value->size != 1) {
	    return FALSE;
	}

	val = *(INT32 *) value->data;
	if (val < pSDVO->dispParams.Brightness.Min
	    || val > pSDVO->dispParams.Brightness.Max)
	    return FALSE;

	status = i830_sdvo_set_brightness(output, val);
	pSDVO->dispParams.Brightness.Value = val;
	PSB_DEBUG(output->scrn->scrnIndex, 3,
		  "i830_sdvo_set_brightness, brightness is %ld\n", val);
	return TRUE;
    }

    if (property == tv_contrast_atom) {
	INT32 val;

	if (value->type != XA_INTEGER || value->format != 32 ||
	    value->size != 1) {
	    return FALSE;
	}

	val = *(INT32 *) value->data;
	if (val < pSDVO->dispParams.Contrast.Min
	    || val > pSDVO->dispParams.Contrast.Max)
	    return FALSE;

	status = i830_sdvo_set_contrast(output, val);
	pSDVO->dispParams.Contrast.Value = val;
	PSB_DEBUG(output->scrn->scrnIndex, 3,
		  "i830_sdvo_set_contrast, contrast is %ld\n", val);
	return TRUE;
    }

    if (property == tv_overscan_x_atom) {
	INT32 val;

	if (value->type != XA_INTEGER || value->format != 32 ||
	    value->size != 1) {
	    return FALSE;
	}

	val = *(INT32 *) value->data;
	if (val < pSDVO->OverScanX.Min || val > pSDVO->OverScanX.Max)
	    return FALSE;

	status = i830_sdvo_set_horizontal_overscan(output, val);
	pSDVO->OverScanX.Value = val;
	PSB_DEBUG(output->scrn->scrnIndex, 3,
		  "i830_sdvo_set_horzontal_overscan, x overscan is %ld\n",
		  val);
	return TRUE;
    }

    if (property == tv_overscan_y_atom) {
	INT32 val;

	if (value->type != XA_INTEGER || value->format != 32 ||
	    value->size != 1) {
	    return FALSE;
	}

	val = *(INT32 *) value->data;
	if (val < pSDVO->OverScanY.Min || val > pSDVO->OverScanY.Max)
	    return FALSE;

	/*status = i830_sdvo_set_vertical_overscan(output, val);*/
	pSDVO->OverScanY.Value = val;

        if(pSDVO->ActiveDevice == SDVO_DEVICE_TV)
            i830_sdvo_mode_set(output,pSDVO->currentMode,pSDVO->currentMode);
        
	PSB_DEBUG(output->scrn->scrnIndex, 2,
		  "i830_sdvo_set_vertical_overscan, y overscan is %ld, status is %d\n",
		  val, status);
	return TRUE;
    }

    return TRUE;
}
#endif /* RANDR_12_INTERFACE */

static const xf86OutputFuncsRec i830_sdvo_output_funcs = {
    .create_resources = i830_sdvo_create_resources,
    .dpms = i830_sdvo_dpms,
    .save = i830_sdvo_save,
    .restore = i830_sdvo_restore,
    .mode_valid = i830_sdvo_mode_valid,
    .mode_fixup = i830_sdvo_mode_fixup,
    .prepare = i830_output_prepare,
    .mode_set = i830_sdvo_mode_set,
    .commit = i830_output_commit,
    .detect = i830_sdvo_detect,
    .get_modes = i830_sdvo_get_modes,
#ifdef RANDR_12_INTERFACE
    .set_property = i830_sdvo_set_property,
#endif
    .destroy = i830_sdvo_destroy
};

xf86OutputPtr
psbSDVOInit(ScrnInfoPtr pScrn, int output_device, char *deviceName)
{
    xf86OutputPtr output;
    PsbOutputPrivatePtr intel_output;
    PsbSDVOOutputPtr pSDVO;
    int i;
    unsigned char ch[0x40];
    I2CBusPtr i2cbus = NULL, ddcbus;
    char *name_prefix = NULL;
    char *name_suffix = NULL;

    int count = 3;
    CARD8 response[2];
    CARD8 status;
    PsbDevicePtr pDevice = psbDevicePTR(psbPTR(pScrn));

    PSB_DEBUG(pScrn->scrnIndex, 3, "i830_psbSDVOInit\n");

	if (pDevice->sku_bSDVOEnable == FALSE)
	{
		PSB_DEBUG(pScrn->scrnIndex, 2, "i830_psbSDVOIniti: SDVO is disabled\n");
		return NULL;
	}

    output = xf86OutputCreate(pScrn, &i830_sdvo_output_funcs, NULL);
    if (!output)
	return NULL;
    pSDVO = xnfcalloc(sizeof(PsbSDVOOutputRec), 1);
    if (!pSDVO) {
	xf86OutputDestroy(output);
	return NULL;
    }
    intel_output = &pSDVO->psbOutput;
    intel_output->pDevice = psbDevicePTR(psbPTR(pScrn));
    output->driver_private = intel_output;
    output->interlaceAllowed = FALSE;
    output->doubleScanAllowed = FALSE;
    intel_output->type = PSB_OUTPUT_SDVO;
    intel_output->refCount = 1;

    /* While it's the same bus, we just initialize a new copy to avoid trouble
     * with tracking refcounting ourselves, since the XFree86 DDX bits don't.
     */
    if (output_device == SDVOB)
	I830I2CInit(pScrn, &i2cbus, GPIOE, "SDVOCTRL_E for SDVOB");
    else
	I830I2CInit(pScrn, &i2cbus, GPIOE, "SDVOCTRL_E for SDVOC");

    if (i2cbus == NULL) {
	xf86OutputDestroy(output);
	return NULL;
    }

    if (output_device == SDVOB) {
	pSDVO->d.DevName = "SDVO Controller B";
	pSDVO->d.SlaveAddr = 0x70;
	/*For Poulsbo, only SDVO port B */
	pSDVO->byInputWiring = SDVOB_IN0;
	name_suffix = "-1";
    } else {
	pSDVO->d.DevName = "SDVO Controller C";
	pSDVO->d.SlaveAddr = 0x72;
	name_suffix = "-2";
    }
    pSDVO->d.pI2CBus = i2cbus;
    pSDVO->d.DriverPrivate.ptr = output;
    pSDVO->output_device = output_device;

    if (!xf86I2CDevInit(&pSDVO->d)) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "Failed to initialize %s I2C device\n", SDVO_NAME(pSDVO));
	xf86OutputDestroy(output);
	return NULL;
    }

    intel_output->pI2CBus = i2cbus;

    /* Read the regs to test if we can talk to the device */
    for (i = 0; i < 0x40; i++) {
	if (!i830_sdvo_read_byte_quiet(output, i, &ch[i])) {
	    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		       "No SDVO device found on SDVO%c\n",
		       output_device == SDVOB ? 'B' : 'C');
	    xf86OutputDestroy(output);
	    return NULL;
	}
    }

    /* Set up our wrapper I2C bus for DDC.  It acts just like the regular I2C
     * bus, except that it does the control bus switch to DDC mode before every
     * Start.  While we only need to do it at Start after every Stop after a
     * Start, extra attempts should be harmless.
     */
    ddcbus = xf86CreateI2CBusRec();
    if (ddcbus == NULL) {
	xf86OutputDestroy(output);
	return NULL;
    }
    if (output_device == SDVOB)
	ddcbus->BusName = "SDVOB DDC Bus";
    else
	ddcbus->BusName = "SDVOC DDC Bus";
    ddcbus->scrnIndex = i2cbus->scrnIndex;
    ddcbus->I2CGetByte = i830_sdvo_ddc_i2c_get_byte;
    ddcbus->I2CPutByte = i830_sdvo_ddc_i2c_put_byte;
    ddcbus->I2CStart = i830_sdvo_ddc_i2c_start;
    ddcbus->I2CStop = i830_sdvo_ddc_i2c_stop;
    ddcbus->I2CAddress = i830_sdvo_ddc_i2c_address;
    ddcbus->DriverPrivate.ptr = output;

    if (!xf86I2CBusInit(ddcbus)) {
	xf86OutputDestroy(output);
	return NULL;
    }

    intel_output->pI2CBus = i2cbus;
    intel_output->pDDCBus = ddcbus;

    if (!i830_sdvo_get_capabilities(output, &pSDVO->caps)) {
	/*No SDVO support, power down the pipe */
	i830_sdvo_dpms(output, DPMSModeOff);
	xf86DrvMsg(intel_output->pI2CBus->scrnIndex, X_ERROR,
		   ": No SDVO detected\n");
	xf86OutputDestroy(output);
	return NULL;
    }

    PSB_DEBUG(output->scrn->scrnIndex, 3,
	      "sdvo_get_capabilities, caps.output_flags=%x\n",
	      pSDVO->caps.output_flags);
    memset(&pSDVO->active_outputs, 0, sizeof(pSDVO->active_outputs));

#if 0
    i830_sdvo_reset(output);
#endif
    while (count--) {
	i830_sdvo_write_cmd(output, SDVO_CMD_GET_ATTACHED_DISPLAYS, NULL, 0);
	status = i830_sdvo_read_response(output, &response, 2);

	if (status == SDVO_CMD_STATUS_PENDING) {
	    i830_sdvo_reset(output);
	    continue;
	}

	if (status != SDVO_CMD_STATUS_SUCCESS) {
	    usleep(1000);
	    continue;
	}
    }
    if (response[0] != 0 || response[1] != 0) {
	/*Check what device types are connected to the hardware CRT/HDTV/S-Video/Composite */
	/*in case of CRT and multiple TV's attached give preference in the order mentioned below */
	/* 1. RGB */
	/* 2. HDTV */
	/* 3. S-Video */
	/* 4. composite */
	if (pSDVO->caps.output_flags & SDVO_OUTPUT_TMDS0) {
	    pSDVO->active_outputs = SDVO_OUTPUT_TMDS0;
	    output->subpixel_order = SubPixelHorizontalRGB;
	    name_prefix = "TMDS";
	    pSDVO->ActiveDevice = SDVO_DEVICE_TMDS;
	} else if (pSDVO->caps.output_flags & SDVO_OUTPUT_TMDS1) {
	    pSDVO->active_outputs = SDVO_OUTPUT_TMDS1;
	    output->subpixel_order = SubPixelHorizontalRGB;
	    name_prefix = "TMDS";
	    pSDVO->ActiveDevice = SDVO_DEVICE_TMDS;
	} else if (response[0] & SDVO_OUTPUT_RGB0) {
	    pSDVO->active_outputs = SDVO_OUTPUT_RGB0;
	    output->subpixel_order = SubPixelHorizontalRGB;
	    name_prefix = "RGB0";
	    pSDVO->ActiveDevice = SDVO_DEVICE_CRT;
	} else if ((response[1] << 8 | response[0]) & SDVO_OUTPUT_RGB1) {
	    pSDVO->active_outputs = SDVO_OUTPUT_RGB1;
	    output->subpixel_order = SubPixelHorizontalRGB;
	    name_prefix = "RGB1";
	    pSDVO->ActiveDevice = SDVO_DEVICE_CRT;
	} else if (response[0] & SDVO_OUTPUT_YPRPB0) {
	    pSDVO->active_outputs = SDVO_OUTPUT_YPRPB0;
	} else if ((response[1] << 8 | response[0]) & SDVO_OUTPUT_YPRPB1) {
	    pSDVO->active_outputs = SDVO_OUTPUT_YPRPB1;
	}
	/* SCART is given Second preference */
	else if (response[0] & SDVO_OUTPUT_SCART0) {
	    pSDVO->active_outputs = SDVO_OUTPUT_SCART0;

	} else if ((response[1] << 8 | response[0]) & SDVO_OUTPUT_SCART1) {
	    pSDVO->active_outputs = SDVO_OUTPUT_SCART1;
	}
        /* if S-Video type TV is connected along with Composite type TV give preference to S-Video */
        else if (response[0] & SDVO_OUTPUT_SVID0) {
            pSDVO->active_outputs = SDVO_OUTPUT_SVID0;

        } else if ((response[1] << 8 | response[0]) & SDVO_OUTPUT_SVID1) {
            pSDVO->active_outputs = SDVO_OUTPUT_SVID1;
        }
        /* Composite is given least preference */
        else if (response[0] & SDVO_OUTPUT_CVBS0) {
            pSDVO->active_outputs = SDVO_OUTPUT_CVBS0;
        } else if ((response[1] << 8 | response[0]) & SDVO_OUTPUT_CVBS1) {
            pSDVO->active_outputs = SDVO_OUTPUT_CVBS1;
        } else {
            PSB_DEBUG(output->scrn->scrnIndex, 2, "no display attached\n");
            unsigned char bytes[2];

            memcpy(bytes, &pSDVO->caps.output_flags, 2);
            xf86DrvMsg(intel_output->pI2CBus->scrnIndex, X_ERROR,
                       "%s: No active TMDS or RGB outputs (0x%02x%02x) 0x%08x\n",
                       SDVO_NAME(pSDVO), bytes[0], bytes[1],
                       pSDVO->caps.output_flags);
            name_prefix = "Unknown";
        }

         /* init para for TV connector */
        if (pSDVO->active_outputs & SDVO_OUTPUT_TV0) {
            output->subpixel_order = SubPixelHorizontalRGB;
            name_prefix = "TV0";
            /* Init TV mode setting para */
            pSDVO->ActiveDevice = SDVO_DEVICE_TV;
            pSDVO->bGetClk = TRUE;
            if (pSDVO->active_outputs == SDVO_OUTPUT_YPRPB0 ||
                                         pSDVO->active_outputs == SDVO_OUTPUT_YPRPB1) {
                pSDVO->TVStandard = HDTV_SMPTE_274M_1080i60;
                pSDVO->TVMode = TVMODE_HDTV;
            } else {
                pSDVO->TVStandard = TVSTANDARD_NTSC_M;
                pSDVO->TVMode = TVMODE_SDTV;
            }
            intel_output->pDevice->TVEnabled = TRUE;
             /*Init Display parameter for TV */
            pSDVO->OverScanX.Value = 0xffffffff;
            pSDVO->OverScanY.Value = 0xffffffff;
            pSDVO->dispParams.Brightness.Value = 0x80;
            pSDVO->dispParams.FlickerFilter.Value = 0xffffffff;
            pSDVO->dispParams.AdaptiveFF.Value = 7;
            pSDVO->dispParams.TwoD_FlickerFilter.Value = 0xffffffff;
            pSDVO->dispParams.Contrast.Value = 0x40;
            pSDVO->dispParams.PositionX.Value = 0x200;
            pSDVO->dispParams.PositionY.Value = 0x200;
            pSDVO->dispParams.DotCrawl.Value = 1;
            pSDVO->dispParams.ChromaFilter.Value = 1;
            pSDVO->dispParams.LumaFilter.Value = 2;
            pSDVO->dispParams.Sharpness.Value = 4;
            pSDVO->dispParams.Saturation.Value = 0x45;
            pSDVO->dispParams.Hue.Value = 0x40;
            pSDVO->dispParams.Dither.Value = 0;
        }

        strcpy(deviceName, name_prefix);
        strcat(deviceName, name_suffix);
        if (!xf86OutputRename(output, deviceName)) {
            xf86OutputDestroy(output);
            return NULL;
        }
    } else {
        /*No SDVO display device attached */
        i830_sdvo_dpms(output, DPMSModeOff);
        pSDVO->active_outputs = 0;
        output->subpixel_order = SubPixelHorizontalRGB;
        name_prefix = "SDVO";
        pSDVO->ActiveDevice = SDVO_DEVICE_NONE;
        strcpy(deviceName, name_prefix);
        strcat(deviceName, name_suffix);
        if (!xf86OutputRename(output, deviceName)) {
            xf86OutputDestroy(output);
            return NULL;
        }

    }

    (void)i830_sdvo_set_active_outputs(output, pSDVO->active_outputs);

    /* Set the input timing to the screen. Assume always input 0. */
    i830_sdvo_set_target_input(output, TRUE, FALSE);

    i830_sdvo_get_input_pixel_clock_range(output, &pSDVO->pixel_clock_min,
                                          &pSDVO->pixel_clock_max);

    /* Check whether the device is HDMI supported. Jamesx */
    if (pSDVO->ActiveDevice ==  SDVO_DEVICE_TMDS) {
        CARD8 status;
        CARD8 byargs[2];

        byargs[0] = 0;
        byargs[1] = 0;

        i830_sdvo_write_cmd(output, SDVO_CMD_GET_SUPP_ENCODE, NULL, 0);

        intel_output->isHDMI_Device = 0;        //initial the value to 0
        status = i830_sdvo_read_response(output, byargs, 2);
        if (status != SDVO_CMD_STATUS_SUCCESS)
            PSB_DEBUG(output->scrn->scrnIndex, 3,
                        "psbSDVOInit: check HDMI device fail, no HDMI device\n");
        else {
            intel_output->isHDMI_Device = byargs[1];
            PSB_DEBUG(output->scrn->scrnIndex, 3,
                      "psbSDVOInit: HDMI device value is %d\n",
                      intel_output->isHDMI_Device);
        }
        intel_output->isHDMI_Monitor = 0;       //Will check the monitor HDMI supporting later
            
        uint8_t colorimetry = SDVO_COLORIMETRY_RGB256;
        i830_sdvo_write_cmd(output, SDVO_CMD_SET_COLORIMETRY, &colorimetry, 1);
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "%s device VID/DID: %02X:%02X.%02X, "
               "clock range %.1fMHz - %.1fMHz, "
               "input 1: %c, input 2: %c, "
               "output 1: %c, output 2: %c\n",
               SDVO_NAME(pSDVO),
               pSDVO->caps.vendor_id, pSDVO->caps.device_id,
               pSDVO->caps.device_rev_id,
               pSDVO->pixel_clock_min / 1000.0,
               pSDVO->pixel_clock_max / 1000.0,
               (pSDVO->caps.sdvo_inputs_mask & 0x1) ? 'Y' : 'N',
               (pSDVO->caps.sdvo_inputs_mask & 0x2) ? 'Y' : 'N',
               pSDVO->caps.output_flags &
               (SDVO_OUTPUT_TMDS0 | SDVO_OUTPUT_RGB0) ? 'Y' : 'N',
               pSDVO->caps.output_flags &
               (SDVO_OUTPUT_TMDS1 | SDVO_OUTPUT_RGB1) ? 'Y' : 'N');

    return output;
}

Bool Edid_IsSupportedCeMode(PEDID_DTD pDTD)
{
        unsigned long  modeUID;
        unsigned long  xRes, yRes, rRate;
        unsigned long  hTotal, vTotal, pixelClock,hBlank,vBlank;
        unsigned long  supModes;
        int num;
        Bool supportedMode = FALSE;

        supModes = sizeof (g_CeShortVideoModes) / sizeof (unsigned long);

        xRes = (pDTD->ucHA_high << 8) + pDTD->ucHA_low;
        yRes = (pDTD->ucVA_high << 8) + pDTD->ucVA_low;

        if (pDTD->ucInterlaced)
                yRes = yRes*2;

        pixelClock = pDTD->wPixelClock * 10000;

        if ((pixelClock==0) || (xRes==0) || (yRes==0))
                return FALSE;

        hBlank = (pDTD->ucHBL_high << 8) + pDTD->ucHBL_low;
        hTotal = xRes + hBlank;
        vBlank = (pDTD->ucVBL_high << 8) + pDTD->ucVBL_low;
        if (pDTD->ucInterlaced)
                vBlank = vBlank*2;
        vTotal = yRes + vBlank;
        rRate = (pixelClock + hTotal * vTotal / 2)/ (hTotal * vTotal);
        if (pDTD->ucInterlaced)
                rRate = rRate*2;

        modeUID = m_ModeUID (xRes, yRes, rRate, pDTD->ucInterlaced);

        /*Check whether we have this ModeUID in Short Video Descriptor Static table*/
        for (num = 0; num < supModes; num++)
        {
                if (g_CeShortVideoModes[num] == modeUID)
                {
                        supportedMode = TRUE;
                        break;
                }
        }

        return supportedMode;
}

Bool Edid_ConvertDTDTiming (PEDID_DTD  pDTD, DCDisplayInfo *  out_pTimingInfo)
{
        unsigned long  modeUID;
        unsigned long  xRes, yRes, rRate;
        unsigned long  hTotal, vTotal, pixelClock, hBlank, vBlank, hSPW, vSPW;
        unsigned long  hSO, vSO;

        xRes = (pDTD->ucHA_high << 8) + pDTD->ucHA_low;
        yRes = (pDTD->ucVA_high << 8) + pDTD->ucVA_low;

        if (pDTD->ucInterlaced)
                yRes = yRes*2;

        pixelClock = pDTD->wPixelClock * 10000;

        if ((pixelClock==0) || (xRes==0) || (yRes==0))
        {
                return FALSE;
        }

        hBlank = (pDTD->ucHBL_high << 8) + pDTD->ucHBL_low;
        hTotal = xRes + hBlank;
        vBlank = (pDTD->ucVBL_high << 8) + pDTD->ucVBL_low;
        if (pDTD->ucInterlaced)
                vBlank = vBlank*2;
        vTotal = yRes + vBlank;
        rRate = (pixelClock + hTotal * vTotal / 2)/ (hTotal * vTotal);
        if (pDTD->ucInterlaced)
                rRate = rRate*2;

        modeUID = m_ModeUID (xRes, yRes, rRate, pDTD->ucInterlaced);

        hSPW = (pDTD->ucHSPW_high << 8) + pDTD->ucHSPW_low;
        vSPW = (pDTD->ucVSPW_high << 4) + pDTD->ucVSPW_low;

        hSO = (pDTD->ucHSO_high << 8) + pDTD->ucHSO_low;
        vSO = (pDTD->ucVSO_high << 4) + pDTD->ucVSO_low;
        if (pDTD->ucInterlaced)
                vSO = vSO*2;

        out_pTimingInfo->dwDotClock = pixelClock;
        out_pTimingInfo->dwHTotal = hTotal;
        out_pTimingInfo->dwHActive = xRes;
        out_pTimingInfo->dwHBlankStart = xRes + pDTD->ucHBorder;
        //out_pTimingInfo->dwHBlankEnd = hTotal - pDTD->ucHBorder - 1;
        out_pTimingInfo->dwHBlankEnd = hTotal - pDTD->ucHBorder;
        out_pTimingInfo->dwHSyncStart = xRes + hSO;
        //out_pTimingInfo->dwHSyncEnd = out_pTimingInfo->dwHSyncStart + hSPW - 1;
        out_pTimingInfo->dwHSyncEnd = out_pTimingInfo->dwHSyncStart + hSPW;
        out_pTimingInfo->dwHRefresh = pixelClock/hTotal;

        out_pTimingInfo->dwVTotal = vTotal;
        out_pTimingInfo->dwVActive = yRes;
        out_pTimingInfo->dwVBlankStart = yRes + pDTD->ucVBorder;
        //out_pTimingInfo->dwVBlankEnd = vTotal - pDTD->ucVBorder - 1;
        out_pTimingInfo->dwVBlankEnd = vTotal - pDTD->ucVBorder;
        out_pTimingInfo->dwVSyncStart = yRes + vSO;

        //out_pTimingInfo->dwVSyncEnd = out_pTimingInfo->dwVSyncStart + vSPW - 1;
        out_pTimingInfo->dwVSyncEnd = out_pTimingInfo->dwVSyncStart + vSPW;
        out_pTimingInfo->dwVRefresh = rRate;

        out_pTimingInfo->bInterlaced = (pDTD->ucInterlaced == 1) ? TRUE : FALSE;
        out_pTimingInfo->bHSyncPolarity = (pDTD->ucHSync_Pol == 1) ? TRUE : FALSE;
        out_pTimingInfo->bVSyncPolarity = (pDTD->ucVSync_Pol == 1) ? TRUE : FALSE;

        return TRUE;
}

static DisplayModePtr
GetModeFromDetailedTiming(DCDisplayInfo *pTimingInfo)
{
        DisplayModePtr Mode = NULL;

        if (pTimingInfo) {
           Mode = xnfcalloc(1, sizeof(DisplayModeRec));

           if (Mode) {
              Mode->Clock = pTimingInfo->dwDotClock / 1000;  //need divide 1000, kHz is used
              Mode->HDisplay = pTimingInfo->dwHActive;
              Mode->HSyncStart = pTimingInfo->dwHSyncStart;
              Mode->HSyncEnd = pTimingInfo->dwHSyncEnd;
              Mode->HTotal = pTimingInfo->dwHTotal;
              Mode->HSkew = 0;
                       
              Mode->VDisplay = pTimingInfo->dwVActive;
              Mode->VSyncStart = pTimingInfo->dwVSyncStart;
              Mode->VSyncEnd = pTimingInfo->dwVSyncEnd;
              Mode->VTotal = pTimingInfo->dwVTotal;
              Mode->VScan = 0;          

              Mode->Flags = 0;

              if (pTimingInfo->bInterlaced)
                 Mode->Flags |= V_INTERLACE;
                 Mode->Flags |= (pTimingInfo->bHSyncPolarity == TRUE)? V_PHSYNC : V_NHSYNC;
                 Mode->Flags |= (pTimingInfo->bVSyncPolarity == TRUE)? V_PVSYNC : V_NVSYNC;

              xf86SetModeDefaultName(Mode);

              //debug
             Mode->prev = NULL;
             Mode->next = NULL;
             Mode->status = 0;
             Mode->type = M_T_DRIVER;

             Mode->ClockIndex = 0;
             Mode->SynthClock = 0;
             Mode->CrtcHDisplay = 0;
             Mode->CrtcHBlankStart = 0;
             Mode->CrtcHSyncStart = 0;
             Mode->CrtcHSyncEnd = 0;
             Mode->CrtcHBlankEnd = 0;
             Mode->CrtcHTotal = 0;
             Mode->CrtcHSkew = 0;
             Mode->CrtcVDisplay = 0;
             Mode->CrtcVBlankStart = 0;
             Mode->CrtcVSyncStart = 0;
             Mode->CrtcVSyncEnd = 0;
             Mode->CrtcVBlankEnd = 0;
             Mode->CrtcVTotal = 0;
             Mode->CrtcHAdjusted = FALSE;
             Mode->CrtcVAdjusted = FALSE;
             Mode->PrivSize = 0;
             Mode->Private = NULL;
             Mode->PrivFlags = 0;
             Mode->HSync = 0.0;
             Mode->VRefresh = 0.0;
           }
        }
 
        return Mode;
}

int  Edid_AddCECompatibleModes(unsigned char * EDIDExtension,DisplayModePtr mode)
{
        DCDisplayInfo out_pTimingInfo;
        unsigned char * ExtData = EDIDExtension;
        Bool supportedMode = FALSE;
        EDID_DTD DTD[6];   //We can have max of 6 DTDs
        PEDID_DTD pDTD = NULL;
        int DTDBegin;
        int DTDCount;
        int AddCount = 0;
        int i = 0;
        int j = 0;    //for index number of add_mode be used
        Bool HasConvertDTDTiming;
        DisplayModePtr AddMode;

        if (ExtData) {
           if ((*ExtData++ == 0x2) && (*ExtData++ <= 0x3)) {
               DTDBegin = *ExtData++;

               DTDCount = (EDID_BLOCK_SIZE - DTDBegin) / sizeof(EDID_DTD);
               if ((DTDCount * sizeof(EDID_DTD) + DTDBegin) > DTDMAX)
                  return 0;

               if (DTDCount > 6)
                   return 0;

               memcpy(DTD, EDIDExtension + DTDBegin, DTDCount * sizeof(EDID_DTD));

               pDTD = &DTD[0];
               while (i < DTDCount && pDTD != NULL) {
                   if (pDTD->wPixelClock > 0x101) {
                      supportedMode = Edid_IsSupportedCeMode(pDTD);

                      if (supportedMode) {
                         HasConvertDTDTiming = Edid_ConvertDTDTiming(pDTD, &out_pTimingInfo);

                         if (HasConvertDTDTiming) {
                            AddMode = GetModeFromDetailedTiming(&out_pTimingInfo);
                            xf86ModesAdd(mode, AddMode);
                            j++;
                         }
                      }
                   }
                   i++;
                   pDTD++;
                   AddCount++;
               }
           }
        }

        return AddCount;
}
