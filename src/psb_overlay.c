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
 *    John Ye <john.ye@intel.com>
 *
 */

#include "psb_overlay.h"

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

#include "libmm/mm_defines.h"
#include "libmm/mm_interface.h"
#include "xf86mm.h"
#include "xf86drm.h"
#include "xf86Crtc.h"
#include <assert.h>
#include <math.h>
#ifndef PI
#define PI                      3.1415926535897932384626433832795028841971f
#endif


/* Static functions, available for ILVDS downscale method of using overlay plane*/
/* downscale calculations use floating point math!*/
void PBDCOverlay_SetRegisters(float *fCoeff, uint16_t wMantSize, POV_COEFF pCoeff, uint16_t wPosition)
{
	uint16_t    maxVal, wCoeff, wRes;
	uint16_t    sign;
	float   coeff;

	sign = 0;
	maxVal = 1 << wMantSize;
	coeff = *fCoeff;
	if (coeff < 0) {
		sign = 1;
		coeff = - coeff;
	}

	wRes = 12 - wMantSize;

	if ( (wCoeff = (uint16_t)(coeff * 4 * maxVal + 0.5f)) < maxVal ) {
		pCoeff[wPosition].exponent = 3;
		pCoeff[wPosition].mantissa = wCoeff << wRes; 
		*fCoeff = (float)wCoeff/(float)(4 * maxVal);
	} else if ( (wCoeff = (uint16_t)(coeff * 2 * maxVal + 0.5f)) < maxVal ) {
		pCoeff[wPosition].exponent = 2;
		pCoeff[wPosition].mantissa = wCoeff << wRes;
		*fCoeff = (float)wCoeff/(float)(2 * maxVal);
	} else if ( (wCoeff = (uint16_t)(coeff * maxVal + 0.5f)) < maxVal ) {
		pCoeff[wPosition].exponent = 1;
		pCoeff[wPosition].mantissa = (uint16_t)wCoeff << wRes;
		*fCoeff = (float)wCoeff/(float)maxVal;
	} else if ( (wCoeff = (uint16_t)(coeff * maxVal * 0.5f + 0.5f)) < maxVal ) {
		pCoeff[wPosition].exponent = 0;
		pCoeff[wPosition].mantissa = wCoeff << wRes;
		*fCoeff = (float)wCoeff/(float)(maxVal >> 1);
	} else {
		assert(0);  /*Coeff out of range!*/
	}

	pCoeff[wPosition].sign = sign;

	if (sign)
		*fCoeff = -(*fCoeff);
}


