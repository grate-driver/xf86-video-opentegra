#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "xf86.h"
#include "dri2.h"

#include "driver.h"

static DRI2BufferPtr TegraDRI2CreateBuffer(DrawablePtr drawable,
                                           unsigned int attachment,
                                           unsigned int format)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(drawable->pScreen);
    DRI2BufferPtr buffer = NULL;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(drawable=%p, attachment=%u, format=%x)\n",
               __func__, drawable, attachment, format);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s() = %p\n", __func__, buffer);
    return buffer;
}

static void TegraDRI2DestroyBuffer(DrawablePtr drawable, DRI2BufferPtr buffer)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(drawable->pScreen);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(drawable=%p, buffer=%p)\n",
               __func__, drawable, buffer);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s()\n", __func__);
}

static void TegraDRI2CopyRegion(DrawablePtr drawable, RegionPtr region,
                                DRI2BufferPtr target, DRI2BufferPtr source)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(drawable->pScreen);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(drawable=%p, region=%p, target=%p, source=%p)\n",
               __func__, drawable, region, target, source);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s()\n", __func__);
}

static int TegraDRI2GetMSC(DrawablePtr drawable, CARD64 *ust, CARD64 *msc)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(drawable->pScreen);
    int ret = 0;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(drawable=%p, ust=%p, msc=%p)\n",
               __func__, drawable, ust, msc);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s() = %d\n", __func__, ret);
    return ret;
}

static int TegraDRI2ScheduleSwap(ClientPtr client, DrawablePtr drawable,
                                 DRI2BufferPtr target, DRI2BufferPtr source,
                                 CARD64 *msc, CARD64 divisor, CARD64 remainder,
                                 DRI2SwapEventPtr func, void *data)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(drawable->pScreen);
    int ret = 0;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(client=%p, drawable=%p, target=%p, source=%p, msc=%p, divisor=%" PRIu64 ", remainder=%" PRIu64 ", func=%p, data=%p)\n",
               __func__, client, drawable, target, source, msc, divisor,
               remainder, func, data);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s() = %d\n", __func__, ret);
    return ret;
}

static int TegraDRI2ScheduleWaitMSC(ClientPtr client, DrawablePtr drawable,
                                    CARD64 msc, CARD64 divisor,
                                    CARD64 remainder)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(drawable->pScreen);
    int ret = 0;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(client=%p, drawable=%p, msc=%" PRIu64 ", divisor=%" PRIu64 ", remainder=%" PRIu64")\n",
               __func__, client, drawable, msc, divisor, remainder);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s() = %d\n", __func__, ret);
    return ret;
}

void TegraDRI2ScreenInit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    TegraPtr tegra = TegraPTR(pScrn);
    DRI2InfoRec info = {
        .version = 6,
        .fd = tegra->fd,
        .driverName = "tegra",
        .deviceName = tegra->path,
        .CreateBuffer = TegraDRI2CreateBuffer,
        .DestroyBuffer = TegraDRI2DestroyBuffer,
        .CopyRegion = TegraDRI2CopyRegion,
        .ScheduleSwap = TegraDRI2ScheduleSwap,
        .GetMSC = TegraDRI2GetMSC,
        .ScheduleWaitMSC = TegraDRI2ScheduleWaitMSC,
        .AuthMagic = drmAuthMagic,
    };

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pScreen=%p)\n", __func__,
               pScreen);

    if (xf86LoaderCheckSymbol("DRI2Version")) {
        int major = 0, minor = 0;

        DRI2Version(&major, &minor);

        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "DRI2 v%d.%d\n", major,
                   minor);
    }

    DRI2ScreenInit(pScreen, &info);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s()\n", __func__);
}

void TegraDRI2ScreenExit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "> %s(pScreen=%p)\n", __func__,
               pScreen);

    DRI2CloseScreen(pScreen);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "< %s()\n", __func__);
}
