#ifndef PSB_LVDS_H_
#define PSB_LVDS_H_
#include <psb_reg.h>
#include "xf86.h"
#include "psb_driver.h"
#include "i810_reg.h"
#include "i830.h"
#include "i830_bios.h"
#include "X11/Xatom.h"
#include "libmm/mm_defines.h"

#ifdef __linux__		       /* for ACPI detection */
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#endif

#define BLC_POLARITY_NORMAL		0	/* Normal polarity (0 = full off) */
#define BLC_POLARITY_INVERSE	        1	/* Inverse polarity (0 = full on) */

/* Constants for PWM modulation freq calculation*/
#define BLC_PWM_FREQ_CALC_CONSTANT	32	/* Per hardware spec (32Dclks) */
/* The const is used to get effective results from the integer math, and to not divide by 0.*/
#define BLC_PWM_PRECISION_FACTOR	10000000
#define MHz			        1000000	/* MHz */

/* The max/min PWM frequency in BPCR[31:17] - */
/* The smallest number is 1 (not 0) that can fit in the 15-bit field of the and then*/
/* shifts to the left by one bit to get the actual 16-bit value that the 15-bits correspond to.*/
#define BLC_MAX_PWM_REG_FREQ	        0xFFFE
#define BLC_MIN_PWM_REG_FREQ	        0x2

#define BLC_PWM_POLARITY_BIT_CLEAR	0xFFFE	/* BPCR[0] */
#define BLC_PWM_LEGACY_MODE_ENABLE	0x0001	/* BPCR[16] */
#define BRIGHTNESS_MASK		        0xFF
#define BRIGHTNESS_MAX_LEVEL	        100

typedef struct _PsbLVDSOutputRec
{
    PsbOutputPrivateRec psbOutput;
    CARD32 savePP_ON;
    CARD32 savePP_OFF;
    CARD32 saveLVDS;
    CARD32 savePP_CONTROL;
    CARD32 savePP_CYCLE;
    CARD32 saveBLC_PWM_CTL;
    CARD32 backlight_duty_cycle;
    CARD32 backlight;
    DisplayModePtr panelFixedMode;
    Bool panelWantsDither;

    /* I2C bus for BLC control. */
    I2CDevRec blc_d;

} PsbLVDSOutputRec, *PsbLVDSOutputPtr;

#endif /* PSB_LVDS_H_ */

