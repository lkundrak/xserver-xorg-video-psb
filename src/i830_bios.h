/*
 * Copyright © 2006 Intel Corporation
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
 *
 */
#ifndef _I830_BIOS_H
#define _I830_BIOS_H

struct vbt_header
{
    char signature[20];			/**< Always starts with 'VBT$' */
    CARD16 version;			/**< decimal */
    CARD16 header_size;			/**< in bytes */
    CARD16 vbt_size;			/**< in bytes */
    CARD8 vbt_checksum;
    CARD8 reserved0;
    CARD32 bdb_offset;			/**< from beginning of VBT */
    CARD32 aim1_offset;			/**< from beginning of VBT */
    CARD32 aim2_offset;			/**< from beginning of VBT */
    CARD32 aim3_offset;			/**< from beginning of VBT */
    CARD32 aim4_offset;			/**< from beginning of VBT */
} __attribute__ ((packed));

struct bdb_header
{
    char signature[16];			/**< Always 'BIOS_DATA_BLOCK' */
    CARD16 version;			/**< decimal */
    CARD16 header_size;			/**< in bytes */
    CARD16 bdb_size;			/**< in bytes */
} __attribute__ ((packed));

#define LVDS_CAP_EDID			(1 << 6)
#define LVDS_CAP_DITHER			(1 << 5)
#define LVDS_CAP_PFIT_AUTO_RATIO	(1 << 4)
#define LVDS_CAP_PFIT_GRAPHICS_MODE	(1 << 3)
#define LVDS_CAP_PFIT_TEXT_MODE		(1 << 2)
#define LVDS_CAP_PFIT_GRAPHICS		(1 << 1)
#define LVDS_CAP_PFIT_TEXT		(1 << 0)
struct lvds_bdb_1
{
    CARD8 id;				/**< 40 */
    CARD16 size;
    CARD8 panel_type;
    CARD8 reserved0;
    CARD16 caps;
} __attribute__ ((packed));

struct lvds_bdb_2_fp_params
{
    CARD16 x_res;
    CARD16 y_res;
    CARD32 lvds_reg;
    CARD32 lvds_reg_val;
    CARD32 pp_on_reg;
    CARD32 pp_on_reg_val;
    CARD32 pp_off_reg;
    CARD32 pp_off_reg_val;
    CARD32 pp_cycle_reg;
    CARD32 pp_cycle_reg_val;
    CARD32 pfit_reg;
    CARD32 pfit_reg_val;
    CARD16 terminator;
} __attribute__ ((packed));

struct lvds_bdb_2_fp_edid_dtd
{
    CARD16 dclk;		/**< In 10khz */
    CARD8 hactive;
    CARD8 hblank;
    CARD8 high_h;		/**< 7:4 = hactive 11:8, 3:0 = hblank 11:8 */
    CARD8 vactive;
    CARD8 vblank;
    CARD8 high_v;		/**< 7:4 = vactive 11:8, 3:0 = vblank 11:8 */
    CARD8 hsync_off;
    CARD8 hsync_pulse_width;
    CARD8 vsync_off;
    CARD8 high_hsync_off;	/**< 7:6 = hsync off 9:8 */
    CARD8 h_image;
    CARD8 v_image;
    CARD8 max_hv;
    CARD8 h_border;
    CARD8 v_border;
    CARD8 flags;
#define FP_EDID_FLAG_VSYNC_POSITIVE	(1 << 2)
#define FP_EDID_FLAG_HSYNC_POSITIVE	(1 << 1)
} __attribute__ ((packed));

struct lvds_bdb_2_entry
{
    CARD16 fp_params_offset;		/**< From beginning of BDB */
    CARD8 fp_params_size;
    CARD16 fp_edid_dtd_offset;
    CARD8 fp_edid_dtd_size;
    CARD16 fp_edid_pid_offset;
    CARD8 fp_edid_pid_size;
} __attribute__ ((packed));

struct lvds_bdb_2
{
    CARD8 id;			/**< 41 */
    CARD16 size;
    CARD8 table_size;
    struct lvds_bdb_2_entry panels[16];
} __attribute__ ((packed));

struct lvds_bdb_blc
{
    CARD8 id;			/**< 43 */
    CARD16 size;
    CARD8 table_size;
} __attribute__ ((packed));

struct lvds_blc
{
    CARD8 type:2;
    CARD8 pol:1;
    CARD8 gpio:3;
    CARD8 gmbus:2;
    CARD16 freq;
    CARD8 minbrightness;
    CARD8 i2caddr;
    CARD8 brightnesscmd;
    /* more... */
} __attribute__ ((packed));