void PBDCOverlay_UpdateCoeff(uint16_t wTaps, float fCutoff, bool bHor, bool bY, POV_COEFF pCoeff)
{
	uint16_t    i, j, j1, num, pos, wMantSize;
	bool    bVandC;
	float   val, sinc, window, sum;
	float   fCoeff[5*32], ffCoeff[17][5];
	float   fDiff;
	uint16_t    wTapAdjust[5], wTap2Fix;

	if (wTaps == 2) {
		for (i = 0; i < 17; i++) {
			for (j = 0; j < 3; j++) {
				pos = j + i * 3;
				pCoeff[pos].exponent = 0;
				pCoeff[pos].mantissa = 0;
				pCoeff[pos].sign = 0;
			}
		}
		return;
	}
    
	if (bHor) /*H Scale*/
		wMantSize = 7;
	else
		wMantSize = 6;

	bVandC = (!bHor) & (!bY); /*vertal & Chroma*/

	num = wTaps * 16;
	for (i = 0; i < num*2; i++) {
		val = (1.0f/fCutoff) * wTaps * PI * (i - num)/(2 * num);
		if ( val == 0.0f )
			sinc = 1.0f;
		else
			sinc = (float)sin(val)/val;
		/* hanning window */
		window = (0.5f - 0.5f * (float)cos(i * PI/num));

		fCoeff[i] = sinc * window;
	}

	for (i = 0; i < 17; i++) {
		/* normalize the coefficient */
		sum = 0.0;
		for (j = 0; j < wTaps; j++) {
			pos = i + j * 32;
			sum += fCoeff[pos];
		}
		for (j = 0; j < wTaps; j++) {
			pos = i + j * 32;
			ffCoeff[i][j] = fCoeff[pos]/sum;
		}

		/* set the coefficient registers and get the data in floating point format */
		for (j = 0; j < wTaps; j++) {
			pos = j + i * wTaps;
			if ( (j == (wTaps - 1)/2) && (!bVandC) )
				PBDCOverlay_SetRegisters(&ffCoeff[i][j], (uint16_t)(wMantSize + 2), pCoeff, pos);
			else
				PBDCOverlay_SetRegisters(&ffCoeff[i][j], wMantSize, pCoeff, pos);
		}

		wTapAdjust[0] = (wTaps - 1)/2;
		for (j = 1, j1 = 1; j <= wTapAdjust[0]; j++, j1++) {
			wTapAdjust[j1] = wTapAdjust[0] - j;
			wTapAdjust[++j1] = wTapAdjust[0] + j;
		} 

		/* adjust the coefficient */
		sum = 0.0;
		for (j = 0; j < wTaps; j++)
			sum += ffCoeff[i][j];

		if (sum != 1.0) {
			for (j1 = 0; j1 < wTaps; j1++) {
				wTap2Fix = wTapAdjust[j1];
				fDiff = 1.0f - sum;
				ffCoeff[i][wTap2Fix] += fDiff;
				pos = wTap2Fix + i * wTaps;
				if ( (wTap2Fix == (wTaps - 1)/2) && (!bVandC) )
					PBDCOverlay_SetRegisters(&ffCoeff[i][wTap2Fix], (uint16_t)(wMantSize + 2), pCoeff, pos);
				else
					PBDCOverlay_SetRegisters(&ffCoeff[i][wTap2Fix], wMantSize, pCoeff, pos);

				sum = 0.0f;
				for (j = 0; j < wTaps; j++)
					sum += ffCoeff[i][j];
				if (sum == 1.0f)
					break;
			}
		}
	}
}



void PBDCOverlay_SetOverlayCoefficients(POVERLAY_REGS pOverlayRegisters)
{
	uint16_t        wVTaps;
	float       fHYScale, fHUVScale, fVYScale, fVUVScale;
	uint32_t       dwHFYScale;
	/*    PVOID       pFloatStateBuf = NULL;*/

	uint32_t dwSrcW = pOverlayRegisters->SrcWidth.Y_Width;
	uint32_t dwSrcH = pOverlayRegisters->SrcHeight.Y_Height;
	uint32_t dwDstW = pOverlayRegisters->DestSize.Width;
	uint32_t dwDstH = pOverlayRegisters->DestSize.Height;

        wVTaps = 2;

	/*Save Floating point HW state before floating point operation and restore it after.*/
#if 0
	if  (!SaveFloatingPointState(&pFloatStateBuf))
		{
			DPF((DBG_CRITICAL,  "SetOverlayCoefficients: Error saving floating point state!\n"));
			DBG_ASSERT(FALSE);
		}
#endif
	/* Horizontal Y coefficients*/
	/* assume (DstW < SrcW)*/
	{
		dwHFYScale = ((pOverlayRegisters->FHDScale) >> 16) & 0x1f;
		fHYScale   = ((float)(dwSrcW >> dwHFYScale) / (float)dwDstW);
		if (fHYScale > 3.0f)
			{
				fHYScale = 3.0f; /*clamped to 3.0*/
			}
	}

	/* Vertical Y coefficients*/
	fVYScale = 1.0;

	/* UV coefficients*/
	fHUVScale = fHYScale;
	fVUVScale = fVYScale;

	PBDCOverlay_UpdateCoeff(5, fHYScale, 1, 1, pOverlayRegisters->HYCoeff);
	PBDCOverlay_UpdateCoeff(wVTaps, fVYScale, 0, 1, pOverlayRegisters->VYCoeff);
	PBDCOverlay_UpdateCoeff(3, fHUVScale, 1, 0, pOverlayRegisters->HUVCoeff);
	PBDCOverlay_UpdateCoeff(wVTaps, fVUVScale, 0, 0, pOverlayRegisters->VUVCoeff);

#if 0
	RestoreFloatingPointState(&pFloatStateBuf);
#endif

}



