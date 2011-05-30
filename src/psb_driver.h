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
#ifndef _PSB_H_
#define _PSB_H_

#include "xf86.h"
#include "xf86_OSproc.h"
#include "compiler.h"
#include "xf86PciInfo.h"
#include "vgaHW.h"
#include "xf86Pci.h"
#include "vbe.h"
#include "vbeModes.h"
#include "xf86DDC.h"
#include "shadow.h"
#include "xf86int10.h"
#include "dgaproc.h"
#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 6
#include "xf86Resources.h"
#include "xf86RAC.h"
#endif
#include "fb.h"
#include "xaa.h"
#include "xf86xv.h"
#include "xf86Crtc.h"
#include "xf86str.h"

#include "libmm/mm_defines.h"
#include "libmm/mm_interface.h"

#include "psb_buffers.h"
#include "psb_accel.h"

#include "i830_bios.h"

#define DEBUG_ERRORF if (0) ErrorF

#ifdef XF86DRI
#include "psb_dri.h"
#define _XF86DRI_SERVER_
#include "GL/glxint.h"
#include "dri.h"
#include "psb_ioctl.h"
#if 0
#define PSB_LEGACY_DRI \
    ((DRIINFO_MAJOR_VERSION != 5) || (DRIINFO_MINOR_VERSION < 2))
#else
#define PSB_LEGACY_DRI 1
#endif
#endif

#define PSB_NUM_PIPES 2
#define PSB_MAX_CRTCS 2
#define PSB_MAX_SCREENS 2
#define PSB_SAVESWF_NUM 17

#define PSB_VERSION      4000
#define PSB_NAME         "PSB"
#define PSB_DRIVER_NAME  "psb"
#define PSB_MAJOR_VERSION 0
#define PSB_MINOR_VERSION 32
#define PSB_PATCHLEVEL    0

#define PCI_CHIP_PSB1  0x8108
#define PCI_CHIP_PSB2  0x8109

/*
 * We need this excessive hardware timeout since the
 * 3D engine may process scenes with arbitrarily complex
 * pixel shaders, and the DRM is not stopping it unless
 * it locks.
 */

#define PSB_TIMEOUT_USEC 29900000

#if (XORG_VERSION_MINOR >= 5 || (XORG_VERSION_MINOR == 4 && XORG_VERSION_PATCH == 99))
typedef struct pci_device *pciVideoPtr;

#ifndef PCI_DOM_MASK
#define PCI_DOM_MASK 0x0ffu
#endif

#define PCI_DOMBUS_MASK (((PCI_DOM_MASK) << 8) | 0x0ffu)
#define PCI_DOM_FROM_TAG(tag) (((tag)>>24) & (PCI_DOM_MASK))
#define PCI_BUS_FROM_TAG(tag) (((tag) >> 16) & (PCI_DOMBUS_MASK))
#define PCI_DEV_FROM_TAG(tag) (((tag) & 0x0000f800u) >> 11)
#define PCI_FUNC_FROM_TAG(tag) (((tag) & 0x00000700u) >> 8)
#endif

typedef enum _PsbLidState
{
    PSB_LIDSTATE_OPEN = 0,
    PSB_LIDSTATE_CLOSED
} PsbLidState;

#define LIDSTAT_LINE_SIZE 256
#define PROCLIDSTATE "/proc/acpi/button/lid/LID/state"

typedef enum _PsbPanelFittingMode
{
    PSB_PANELFITTING_FIT = 0,
    PSB_PANELFITTING_CENTERED,
    PSB_PANELFITTING_ASPECT
} PsbPanelFittingMode;

#define PSB_DEBUG(_scrn, _level, ...)			\
  xf86DrvMsgVerb(_scrn, X_INFO, _level, "Debug: " __VA_ARGS__ )
#define ALIGN_TO(_arg, _align) \
  (((_arg) + ((_align) - 1)) & ~((_align) - 1))

/*
 * Multihead / Multiscreen note:
 * To clear up any confusion the following conventions are followed:
 *
 * All resources common for a device are associated with a PsbDevice structure. Other structures may
 * have pointers to the PsbDevice structure they belong to.
 *
 * Each screen is associated with a logical framebuffer area somewhere in memory.
 * This logical framebuffer area may or may not have shadow copies (rotated or un-rotated).
 * Each screen is also assigned to a particular device.
 *
 * Each available output is a device resource and should be associated with a screen. This should either
 * be done from the configuration file, using default driver settings or through a utility function.
 *
 * A crtc is used to connect one or many outputs to their screen.
 *
 */

typedef struct _PsbLvdsBlcDataRec
{
    CARD8 type;
    CARD8 pol;
    CARD16 freq;
    CARD8 minbrightness;
    CARD8 i2caddr;
    CARD8 brightnesscmd;
} PsbLvdsBlcDataRec, *PsbLvdsBlcDataPtr;

typedef struct _PsbDevice
{

    unsigned deviceIndex;
    EntityInfoPtr pEnt;
    GDevPtr device;
    pciVideoPtr pciInfo;
    PCITAG pciTag;
    unsigned long regPhys;
    unsigned long regSize;
    unsigned long fbPhys;
    unsigned long fbSize;
    unsigned long stolenBase;
    unsigned long stolenSize;

    CARD8 *regMap;
    CARD8 *fbMap;

    int lastInstance;
    unsigned refCount;

    Bool outputInit;

#ifdef XF86DRI
    Bool hasDRM;
    int drmFD;
    drm_context_t drmContext;
    char *busIdString;
    int drmVerMajor;
    int drmVerMinor;
    int drmVerPL;
    int lockRefCount;
    void *pLSAREA;
    int irq;
#endif

    ScrnInfoPtr pScrns[PSB_MAX_SCREENS];
    unsigned numScreens;

    CARD32 saveSWF0;
    CARD32 saveSWF4;

    CARD32 saveSWF[PSB_SAVESWF_NUM];
    CARD32 saveVCLK_DIVISOR_VGA0;
    CARD32 saveVCLK_DIVISOR_VGA1;
    CARD32 saveVCLK_POST_DIV;
    CARD32 saveVGACNTRL;

    Bool swfSaved;
    Bool hwSaved;
    Bool deviceUp;

    MMManager *man;
    Bool mmLocked;
    int vtRefCount;

    char sdvoBName[60];

    CARD32 *OpRegion;
    unsigned int OpRegionSize;
    OsTimerPtr devicesTimer;
    Bool TVEnabled;

/*
 * Hardware data
 */
    unsigned int CoreClock;
    unsigned int MemClock;
    unsigned int Latency[2];
    unsigned int WorstLatency[2];

	unsigned int sku_value; 
	Bool sku_bSDVOEnable;
	Bool sku_bMaxResEnableInt;
/*
 *backlight data block
 */
    PsbLvdsBlcDataRec blc_data;
    CARD32 PWMControlRegFreq;

} PsbDevice, *PsbDevicePtr;

