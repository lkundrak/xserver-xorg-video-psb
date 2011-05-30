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

#ifndef   	PSB_OVERLAY_H_
#define   	PSB_OVERLAY_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include "psb_driver.h"
#include "xf86Crtc.h"

typedef int8_t     bool;

typedef struct OV_COEFF_t
{
    uint16_t mantissa   : 12,
    exponent        :  3,
    sign            :  1;
} OV_COEFF, *POV_COEFF; 


typedef struct _OVERLAY_REGS{
    union	/* 0x00*/
    {
        uint32_t Buffer0YPtr; /* Y/RGB data base address*/
        uint32_t OBUF_0Y;
    };

    union	/* 0x04*/
    {
        uint32_t Buffer1YPtr;
        uint32_t OBUF_1Y;
    };

    union	/* 0z08*/
    {
        uint32_t Buffer0UPtr; /* Planar U data base address*/
        uint32_t OBUF_0U;
    };

    union	/* 0x0C*/
    {
        uint32_t Buffer0VPtr; /*Planar V data base address*/
        uint32_t OBUF_0V;
    };

    union
    {
        uint32_t Buffer1UPtr;
        uint32_t OBUF_1U;
    };

    union
    {
        uint32_t Buffer1VPtr;
        uint32_t OBUF_1V;
    };

    union
    {
        struct 
        {
            uint16_t Y_Stride;
            uint16_t UV_Stride;
        } Stride;
		uint32_t OSTRIDE;
    };
    union
    {
        struct 
        {
            uint16_t Field0;
            uint16_t Field1;
        } Y_VPhase;
        uint32_t YRGB_VPH;
    };

    union
    {
        struct 
        {
            uint16_t Field0;
            uint16_t Field1;
        } UV_VPhase;
        uint32_t UV_VPH; 
    };

    union
    {
        struct 
        {
            uint16_t Y_Phase;
            uint16_t UV_Phase;
        } HPhase;
        uint32_t dwHPhase;
    };

    uint32_t   InitPhase;

    union
    {
        struct 
        {
            uint16_t Left;
            uint16_t Top;
        } DestPosition;
        uint32_t   dwDestPosition;
    };
    union
    {
        struct 
        {
            uint16_t Width;
            uint16_t Height;
        } DestSize;
        uint32_t   dwDestSize;  
    };
    union
    {
        struct 
        {
            uint16_t Y_Width;
            uint16_t UV_Width;
        } SrcWidth;
        uint32_t   dwSrcWidth;
    };
    union
    {
        struct 
        {
            uint16_t Y_Width;
            uint16_t UV_Width;
        } SrcSWORDWidth;
        uint32_t   dwSrcSWORDWidth;
    };
    union
    {
        struct 
        {
            uint16_t Y_Height;
            uint16_t UV_Height;
        } SrcHeight;
        uint32_t   dwSrcHeight;
    };
    uint32_t  YRGBScale;
    uint32_t  UV_Scale;
    uint32_t  CCorrection0;
    uint32_t  CCorrection1;
    uint32_t  DestCKey;
    uint32_t  DestCMask;
    uint32_t  SrcCKeyHi;
    uint32_t  SrcCKeyLow;
    uint32_t  SrcCMask;
    uint32_t  Config;
    uint32_t  Command;
    uint32_t  Reserved1;
    union
    {
        struct 
        {
            uint16_t Left;
            uint16_t Top;
        } AlphaWinPos;
        uint32_t   dwAlphaWinPos;
        uint32_t   OSTART_0Y; /* (Gen4): Y data base address*/
    };
    union
    {
        struct 
        {
            uint16_t Width;
            uint16_t Height;
        } AlphaWinSize;
        uint32_t   dwAlphaWinSize;  
        uint32_t   OSTART_1Y;
    };
    uint32_t  OSTART_0U; /* (Gen4): Planar U data base address*/
    uint32_t  OSTART_0V; /* (Gen4): Planar V data base address*/
    uint32_t  OSTART_1U;
    uint32_t  OSTART_1V;
    union
    {
        struct 
        {
            uint16_t x;
            uint16_t y;
        } TileOffset0Y;
        uint32_t OTILEOFF_0Y; /* (Gen4, tiled memory): (y, x) coordinate for start of Y data*/
    };
    union
    {
        struct 
        {
            uint16_t x;
            uint16_t y;
        } TileOffset1Y;
        uint32_t OTILEOFF_1Y;
    };
    union
    {
        struct 
        {
            uint16_t x;
            uint16_t y;
        } TileOffset0U;
        uint32_t OTILEOFF_0U; /* (Gen4, tiled memory): (y, x) coordinate for start of U data*/
    };
    union
    {
        struct 
        {
            uint16_t x;
            uint16_t y;
        } TileOffset0V;
        uint32_t OTILEOFF_0V; /* (Gen4, tiled memory): (y, x) coordinate for start of V data*/
    };
    union
    {
        struct 
        {
            uint16_t x;
            uint16_t y;
        } TileOffset1U;
        uint32_t OTILEOFF_1U;
    };
    union
    {
        struct 
        {
            uint16_t x;
            uint16_t y;
        } TileOffset1V;
        uint32_t OTILEOFF_1V;
    };
    uint32_t  FHDScale;
    uint32_t  VDScale;
    uint32_t  Reserved12[86];
    union
    {
        OV_COEFF    VYCoeff[52]; /*offset 0x200*/
        uint32_t       dwVYCoeff[26]; /*3*17/2 + 1*/
    };
    uint32_t   Reserved13[38];
    union
    {
        OV_COEFF    HYCoeff[86]; /*offset 0x300*/
        uint32_t       dwHYCoeff[43]; /*5*17/2 + 1*/
    };
    uint32_t   Reserved14[85];
    union
    {
        OV_COEFF    VUVCoeff[52]; /*offset 0x500*/
        uint32_t       dwVUVCoeff[26]; /*3*17/2 + 1*/
    };
    uint32_t   Reserved15[38];
    union
    {
        OV_COEFF    HUVCoeff[52]; /*offset 0x600*/
        uint32_t       dwHUVCoeff[26]; /*3*17/2 + 1*/
    };
    uint32_t   Reserved16[38];
	uint32_t	Reserved17[576];	/* offset 0x700 - 0xFFF (4K)*/

} OVERLAY_REGS, *POVERLAY_REGS;

#define BIT0  0x00000001
#define BIT1  0x00000002
#define BIT2  0x00000004
#define BIT3  0x00000008

extern void psb_dpms_overlay(xf86CrtcPtr crtc, int turnon);

#if 0
#define over_msg(fmt, arg...) do { fprintf(stderr, "overlay: " fmt, ##arg);} while(0)
#else
#define over_msg(msg) do {} while(0)
#endif

#endif 	    /* !PSB_OVERLAY_H_ */
