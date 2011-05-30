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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xf86.h"
#include "xf86_OSproc.h"
#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 6
#include "xf86Resources.h"
#endif
#include "compiler.h"

#include "xf86xv.h"
#include <X11/extensions/Xv.h>

#include "psb_driver.h"
#include "fourcc.h"
#include "regionstr.h"
#include "../libmm/mm_interface.h"
#include "psb_drm.h"
#include "Xpsb.h"

#define MAKE_ATOM(a) MakeAtom(a, sizeof(a) - 1, TRUE)

#define PSB_IMAGE_MAX_WIDTH		2047
#define PSB_IMAGE_MAX_HEIGHT	        2047
#define PSB_HDTV_LIMIT_X                1280
#define PSB_HDTV_LIMIT_Y                720

/*
 * YUV to RBG conversion.
 */

static float yOffset = 16.f;
static float yRange = 219.f;
static float videoGamma = 2.8f;

/*
 * The ITU-R BT.601 conversion matrix for SDTV.
 */

static float bt_601[] = {
    1.0, 0.0, 1.4075,
    1.0, -0.3455, -0.7169,
    1.0, 1.7790, 0.
};

/*
 * The ITU-R BT.709 conversion matrix for HDTV.
 */

static float bt_709[] = {
    1.0, 0.0, 1.581,
    1.0, -0.1881, -0.47,
    1.0, 1.8629, 0.
};

#ifndef exaMoveInPixmap
void exaMoveInPixmap(PixmapPtr pPixmap);
#endif

static XF86VideoEncodingRec DummyEncoding[1] = {
    {
     0,
     "XV_IMAGE",
     PSB_IMAGE_MAX_WIDTH, PSB_IMAGE_MAX_HEIGHT,
     {1, 1}
     }
};

#define PSB_NUM_FORMATS 3
static XF86VideoFormatRec Formats[PSB_NUM_FORMATS] = {
    {15, TrueColor}, {16, TrueColor}, {24, TrueColor}
};

static XF86AttributeRec Attributes[] = {
    /* {XvSettable | XvGettable, 0, (1 << 24) - 1, "XV_COLORKEY"}, */
    {XvSettable | XvGettable, -50, 50, "XV_BRIGHTNESS"},
    {XvSettable | XvGettable, -30, 30, "XV_HUE"},
    {XvSettable | XvGettable, 0, 200, "XV_SATURATION"},
    {XvSettable | XvGettable, -100, 100, "XV_CONTRAST"},
};

#define PSB_NUM_ATTRIBUTES sizeof(Attributes)/sizeof(XF86AttributeRec)

/*
 * FOURCC definitions
 */
#define FOURCC_NV12     (('2' << 24) + ('1' << 16) + ('V' << 8) + 'N')