/*
 * Temporary i830 compatibility stuff.
 */

typedef struct _I830Rec
{
    EntityInfoPtr pEnt;
    PCITAG PciTag;
    pciVideoPtr PciInfo;
    PsbDevicePtr pDevice;
} I830Rec, *I830Ptr;

#ifdef XF86DRI
typedef struct _PsbGLXConfig
{
    int numConfigs;
    __GLXvisualConfig *pConfigs;
    PsbConfigPrivPtr pPsbConfigs;
    PsbConfigPrivPtr *pPsbConfigPtrs;
} PsbGLXConfigRec, *PsbGLXConfigPtr;
#endif

typedef struct _PsbRec
{
/*
 * General stuff.
 */
    ScrnInfoPtr pScrn;
    PsbDevicePtr pDevice;
    CloseScreenProcPtr closeScreen;
    void (*PointerMoved) (int, int, int);
    Bool multiHead;
    Bool secondary;
    Bool ignoreACPI;
    unsigned long serverGeneration;

    struct _MMListHead buffers;

    PsbScanoutPtr front;

/*
 * Crtcs we can potentially use.
 */
    xf86CrtcPtr crtcs[PSB_MAX_CRTCS];
    unsigned numCrtcs;

/*
 *  Outputs we can potentially use.
 */

    MMListHead outputs;

/*
 * Virtual screen.
 */

    unsigned cpp;
    unsigned stride;
    unsigned long fbSize;
    CARD8 *fbMap;

    OptionInfoPtr options;

/*
 * ShadowFB
 */

    CARD8 *shadowMem;
    Bool shadowFB;
    CreateScreenResourcesProcPtr createScreenResources;
    ShadowUpdateProc update;

/*
 * Acceleration
 */
    Bool noAccel;

/*
 * No Panel support
 */
    Bool noPanel;

/*
 * Lid state timer support
 */
    Bool lidTimer;

/*
 * Downscaling support
 */
    Bool downScale;

/*
 * De-tearing support, to eliminate video playback tearing issue
 */
    Bool vsync;

/*
 * Default panel fitting mode
 */
    Bool noFitting;

/*
 * Panelfitting mode
 */
    PsbPanelFittingMode panelFittingMode;

    PsbExaPtr pPsbExa;
    unsigned long exaSize;
    unsigned long exaScratchSize;
    PsbTwodContextRec td;
    Bool exaSuperIoctl;
/*
 * DGA
 */
    DGAModePtr DGAModes;
    int numDGAModes;
    Bool DGAactive;
    int DGAViewportStatus;

/*
 * For Super ioctl acceleration
 */

    Psb2DBufferRec superC;
    Bool has2DBuffer;

/*
 * i830 driver compatibility.
 */

    I830Rec i830Ptr;

/*
 *  Misc
 */

    Bool sWCursor;

#define WA_NOFB_GARBAGE_DISPLAY
#ifdef WA_NOFB_GARBAGE_DISPLAY
    Bool has_fbdev;
#endif

/*
 * Xv
 */
    int colorKey;
    XF86VideoAdaptorPtr adaptor;

/*
 * DRI
 */

#ifdef XF86DRI
    Bool dri;
    Bool driEnabled;
    DRIInfoPtr pDRIInfo;
    int drmFD;
    PsbGLXConfigRec glx;
    struct _MMListHead sAreaList;
    Bool xpsb;
    Bool hasXpsb;
    Bool xtra;
#endif

} PsbRec, *PsbPtr;

#define PSB_CRTC0 (1 << 0)
#define PSB_CRTC1 (1 << 1)

typedef struct _PsbCrtcPrivateRec
{
    unsigned pipe;
    unsigned refCount;
    PsbScanoutPtr rotate;

    /*
     * HW Cursor
     */

    Bool cursor_is_argb;
    unsigned long cursor_addr;
    unsigned long cursor_argb_addr;

    unsigned long cursor_offset;
    unsigned long cursor_argb_offset;

    struct _MMBuffer *cursor;

    CARD8 lutR[256];
    CARD8 lutG[256];
    CARD8 lutB[256];

    CARD32 saveDSPCNTR;
    CARD32 savePIPECONF;
    CARD32 savePIPESRC;
    CARD32 saveDPLL;
    CARD32 saveFP0;
    CARD32 saveFP1;
    CARD32 saveHTOTAL;
    CARD32 saveHBLANK;
    CARD32 saveHSYNC;
    CARD32 saveVTOTAL;
    CARD32 saveVBLANK;
    CARD32 saveVSYNC;
    CARD32 saveDSPSTRIDE;
    CARD32 saveDSPSIZE;
    CARD32 saveDSPPOS;
    CARD32 saveDSPBASE;
    CARD32 savePFITCTRL;
    CARD32 savePalette[256];

    DisplayModeRec saved_mode;
    DisplayModeRec saved_adjusted_mode;
    int x;
    int y;
    int downscale; /* downscale enabled or not */
    float scale_x; /* downscale ratio of x direction */
    float scale_y; /* downscale ratio of y direction */   
} PsbCrtcPrivateRec, *PsbCrtcPrivatePtr;

