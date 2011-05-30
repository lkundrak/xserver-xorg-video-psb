#ifndef _I830_H_
#define _I830_H_ 1

#include <xf86i2c.h>
#include <xf86str.h>
#include <xf86RandR12.h>
#include "psb_driver.h"
#include "i810_reg.h"

#define MAX_OUTPUTS 6

#define I830_I2C_BUS_DVO 1
#define I830_I2C_BUS_SDVO 2

/* these are outputs from the chip - integrated only
   external chips are via DVO or SDVO output */
#define I830_OUTPUT_UNUSED 0
#define I830_OUTPUT_ANALOG 1
#define I830_OUTPUT_DVO 2
#define I830_OUTPUT_SDVO 3
#define I830_OUTPUT_LVDS 4
#define I830_OUTPUT_TVOUT 5

#define I830_DVO_CHIP_NONE 0
#define I830_DVO_CHIP_LVDS 1
#define I830_DVO_CHIP_TMDS 2
#define I830_DVO_CHIP_TVOUT 4

#define PCI_CHIP_I945_GM 0x27A2

#define INREG(_offs) \
  (*(volatile CARD32 *)(pI830->pDevice->regMap + (_offs)))
#define OUTREG(_offs, _val)			\
  INREG(_offs) = (_val)
#define I830PTR(_pScrn) (& psbPTR(_pScrn)->i830Ptr)
#define I830CrtcPrivatePtr PsbCrtcPrivatePtr
#define I830OutputPrivatePtr PsbOutputPrivatePtr
#define I830OutputPrivateRec PsbOutputPrivateRec

#define i830_output_prepare psbOutputPrepare
#define i830_output_commit psbOutputCommit

#define IS_I830(_dummy) FALSE
#define IS_845G(_dummy) FALSE

extern DisplayModePtr i830_ddc_get_modes(xf86OutputPtr output);
extern void i830_lvds_init(ScrnInfoPtr pScrn);
extern void i830_set_lvds_blc_data(ScrnInfoPtr pScrn, CARD8 blctype,
				   CARD8 pol, CARD16 freq, CARD8 minlevel,
				   CARD8 address, CARD8 level);
extern Bool I830I2CInit(ScrnInfoPtr pScrn, I2CBusPtr * bus_ptr, int i2c_reg,
			char *name);

#endif