#define XVIMAGE_NV12 \
   { \
	FOURCC_NV12, \
        XvYUV, \
	LSBFirst, \
	{'N','V','1','2', \
	  0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
	12, \
	XvPlanar, \
	2, \
	0, 0, 0, 0, \
	8, 8, 8, \
	1, 2, 2, \
	1, 2, 2, \
	{'Y','U','V', \
	  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
	XvTopToBottom \
   }

static XF86ImageRec Images[] = {
    XVIMAGE_UYVY,
    XVIMAGE_YUY2,
    XVIMAGE_YV12,
    XVIMAGE_I420,
    XVIMAGE_NV12,
};

#define PSB_NUM_IMAGES sizeof(Images)/sizeof(XF86ImageRec)

/*
 * Currently we have 4 ports on the adaptor.
 * We might want to make this a configurable option.
 */

#define PSB_ADAPT0_NUM_PORTS 4

/*define some structure used by the ported code  */

static Atom xvBrightness;
static Atom xvContrast;
static Atom xvSaturation;
static Atom xvHue;

#define HUE_DEFAULT_VALUE   0
#define HUE_MIN            -30
#define HUE_MAX             30

#define BRIGHTNESS_DEFAULT_VALUE   0
#define BRIGHTNESS_MIN            -50
#define BRIGHTNESS_MAX             50

#define CONTRAST_DEFAULT_VALUE     0
#define CONTRAST_MIN              -100
#define CONTRAST_MAX               100

#define SATURATION_DEFAULT_VALUE   100
#define SATURATION_MIN             0
#define SATURATION_MAX             200

#define CLAMP_ATTR(a,max,min) (a>max?max:(a<min?min:a))

typedef enum _psb_nominalrange
{
    PSB_NominalRangeMask = 0x07,
    PSB_NominalRange_Unknown = 0,
    PSB_NominalRange_Normal = 1,
    PSB_NominalRange_Wide = 2,
    /* explicit range forms */
    PSB_NominalRange_0_255 = 1,
    PSB_NominalRange_16_235 = 2,
    PSB_NominalRange_48_208 = 3
} psb_nominalrange;

typedef enum _psb_videotransfermatrix
{
    PSB_VideoTransferMatrixMask = 0x07,
    PSB_VideoTransferMatrix_Unknown = 0,
    PSB_VideoTransferMatrix_BT709 = 1,
    PSB_VideoTransferMatrix_BT601 = 2,
    PSB_VideoTransferMatrix_SMPTE240M = 3
} psb_videotransfermatrix;

typedef struct _psb_fixed32
{
    union
    {
	struct
	{
	    unsigned short Fraction;
	    short Value;
	};
	long ll;
    };
} psb_fixed32;

#define Degree (2*PI / 360.0)
#define PI 3.1415927

typedef struct _psb_transform_coeffs_
{
    double rY, rCb, rCr;
    double gY, gCb, gCr;
    double bY, bCb, bCr;
} psb_transform_coeffs;

typedef struct _psb_coeffs_
{
    signed char rY;
    signed char rU;
    signed char rV;
    signed char gY;
    signed char gU;
    signed char gV;
    signed char bY;
    signed char bU;
    signed char bV;
    unsigned char rShift;
    unsigned char gShift;
    unsigned char bShift;
    signed short rConst;
    signed short gConst;
    signed short bConst;
} psb_coeffs_s, *psb_coeffs_p;

typedef struct _PsbPortPrivRec
{
    RegionRec clip;
    struct _MMManager *man;
    struct _MMBuffer *videoBuf[2];
    int curBuf;
    int videoBufSize;
    float conversionData[11];
    Bool hdtv;
    XpsbSurface srf[3][2];
    XpsbSurface dst;
    unsigned int bufPitch;

    /* information of display attribute */
    psb_fixed32 brightness;
    psb_fixed32 contrast;
    psb_fixed32 saturation;
    psb_fixed32 hue;

    /* gets set by any changes to Hue, Brightness,saturation, hue or csc matrix. */
    psb_coeffs_s coeffs;
    unsigned long sgx_coeffs[9];

    unsigned int src_nominalrange;
    unsigned int dst_nominalrange;
    unsigned int video_transfermatrix;
} PsbPortPrivRec, *PsbPortPrivPtr;

static void
psbSetupConversionData(PsbPortPrivPtr pPriv, Bool hdtv)
{
    if (pPriv->hdtv != hdtv) {
	int i;

	if (hdtv)
	    memcpy(pPriv->conversionData, bt_709, sizeof(bt_709));
	else
	    memcpy(pPriv->conversionData, bt_601, sizeof(bt_601));

	for (i = 0; i < 9; ++i) {
	    pPriv->conversionData[i] /= yRange;
	}

	/*
	 * Adjust for brightness, contrast, hue and saturation here.
	 */

	pPriv->conversionData[9] = -yOffset;
	/*
	 * Not used ATM
	 */
	pPriv->conversionData[10] = videoGamma;
	pPriv->hdtv = hdtv;
    }
}

static void psb_setup_coeffs(PsbPortPrivPtr);
static void psb_pack_coeffs(PsbPortPrivPtr, unsigned long *);

static void
psbSetupPlanarConversionData(PsbPortPrivPtr pPriv, Bool hdtv)
{
    psb_setup_coeffs(pPriv);
    psb_pack_coeffs(pPriv, &pPriv->sgx_coeffs[0]);
}

static void psbStopVideo(ScrnInfoPtr pScrn, pointer data, Bool shutdown);

static PsbPortPrivPtr
psbPortPrivCreate(ScrnInfoPtr pScrn)
{
    PsbDevicePtr pDevice = psbDevicePTR(psbPTR(pScrn));
    PsbPortPrivPtr pPriv;

    pPriv = xcalloc(1, sizeof(*pPriv));
    if (!pPriv)
	return NULL;
    pPriv->man = pDevice->man;
    REGION_NULL(pScreen, &pPriv->clip);
    pPriv->hdtv = TRUE;
    psbSetupConversionData(pPriv, FALSE);

    /* coeffs defaut value */
    pPriv->brightness.Value = BRIGHTNESS_DEFAULT_VALUE;
    pPriv->brightness.Fraction = 0;

    pPriv->contrast.Value = CONTRAST_DEFAULT_VALUE;
    pPriv->contrast.Fraction = 0;
    pPriv->hue.Value = HUE_DEFAULT_VALUE;
    pPriv->hue.Fraction = 0;
    pPriv->saturation.Value = SATURATION_DEFAULT_VALUE;
    pPriv->saturation.Fraction = 0;

    psbSetupPlanarConversionData(pPriv, FALSE);

    return pPriv;
}

static void
psbPortPrivDestroy(ScrnInfoPtr pScrn, PsbPortPrivPtr pPriv)
{
    if (!pPriv)
	return;

    psbStopVideo(pScrn, pPriv, TRUE);

    if (pPriv->videoBuf[0]) {
	mmBufDestroy(pPriv->videoBuf[0]);
	mmBufDestroy(pPriv->videoBuf[1]);
    }
    xfree(pPriv);
}

static int
psbCheckVideoBuffer(PsbPortPrivPtr pPriv, unsigned int size)
{
    size = ALIGN_TO(size, 4096);

    if (pPriv->videoBuf[0] && pPriv->videoBufSize != size) {
	mmBufDestroy(pPriv->videoBuf[0]);
	mmBufDestroy(pPriv->videoBuf[1]);
	pPriv->videoBuf[0] = NULL;
	pPriv->videoBuf[1] = NULL;
    }
    if (!pPriv->videoBuf[0]) {
	pPriv->videoBuf[0] = pPriv->man->createBuf(pPriv->man,
						   size,
						   0,
						   DRM_PSB_FLAG_MEM_MMU |
						   DRM_BO_FLAG_READ,
						   DRM_BO_HINT_DONT_FENCE);
	if (!pPriv->videoBuf[0])
	    return BadAlloc;

	pPriv->videoBuf[1] = pPriv->man->createBuf(pPriv->man,
						   size,
						   0,
						   DRM_PSB_FLAG_MEM_MMU |
						   DRM_BO_FLAG_READ,
						   DRM_BO_HINT_DONT_FENCE);
	if (!pPriv->videoBuf[1]) {
	    mmBufDestroy(pPriv->videoBuf[0]);
	    pPriv->videoBuf[0] = NULL;
	    return BadAlloc;
	}

	pPriv->videoBufSize = size;
	pPriv->srf[0][0].buffer = mmKernelBuf(pPriv->videoBuf[0]);
	pPriv->srf[0][0].offset = 0;
	pPriv->srf[0][1].buffer = mmKernelBuf(pPriv->videoBuf[1]);
	pPriv->srf[0][1].offset = 0;
    }
    return Success;
}

static int
psbSetPortAttribute(ScrnInfoPtr pScrn,
		    Atom attribute, INT32 value, pointer data)
{
    PsbPortPrivPtr pPriv = (PsbPortPrivPtr) data;
    int update_coeffs = 0;

    if (attribute == xvBrightness) {
	pPriv->brightness.Value =
	    CLAMP_ATTR(value, BRIGHTNESS_MAX, BRIGHTNESS_MIN);
	update_coeffs = 1;
    } else if (attribute == xvContrast) {
	pPriv->contrast.Value = CLAMP_ATTR(value, CONTRAST_MAX, CONTRAST_MIN);
	update_coeffs = 1;
    } else if (attribute == xvHue) {
	pPriv->hue.Value = CLAMP_ATTR(value, HUE_MAX, HUE_MIN);
	update_coeffs = 1;
    } else if (attribute == xvSaturation) {
	pPriv->saturation.Value =
	    CLAMP_ATTR(value, SATURATION_MAX, SATURATION_MIN);
	update_coeffs = 1;
    } else
	return BadValue;

    if (update_coeffs) {
	psb_setup_coeffs(pPriv);
	psb_pack_coeffs(pPriv, &pPriv->sgx_coeffs[0]);
	update_coeffs = 0;
    }

    return Success;
}

static int
psbGetPortAttribute(ScrnInfoPtr pScrn,
		    Atom attribute, INT32 * value, pointer data)
{
    PsbPortPrivPtr pPriv = (PsbPortPrivPtr) data;

    if (attribute == xvBrightness)
	*value = pPriv->brightness.Value;
    else if (attribute == xvContrast)
	*value = pPriv->contrast.Value;
    else if (attribute == xvHue)
	*value = pPriv->hue.Value;
    else if (attribute == xvSaturation)
	*value = pPriv->saturation.Value;
    else
	return BadValue;

    return Success;
}

static void
psbQueryBestSize(ScrnInfoPtr pScrn,
		 Bool motion,
		 short vid_w, short vid_h,
		 short drw_w, short drw_h,
		 unsigned int *p_w, unsigned int *p_h, pointer data)
{
    if (vid_w > (drw_w << 1))
	drw_w = vid_w >> 1;
    if (vid_h > (drw_h << 1))
	drw_h = vid_h >> 1;

    *p_w = drw_w;
    *p_h = drw_h;
}

static void
psbCopyPackedData(ScrnInfoPtr pScrn, PsbPortPrivPtr pPriv,
		  unsigned char *buf,
		  int srcPitch, int dstPitch, int top, int left, int h, int w)
{
    unsigned char *src, *dst;
    int i;
    struct _MMBuffer *dstBuf = pPriv->videoBuf[pPriv->curBuf];

    src = buf + (top * srcPitch) + (left << 1);

    /*
     * Note. Map also syncs with previous usage.
     */

    dstBuf->man->mapBuf(dstBuf, MM_FLAG_WRITE, 0);
    dst = mmBufVirtual(dstBuf);
    w <<= 1;
    for (i = 0; i < h; i++) {
	memcpy(dst, src, w);
	src += srcPitch;
	dst += dstPitch;
    }
    dstBuf->man->unMapBuf(dstBuf);
}

static void
psbCopyPlanarYUVData(ScrnInfoPtr pScrn, PsbPortPrivPtr pPriv,
		     unsigned char *buf,
		     int srcPitch, int dstPitch, int dstPitch2,
		     int top, int left, int h, int w, int id)
{
    unsigned char *src_y, *src_u, *src_v, *dst_y, *dst_u, *dst_v;
    int i;
    struct _MMBuffer *dstBuf = pPriv->videoBuf[pPriv->curBuf];

    src_y = buf + (top * srcPitch) + left;
    if (id == FOURCC_YV12) {	       /* YUV */
	src_u = buf + srcPitch * h + top * (srcPitch >> 1) + (left >> 1);
	src_v = buf + srcPitch * h + (srcPitch >> 1) * (h >> 1)
	    + top * (srcPitch >> 1) + (left >> 1);
    } else {			       /* YVU */
	src_v = buf + srcPitch * h + top * (srcPitch >> 1) + (left >> 1);
	src_u = buf + srcPitch * h + (srcPitch >> 1) * (h >> 1)
	    + top * (srcPitch >> 1) + (left >> 1);
    }

    /*
     * Note. Map also syncs with previous usage.
     */

    dstBuf->man->mapBuf(dstBuf, MM_FLAG_WRITE, 0);

    /* dst always YUV, not YVU for I420 */
    dst_y = mmBufVirtual(dstBuf);
    dst_u = dst_y + dstPitch * h;
    dst_v = dst_u + dstPitch2 * (h >> 1);

    /* copy Y data */
    for (i = 0; i < h; i++) {
	memcpy(dst_y, src_y, w);
	src_y += srcPitch;
	dst_y += dstPitch;
    }

    /* copy UV data */
    srcPitch >>= 1;
    w >>= 1;
    h >>= 1;

    for (i = 0; i < h; i++) {
	memcpy(dst_u, src_u, w);
	src_u += srcPitch;
	dst_u += dstPitch2;
    }

    /* copy V data */
    for (i = 0; i < h; i++) {
	memcpy(dst_v, src_v, w);
	src_v += srcPitch;
	dst_v += dstPitch2;
    }

    dstBuf->man->unMapBuf(dstBuf);
}

static void
psbCopyPlanarNV12Data(ScrnInfoPtr pScrn, PsbPortPrivPtr pPriv,
		      unsigned char *buf,
		      int srcPitch, int dstPitch,
		      int top, int left, int h, int w)
{
    unsigned char *src_y, *src_uv, *dst_y, *dst_uv;
    int i;
    struct _MMBuffer *dstBuf = pPriv->videoBuf[pPriv->curBuf];

    src_y = buf + (top * srcPitch) + left;
    src_uv = buf + srcPitch * h + top * srcPitch + left;

    /*
     * Note. Map also syncs with previous usage.
     */

    dstBuf->man->mapBuf(dstBuf, MM_FLAG_WRITE, 0);

    dst_y = mmBufVirtual(dstBuf);
    dst_uv = dst_y + dstPitch * h;

    /* copy Y data */
    for (i = 0; i < h; i++) {
	memcpy(dst_y, src_y, w);
	src_y += srcPitch;
	dst_y += dstPitch;
    }

    /* copy UV data */
    h >>= 1;
    for (i = 0; i < h; i++) {
	memcpy(dst_uv, src_uv, w);
	src_uv += srcPitch;
	dst_uv += dstPitch;
    }

    dstBuf->man->unMapBuf(dstBuf);
}

int
psbDisplayVideo(ScrnInfoPtr pScrn, PsbPortPrivPtr pPriv, int id,
		RegionPtr dstRegion,
		short width, short height, int video_pitch,
		int x1, int y1, int x2, int y2,
		short src_w, short src_h, short drw_w, short drw_h,
		PixmapPtr pPixmap)
{
    struct _MMBuffer *dstBuf;
    BoxPtr pbox;
    int nbox;
    int dxo, dyo;
    unsigned long pre_add;
    Bool hdtv;
    XpsbSurface dst;
    XpsbSurfacePtr src[3];
    float tc0[6], tc1[6], tc2[6];
    int num_texture = 0;
    float *conversion_data = NULL;    

    hdtv = ((src_w >= PSB_HDTV_LIMIT_X) && (src_h >= PSB_HDTV_LIMIT_Y));

    src[0] = &pPriv->srf[0][pPriv->curBuf];
    src[0]->w = src_w;
    src[0]->h = src_h;
    src[0]->stride = video_pitch;
    src[0]->minFilter = Xpsb_nearest;
    src[0]->magFilter = Xpsb_linear;
    src[0]->uMode = Xpsb_clamp;
    src[0]->vMode = Xpsb_clamp;
    src[0]->texCoordIndex = 0;

    /* Use "isYUVPacked" to indicate:
     *   0: this surface is not for Xvideo
     *   1: is YUV data, and it is the first planar
     *   2: is YUV data, and it is the second planar
     *   3: is YUV data, and it is the third planar
     */
    src[0]->isYUVPacked = 1;

    /* pass id, ignore "packed" or "planar" */
    src[0]->packedYUVId = id;

    switch (id) {
    case FOURCC_UYVY:
    case FOURCC_YUY2:
	num_texture = 1;
	psbSetupConversionData(pPriv, hdtv);
	conversion_data = &pPriv->conversionData[0];
	break;
    case FOURCC_NV12:
	num_texture = 2;

	src[1] = &pPriv->srf[1][pPriv->curBuf];
	memcpy(src[1], src[0], sizeof(XpsbSurface));
	src[1]->h /= 2;
	src[1]->w /= 2;		       /* width will be used as stride in Xpsb */
	src[1]->stride = ALIGN_TO(src[1]->w, 32);
	src[1]->texCoordIndex = 1;
	src[1]->offset = src[0]->offset + video_pitch * src_h;
	src[1]->isYUVPacked = 2;

	conversion_data = (float *)(&pPriv->sgx_coeffs[0]);
	break;
    case FOURCC_YV12:
    case FOURCC_I420:
	num_texture = 3;

	src[1] = &pPriv->srf[1][pPriv->curBuf];
	memcpy(src[1], src[0], sizeof(XpsbSurface));
	src[1]->w /= 2;
	src[1]->h /= 2;
	src[1]->stride = ALIGN_TO(src[1]->w, 32);
	src[1]->texCoordIndex = 1;
	src[1]->offset = src[0]->offset + video_pitch * src_h;
	src[1]->isYUVPacked = 2;

	src[2] = &pPriv->srf[2][pPriv->curBuf];
	memcpy(src[2], src[1], sizeof(XpsbSurface));
	src[2]->texCoordIndex = 2;
	src[2]->offset = src[1]->offset + src[1]->stride * (src_h / 2);
	src[2]->isYUVPacked = 3;

	conversion_data = (float *)(&pPriv->sgx_coeffs[0]);
	break;
    default:
	break;
    }

    /* Set up the offset for translating from the given region (in screen
     * coordinates) to the backing pixmap.
     */

    while (!psbExaGetSuperOffset(pPixmap, &pre_add, &dstBuf))
	exaMoveInPixmap(pPixmap);

    dst.buffer = mmKernelBuf(dstBuf);
    dst.offset = pre_add;
    dst.stride = pPixmap->devKind;

    switch (pPixmap->drawable.depth) {
    case 15:
	dst.pictFormat = PICT_x1r5g5b5;
	break;
    case 16:
	dst.pictFormat = PICT_r5g6b5;
	break;
    case 24:
    case 32:
	dst.pictFormat = PICT_x8r8g8b8;
	break;
    default:
	return FALSE;
    }

    REGION_TRANSLATE(pScrn->pScreen, dstRegion, -pPixmap->screen_x,
		     -pPixmap->screen_y);

    dxo = dstRegion->extents.x1;
    dyo = dstRegion->extents.y1;

    pbox = REGION_RECTS(dstRegion);
    nbox = REGION_NUM_RECTS(dstRegion);

#ifdef PSB_DETEAR
    /* we don't support multiple box render detearing with temp buffer
       very well, have to fall back to original rendering */
    int fallback = (nbox>1);
#endif	/* PSB_DETEAR */

    while (nbox--) {
	int box_x1 = pbox->x1;
	int box_y1 = pbox->y1;
	int box_x2 = pbox->x2;
	int box_y2 = pbox->y2;

	tc0[0] = tc0[2] = tc0[4] = (double)(box_x1 - dxo) / (double)drw_w;	/* u0 */
	tc0[1] = tc0[3] = tc0[5] = (double)(box_y1 - dyo) / (double)drw_h;	/* v0 */

	tc1[0] = tc1[2] = tc1[4] = (double)(box_x2 - dxo) / (double)drw_w;	/* u1 */
	tc1[1] = tc1[3] = tc1[5] = tc0[1];	/* v0 */

	tc2[0] = tc2[2] = tc2[4] = tc0[0];	/* u0 */
	tc2[1] = tc2[3] = tc2[5] = (double)(box_y2 - dyo) / (double)drw_h;	/* v1 */

	dst.w = box_x2 - box_x1;
	dst.h = box_y2 - box_y1;
	dst.x = box_x1;
	dst.y = box_y1;

	pbox++;

#ifdef PSB_DETEAR
	PsbPtr pPsb = psbPTR(pScrn);

	if (pPsb->vsync && !fallback) {
		psbBlitYUVDetear(pScrn,
				 &dst,
				 &src[0],
				 num_texture,
				 TRUE,
				 src[0]->packedYUVId,
				 tc0, tc1, tc2, 
				 num_texture, 
				 conversion_data);
	} else
#endif	/* PSB_DETEAR */
		{
			psbBlitYUV(pScrn,
				   &dst,
				   &src[0],
				   num_texture,
				   TRUE,
				   src[0]->packedYUVId,
				   tc0, tc1, tc2, 
				   num_texture, 
				   conversion_data);			
		} 
    }

    DamageDamageRegion(&pPixmap->drawable, dstRegion);
    return TRUE;
}

/*
 * The source rectangle of the video is defined by (src_x, src_y, src_w, src_h).
 * The dest rectangle of the video is defined by (drw_x, drw_y, drw_w, drw_h).
 * id is a fourcc code for the format of the video.
 * buf is the pointer to the source data in system memory.
 * width and height are the w/h of the source data.
 * If "sync" is TRUE, then we must be finished with *buf at the point of return
 * (which we always are).
 * clipBoxes is the clipping region in screen space.
 * data is a pointer to our port private.
 * pDraw is a Drawable, which might not be the screen in the case of
 * compositing.  It's a new argument to the function in the 1.1 server.
 */

static int
psbPutImage(ScrnInfoPtr pScrn,
	    short src_x, short src_y,
	    short drw_x, short drw_y,
	    short src_w, short src_h,
	    short drw_w, short drw_h,
	    int id, unsigned char *buf,
	    short width, short height,
	    Bool sync, RegionPtr clipBoxes, pointer data, DrawablePtr pDraw)
{
    PsbPortPrivPtr pPriv = (PsbPortPrivPtr) data;
    ScreenPtr pScreen = screenInfo.screens[pScrn->scrnIndex];
    PixmapPtr pPixmap;
    INT32 x1, x2, y1, y2;
    int srcPitch, dstPitch, dstPitch2 = 0, destId;
    int size = 0;
    BoxRec dstBox;
    int ret;

    /* Clip */
    x1 = src_x;
    x2 = src_x + src_w;
    y1 = src_y;
    y2 = src_y + src_h;

    dstBox.x1 = drw_x;
    dstBox.x2 = drw_x + drw_w;
    dstBox.y1 = drw_y;
    dstBox.y2 = drw_y + drw_h;

    if (!xf86XVClipVideoHelper(&dstBox, &x1, &x2, &y1, &y2, clipBoxes,
			       width, height))
	return Success;

    destId = id;

    switch (id) {
    case FOURCC_UYVY:
    case FOURCC_YUY2:
	srcPitch = width << 1;
	/*
	 * Hardware limitation.
	 */
	dstPitch = ALIGN_TO(width, 32) << 1;
	size = dstPitch * height;
	break;
    case FOURCC_YV12:
    case FOURCC_I420:
	srcPitch = width;
	dstPitch = ALIGN_TO(width, 32);
	dstPitch2 = ALIGN_TO(width >> 1, 32);
	size = dstPitch * height + /* UV */ 2 * dstPitch2 * (height >> 1);
	break;
    case FOURCC_NV12:
	srcPitch = width;
	dstPitch = ALIGN_TO(width, 32);
	dstPitch2 = ALIGN_TO(width >> 1, 32);
	size = dstPitch * height + /* UV */ 2 * dstPitch2 * (height >> 1);
	break;
    default:
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "Unsupported Fourcc 0x%x\n", id);
	return BadValue;
    }

    ret = psbCheckVideoBuffer(pPriv, size);

    if (ret)
	return ret;

    switch (id) {
    case FOURCC_UYVY:
    case FOURCC_YUY2:
	psbCopyPackedData(pScrn, pPriv, buf, srcPitch, dstPitch,
			  src_y, src_x, height, width);
	break;

    case FOURCC_YV12:
    case FOURCC_I420:
	psbCopyPlanarYUVData(pScrn, pPriv, buf, srcPitch, dstPitch, dstPitch2,
			     src_y, src_x, height, width, id);
	break;
    case FOURCC_NV12:
	psbCopyPlanarNV12Data(pScrn, pPriv, buf, srcPitch, dstPitch,
			      src_y, src_x, height, width);
	break;
    default:
	break;
    }

    if (pDraw->type == DRAWABLE_WINDOW) {
	pPixmap = (*pScreen->GetWindowPixmap) ((WindowPtr) pDraw);
    } else {
	pPixmap = (PixmapPtr) pDraw;
    }


    psbDisplayVideo(pScrn, pPriv, destId, clipBoxes, width, height,
		    dstPitch, x1, y1, x2, y2,
		    src_w, src_h, drw_w, drw_h, pPixmap);

    pPriv->curBuf = (pPriv->curBuf + 1) & 1;
    return Success;
}

static void
psbStopVideo(ScrnInfoPtr pScrn, pointer data, Bool shutdown)
{
    PsbPortPrivPtr pPriv = (PsbPortPrivPtr) data;

    REGION_EMPTY(pScrn->pScreen, &pPriv->clip);
}

static int
psbQueryImageAttributes(ScrnInfoPtr pScrn,
			int id,
			unsigned short *w, unsigned short *h,
			int *pitches, int *offsets)
{
    int size;

    if (*w > PSB_IMAGE_MAX_WIDTH)
	*w = PSB_IMAGE_MAX_WIDTH;
    if (*h > PSB_IMAGE_MAX_HEIGHT)
	*h = PSB_IMAGE_MAX_HEIGHT;

    *w = (*w + 1) & ~1;
    if (offsets)
	offsets[0] = 0;

    switch (id) {
    case FOURCC_UYVY:
    case FOURCC_YUY2:
	size = *w << 1;
	if (pitches)
	    pitches[0] = size;
	size *= *h;
	break;
    case FOURCC_YV12:
    case FOURCC_I420:
	if (pitches) {
	    pitches[0] = (*w);
	    pitches[1] = (*w) >> 1;
	    pitches[2] = (*w) >> 1;
	}
	if (offsets) {
	    offsets[0] = 0;
	    offsets[1] = (*w) * (*h);
	    offsets[2] = offsets[1] + ((*w) >> 1) * ((*h) >> 1);
	}

	size =
	    (*w) * (*h) + ((*w) >> 1) * ((*h) >> 1) +
	    ((*w) >> 1) * ((*h) >> 1);
	break;
    case FOURCC_NV12:
	if (pitches) {
	    pitches[0] = (*w);
	    pitches[1] = (*w);
	}
	if (offsets) {
	    offsets[0] = 0;
	    offsets[1] = (*w) * (*h);
	}

	size = (*w) * (*h) + 2 * ((*w) >> 1) * ((*h) >> 1);
	break;
    default:			       /* YUY2 UYVY */
	size = *w << 1;
	if (pitches)
	    pitches[0] = size;
	size *= *h;
	break;
    }

    return size;
}

void
psbFreeAdaptor(ScrnInfoPtr pScrn, XF86VideoAdaptorPtr adapt)
{
    PsbPortPrivPtr pPriv;
    DevUnion *pDev;
    int i;

    if (!adapt)
	return;

    pDev = (DevUnion *) adapt->pPortPrivates;
    if (pDev) {
	for (i = 0; i < adapt->nPorts; ++i) {
	    pPriv = (PsbPortPrivPtr) pDev[i].ptr;
	    psbPortPrivDestroy(pScrn, pPriv);
	}
	xfree(pDev);
    }
    if (adapt->pAttributes)
	xfree(adapt->pAttributes);
    xfree(adapt);
}

static XF86VideoAdaptorPtr
psbSetupImageVideo(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    XF86VideoAdaptorPtr adapt;
    PsbPortPrivPtr pPriv;
    XF86AttributePtr att;
    int i;

    if (!(adapt = xcalloc(1, sizeof(XF86VideoAdaptorRec))))
	return NULL;

    adapt->type = XvWindowMask | XvInputMask | XvImageMask;
    adapt->flags = VIDEO_OVERLAID_IMAGES /*| VIDEO_CLIP_TO_VIEWPORT */ ;
    adapt->name = "Intel(R) Textured Overlay";
    adapt->nEncodings = 1;
    adapt->pEncodings = DummyEncoding;
    adapt->nFormats = PSB_NUM_FORMATS;
    adapt->pFormats = Formats;

    adapt->nAttributes = PSB_NUM_ATTRIBUTES;
    adapt->pAttributes =
	xcalloc(adapt->nAttributes, sizeof(XF86AttributeRec));
    /* Now copy the attributes */
    att = adapt->pAttributes;
    if (!att)
	goto out_err;

    memcpy((char *)att, (char *)Attributes,
	   sizeof(XF86AttributeRec) * PSB_NUM_ATTRIBUTES);

    adapt->nImages = PSB_NUM_IMAGES;
    adapt->pImages = Images;
    adapt->PutVideo = NULL;
    adapt->PutStill = NULL;
    adapt->GetVideo = NULL;
    adapt->GetStill = NULL;
    adapt->StopVideo = psbStopVideo;
    adapt->SetPortAttribute = psbSetPortAttribute;
    adapt->GetPortAttribute = psbGetPortAttribute;
    adapt->QueryBestSize = psbQueryBestSize;
    adapt->PutImage = psbPutImage;
    adapt->ReputImage = NULL;
    adapt->QueryImageAttributes = psbQueryImageAttributes;

    adapt->pPortPrivates = (DevUnion *)
	xcalloc(PSB_ADAPT0_NUM_PORTS, sizeof(DevUnion));

    if (!adapt->pPortPrivates)
	goto out_err;

    adapt->nPorts = 0;
    for (i = 0; i < PSB_ADAPT0_NUM_PORTS; ++i) {
	pPriv = psbPortPrivCreate(pScrn);
	if (!pPriv)
	    goto out_err;

	adapt->pPortPrivates[i].ptr = (pointer) pPriv;
	adapt->nPorts++;
    }

    return adapt;

  out_err:

    if (adapt->nPorts == 0)
	psbFreeAdaptor(pScrn, adapt);
    else
	return adapt;

    return NULL;
}

XF86VideoAdaptorPtr
psbInitVideo(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    XF86VideoAdaptorPtr *adaptors, *newAdaptors = NULL;
    XF86VideoAdaptorPtr adaptor = NULL;
    int num_adaptors;

    xvBrightness = MAKE_ATOM("XV_BRIGHTNESS");
    xvContrast = MAKE_ATOM("XV_CONTRAST");
    xvSaturation = MAKE_ATOM("XV_SATURATION");
    xvHue = MAKE_ATOM("XV_HUE");

    num_adaptors = xf86XVListGenericAdaptors(pScrn, &adaptors);

    newAdaptors = xalloc((num_adaptors + 1) * sizeof(XF86VideoAdaptorPtr *));
    if (newAdaptors == NULL)
	return NULL;

    memcpy(newAdaptors, adaptors, num_adaptors * sizeof(XF86VideoAdaptorPtr));
    adaptors = newAdaptors;

    adaptor = psbSetupImageVideo(pScreen);
    if (adaptor != NULL) {
	adaptors[num_adaptors++] = adaptor;
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Set up textured video\n");
    } else {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "Failed to set up textured video\n");
    }

    if (num_adaptors)
	xf86XVScreenInit(pScreen, adaptors, num_adaptors);

    xfree(adaptors);
    return adaptor;
}