#define psbCrtcPrivate(_crtc)			\
    ((PsbCrtcPrivatePtr) (_crtc)->driver_private)

#define PSB_OUTPUT_UNUSED 0
#define PSB_OUTPUT_SDVO 1
#define PSB_OUTPUT_LVDS 2

typedef struct _PsbOutputPrivateRec
{
    int type;
    int refCount;
    I2CBusPtr pDDCBus;
    I2CBusPtr pI2CBus;
    PsbDevicePtr pDevice;
    Bool load_detect_temp;
    unsigned crtcMask;
    ScrnInfoPtr pScrn;		       /* Screen this output is currently assigned to. */
    const char *name;
    int isHDMI_Device;		       /* Add to record whether it is a HDMI device, Jamesx */
    int isHDMI_Monitor;		       /* Add to record whether it is a HDMI monitor, Jamesx */
    MMListHead driverOutputs;
} PsbOutputPrivateRec, *PsbOutputPrivatePtr;

typedef struct OpRegion_Header
{
    char sign[16];
    CARD32 size;
    CARD32 over;
    char sver[32];
    char vver[16];
    char gver[16];
    CARD32 mbox;
    char rhd1[164];
} OpRegionRec, *OpRegionPtr;


/******************** HDMI InfoFrame Start *******************************/

#define DIP_TYPE_AVI    0x82
#define DIP_VERSION_AVI 0x2
#define DIP_LEN_AVI     13

#define DIP_TYPE_SPD    0x83
#define DIP_VERSION_SPD 0x1
#define DIP_LEN_SPD     25

#define DIP_TYPE_AUDIO    0x84
#define DIP_VERSION_AUDIO 0x1
#define DIP_LEN_AUDIO     10

// LinearPCM Consolidated Audio Data(CAD) structure
struct LPCM_CAD {
    uint8_t ucMaxCh_CPOn  :3; // Max channels-1 supported with CP turned ON
    uint8_t ucMaxCh_CPOf  :3; // Max channels-1 supported with CP turned OFF
    uint8_t uc20Bit       :1; // 20-bit sample support
    uint8_t uc24Bit       :1; // 24-bit sample support
};

// EDID Like Data aka ELD structure
struct dip_eld_infoframe {
    // Byte[0] = ELD Version Number
    uint8_t ucCEA_ver:3;      // CEA Version Number
                                    // 0 - signifies CEA Version 3
                                    // 1 - signifies CEA Version 4
    uint8_t ucELD_ver:5;      // ELD Version Number
                                    //  00000b - reserved
                                    //  00001b - first rev
                                    //  00010b:11111b - reserved for future

    // Byte[1] = Capability Flags
    //  +------------------------+
    //  |7|6|5|4|3| 2  |  1 | 0  |
    //  +------------------------+
    //  |R|R|R|R|R|44MS|RPTR|HDCP|
    //  +------------------------+

    uint8_t ucHDCP    :1;     // Driver, SDVO Device, and Receiver supports HDCP
    uint8_t ucRPTR    :1;     // Receiver is repeater
    uint8_t uc44MS    :1;     // 44.1kHz multiples are supported by the sink
                              //	0 - 88.2 & 176.kHz are not supported by sink
                              //  1 - If 96kHz is supproted then 88.2kHz is also supported
                              //	1 - if 192kHz is supported then 176.4 is also supported
                              // 44.1kHz is always supported by a sink
    uint8_t ucRsvd37  :5;     // Reserved bits

    // Byte[2-3] = Length Parameter
    uint16_t  ucMNL       :3;    // Monitor Name Length 7-based
                                 // 0 = 0 and no Monitor name
                                 // 1 = MNL is 7
                                 // 2 = MNL is 8
                                 // 7 = MNL is 13
    uint16_t  ucVSDBL     :3;    // VSDB length in bytes
                                 //  0 - length is ZERO and no VSDB included
                                 //  1 - VSDB byte 6 is present
                                 //  2 - VSDB byte 6-7 are present
                                 //  ...
                                 //  7 - VSDB byte 6-12 are present
    uint16_t  ucSADC      :4;    // count of SAD blocks
    uint16_t  ucRsvd1015  :6;    // reserved bits

    uint16_t  usManufacturerName;       // 2-byte ID Manufacturer Name from base EDID
    uint16_t  usProductCode;            // 2-byte Product Code from base EDID
    struct LPCM_CAD  uc48CAD;                  // Consolidated Audio Descriptor for 48kHz
    struct LPCM_CAD  uc96CAD;                  // Consolidated Audio Descriptor for 96kHz
    struct LPCM_CAD  uc192CAD;                 // Consolidated Audio Descriptor for 192kHz
    uint8_t   ucSAB[3];                 // 3-byte EIA 861B Speaker Allocation Block

    // Rest of the ELD packet
    uint8_t   ucData[242];              // Monitor Descriptor - ASCII string of monitor name
                                        // VSDB as extracted from EDID strcutre
                                        // Fill in from 6th byte of VBDB onwards, ignore 1-5bytes of VBDB
                                        // SAD 0 - 3-byte CEA-861B Short Audio Descriptor for non-LPCM content
                                        // SAD 1 - 3-byte CEA-861B Short Audio Descriptor for non-LPCM content
                                        // ...
                                        // ...
                                        // SAD N - 3-byte CEA-861B Short Audio Descriptor for non-LPCM content
} __attribute__((packed));