extern DisplayModePtr
i830_bios_get_panel_mode(ScrnInfoPtr pScrn, Bool * panelWantsDither);

#if 0
//debug
#define MODEPREFIX(name) NULL, NULL, name, 0, M_T_DRIVER
#define MODESUFFIX 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,FALSE,FALSE,0,NULL,0,0.0,0.0

static DisplayModeRec psbHDMIModes[] = {
    { MODEPREFIX("640x480"),    25312,  640,  656,  752,  800, 0,  480,  489,  491,  525, 0, V_NHSYNC | V_NVSYNC, MODESUFFIX },
    { MODEPREFIX("720x480"),    26591,  720,  736,  808,  896, 0,  480,  480,  483,  497, 0, V_NHSYNC | V_PVSYNC, MODESUFFIX },
    { MODEPREFIX("720x480"),    26591,  720,  736,  808,  896, 0,  480,  480,  483,  497, 0, V_NHSYNC | V_PVSYNC, MODESUFFIX },
    { MODEPREFIX("1280x720"),   74250, 1280, 1320, 1376, 1650, 0,  720,  722,  728,  750, 0, V_PHSYNC | V_NVSYNC, MODESUFFIX },
    { MODEPREFIX("1920x1080"),  74250, 1920, 2008, 2052, 2200, 0, 1080, 1084, 1094, 1125, 0, V_PHSYNC | V_PVSYNC, MODESUFFIX },
    { MODEPREFIX("720x480"),    26591,  720,  736,  808,  896, 0,  480,  480,  483,  497, 0, V_NHSYNC | V_PVSYNC, MODESUFFIX },
    { MODEPREFIX("720x480"),    26591,  720,  736,  808,  896, 0,  480,  480,  483,  497, 0, V_NHSYNC | V_PVSYNC, MODESUFFIX },
    { MODEPREFIX(NULL), 0,0,0,0,0,0,0,0,0,0,0,0,MODESUFFIX},
    { MODEPREFIX(NULL), 0,0,0,0,0,0,0,0,0,0,0,0,MODESUFFIX},
    { MODEPREFIX("2880x480"),   54000, 2880, 2956, 3204, 3432, 0, 480, 488, 494, 525, 0, V_NHSYNC | V_NVSYNC | V_INTERLACE, MODESUFFIX},
    { MODEPREFIX("2880x480"),   54000, 2880, 2956, 3204, 3432, 0, 480, 488, 494, 525, 0, V_NHSYNC | V_NVSYNC | V_INTERLACE, MODESUFFIX},
    { MODEPREFIX("2880x240"),   54000, 2880, 2956, 3204, 3432, 0, 240, 244, 247, 262, 0, V_NHSYNC | V_NVSYNC, MODESUFFIX},
    { MODEPREFIX("2880x240"),   54000, 2880, 2956, 3204, 3432, 0, 240, 244, 247, 262, 0, V_NHSYNC | V_NVSYNC, MODESUFFIX},
    { MODEPREFIX("1440x480"),   54000, 1440, 1472, 1596, 1716, 0,  480,  489,  495,  525, 0, V_NHSYNC | V_NVSYNC, MODESUFFIX },
    { MODEPREFIX("1440x480"),   54000, 1440, 1472, 1596, 1716, 0,  480,  489,  495,  525, 0, V_NHSYNC | V_NVSYNC, MODESUFFIX },
    { MODEPREFIX("1920x1080"),  148500, 1920, 2008, 2052, 2200, 0, 1080, 1084, 1089, 1125, 0, V_PHSYNC | V_PVSYNC, MODESUFFIX },
    { MODEPREFIX("720x576"),    27000,  720,  732,  796,  864, 0,  576,  581,  586,  625, 0, V_NHSYNC | V_NVSYNC, MODESUFFIX },
    { MODEPREFIX("720x576"),    27000,  720,  732,  796,  864, 0,  576,  581,  586,  625, 0, V_NHSYNC | V_NVSYNC, MODESUFFIX },
    { MODEPREFIX("1280x720"),   74250, 1280, 1720, 1760, 1980, 0,  720,  725,  730,  750, 0, V_PHSYNC | V_PVSYNC, MODESUFFIX },
    { MODEPREFIX("1920x1080"),  74250, 1920, 2448, 2492, 2640, 0, 1080, 1084, 1094, 1125, 0, V_PHSYNC | V_PVSYNC, MODESUFFIX },
    { MODEPREFIX("720x576"),    27000,  720,  732,  796,  864, 0,  576,  581,  586,  625, 0, V_NHSYNC | V_NVSYNC | V_INTERLACE, MODESUFFIX },
    { MODEPREFIX("720x576"),    27000,  720,  732,  796,  864, 0,  576,  581,  586,  625, 0, V_NHSYNC | V_NVSYNC | V_INTERLACE, MODESUFFIX },
    { MODEPREFIX(NULL), 0,0,0,0,0,0,0,0,0,0,0,0,MODESUFFIX},
    { MODEPREFIX(NULL), 0,0,0,0,0,0,0,0,0,0,0,0,MODESUFFIX},
    { MODEPREFIX("2880x576"),    54000, 2880, 2928, 3180, 3456, 0,  576,  580,  586,  625, 0, V_NHSYNC | V_NVSYNC | V_INTERLACE, MODESUFFIX },
    { MODEPREFIX("2880x576"),    54000, 2880, 2928, 3180, 3456, 0,  576,  580,  586,  625, 0, V_NHSYNC | V_NVSYNC | V_INTERLACE, MODESUFFIX },
    { MODEPREFIX("2880x288"),    54000, 2880, 2928, 3180, 3456, 0,  288,  290,  293,  312, 0, V_NHSYNC | V_NVSYNC, MODESUFFIX },
    { MODEPREFIX("2880x288"),    54000, 2880, 2928, 3180, 3456, 0,  288,  290,  293,  312, 0, V_NHSYNC | V_NVSYNC, MODESUFFIX },
    { MODEPREFIX("1440x576"),    54000, 1440, 1464, 1592, 1728, 0,  576,  581,  586,  625, 0, V_NHSYNC | V_NVSYNC, MODESUFFIX },
    { MODEPREFIX("1440x576"),    54000, 1440, 1464, 1592, 1728, 0,  576,  581,  586,  625, 0, V_NHSYNC | V_NVSYNC, MODESUFFIX },
    { MODEPREFIX("1920x1080"),   148500, 1920, 2448, 2492, 2640, 0, 1080, 1084, 1089, 1125, 0, V_PHSYNC | V_PVSYNC, MODESUFFIX },
    { MODEPREFIX("1920x1080"),   74250, 1920, 2558, 2602, 2750, 0, 1080, 1084, 1089, 1125, 0, V_PHSYNC | V_PVSYNC, MODESUFFIX },
    { MODEPREFIX("1920x1080"),   74250, 1920, 2448, 2492, 2640, 0, 1080, 1084, 1089, 1125, 0, V_PHSYNC | V_PVSYNC, MODESUFFIX },
    { MODEPREFIX("1920x1080"),   74250, 1920, 2008, 2052, 2200, 0, 1080, 1084, 1089, 1125, 0, V_PHSYNC | V_PVSYNC, MODESUFFIX },
    { MODEPREFIX(NULL), 0,0,0,0,0,0,0,0,0,0,0,0,MODESUFFIX },
};
#endif