/*
 * Calculates the coefficintes of a YUV->RGB conversion based on
 * the provided basis coefficients (already had HUe and Satu applied).
 * Performs brightness and contrast adjustment as well as the required
 * offsets to put into correct range for hardware conversion.
 */
static void
psb_create_coeffs(double yOff, double uOff, double vOff, double rgbOff,
		  double yScale, double uScale, double vScale,
		  double brightness, double contrast,
		  double *pYCoeff, double *pUCoeff, double *pVCoeff,
		  double *pConstant)
{
    *pYCoeff = yScale * contrast;
    *pUCoeff = uScale * contrast;
    *pVCoeff = vScale * contrast;

    *pConstant = (((yOff + brightness) * yScale)
		  + (uOff * uScale) + (vOff * vScale)) * contrast + rgbOff;
}

/**
 * Checks if the specified coefficients are within the ranges required
 * and returns true if they are else false.
 */
static int
psb_check_coeffs(double Ycoeff, double Ucoeff, double Vcoeff,
		 double ConstantTerm, signed char byShift)
{
    if ((Ycoeff > 127) || (Ycoeff < -128)) {
	return 1;
    }
    if ((Ucoeff > 127) || (Ucoeff < -128)) {
	return 1;
    }
    if ((Vcoeff > 127) || (Vcoeff < -128)) {
	return 1;
    }
    if ((ConstantTerm > 32766) || (ConstantTerm < -32767)) {
	return 1;
    }
    return 0;
}