struct dip_avi_infoframe {
    uint8_t type;
    uint8_t version;
    uint8_t len;
    uint8_t checksum;
    /* Packet Byte #1 */
    uint8_t S:2;
    uint8_t B:2;
    uint8_t A:1;
    uint8_t Y:2;
    uint8_t rsvd1:1;
    /* Packet Byte #2 */
    uint8_t R:4;
    uint8_t M:2;
    uint8_t C:2;
    /* Packet Byte #3 */
    uint8_t SC:2;
    uint8_t Q:2;
    uint8_t EC:3;
    uint8_t ITC:1;
    /* Packet Byte #4 */
    uint8_t VIC:7;
    uint8_t rsvd2:1;
    /* Packet Byte #5 */
    uint8_t PR:4;
    uint8_t rsvd3:4;
    /* Packet Byte #6~13 */
    uint16_t top_bar_end;
    uint16_t bottom_bar_start;
    uint16_t left_bar_end;
    uint16_t right_bar_start;
    /* Reserved */
    uint8_t payload[14];
} __attribute__((packed));

struct dip_spd_infoframe {
    uint8_t type;
    uint8_t version;
    uint8_t len;
    uint8_t checksum;
    uint8_t name[8];	// Vendor Name, 8 characters
    uint8_t desc[16];	// Product Description, 16 characters
    uint8_t SDI;	// Source Device Information
} __attribute__((packed));

struct dip_audio_infoframe {
    uint8_t type;
    uint8_t version;
    uint8_t len;
    uint8_t checksum;
    /* Packet Byte #1 */
    uint8_t CC:3;
    uint8_t rsvd1:1;
    uint8_t CT:4;
    /* Packet Byte #2 */
    uint8_t SS:2;
    uint8_t SF:3;
    uint8_t rsvd2:3;
    /* Packet Byte #3 */
    uint8_t CXT:5;
    uint8_t rsvd3:3;
    /* Packet Byte #4 */
    uint8_t CA;
    /* Packet Byte #5 */
    uint8_t rsvd4:3;
    uint8_t LSV:4;
    uint8_t DM_INH:1;
    /* Reserved */
    uint8_t payload[22];
} __attribute__((packed));

// AVI InfoFrame definitions - start
// Scan Info
typedef enum AVI_SCAN_INFO_t{
    AVI_SCAN_NODATA     = 0,     // No data
    AVI_SCAN_OVERSCAN   = 1,     // Overscanned (TV)
    AVI_SCAN_UNDERSCAN  = 2,     // Underscanned (Computer)
    AVI_SCAN_FUTURE     = 3      // Future
}AVI_SCAN_INFO;

// Bar Info
typedef enum AVI_BAR_INFO_t{
    AVI_BAR_INVALID         = 0,      // Bar data not valid
    AVI_BAR_VALID_VERTICAL  = 1,      // Vertical Bar data valid
    AVI_BAR_VALID_HORIZONTAL= 2,      // Horizontal Bar data valid
    AVI_BAR_VALID_BOTH      = 3       // Vertical & Horizontal Bar data valid
}AVI_BAR_INFO;

// Active Format Information
typedef enum AVI_AFI_INFO_t{
    AVI_AFI_INVALID = 0,    // No data
    AVI_AFI_VALID   = 1     // Active Format Information valid
}AVI_AFI_INFO;

// AVI Pixel Encoding modes
typedef enum AVI_ENCODING_MODE_t{
    AVI_RGB_MODE      = 0,  // RGB pixel encoding mode
    AVI_YCRCB422_MODE = 1,  // YCrCb 4:2:2 mode
    AVI_YCRCB444_MODE = 2,  // YCrCb 4:4:4 mode
    AVI_FUTURE_MODE   = 3   // Future mode
}AVI_ENCODING_MODE;

// AVI Active Format Aspect Ratio
typedef enum AVI_AFAR_INFO_t{
    AVI_AFAR_SAME   = 8,     // same as picture aspect ratio
    AVI_AFAR_4_3    = 9,     // 4:3 center
    AVI_AFAR_16_9   = 10,    // 16:9 center
    AVI_AFAR_14_9   = 11     // 14:9 center
}AVI_AFAR_INFO;

// AVI Picture Aspect Ratio
typedef enum AVI_PAR_INFO_t{
    AVI_PAR_NODATA  = 0,      // No Data
    AVI_PAR_4_3     = 1,      // 4:3
    AVI_PAR_16_9    = 2,      // 16:9
    AVI_PAR_FUTURE  = 3       // Future
}AVI_PAR_INFO;

// AVI Colorimetry Information
typedef enum AVI_COLOR_INFO_t{
    AVI_COLOR_NODATA = 0,    // No data
    AVI_COLOR_ITU601 = 1,    // SMPTE 170M, ITU601
    AVI_COLOR_ITU709 = 2,    // ITU709
    AVI_COLOR_FUTURE = 3     // Future
}AVI_COLOR_INFO;

// AVI Non-uniform Picture Scaling Info
typedef enum AVI_SCALING_INFO_t{
    AVI_SCALING_NODATA      = 0,  // No scaling
    AVI_SCALING_HORIZONTAL  = 1,  // horizontal scaling
    AVI_SCALING_VERTICAL    = 2,  // vertical scaling
    AVI_SCALING_BOTH        = 3   // horizontal & vertical scaling
}AVI_SCALING_INFO;
// AVI InfoFrame definitions - end

// SPD InfoFrame definitions - start
// SPD InfoFrame Data Byte 25, refer Table-17 in CEA-861b
typedef enum SPD_SRC_TYPES_t{
    SPD_SRC_UNKNOWN = 0x00,	// unknown
    SPD_SRC_DIGITAL_STD = 0x01,	// Digital STB
    SPD_SRC_DVD = 0x02,		// DVD
    SPD_SRC_DVHS = 0x03,	// DVH-S
    SPD_SRC_HDD_VIDEO = 0x04,	// HDD-Video
    SPD_SRC_DVC = 0x05,		// DVC
    SPD_SRC_DSC = 0x06,		// DSC
    SPD_SRC_VCD = 0x07,		// VCD
    SPD_SRC_GAME = 0x08,	// Game
    SPD_SRC_PC = 0x09,		// PC General
}SPD_SRC_TYPES;
// SPD InfoFrame definitions - end

