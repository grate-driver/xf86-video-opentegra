/*
 * Copyright © 2012, 2013 Thierry Reding
 * Copyright © 2013 Erik Faye-Lund
 * Copyright © 2014 NVIDIA Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>

#include <xf86drm.h>

#include "uapi_v1.h"

int drm_tegra_fence_is_busy_v1(struct drm_tegra_fence *fence)
{
	struct drm_tegra_syncpt_wait args;
	unsigned long request;

	if (!fence)
		return -EINVAL;

	memset(&args, 0, sizeof(args));
	args.id = fence->syncpt;
	args.thresh = fence->value;

	/*
	 * Kernel driver returns -EAGAIN if timeout=0 and fence
	 * isn't reached, so we can't use drmCommandWriteRead()
	 * since it will spin until fence is reached and this
	 * is not what we want here.
	 */
	request = DRM_IOC(DRM_IOC_READ | DRM_IOC_WRITE, DRM_IOCTL_BASE,
			  DRM_COMMAND_BASE + DRM_TEGRA_SYNCPT_WAIT,
			  sizeof(args));

	return ioctl(fence->drm->fd, request, &args);
}

int drm_tegra_fence_wait_timeout_v1(struct drm_tegra_fence *fence,
				    unsigned long timeout)
{
	struct drm_tegra_syncpt_wait args;

	if (!fence)
		return -EINVAL;

	memset(&args, 0, sizeof(args));
	args.id = fence->syncpt;
	args.thresh = fence->value;
	args.timeout = timeout;

	return drmCommandWriteRead(fence->drm->fd, DRM_TEGRA_SYNCPT_WAIT,
				   &args, sizeof(args));
}

void drm_tegra_fence_free_v1(struct drm_tegra_fence *fence)
{
	free(fence);
}