/*
 * Converts a floating point function in the form
 *    a*yCoeff + b*uCoeff + c * vCoeff + d
 *  Into a fixed point function of the forrm
 *   (a*pY + b * pU + c * pV + constant)>>pShift
 */
static void
psb_convert_coeffs(double Ycoeff, double Ucoeff, double Vcoeff,
		   double ConstantTerm, signed char *pY, signed char *pU,
		   signed char *pV, signed short *constant,
		   unsigned char *pShift)
{
    *pShift = 0;

    Ycoeff *= 256;
    Ucoeff *= 256;
    Vcoeff *= 256;
    ConstantTerm *= 256;
    *pShift = 8;

    /*
     * What we want to do is scale up the coefficients so that they just fit into their
     * allowed bits, so we are using signed maths giving us coefficients can be between +-128.
     * The constant can be between =- 32767.
     * The divide can be between 0 and 256 (on powers of two only).
     * A mathematical approach would be nice, but for simplicity do an iterative compare
     * and divide. Until something fits.
     */
    while (psb_check_coeffs(Ycoeff, Ucoeff, Vcoeff, ConstantTerm, *pShift)) {
	Ycoeff /= 2;
	Ucoeff /= 2;
	Vcoeff /= 2;
	ConstantTerm /= 2;
	(*pShift)--;
    }
    *pY = (signed char)(Ycoeff + 0.5);
    *pU = (signed char)(Ucoeff + 0.5);
    *pV = (signed char)(Vcoeff + 0.5);
    *constant = (signed short)(ConstantTerm + 0.5);
}