/********************************************************************                                                                                           
METHOD    : FractionToDword()                                                                                                                                 
PURPOSE   : Convert fraction to Dword                                                                                                                           
ARGUMENTS : d, a double precision fraction 0 <= value < 1                                                                                                       
            numbits, the number of bits to put in the fraction                                                                                                  
RETURN    : ulResult, an numbits-bit representation of the fraction                                                                                             
*********************************************************************/                                                                                          
uint32_t FractionToDword(double d, int numbits)
{
        uint32_t ulResult;
        uint64_t mask;
        union Union_DoubleBitPattern { 
                uint64_t ulBits; 
                double dDouble; 
        } Pattern; 
                                                                                                                                                                
        Pattern.dDouble = d + 1.0; 
                                                                                                                                                                
        mask = (1<<(numbits+1))-1; 
        mask <<= (52-numbits-1);   
                                                                                                                                                                
        ulResult = (uint32_t)((Pattern.ulBits & mask) >> (52-numbits-1)); 
        ulResult += ulResult & 0x1; 
        ulResult = ulResult >> 1; 
        return ulResult;
}

static OVERLAY_REGS overlay_reglist;

void psb_overlay_setup_reglist(xf86CrtcPtr crtc, POVERLAY_REGS reglist, int turnon, uint16_t usSrcWidth, uint16_t usSrcHeight, uint16_t usDestWidth, uint16_t usDestHeight)
{
	PsbCrtcPrivatePtr pCrtc = psbCrtcPrivate(crtc);
	ScrnInfoPtr pScrn = crtc->scrn;
	PsbPtr pPsb = psbPTR(pScrn);
	/* this pDevice referenced by PSB_READ32() */
	PsbDevicePtr pDevice = psbDevicePTR(psbPTR(pScrn));

	/* we only support LVDS now */
	int dspcntr_reg =  DSPBCNTR;
	int dspbase_reg =  DSPBBASE;
	int pipeconf_reg = PIPEBCONF;
	int dspstride_reg =  DSPBSTRIDE;
	int dspsize_reg =  DSPBSIZE;

	CARD32 temp;
	int ret;
	int i;

	memset(reglist , 0, sizeof(OVERLAY_REGS)); /* clear reg list*/

	/* we only support LVDS now */
	reglist->Buffer0YPtr = PSB_READ32(dspbase_reg);

	reglist->Stride.Y_Stride = PSB_READ32(dspstride_reg);

	/* XXX possibly the position should be (0,0), or should be
	   read from crtc */
	/* reglist->DestPosition.Left = (uint16_t)(1024-640)/2; */
	/* reglist->DestPosition.Top = (uint16_t)(768-480)/2; */

	reglist->DestSize.Width = usDestWidth; /* DWINSZ*/
	reglist->DestSize.Height = usDestHeight; /* DWIMSZ*/

	reglist->SrcWidth.Y_Width = usSrcWidth; /* SWIDTH*/
	reglist->SrcSWORDWidth.Y_Width = (uint16_t)
		(((((	reglist->Buffer0YPtr + 
			(reglist->SrcWidth.Y_Width * 32/8)	+ /* 32bpp = 4Bpp*/
			0x3F) >> 6) - (reglist->Buffer0YPtr >> 6)) << 1) - 1) << 2;
	reglist->SrcHeight.Y_Height = usSrcHeight; /* SHEIGHT*/

	double fRatio = (double)usSrcHeight/(double)usDestHeight;
	uint16_t usScaleFraction = (uint16_t)FractionToDword(fRatio - (int)fRatio, 12);
	reglist->YRGBScale =  usScaleFraction << 20; /* YRGB_VFRACT*/
	reglist->YRGBScale |= (usSrcWidth/usDestWidth) << 16; /* YRGB_HINT*/
	
	fRatio = (double)usSrcWidth/(double)usDestWidth;
	usScaleFraction = (uint16_t)FractionToDword(fRatio - (int)fRatio, 12);
	reglist->YRGBScale |= usScaleFraction << 3; /* YRGB_HFRACT*/

	/* UV scales the same as Y (and seems to be required even for
	   RGB surface) */
	reglist->UV_Scale = reglist->YRGBScale; 
	reglist->VDScale = (usSrcHeight/usDestHeight) << 16; /* YRGB_VINT*/
	reglist->VDScale |= (usSrcHeight/usDestHeight); /* UV_VINT (B-Spec says not used for packed formats, but doesn't hurt*/
	   	
	reglist->CCorrection0 = (1<<6) << 18; /* OCLRC0 - Normal Contract (3.6 format) & Brightness*/
	reglist->CCorrection1 = 1<<7; /* OCLRC1 - Normal Contract & Brightness (3.7 format)*/

#if 0 /* alpha it over desktop (for test alpha'd w/ PlaneA)*/
	reglist->DestCMask = 1<<30; /* Constant Alpha On*/
	reglist->SrcCMask = 0xE0; /* const Alpha ~12%*/
#endif

	reglist->Command = (1 << 10); /* RGB 8:8:8 format*/

	reglist->Config |= (1 << 4); /* Disable YUV2RGB (not needed, so might as well not use it)*/
	reglist->Config |= (1 << 16); /* Use Pipe's Gamma (not just Overlay's)*/

	PBDCOverlay_SetOverlayCoefficients(reglist);

	/* direct the output to pipe B */
	reglist->Config |= (1 << 18);

	if (turnon) 
		reglist->Command |= 1; /* Enable Overlay */
	else 
		reglist->Command &= ~1; /* Disable Overlay */

	return;
}