#define MI_XRES_SCALE           1               /* Right shift performed to X resolutio*/
#define MI_YRES_SCALE           1               /* Right shift performed to Y resolutio*/
#define MI_XRES_SHIFT           21
#define MI_YRES_SHIFT           10
#define MI_INTERLACE_SHIFT      8
#define MI_RRATE_SHIFT          0

#define EDID_BLOCK_SIZE         128
//#define DTDMAX                  123  //edid block - starting 4 byte + 1 byte checksum
#define DTDMAX                  127  //edid block - starting 4 byte + 1 byte checksum

/* Macro for mode UI*/
#define m_ModeUID(xRes, yRes, rRate, bInterlace) \
        (       ((xRes>>MI_XRES_SCALE)<<MI_XRES_SHIFT) | \
                ((yRes>>MI_YRES_SCALE)<<MI_YRES_SHIFT) | \
                ((rRate)<<MI_RRATE_SHIFT) | \
                ((bInterlace)<<MI_INTERLACE_SHIFT) )

/*//////////////////////////////////////////////////*/
/**/
/*  CE-861b Supported Short Video Descriptors Tabl*/
/**/
/*//////////////////////////////////////////////////*/
typedef struct _PBDCCE_SHORT_VIDEO_MODE
{
        unsigned char ceIndex;
        /* ulAspectRatio is not being used. Investigate the*/
        /* need. Ref : Manj*/
        /* bool  ulAspectRatio; // 1 - 16:9  ..... 0 - 4:3*/

        unsigned long modeUID;
} PBDCCE_SHORT_VIDEO_MODE,*PPBDCCE_SHORT_VIDEO_MODE;