/**
 * Performs a hue and saturation adjustment on the CSC coefficients supplied.
 */
static void
psb_transform_sathuecoeffs(psb_transform_coeffs * dest,
			   const psb_transform_coeffs * const source,
			   double fHue, double fSat)
{
    double fHueSatSin, fHueSatCos;

    fHueSatSin = sin(fHue) * fSat;
    fHueSatCos = cos(fHue) * fSat;

    dest->rY = source->rY;
    dest->rCb = source->rCb * fHueSatCos - source->rCr * fHueSatSin;
    dest->rCr = source->rCr * fHueSatCos + source->rCb * fHueSatSin;

    dest->gY = source->gY;
    dest->gCb = source->gCb * fHueSatCos - source->gCr * fHueSatSin;
    dest->gCr = source->gCr * fHueSatCos + source->gCb * fHueSatSin;

    dest->bY = source->bY;
    dest->bCb = source->bCb * fHueSatCos - source->bCr * fHueSatSin;
    dest->bCr = source->bCr * fHueSatCos + source->bCb * fHueSatSin;
}

/*
 * Scales the tranfer matrix depending on the input/output
 * nominal ranges.
 */
static void
psb_scale_transfermatrix(psb_transform_coeffs * transfer_matrix,
			 double YColumScale, double CbColumScale,
			 double CrColumnScale)
{
    /* First column of the transfer matrix */
    transfer_matrix->rY *= YColumScale;
    transfer_matrix->gY *= YColumScale;
    transfer_matrix->bY *= YColumScale;

    /* Second column of the transfer matrix */
    transfer_matrix->rCb *= CbColumScale;
    transfer_matrix->gCb *= CbColumScale;
    transfer_matrix->bCb *= CbColumScale;

    /* Third column of the transfer matrix */
    transfer_matrix->rCr *= CrColumnScale;
    transfer_matrix->gCr *= CrColumnScale;
    transfer_matrix->bCr *= CrColumnScale;
}