void psb_overlay_write_reglist(xf86CrtcPtr crtc, POVERLAY_REGS reglist, int turnon)
{
	unsigned long offset;
	unsigned long buf_addr, buf_offset;
	CARD32 buf_user_start;
	unsigned long buf_virtual;
	unsigned long buf_size;

	PsbCrtcPrivatePtr pCrtc = psbCrtcPrivate(crtc);
	ScrnInfoPtr pScrn = crtc->scrn;
	PsbPtr pPsb = psbPTR(pScrn);
	PsbDevicePtr pDevice = psbDevicePTR(psbPTR(pScrn));
	MMManager *man = pDevice->man;
	struct _MMBuffer* buf;

	int ret;

	buf = man->createBuf(man, sizeof(OVERLAY_REGS), 0, 
			     MM_FLAG_READ | MM_FLAG_MEM_VRAM |
			     MM_FLAG_NO_EVICT | MM_FLAG_MAPPABLE,
			     MM_HINT_DONT_FENCE);	

	if (!buf) {
		over_msg( "Error: create buf failed\n");
		return;
	}

	ret = man->mapBuf(buf, MM_FLAG_READ | MM_FLAG_WRITE, 0);
	if (ret) {
		over_msg( "Error: mapBuf failed!\n");
		goto out_err;
	}
	/* XXX should unMap later? */
	man->unMapBuf(buf); 
	
	/*  OVADD wants the offset relative to the aperture start. */	
	offset = mmBufOffset(buf) & 0x0FFFFFFF;
	buf_addr = pDevice->stolenBase + offset;

	/* the userspace visible virtual address */
	buf_virtual = man->bufVirtual(buf); 
	buf_size = man->bufSize(buf);	

	if (turnon) {
		/* set the chicken bit */
		PSB_WRITE32(0x70400, PSB_READ32(0x70400) | 1<<30);
	}

	/* Tell DC Hardware to read in overlay reg list*/
	memcpy(buf_virtual, reglist, sizeof(OVERLAY_REGS)); 
	PSB_WRITE32(OVADD,  buf_addr | BIT0); //XXX why | BIT0?	

	if (!turnon) {
		PSB_WRITE32(0x70400, PSB_READ32(0x70400) & ~(1<<30)); /* unset the chicken bit */		
	}

	return;

 out_err:
	man->destroyBuf(buf);	
	return;
}

/* This function handles the sophisticated decision making process on
   when to turn on/off the overlay plane, and then call proper
   functions to execute */