// Pixel Replication multipliers
typedef enum HDMI_PIXEL_REPLICATION_t{
    HDMI_PR_ONE = 0,    // No repetition (ie., pixel sent once)
    HDMI_PR_TWO,        // Pixel sent 2 times (ie.,repeated once)
    HDMI_PR_THREE,      // Pixel sent 3 times
    HDMI_PR_FOUR,       // Pixel sent 4 times
    HDMI_PR_FIVE,       // Pixel sent 5 times
    HDMI_PR_SIX,        // Pixel sent 6 times
    HDMI_PR_SEVEN,      // Pixel sent 7 times
    HDMI_PR_EIGHT,      // Pixel sent 8 times
    HDMI_PR_NINE,       // Pixel sent 9 times
    HDMI_PR_TEN         // Pixel sent 10 times
}HDMI_PIXEL_REPLICATION;

#define EDID_STD_ASPECT_RATIO_16_10	0x0
#define EDID_STD_ASPECT_RATIO_4_3	0x1
#define EDID_STD_ASPECT_RATIO_5_4	0x2
#define EDID_STD_ASPECT_RATIO_16_9	0x3

typedef enum _PBDC_SDVO_HDMI_BUF_INDEX {
    ELD_INDEX = 0,
    AVI_INDEX = 1,
    SPD_INDEX = 2,
    AUDIO_INDEX = 3,
} PBDC_SDVO_HDMI_BUF_INDEX;


typedef unsigned char  U8;
typedef unsigned short U16;
typedef unsigned long  U32;

//	Standard timing identification
typedef union _STD_TIMIMG {
	U16  usStdTiming;
	struct {
#pragma pack(1)
		U8	ucHActive;			// (HActive/8) - 31;
		struct {
			U8	ucRefreshRate : 6;	// Refresh Rate - 60
			U8	ucAspectRatio : 2;	// Aspect ratio (HActive/VActive)
								// 00:  1:1 Aspect ratio
								// 01:  4:3 Aspect ratio
								// 10:  5:4 Aspect ratio
								// 11: 16:9 Aspect ratio
		};
	};
#pragma pack()
} STD_TIMING, *PSTD_TIMING;

typedef struct _COLOR_POINT
{
#pragma pack(1)
	U8 ucWhite_point_index_number_1;
	U8 ucWhite_low_bits_1;
	U8 ucWhite_x_1;
	U8 ucWhite_y_1;
	U8 ucWhite_gamma_1;
	U8 ucWhite_point_index_number_2;
	U8 ucWhite_low_bits_2;
	U8 ucWhite_x_2;
	U8 ucWhite_y_2;
	U8 ucWhite_gamma_2;
	U8 ucByte_15;
	U8 ucByte_16_17[2];
#pragma pack()
} COLOR_POINT;

typedef struct _MONITOR_RANGE_LIMITS
{	
#pragma pack(1)

	U8 ucMin_vert_rate;			//Min Vertical Rate,in Hz
	U8 ucMax_vert_rate;			//Max Vertical Rate, in Hz
	U8 ucMin_horz_rate;			//Min Horizontal Rate, in Hz
	U8 ucMax_horz_rate;			//Max Horizontal Rate, in Hz
	U8 ucMax_pixel_clock;		//Max Pixel Clock,Value/10 Mhz
	U8 ucTiming_formula_support;	//00 - No Secondary Timing Formula Supported
									//02 - Secondary GTF Curve Supported
	//If timing_formula_support is 02
	U8 ucReserved;				//00h
	U8 ucStart_freq;				//Horizontal Freq, Value/2, KHz
	U8 ucByte_C;					//C*2
	U8 ucLSB_M;					//LSB of M Value
	U8 ucMSB_M;					//MSB of M Value
	U8 ucByte_K;					//K Value
	U8 ucByte_J;					//J*2

#pragma pack()
} MONITOR_RANGE_LIMITS, *PMONITOR_RANGE_LIMITS;

typedef struct _MONITOR_DESCRIPTOR
{
#pragma pack(1)
	U16 wFlag;			// = 0000 when block is used as descriptor
	U8 ucFlag0;		// Reserved

	U8 ucDataTypeTag;

	U8 ucFlag1;		// 00 for descriptor
	
	union {

		// Monitor S/N (ucDataTypeTag = FF)
		U8 ucMonitorSerialNumber[13];

		// ASCII string (ucDataTypeTag = FE)
		U8 ucASCIIString[13];

		// Monitor range limit (ucDataTypeTag = FD)
		MONITOR_RANGE_LIMITS MonitorRangeLimits;

		// Monitor name (ucDataTypeTag = FC)
		U8 ucMonitorName[13];

		// Color point (ucDataTypeTag = FB)
		COLOR_POINT ColorPoint;

		// Standard timings (ucDataTypeTag = FA)
		struct {
			STD_TIMING ExtraStdTiming[6];
			U8 ucFixedValueOfA0;		// Should be 0xA0
		};

		// Manufacturer specific value (ucDataTypeTag = 0F-00)
		U8 ucMfgSpecificData[13];
	};
#pragma pack()
} MONITOR_DESCRIPTOR, *PMONITOR_DESCRIPTOR;

#define NUM_DTD_BLOCK 4