/*
 * ITU-R BT.601, BT.709 and SMPTE 240M transfer matrices from DXVA 2.0
 * Video Color Field definitions Design Spec(Version 0.03).
 * [R', G', B'] values are in the range [0, 1], Y' is in the range [0,1]
 * and [Pb, Pr] components are in the range [-0.5, 0.5].
 */
static psb_transform_coeffs s601 = {
    1, -0.000001, 1.402,
    1, -0.344136, -0.714136,
    1, 1.772, 0
};

static psb_transform_coeffs s709 = {
    1, 0, 1.5748,
    1, -0.187324, -0.468124,
    1, 1.8556, 0
};

static psb_transform_coeffs s240M = {
    1, -0.000657, 1.575848,
    1, -0.226418, -0.476529,
    1, 1.825958, 0.000378
};

/*
  These are the corresponding matrices when using NominalRange_16_235
  for the input surface and NominalRange_0_255 for the outpur surface:

  static const psb_transform_coeffs s601 = {
  1.164,		0,		1.596,
  1.164,		-0.391,		-0.813,
  1.164,		2.018,		0
  };

  static const psb_transform_coeffs s709 = {
  1.164,		0,		1.793,
  1.164,		-0.213,		-0.534,
  1.164,		2.115,		0
  };

  static const psb_transform_coeffs s240M = {
  1.164,		-0.0007,	1.793,
  1.164,		-0.257,		-0.542,
  1.164,		2.078,		0.0004
  };
*/

