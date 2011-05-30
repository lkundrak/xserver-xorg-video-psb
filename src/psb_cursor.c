/**************************************************************************

Copyright 1998-1999 Precision Insight, Inc., Cedar Park, Texas.
Copyright © 2002 David Dawes
All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sub license, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice (including the
next paragraph) shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

**************************************************************************/
/*
 * Authors:
 *   Keith Whitwell <keith@tungstengraphics.com>
 *   David Dawes <dawes@xfree86.org>
 *
 * Updated for Dual Head capabilities:
 *   Alan Hourihane <alanh@tungstengraphics.com>
 *
 * Add ARGB HW cursor support:
 *   Alan Hourihane <alanh@tungstengraphics.com>
 *
 * Poulsbo port
 *   Thomas Hellstrom <thomas-at-tungstengraphics-dot-com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <psb_reg.h>
#include "xf86.h"
#include "xf86_OSproc.h"
#include "compiler.h"

#include "psb_driver.h"
#include "i810_reg.h"

#define I810_CURSOR_X 64
#define I810_CURSOR_Y I810_CURSOR_X

static void
psbSetPipeCursorBase(xf86CrtcPtr crtc)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    PsbCrtcPrivatePtr intel_crtc = crtc->driver_private;
    int pipe = intel_crtc->pipe;
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);
    int cursor_base;

    //PSB_DEBUG(pScrn->scrnIndex, 3, "i830_psbSetPipeCursorBase\n");

    cursor_base = (pipe == 0) ? CURSOR_A_BASE : CURSOR_B_BASE;

    if (intel_crtc->cursor_is_argb)
	PSB_WRITE32(cursor_base, intel_crtc->cursor_argb_addr);
    else
	PSB_WRITE32(cursor_base, intel_crtc->cursor_addr);
}

void
psbInitHWCursor(ScrnInfoPtr pScrn)
{
    PsbPtr pPsb = psbPTR(pScrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);
    CARD32 temp;
    int i;

    //PSB_DEBUG(pScrn->scrnIndex, 3, "i830_psbInitHWCursor\n");

    /* Initialise the HW cursor registers, leaving the cursor hidden. */
    for (i = 0; i < pPsb->numCrtcs; i++) {
	int cursor_control = i == 0 ? CURSOR_A_CONTROL : CURSOR_B_CONTROL;

	temp = PSB_READ32(cursor_control);
	temp &= ~(CURSOR_MODE | MCURSOR_GAMMA_ENABLE |
		  MCURSOR_MEM_TYPE_LOCAL | MCURSOR_PIPE_SELECT);
	temp |= (i << 28);
	temp |= CURSOR_MODE_DISABLE;

	/* Need to set control, then address. */
	PSB_WRITE32(cursor_control, temp);
	psbSetPipeCursorBase(pPsb->crtcs[i]);
    }
}

Bool
psbCursorInit(ScreenPtr pScreen)
{
    //PSB_DEBUG(0, 3, "i830_psbCursorInit\n");
    return xf86_cursors_init(pScreen, I810_CURSOR_X, I810_CURSOR_Y,
			     (HARDWARE_CURSOR_TRUECOLOR_AT_8BPP |
			      HARDWARE_CURSOR_BIT_ORDER_MSBFIRST |
			      HARDWARE_CURSOR_INVERT_MASK |
			      HARDWARE_CURSOR_SWAP_SOURCE_AND_MASK |
			      HARDWARE_CURSOR_AND_SOURCE_WITH_MASK |
			      HARDWARE_CURSOR_SOURCE_MASK_INTERLEAVE_64 |
			      HARDWARE_CURSOR_ARGB));
}

void
psb_crtc_load_cursor_image(xf86CrtcPtr crtc, unsigned char *src)
{
    PsbCrtcPrivatePtr intel_crtc = crtc->driver_private;
    CARD8 *pcurs;

    //PSB_DEBUG(crtc->scrn->scrnIndex, 3, "i830_psb_crtc_load_cursor_image\n");

    pcurs =
	(CARD8 *) mmBufVirtual(intel_crtc->cursor) +
	intel_crtc->cursor_offset;

    intel_crtc->cursor_is_argb = FALSE;
    memcpy(pcurs, src, I810_CURSOR_X * I810_CURSOR_Y / 4);
}

void
psb_crtc_load_cursor_argb(xf86CrtcPtr crtc, CARD32 * image)
{
    PsbCrtcPrivatePtr intel_crtc = crtc->driver_private;
    CARD32 *pcurs;

    //PSB_DEBUG(crtc->scrn->scrnIndex, 3, "i830_psb_crtc_load_cursor_argb\n");

    pcurs = (CARD32 *) ((unsigned long)mmBufVirtual(intel_crtc->cursor) +
			intel_crtc->cursor_argb_offset);

    intel_crtc->cursor_is_argb = TRUE;
    memcpy(pcurs, image, I810_CURSOR_Y * I810_CURSOR_X * 4);
}