/*///////////////////////////////////////////////////////////////////////*/
/*  CE-861b Supported Short Video Descriptors Table*/
/*      <Index of the entry into the Table><Timing Info >*/
/*      Ref : CE-Extension Spec*/
/*///////////////////////////////////////////////////////////////////////*/
PBDCCE_SHORT_VIDEO_MODE  g_SupportedCeShortVideoModes[];
//unsigned long g_ulNumSupportedCEModes = sizeof(g_SupportedCeShortVideoModes)/sizeof(g_SupportedCeShortVideoModes[0]);


typedef struct _EDID_DTD {
#pragma pack(1)
      unsigned short wPixelClock; //pixel clock /10000

      unsigned char ucHA_low;    //lower 8 bits of H.active pixels
      unsigned char ucHBL_low;   //lower 8 bits of H.blanking
      union {
           unsigned char ucHAHBL_high;
           struct {
                unsigned char ucHBL_high : 4; //upper 4 bits of H.blanking
                unsigned char ucHA_high : 4;  //upper 4 bits of H.active pixels
           };
      };

      unsigned char ucVA_low;    //lower 8 bits of V.active lines
      unsigned char ucVBL_low;   //lower 8 bits of V.blanking
      union {
           unsigned char ucVAVBL_high;
           struct {
                 unsigned char ucVBL_high : 4;
                 unsigned char ucVA_high : 4;
           };
      };

      unsigned char ucHSO_low;          //lower 8 bits of H.sync offset
      unsigned char ucHSPW_low;         //lower 8 bits of H.sync pulse width
      union {
           unsigned char ucVSOVSPW_low;
           struct {
                 unsigned char ucVSPW_low : 4;
                 unsigned char ucVSO_low : 4;
           };
      };
      union {
           unsigned char ucHSVS_high;
           struct {
                 unsigned char ucVSPW_high : 2;
                 unsigned char ucVSO_high : 2;
                 unsigned char ucHSPW_high : 2;
                 unsigned char ucHSO_high : 2;
           };
      };

      unsigned char ucHIS_low;
      unsigned char ucVIS_low;
      union {
           unsigned char ucHISVIS_high;
           struct {
                 unsigned char ucVIS_high : 4;
                 unsigned char ucHIS_high : 4;
           };
      };

      unsigned char ucHBorder;
      unsigned char ucVBorder;

      union {
           unsigned char ucFlags;
           struct {
                 unsigned char ucStereo1 : 1;
                 unsigned char ucHSync_Pol : 1;
                 unsigned char ucVSync_Pol : 1;
                 unsigned char ucSync_Conf : 2;

                 unsigned char ucStereo2 : 2;
                 unsigned char ucInterlaced : 1;
           };
      };
#pragma pack()
}EDID_DTD, *PEDID_DTD;

typedef struct _DCDisplayInfo
{
                unsigned long   dwDotClock;      /* Pixel clock in Hz*/

                unsigned long   dwHTotal;        /* Horizontal total in pixels*/
                unsigned long   dwHActive;       /* Active in pixels*/
                unsigned long   dwHBlankStart;   /* From start of active in pixels*/
                unsigned long   dwHBlankEnd;     /* From start of active in pixels*/
                unsigned long   dwHSyncStart;    /* From start of active in pixels*/
                unsigned long   dwHSyncEnd;      /* From start of active in pixels*/
                unsigned long   dwHRefresh;      /* Refresh Rate*/
                unsigned long   dwVTotal;        /* Vertical total in lines*/
                unsigned long   dwVActive;       /* Active lines*/
                unsigned long   dwVBlankStart;   /* From start of active lines*/
                unsigned long   dwVBlankEnd;     /* From start of active lines*/
                unsigned long   dwVSyncStart;    /* From start of active lines*/
                unsigned long   dwVSyncEnd;      /* From start of active lines*/
                unsigned long   dwVRefresh;      /* Refresh Rate*/

                Bool bInterlaced;
                Bool bHSyncPolarity;
                Bool bVSyncPolarity;
} DCDisplayInfo;

Bool Edid_IsSupportedCeMode(PEDID_DTD pDTD);
Bool Edid_ConvertDTDTiming(PEDID_DTD pDTD, DCDisplayInfo * out_pTimingInfo);
int  Edid_AddCECompatibleModes(unsigned char *EDIDExtension, DisplayModePtr mode);

#endif