typedef struct _EDID_1_X {
#pragma pack(1)
	char		Header[8];        // EDID1.x header "0 FFh FFh FFh FFh FFh FFh 0"
	U8		ProductID[10];    // Vendor / Product identification
	U8		ucVersion;        // EDID version no.
	U8		ucRevision;       // EDID revision no.
	union {
		U8	ucVideoInput;	  // Video input definition
		struct {
			U8	ucSyncInput	 : 4;	// Sync input supported
			U8	ucSetup		 : 1;	// Display setup
			U8	ucSigLevStd	 : 2;	// Signal level Standard
			U8	ucDigitInput	 : 1;	// 1: Digital input; 0: Analog input
		};
	};
	U8		ucMaxHIS;		// Maximum H. image size in cm
	U8		ucMaxVIS;		// Maximum V. image size in cm
	U8		ucGamma;		// Display gamma value
	union {
		U8	ucDMPSFeature;	// DPMS feature support
		struct {
			U8	ucGTFSupport : 1;	// GTF timing support (1: Yes)
			U8	ucPTM		 : 1;	// Preferred timing is 1st DTD (1: Yes)
			U8	ucColorSpace : 1;	// Use STD color space (1:Yes)
			U8	ucDispType	 : 2;	// Display type
								// 00: Monochrome
								// 01: R/G/B color display
								// 10: Non R/G/B multicolor display
								// 11: Undefined
			U8	ucActiveOff	 : 1;	// Active off
			U8	ucSuspend	 : 1;	// Suspend
			U8	ucStandBy	 : 1;	// Stand-by
		};
	};
	U8		ColorChars[10];	// Color characteristics
	//
	// Established timings: 3 bytes (Table 3.14 of EDID spec)
	union {
		U8 EstTiming1;
		struct {
			U8 bSupports800x600_60	: 1;
			U8 bSupports800x600_56	: 1;
			U8 bSupports640x480_75	: 1;
			U8 bSupports640x480_72	: 1;
			U8 bSupports640x480_67	: 1;
			U8 bSupports640x480_60	: 1;
			U8 bSupports720x400_88	: 1;
			U8 bSupports720x400_70	: 1;
		};
	};
	union {
		U8 EstTiming2;
		struct {
			U8 bSupports1280x1024_75 : 1;
			U8 bSupports1024x768_75	 : 1;
			U8 bSupports1024x768_70	 : 1;
			U8 bSupports1024x768_60	 : 1;
			U8 bSupports1024x768_87i : 1;
			U8 bSupports832x624_75	 : 1;
			U8 bSupports800x600_75	 : 1;
			U8 bSupports800x600_72	 : 1;
		};
	};
	union {
		U8 MfgTimings;
		struct {
			U8 bMfgReservedTimings	: 7;
			U8 bSupports1152x870_75	: 1;
		};
	};
	STD_TIMING	StdTiming[8];	// 8 Standard timing support
	//EDID_DTD	DTD[4];			// Four DTD data blocks
	// Detailed timing section - 72 bytes (4*18 bytes)
	union {
		EDID_DTD	DTD[NUM_DTD_BLOCK];			// Four DTD data blocks

		MONITOR_DESCRIPTOR MonitorInfo[NUM_DTD_BLOCK];
	};
	U8		ucNumExtBlocks;	// Number of extension EDID blocks
	U8		ucChecksum;		// Checksum of the EDID block
#pragma pack()
} EDID_1_X, *PEDID_1_X;

//manju CE-EXTN
typedef struct _CE_EDID {
	U8	ucTag;
	U8	ucRevision;
	U8	ucDTDOffset;
	U8	ucCapabilty;
	U8	data[123];
	U8   ucCheckSum;   
} CE_EDID, *PCE_EDID;

// CEA-861b definitions
#define CEA_VERSION             0x00
#define ELD_VERSION             0x01
#define BASE_ELD_SIZE           0x0E
#define CEA_EDID_HEADER_SIZE    0x04

#define CEA_AUDIO_DATABLOCK     0x1
#define CEA_VIDEO_DATABLOCK     0x2
#define CEA_VENDOR_DATABLOCK    0x3
#define CEA_SPEAKER_DATABLOCK   0x4

#define CEA_DATABLOCK_TAG_MASK                  0xE0
#define CEA_DATABLOCK_LENGTH_MASK               0x1F
#define CEA_SHORT_VIDEO_DESCRIPTOR_CODE_MASK    0x7F

//Basic Audio support definitions
#define BASIC_AUDIO_SUPPORTED			0x40
#define CEA_EXTENSION_BLOCK_BYTE_3		3
#define FL_AND_FR_SPEAKERS_CONNECTED		0x1

// CEA Short Audio Descriptor
typedef struct _CEA_861B_ADB
{
#pragma pack(1)
    union
    {
        U8 ucbyte1;
        struct
        {
            U8   ucMaxChannels       :3; // Bits[0-2]
            U8   ucAudioFormatCode   :4;	// Bits[3-6], see AUDIO_FORMAT_CODES
            U8   ucB1Reserved        :1;	// Bit[7] - reserved
        };
    };
    union
    {
        U8	ucByte2;
        struct
        {
            U8   uc32kHz             :1;	// Bit[0] sample rate = 32kHz
            U8   uc44kHz             :1;	// Bit[1] sample rate = 44kHz
            U8   uc48kHz             :1;	// Bit[2] sample rate = 48kHz
            U8   uc88kHz             :1;	// Bit[3] sample rate = 88kHz
            U8   uc96kHz             :1;	// Bit[4] sample rate = 96kHz
            U8   uc176kHz            :1;	// Bit[5] sample rate = 176kHz
            U8   uc192kHz            :1;	// Bit[6] sample rate = 192kHz
            U8   ucB2Reserved        :1;	// Bit[7] - reserved
        };
    };
    union
    {
        U8   ucByte3;    // maximum bit rate divided by 8kHz
        // following is the format of 3rd byte for uncompressed(LPCM) audio
        struct
        {
            U8	uc16Bit				:1;	// Bit[0]
            U8	uc20Bit				:1;	// Bit[1]
            U8	uc24Bit				:1;	// Bit[2]
            U8	ucB3Reserved		:5;	// Bits[3-7]
        };
    };
#pragma pack()
} CEA_861B_ADB, *PCEA_861B_ADB;