/**
 * Select which transfer matrix to use in the YUV->RGB conversion.
 */
static void
psb_select_transfermatrix(PsbPortPrivRec * pPriv,
			  psb_transform_coeffs * transfer_matrix,
			  double *Y_offset, double *CbCr_offset,
			  double *RGB_offset)
{
    double RGB_scale, Y_scale, Cb_scale, Cr_scale;

    /*
     * Depending on the nominal ranges of the input YUV surface and the output RGB
     * surface, it might be needed to perform some scaling on the transfer matrix.
     * The excursion in the YUV values implies that the first column of the matrix
     * must be divided by the Y excursion, and the second and third columns be
     * divided by the U and V excursions respectively. The offset does not affect
     * the values of the matrix.
     * The excursion in the RGB values implies that all the values in the transfer
     * matrix must be multiplied by the value of the excursion.
     * 
     * Example: Conversion of the SMPTE 240M transfer matrix.
     * 
     * Conversion from [Y', Pb, Pr] to [R', G', B'] in the range of [0, 1]. Y' is in
     * the range of [0, 1]      and Pb and Pr in the range of [-0.5, 0.5].
     * 
     * R'               1       -0.000657       1.575848                Y'
     * G'       =       1       -0.226418       -0.476529       *       Pb
     * B'               1       1.825958        0.000378                Pr
     * 
     * Conversion from [Y', Cb, Cr] to {R', G', B'] in the range of [0, 1]. Y' has an
     * excursion of 219 and an offset of +16, and CB and CR have excursions of +/-112
     * and offset of +128, for a range of 16 through 240 inclusive.
     * 
     * R'               1/219   -0.000657/224   1.575848/224            Y'       16
     * G'       =       1/219   -0.226418/224   -0.476529/224   *       Cb - 128
     * B'               1/219   1.825958/224    0.000378/224            Cr   128
     * 
     * Conversion from [Y', Cb, Cr] to R'G'B' in the range [0, 255].
     * 
     * R'                         1/219 -0.000657/224 1.575848/224                      Y'       16
     * G'       =       255 * 1/219     -0.226418/224 -0.476529/224             *       Cb - 128
     * B'                         1/219 1.825958/224  0.000378/224                      Cr   128
     */

    switch (pPriv->src_nominalrange) {
    case PSB_NominalRange_0_255:
	/* Y has a range of [0, 255], U and V have a range of [0, 255] */
	{
	    double tmp = 0.0;

	    (void)tmp;
	}			       /* workaroud for float point bug? */
	Y_scale = 255.0;
	*Y_offset = 0;
	Cb_scale = Cr_scale = 255;
	*CbCr_offset = 128;
	break;
    case PSB_NominalRange_16_235:
    case PSB_NominalRange_Unknown:
	/* Y has a range of [16, 235] and Cb, Cr have a range of [16, 240] */
	Y_scale = 219;
	*Y_offset = 16;
	Cb_scale = Cr_scale = 224;
	*CbCr_offset = 128;
	break;
    case PSB_NominalRange_48_208:
	/* Y has a range of [48, 208] and Cb, Cr have a range of [48, 208] */
	Y_scale = 160;
	*Y_offset = 48;
	Cb_scale = Cr_scale = 160;
	*CbCr_offset = 128;
	break;

    default:
	/* Y has a range of [0, 1], U and V have a range of [-0.5, 0.5] */
	Y_scale = 1;
	*Y_offset = 0;
	Cb_scale = Cr_scale = 1;
	*CbCr_offset = 0;
	break;
    }

    /*
     * 8-bit computer RGB,      also known as sRGB or "full-scale" RGB, and studio
     * video RGB, or "RGB with  head-room and toe-room." These are defined as follows:
     * 
     * - Computer RGB uses 8 bits for each sample of red, green, and blue. Black
     * is represented by R = G = B = 0, and white is represented by R = G = B = 255.
     * - Studio video RGB uses some number of bits N for each sample of red, green,
     * and blue, where N is 8 or more. Studio video RGB uses a different scaling
     * factor than computer RGB, and it has an offset. Black is represented by
     * R = G = B = 16*2^(N-8), and white is represented by R = G = B = 235*2^(N-8).
     * However, actual values may fall outside this range.
     */
    switch (pPriv->dst_nominalrange) {
    case PSB_NominalRange_0_255:      // for sRGB
    case PSB_NominalRange_Unknown:
	/* R, G and B have a range of [0, 255] */
	RGB_scale = 255;
	*RGB_offset = 0;
	break;
    case PSB_NominalRange_16_235:     // for stRGB
	/* R, G and B have a range of [16, 235] */
	RGB_scale = 219;
	*RGB_offset = 16;
	break;
    case PSB_NominalRange_48_208:     // for Bt.1361 RGB
	/* R, G and B have a range of [48, 208] */
	RGB_scale = 160;
	*RGB_offset = 48;
	break;
    default:
	/* R, G and B have a range of [0, 1] */
	RGB_scale = 1;
	*RGB_offset = 0;
	break;
    }

    switch (pPriv->video_transfermatrix) {
    case PSB_VideoTransferMatrix_BT709:
	memcpy(transfer_matrix, &s709, sizeof(psb_transform_coeffs));
	break;
    case PSB_VideoTransferMatrix_BT601:
	memcpy(transfer_matrix, &s601, sizeof(psb_transform_coeffs));
	break;
    case PSB_VideoTransferMatrix_SMPTE240M:
	memcpy(transfer_matrix, &s240M, sizeof(psb_transform_coeffs));
	break;
    case PSB_VideoTransferMatrix_Unknown:
	/*
	 * Specifies that the video transfer matrix is not specified.
	 * The default value is BT601 for standard definition (SD) video and BT709
	 * for high definition (HD) video.
	 */
	if (1 /*pPriv->sVideoDesc.SampleWidth < 720 */ ) {	/* TODO, width selection */
	    memcpy(transfer_matrix, &s601, sizeof(psb_transform_coeffs));
	} else {
	    memcpy(transfer_matrix, &s709, sizeof(psb_transform_coeffs));
	}
	break;
    default:
	break;
    }

    if (Y_scale != 1 || Cb_scale != 1 || Cr_scale != 1) {
	/* Each column of the transfer matrix has to
	 * be scaled by the excursion of each component
	 */
	psb_scale_transfermatrix(transfer_matrix, 1 / Y_scale, 1 / Cb_scale,
				 1 / Cr_scale);
    }
    if (RGB_scale != 1) {
	/* All the values in the transfer matrix have to be multiplied
	 * by the excursion of the RGB components
	 */
	psb_scale_transfermatrix(transfer_matrix, RGB_scale, RGB_scale,
				 RGB_scale);
    }
}