void
psb_crtc_set_cursor_position(xf86CrtcPtr crtc, int x, int y)
{
    ScrnInfoPtr scrn = crtc->scrn;
    PsbPtr pPsb = psbPTR(scrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);
    PsbCrtcPrivatePtr intel_crtc = psbCrtcPrivate(crtc);
    CARD32 temp;
    unsigned long dspbpos = 0;

    //PSB_DEBUG(crtc->scrn->scrnIndex, 3,
	//      "i830_psb_crtc_set_cursor_position\n");

    dspbpos = PSB_READ32(DSPBPOS);
    x += dspbpos & 0xfff;
    y += (dspbpos >> 16) & 0xfff;

    temp = 0;
    if (x < 0) {
	temp |= (CURSOR_POS_SIGN << CURSOR_X_SHIFT);
	x = -x;
    }
    if (y < 0) {
	temp |= (CURSOR_POS_SIGN << CURSOR_Y_SHIFT);
	y = -y;
    }

    /* downscaling only applies to pipe B */
    if (intel_crtc->downscale && intel_crtc->pipe == 1) {
	    if (intel_crtc->scale_x > 1) 
		    x = x / intel_crtc->scale_x;
	    if (intel_crtc->scale_y > 1) 
		    y = y / intel_crtc->scale_y;
    }

    temp |= ((x & CURSOR_POS_MASK) << CURSOR_X_SHIFT);
    temp |= ((y & CURSOR_POS_MASK) << CURSOR_Y_SHIFT);

    switch (intel_crtc->pipe) {
    case 0:
	PSB_WRITE32(CURSOR_A_POSITION, temp);
	break;
    case 1:
	PSB_WRITE32(CURSOR_B_POSITION, temp);
	break;
    }

    if (crtc->cursor_shown)
	psbSetPipeCursorBase(crtc);
}

void
psb_crtc_show_cursor(xf86CrtcPtr crtc)
{
    ScrnInfoPtr scrn = crtc->scrn;
    PsbPtr pPsb = psbPTR(scrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);
    PsbCrtcPrivatePtr intel_crtc = psbCrtcPrivate(crtc);
    int pipe = intel_crtc->pipe;
    CARD32 temp;
    int cursor_control = (pipe == 0 ? CURSOR_A_CONTROL : CURSOR_B_CONTROL);

    //PSB_DEBUG(crtc->scrn->scrnIndex, 3, "i830_psb_crtc_show_cursor\n");

    temp = PSB_READ32(cursor_control);
    temp &= ~(CURSOR_MODE | MCURSOR_PIPE_SELECT);
    if (intel_crtc->cursor_is_argb)
	temp |= CURSOR_MODE_64_ARGB_AX | MCURSOR_GAMMA_ENABLE;
    else
	temp |= CURSOR_MODE_64_4C_AX;

    temp |= (pipe << 28);	       /* Connect to correct pipe */

    /* Need to set mode, then address. */
    PSB_WRITE32(cursor_control, temp);
    psbSetPipeCursorBase(crtc);
}

void
psb_crtc_hide_cursor(xf86CrtcPtr crtc)
{
    ScrnInfoPtr scrn = crtc->scrn;
    PsbPtr pPsb = psbPTR(scrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);
    PsbCrtcPrivatePtr intel_crtc = psbCrtcPrivate(crtc);
    int pipe = intel_crtc->pipe;
    CARD32 temp;
    int cursor_control = (pipe == 0 ? CURSOR_A_CONTROL : CURSOR_B_CONTROL);

    //PSB_DEBUG(crtc->scrn->scrnIndex, 3, "i830_psb_crtc_hide_cursor\n");

    temp = PSB_READ32(cursor_control);
    temp &= ~(CURSOR_MODE | MCURSOR_GAMMA_ENABLE);
    temp |= CURSOR_MODE_DISABLE;
    temp &= ~(CURSOR_ENABLE | CURSOR_GAMMA_ENABLE);

    /* Need to set mode, then address. */
    PSB_WRITE32(cursor_control, temp);
    psbSetPipeCursorBase(crtc);
}

void
psb_crtc_set_cursor_colors(xf86CrtcPtr crtc, int bg, int fg)
{
    ScrnInfoPtr scrn = crtc->scrn;
    PsbPtr pPsb = psbPTR(scrn);
    PsbDevicePtr pDevice = psbDevicePTR(pPsb);
    PsbCrtcPrivatePtr intel_crtc = psbCrtcPrivate(crtc);
    int pipe = intel_crtc->pipe;
    int pal0 = pipe == 0 ? CURSOR_A_PALETTE0 : CURSOR_B_PALETTE0;

    //PSB_DEBUG(crtc->scrn->scrnIndex, 3, "i830_psb_crtc_set_cursor_colors\n");

    PSB_WRITE32(pal0 + 0, bg & 0x00ffffff);
    PSB_WRITE32(pal0 + 4, fg & 0x00ffffff);
    PSB_WRITE32(pal0 + 8, fg & 0x00ffffff);
    PSB_WRITE32(pal0 + 12, bg & 0x00ffffff);
}