// Audio format codes
typedef enum _AUDIO_FORMAT_CODES {
    AUDIO_LPCM      = 0x0001,   // Linear PCM (eg. IEC60958)
    AUDIO_AC3       = 0x0002,   // AC-3
    AUDIO_MPEG1     = 0x0003,   // MPEG1 (Layers 1 & 2)
    AUDIO_MP3       = 0x0004,   // MP3   (MPEG1 Layer 3)
    AUDIO_MPEG2     = 0x0005,   // MPEG2 (multichannel)
    AUDIO_AAC       = 0x0006,   // AAC
    AUDIO_DTS       = 0x0007,   // DTS
    AUDIO_ATRAC     = 0x0008    // ATRAC
} AUDIO_FORMAT_CODES;

// Audio capability structure
typedef struct _DEVICE_AUDIO_CAPS
{
    int NPLDesign :8; // max number of audio packets device can
                        // deliver per line
    int K0        :8; // The overhead(in pixels) per line requied
                        // by device for setting up audio packets when
                        // CP is disabled
    int K1        :8; // The overhead(in pixels) per line requied
                        // by device for setting up audio packets when
                        // CP is enabled
    // Misc data
    int PR        :4; // Pixel Replication value
    int bIsHDCP   :1; // Driver, Device and Receiver support HDCP
    int bIsRPTR   :1; // Receiver is HDCP repeater
    int Reserved  :2; // reserved bits
} DEVICE_AUDIO_CAPS, *PDEVICE_AUDIO_CAPS;

typedef enum _PBDC_SDVO_HDMI_AUDIO_STATE{
    HDMI_ELD_VALID = 0x01,
    HDMI_PRESENCE_DETECT = 0x02,
    HDMI_CP_READY = 0x04
}PBDC_SDVO_HDMI_AUDIO_STATE;

/******************** HDMI InfoFrame End *******************************/


typedef enum _Watermark_Type
{
    WATERMARK_1,
    WATERMARK_2
} Watermark_Type;

typedef struct _DSPARB_Register
{
    union
    {
        U32 entireRegister;
        struct
        {
            U32 b_Start         : 7;
            U32 c_Start         : 7;
            U32 reserved        : 18;
        };
    };
} DSPARB_Register, *PDSPARB_Register;

typedef enum CePlane_t
{
    DCplane_A,
    DCplane_B,
    DCplane_C,
    DCplane_CursorA,
    DCplane_CursorB,
    DCplane_Overlay1,
    DC_NUM_PLANES,
    DCplane_NotSupported = -1
} CePlane;

typedef struct _Watermark_Register1
{
    union
    {
        U32 entireRegister;
        struct
        {
            U32 DispA_WM1	: 8;
            U32 DispA_BL	: 2;
            U32 reserved	: 6;
            U32 DispB_WM1	: 8;
            U32 DispB_BL	: 2;
            U32 DispC_WM1	: 5;
            U32 reserved1	: 1;
        };
    };
} Watermark_Register1, *PWatermark_Register1;

typedef struct _FIFOWatermark_Register
{
    union
    {
        U32 entireRegister;
        struct
        {
            U32 DispAB_WM2		: 8;
            U32 CursAB_WM2		: 7;
            U32 maxFIFO_Enable		: 1;
            U32 reserved		: 1;
            U32 OverlayUV_WM2		: 7;
            U32 OverlayY_WM2		: 7;
            U32 reserved1		: 1;
        };
    };
} FIFOWatermark_Register, *PFIFOWatermark_Register;

typedef struct _MAXFIFOWatermark_Register
{
    union
    {
        U32 entireRegister;
        struct
        {
            U32 WM1_STATUS1		: 8;
            U32 WM1_STATUS0		: 8;
            U32 reserved		: 16;
        };
    };
} MAXFIFOWatermark_Register, *PMAXFIFOWatermark_Register;


static inline PsbPtr
psbPTR(ScrnInfoPtr pScrn)
{
    return ((PsbPtr) pScrn->driverPrivate);
}

static inline PsbDevicePtr
psbDevicePTR(PsbPtr pPsb)
{
    return pPsb->pDevice;
}

#define PSB_THALIA_OFFSET 0x00040000
#define PSB_SGX_2D_SLAVE_PORT 0x00044000
#define PSB_PAGE_SIZE 4096
#define PSB_PAGE_SHIFT 12
#define PSB_BIOS_POPUP_SIZE 4096;
#define PSB_BSM 0x5C
#define PSB_PGETBL_CTL 0x2020

#define PSB_RSGX32(_offs) \
  (*(volatile CARD32 *)(pDevice->regMap + PSB_THALIA_OFFSET + (_offs)))
#define PSB_WSGX32(_offs, _val)						\
  (*(volatile CARD32 *)(pDevice->regMap + PSB_THALIA_OFFSET + (_offs)) = (_val))
#define PSB_READ32(_offs)				\
  (*(volatile CARD32 *)(pDevice->regMap + (_offs)))
#define PSB_WRITE32(_offs, _val)				\
  (*(volatile CARD32 *)(pDevice->regMap + (_offs)) = (_val))
#define PSB_RSLAVE32(_offs)						\
  (*(volatile CARD32 *)(pDevice->regMap + PSB_SGX_2D_SLAVE_PORT + (_offs)))
#define PSB_WSLAVE32(_offs, _val)		\
  (PSB_RSLAVE32(_offs) = (_val))

/*
 * psb_shadow.c
 */

extern void psbUpdatePackedDepth15(ScreenPtr pScreen, shadowBufPtr pBuf);
extern void psbUpdatePackedDepth24(ScreenPtr pScreen, shadowBufPtr pBuf);
extern void *psbWindowLinear(ScreenPtr pScreen, CARD32 row, CARD32 offset,
			     int mode, CARD32 * size, void *closure);

#ifdef XF86DRI

/*
 * psb_dri.c
 */
extern Bool psbDRIScreenInit(ScreenPtr pScreen);
extern void psbDRICloseScreen(ScreenPtr pScreen);
extern Bool psbDRMDeviceInit(PsbDevicePtr pDevice);
extern void psbDRMDeviceTakeDown(PsbDevicePtr pDevice);
extern void psbDRIUnlock(ScrnInfoPtr pScrn);
extern void psbDRILock(ScrnInfoPtr pScrn, int flags);
extern void psbDRMIrqTakeDown(PsbDevicePtr pDevice);
extern void psbDRMIrqInit(PsbDevicePtr pDevice);
extern Bool psbDRIFinishScreenInit(ScreenPtr pScreen);
extern void psbDRIUpdateScanouts(ScrnInfoPtr pScrn);