/**
 * Updates the CSC coefficients if required.
 */
static void
psb_setup_coeffs(PsbPortPrivRec * pPriv)
{
    double yCoeff, uCoeff, vCoeff, Constant;
    double fContrast;
    double Y_offset, CbCr_offset, RGB_offset;
    int bright_off = 0;
    psb_transform_coeffs coeffs, transfer_matrix;

    /* Offsets in the input and output ranges are
     * included in the constant of the transform equation
     */
    psb_select_transfermatrix(pPriv, &transfer_matrix,
			      &Y_offset, &CbCr_offset, &RGB_offset);

    /*
     * It is at this point we should adjust the parameters for the procamp:
     * - Brightness is handled as an offset of the Y parameter.
     * - Contrast is an adjustment of the Y scale.
     * - Saturation is a scaling of the U anc V parameters.
     * - Hue is a rotation of the U and V parameters.
     */

    bright_off = pPriv->brightness.Value;
    fContrast = (pPriv->contrast.Value + 100) / 100.0;

    /* Apply hue and saturation correction to transfer matrix */
    psb_transform_sathuecoeffs(&coeffs,
			       &transfer_matrix,
			       pPriv->hue.Value * Degree,
			       pPriv->saturation.Value / 100.0);

    /* Create coefficients to get component R
     * (including brightness and contrast correction)
     */
    psb_create_coeffs(-1 * Y_offset, -1 * CbCr_offset, -1 * CbCr_offset,
		      RGB_offset, coeffs.rY, coeffs.rCb, coeffs.rCr,
		      bright_off, fContrast, &yCoeff, &uCoeff, &vCoeff,
		      &Constant);

    /* Convert transform operation from floating point to fixed point */
    psb_convert_coeffs(yCoeff, uCoeff, vCoeff, Constant,	/* input coefficients */
		       &pPriv->coeffs.rY, &pPriv->coeffs.rU,
		       &pPriv->coeffs.rV, &pPriv->coeffs.rConst,
		       &pPriv->coeffs.rShift);

    /* Create coefficients to get component G
     * (including brightness and contrast correction)
     */
    psb_create_coeffs(-1 * Y_offset, -1 * CbCr_offset, -1 * CbCr_offset,
		      RGB_offset, coeffs.gY, coeffs.gCb, coeffs.gCr,
		      bright_off, fContrast, &yCoeff, &uCoeff, &vCoeff,
		      &Constant);

    /* Convert transform operation from floating point to fixed point */
    psb_convert_coeffs(yCoeff, uCoeff, vCoeff, Constant,
		       /* tranfer matrix coefficients for G */
		       &pPriv->coeffs.gY, &pPriv->coeffs.gU,
		       &pPriv->coeffs.gV, &pPriv->coeffs.gConst,
		       &pPriv->coeffs.gShift);

    /* Create coefficients to get component B
     * (including brightness and contrast correction)
     */
    psb_create_coeffs(-1 * Y_offset, -1 * CbCr_offset, -1 * CbCr_offset,
		      RGB_offset, coeffs.bY, coeffs.bCb, coeffs.bCr,
		      bright_off, fContrast, &yCoeff, &uCoeff, &vCoeff,
		      &Constant);

    /* Convert transform operation from floating point to fixed point */
    psb_convert_coeffs(yCoeff, uCoeff, vCoeff, Constant,
		       /* tranfer matrix coefficients for B */
		       &pPriv->coeffs.bY, &pPriv->coeffs.bU,
		       &pPriv->coeffs.bV, &pPriv->coeffs.bConst,
		       &pPriv->coeffs.bShift);
}

static void
psb_pack_coeffs(PsbPortPrivRec * pPriv, unsigned long *sgx_coeffs)
{
    /* We use taps 0,3 and 6 which I means an filter offset of either 4,5,6
     * yyyyuvuv
     * x00x00x0
     */
    sgx_coeffs[0] =
	((pPriv->coeffs.rY & 0xff) << 24) | ((pPriv->coeffs.rU & 0xff) << 0);
    sgx_coeffs[1] = (pPriv->coeffs.rV & 0xff) << 8;
    sgx_coeffs[2] =
	((pPriv->coeffs.rConst & 0xffff) << 4) | (pPriv->coeffs.rShift & 0xf);

    sgx_coeffs[3] =
	((pPriv->coeffs.gY & 0xff) << 24) | ((pPriv->coeffs.gU & 0xff) << 0);
    sgx_coeffs[4] = (pPriv->coeffs.gV & 0xff) << 8;
    sgx_coeffs[5] =
	((pPriv->coeffs.gConst & 0xffff) << 4) | (pPriv->coeffs.gShift & 0xf);

    sgx_coeffs[6] =
	((pPriv->coeffs.bY & 0xff) << 24) | ((pPriv->coeffs.bU & 0xff) << 0);
    sgx_coeffs[7] = (pPriv->coeffs.bV & 0xff) << 8;
    sgx_coeffs[8] =
	((pPriv->coeffs.bConst & 0xffff) << 4) | (pPriv->coeffs.bShift & 0xf);
}
