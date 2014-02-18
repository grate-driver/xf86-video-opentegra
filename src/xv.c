#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "xf86.h"
#include "xf86xv.h"
#include "dixstruct.h"
#include "fourcc.h"

#include <X11/extensions/Xv.h>

#include "driver.h"

typedef struct TegraXvPort {
} TegraXvPort;

static XF86VideoEncodingRec DummyEncoding = {
	0,
	"XV_IMAGE",
	2048,
	2048,
	{ 1, 1 }
};

static XF86VideoFormatRec TegraFormats[] = {
	{ 15, TrueColor },
	{ 16, TrueColor },
	{ 24, TrueColor },
};

static XF86AttributeRec TegraOverlayAttributes[] = {
};

static XF86ImageRec TegraImages[] = {
	XVIMAGE_YUY2,
	XVIMAGE_YV12,
	XVIMAGE_I420,
	XVIMAGE_UYVY,
};

static void TegraStopVideo(ScrnInfoPtr pScrn, pointer data, Bool shutdown)
{
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pScrn=%p, data=%p, shutdown=%d)\n",
		   __func__, pScrn, data, shutdown);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s()\n", __func__);
}

static int TegraSetPortAttribute(ScrnInfoPtr pScrn, Atom attribute, INT32 value,
				 pointer data)
{
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pScrn=%p, attribute=%u, value=%d, data=%p)\n",
		   __func__, pScrn, attribute, value, data);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s()\n", __func__);

	return Success;
}

static int TegraGetPortAttribute(ScrnInfoPtr pScrn, Atom attribute, INT32 *value,
				 pointer data)
{
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pScrn=%p, attribute=%u, value=%p, data=%p)\n",
		   __func__, pScrn, attribute, value, data);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s()\n", __func__);

	return Success;
}

static void TegraQueryBestSize(ScrnInfoPtr pScrn, Bool motion, short vid_w,
			       short vid_h, short drw_w, short drw_h,
			       unsigned int *p_w, unsigned int *p_h,
			       pointer data)
{
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pScrn=%p, motion=%d, vid_w=%u, vid_h=%u, drw_w=%u, drw_h=%u, p_w=%p, p_h=%p, data=%p)\n",
		   __func__, pScrn, motion, vid_w, vid_h, drw_w, drw_h, p_w, p_h,
		   data);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s()\n", __func__);
}

static int TegraPutImage(ScrnInfoPtr pScrn, short src_x, short src_y,
			 short drw_x, short drw_y, short src_w, short src_h,
			 short drw_w, short drw_h, int id, unsigned char *buf,
			 short width, short height, Bool sync, RegionPtr
			 clipBoxes, pointer data, DrawablePtr drawable)
{
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pScrn=%p, src_x=%u, src_y=%u, drw_x=%u, drw_y=%u, src_w=%u, src_h=%u, drw_w=%u, drw_h=%u, id=%d, buf=%p, width=%u, height=%u, sync=%d, clipBoxes=%p, data=%p, drawable=%p)\n",
		   __func__, pScrn, src_x, src_y, drw_x, drw_y, src_w, src_h,
		   drw_w, drw_h, id, buf, width, height, sync, clipBoxes,
		   data, drawable);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s()\n", __func__);

	return Success;
}

static int TegraQueryImageAttributes(ScrnInfoPtr pScrn, int id,
				     unsigned short *w, unsigned short *h,
				     int *pitches, int *offsets)
{
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pScrn=%p, id=%d, w=%p, h=%p, pitches=%p, offsets=%p)\n",
		   __func__, pScrn, id, w, h, pitches, offsets);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s()\n", __func__);
	return 0;
}

static XF86VideoAdaptorPtr TegraSetupOverlayVideo(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	XF86VideoAdaptorPtr adaptor;
	TegraXvPort *port;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pScreen=%p)\n", __func__, pScreen);

	adaptor = calloc(1, sizeof(*adaptor) + sizeof(DevUnion) + sizeof(*port));
	if (!adaptor)
		return NULL;

	adaptor->type = XvWindowMask | XvInputMask | XvImageMask;
	adaptor->flags = VIDEO_OVERLAID_IMAGES;
	adaptor->name = "Tegra Video Overlay";
	adaptor->nEncodings = 1;
	adaptor->pEncodings = &DummyEncoding;
	adaptor->nFormats = TEGRA_ARRAY_SIZE(TegraFormats);
	adaptor->pFormats = TegraFormats;
	adaptor->nPorts = 1;

	adaptor->pPortPrivates = (DevUnion *)&adaptor[1];
	port = (TegraXvPort *)&adaptor->pPortPrivates[1];
	adaptor->pPortPrivates[0].ptr = port;

	adaptor->nAttributes = TEGRA_ARRAY_SIZE(TegraOverlayAttributes);
	adaptor->pAttributes = TegraOverlayAttributes;
	adaptor->nImages = TEGRA_ARRAY_SIZE(TegraImages);
	adaptor->pImages = TegraImages;

	adaptor->PutVideo = NULL;
	adaptor->PutStill = NULL;
	adaptor->GetVideo = NULL;
	adaptor->GetStill = NULL;

	adaptor->StopVideo = TegraStopVideo;
	adaptor->SetPortAttribute = TegraSetPortAttribute;
	adaptor->GetPortAttribute = TegraGetPortAttribute;
	adaptor->QueryBestSize = TegraQueryBestSize;
	adaptor->PutImage = TegraPutImage;
	adaptor->QueryImageAttributes = TegraQueryImageAttributes;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s() = %p\n", __func__, adaptor);
	return adaptor;
}

void TegraVideoScreenInit(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	XF86VideoAdaptorPtr *adaptors;
	int num_adaptors = 1;

	num_adaptors = xf86XVListGenericAdaptors(pScrn, &adaptors);

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Xv: %d generic adaptors\n",
		   num_adaptors);

	if (!num_adaptors) {
		adaptors = calloc(1, sizeof(*adaptors));
		if (adaptors) {
			adaptors[0] = TegraSetupOverlayVideo(pScreen);
			num_adaptors = 1;
		}
	}

	if (num_adaptors)
		xf86XVScreenInit(pScreen, adaptors, num_adaptors);
}

void TegraVideoScreenExit(ScreenPtr pScreen)
{
}