#else
#define psbDRILock(_a, _b)
#define psbDRIUnlock(_a)
#endif

/*
 * psb_outputs.c
 */
extern void psbOutputPrepare(xf86OutputPtr output);
extern void psbOutputCommit(xf86OutputPtr output);

/*Add the functions to get extension EDID data, Jamesx*/
extern unsigned char *psbDDCRead_DDC2(int scrnIndex, I2CBusPtr pBus,
				      int start, int len);
extern DisplayModePtr psbOutputDDCGetModes(xf86OutputPtr output);
extern void psbOutputDestroy(PsbOutputPrivatePtr pOutput);
extern void psbOutputInit(PsbDevicePtr pDevice, PsbOutputPrivatePtr pOutput);
extern void psbOutputEnableCrtcForAllScreens(PsbDevicePtr pDevice, int crtc);
extern void psbOutputDisableCrtcForOtherScreens(ScrnInfoPtr pScrn, int crtc);
extern void psbOutputDestroyAll(ScrnInfoPtr pScrn);
extern Bool psbPtrAddToList(MMListHead * head, void *ptr);
extern Bool psbOutputCompat(ScrnInfoPtr pScrn);
extern Bool psbOutputAssignToScreen(ScrnInfoPtr pScrn, const char *name);
extern void psbOutputReleaseFromScreen(ScrnInfoPtr pScrn, const char *name);
extern xf86OutputPtr psbOutputClone(ScrnInfoPtr pScrn, ScrnInfoPtr origScrn,
				    const char *name);
extern void psbOutputSave(ScrnInfoPtr pScrn);
extern void psbOutputRestore(ScrnInfoPtr pScrn);
extern void psbOutputDPMS(ScrnInfoPtr pScrn, int mode);
extern Bool psbOutputIsDisabled(ScrnInfoPtr pScrn, int pipe);
extern Bool psbOutputCrtcValid(ScrnInfoPtr pScrn, int crtc);

/*
 * psb_lvds.c
 */

extern xf86OutputPtr psbLVDSInit(ScrnInfoPtr pScrn, const char *name);

/*
 * psb_sdvo.c
 */

extern xf86OutputPtr
psbSDVOInit(ScrnInfoPtr pScrn, int output_device, char *name);

/*
 * psb_crtc.c
 */

typedef struct _intel_clock_t
{
    /* given values */
    int n;
    int m1, m2;
    int p1, p2;
    /* derived values */
    int dot;
    int vco;
    int m;
    int p;
} intel_clock_t;

extern void psbCrtcLoadLut(xf86CrtcPtr crtc);
extern void psbDescribeOutputConfiguration(ScrnInfoPtr pScrn);
extern xf86CrtcPtr psbCrtcInit(ScrnInfoPtr pScrn, int pipe);
extern xf86CrtcPtr psbCrtcClone(ScrnInfoPtr pScrn, xf86CrtcPtr origCrtc);
extern void psbWaitForVblank(ScrnInfoPtr pScrn);
extern void psbPipeSetBase(xf86CrtcPtr crtc, int x, int y);
extern void psbCrtcSaveCursors(ScrnInfoPtr pScrn, Bool force);
extern int psbCrtcSetupCursors(ScrnInfoPtr pScrn);
extern void psbCrtcFreeCursors(ScrnInfoPtr pScrn);
extern DisplayModePtr psbCrtcModeGet(ScrnInfoPtr pScrn, xf86CrtcPtr crtc);
extern int psbPanelFitterPipe(CARD32 pfitControl);
extern void psbPrintPll(int scrnIndex, char *prefix, intel_clock_t * clock);

/*
 * psb_driver.c
 */
extern void psbCheckCrtcs(PsbDevicePtr pDevice);

/*
 * psb_cursor.c
 */
void psbInitHWCursor(ScrnInfoPtr pScrn);
Bool psbCursorInit(ScreenPtr pScreen);
extern void psb_crtc_load_cursor_image(xf86CrtcPtr crtc, unsigned char *src);
extern void psb_crtc_load_cursor_argb(xf86CrtcPtr crtc, CARD32 * image);
extern void psb_crtc_set_cursor_position(xf86CrtcPtr crtc, int x, int y);
extern void psb_crtc_set_cursor_colors(xf86CrtcPtr crtc, int bg, int fg);
extern void psb_crtc_hide_cursor(xf86CrtcPtr crtc);
extern void psb_crtc_show_cursor(xf86CrtcPtr crtc);

extern Bool
psbExaGetSuperOffset(PixmapPtr p, unsigned long *offset,
		     struct _MMBuffer **buffer);
/*
 * psb_video.c
 */
extern XF86VideoAdaptorPtr psbInitVideo(ScreenPtr pScreen);
extern void psbFreeAdaptor(ScrnInfoPtr pScrn, XF86VideoAdaptorPtr adapt);

/*
 * psb_composite.c
 */
extern void psbExaDoneComposite3D(PixmapPtr pPixmap);
extern void psbExaComposite3D(PixmapPtr pDst, int srcX, int srcY,
			      int maskX, int maskY, int dstX, int dstY,
			      int width, int height);
Bool psbExaPrepareComposite3D(int op, PicturePtr pSrcPicture,
			      PicturePtr pMaskPicture, PicturePtr pDstPicture,
			      PixmapPtr pSrc, PixmapPtr pMask,
			      PixmapPtr pDst);

/*
 * psb_dga.c
 */
extern Bool PSBDGAReInit(ScreenPtr pScreen);
extern Bool PSBDGAInit(ScreenPtr pScreen);
#endif /*_PSB_H_*/