void psb_dpms_overlay(xf86CrtcPtr crtc, int turnon)
{
	PsbCrtcPrivatePtr pCrtc = psbCrtcPrivate(crtc);
	ScrnInfoPtr pScrn = crtc->scrn;
	PsbPtr pPsb = psbPTR(pScrn);
	PsbDevicePtr pDevice = psbDevicePTR(psbPTR(pScrn));
	xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
	int pipe = pCrtc->pipe;

	uint16_t usSrcWidth;
	uint16_t usSrcHeight;
	uint16_t usDestWidth;
	uint16_t usDestHeight;

	int ret;
	uint32_t temp;
	int i;

	int overlay_on;

	/* to detect if the overlay plane is on or off */
	overlay_on = PSB_READ32(OVADD+0x168) & 1;
	
	/* figure out the LVDS physical resolution limit, i.e. the
	   overlay plane destination mode */
	/* XXX better to do this in LVDS initialize phase */
	for (i = 0; i < xf86_config->num_output; i++) {
	    xf86OutputPtr output = xf86_config->output[i];
	    PsbLVDSOutputPtr pLVDS = NULL;
	    DisplayModePtr pFixedMode = NULL;
	    pLVDS = containerOf(output->driver_private, PsbLVDSOutputRec, psbOutput);

	    if (pLVDS->psbOutput.type != PSB_OUTPUT_LVDS)
		continue;	    
	    
	    pFixedMode = pLVDS->panelFixedMode;
	    
	    /* Overlay plane only supports source smaller than
	       1024x768, therefore the destination should also smaller
	       than 1024x768 */
	    if (pFixedMode->HDisplay > 1024 || pFixedMode->VDisplay > 768) {
		    /* We should not just set destination resolution
		       as 1024x768, otherwise the overlay plane out
		       put will be mixed with the plane B output,
		       which is larger than 1024x768 and can not be
		       covered. */
		    /*In this case, actually the overlay plane should
		       never ever be turned on, so we don't need to
		       worry about turnning it off here. It's ok to
		       just return */
		    return;
	    } else {
		    usDestWidth = pFixedMode->HDisplay;
		    usDestHeight = pFixedMode->VDisplay;
	    }
	}
	
	/* figure out the source plane resolution, i.e. the source
	   buffer resolution */
	temp = PSB_READ32(DSPBSIZE);
	usSrcWidth = (uint16_t) ( temp & 0xffff);
	usSrcHeight = (uint16_t) (temp >> 16) & 0xffff;

	/* being called when SDVO is updating its stride */
	if (pipe == 0) {
		/* should update the stride */
		if (overlay_on)
			goto out_update;
		else
			return;
	}

	/* should not turn on/off if is already on/off */
	/* XXX what if Dpms forgets to turn the overlay plane off when
	   exiting the xserver, and forgets to turn it off when
	   entering the xserver? */
	if (turnon && overlay_on) {
		/* overlay plane is already turned on, don't do it
		   twice */
		return;
	} else if (!turnon && !overlay_on) {
		/* overlay plane is already turnon off, don't do it
		   twice */
		return;
	}

	if (!turnon)
		goto out_turnoff;

        /* shouldn't use overlay unless we need to downscale*/
	if (!(usSrcWidth > usDestWidth || usSrcHeight > usDestHeight)) {
		/* the LVDS mode will be set to equal or smaller than
		   the physical limite, in this case, we should
		   disable the overlay plane */
		if (overlay_on)
			goto out_turnoff;
		else
			return;
	}

 out_turnon:
 out_update:

	psb_overlay_setup_reglist(crtc, &overlay_reglist, TRUE, 
				  usSrcWidth, usSrcHeight, usDestWidth, usDestHeight);
	psb_overlay_write_reglist(crtc, &overlay_reglist, TRUE);

	pCrtc->downscale = 1;
	pCrtc->scale_x = 1.0 * usSrcWidth / usDestWidth;
	pCrtc->scale_y = 1.0 * usSrcHeight / usDestHeight;

	return;

 out_turnoff:

	psb_overlay_setup_reglist(crtc, &overlay_reglist, FALSE, 
				  usSrcWidth, usSrcHeight, usDestWidth, usDestHeight);
	psb_overlay_write_reglist(crtc, &overlay_reglist, FALSE);

	pCrtc->downscale = 0;
	pCrtc->scale_x = 1;
	pCrtc->scale_y = 1;
	
	return;
}

