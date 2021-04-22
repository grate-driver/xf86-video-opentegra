
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <xf86drm.h>

#include "host1x_uapi.h"
#include "private.h"
#include "sync.h"

int
drm_tegra_job_create_fence_v3(struct drm_tegra_job_v3* job,
			      struct drm_tegra_fence** pfence)
{
	struct drm_tegra_fence *fence;
	int err;

	fence = calloc(1, sizeof(*fence));
	if (!fence)
		return -ENOMEM;

	fence->drm = job->drm;
	fence->version = 3;

	err = drmSyncobjCreate(job->drm->fd, 0, &fence->syncobj);
	if (err) {
		fprintf(stderr, "drmSyncobjCreate failed %d\n", err);
		free(fence);
		return err;
	}

	*pfence = fence;

	return 0;
}

int drm_tegra_fence_is_busy_v3(struct drm_tegra_fence *fence)
{
	int err;

	err = drm_tegra_fence_wait_timeout_v3(fence, 0);

	if (err == 0)
		return 0;
	else if (err == -ETIME)
		return 1;
	else
		return err;
}

static uint64_t gettime_ns(void)
{
	struct timespec current;
	clock_gettime(CLOCK_MONOTONIC, &current);
	return (uint64_t)current.tv_sec * 1000000000ull + current.tv_nsec;
}

int drm_tegra_fence_wait_timeout_v3(struct drm_tegra_fence *fence,
				    int timeout)
{
	return drmSyncobjWait(fence->drm->fd, &fence->syncobj, 1,
				gettime_ns() + 1000000LL * (int64_t)timeout,
				DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL|
					DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,
				NULL);
}

void drm_tegra_fence_free_v3(struct drm_tegra_fence *fence)
{
	drmSyncobjDestroy(fence->drm->fd, fence->syncobj);
	free(fence);
}

int drm_tegra_channel_init_v3(struct drm_tegra_channel *channel,
			      struct drm_tegra *drm,
			      enum drm_tegra_class client)
{
	struct drm_tegra_syncpoint_allocate sp_args;
	struct drm_tegra_channel_open args;
	enum host1x_class class;
	int err;

	if (!channel || !drm)
		return -EINVAL;

	switch (client) {
	case DRM_TEGRA_GR2D:
		class = HOST1X_CLASS_GR2D;
		break;

	case DRM_TEGRA_GR3D:
		class = HOST1X_CLASS_GR3D;
		break;

	default:
		return -EINVAL;
	}

	memset(&args, 0, sizeof(args));
	args.host1x_class = class;

	err = drmCommandWriteRead(drm->fd, DRM_TEGRA_CHANNEL_OPEN, &args,
				  sizeof(args));
	if (err < 0)
		return err;

	channel->drm			= drm;
	channel->version		= 3;
	channel->v3.channel_ctx		= args.channel_ctx;
	channel->v3.hardware_version	= args.hardware_version;

	DRMINITLISTHEAD(&channel->v3.mapping_list);

	memset(&sp_args, 0, sizeof(sp_args));

	err = drmCommandWriteRead(drm->fd, DRM_TEGRA_SYNCPOINT_ALLOCATE, &sp_args, sizeof(sp_args));
	if (err < 0)
		goto deinit_channel;

	channel->v3.sp_id = sp_args.id;

	return 0;

deinit_channel:
	drm_tegra_channel_deinit_v3(channel);

	return err;
}

int drm_tegra_channel_open_v3(struct drm_tegra_channel **channelp,
			      struct drm_tegra *drm,
			      enum drm_tegra_class client)
{
	struct drm_tegra_channel *channel;
	int err;

	if (!channelp || !drm)
		return -EINVAL;

	channel = calloc(1, sizeof(*channel));
	if (!channel)
		return -ENOMEM;

	err = drm_tegra_channel_init_v3(channel, drm, client);
	if (err) {
		free(channel);
		return err;
	}

	*channelp = channel;

	return 0;
}

static int drm_tegra_channel_mapping_unref_v3(struct drm_tegra *drm,
					      struct drm_tegra_bo_mapping_v3 *mapping)
{
	struct drm_tegra_channel_unmap unmap_args = {};
	int err;

	DRMLISTDELINIT(&mapping->ch_list);

	if (!atomic_dec_and_test(&mapping->ref))
		return 0;

	unmap_args.channel_ctx = mapping->channel_ctx;
	unmap_args.mapping_id = mapping->id;

	err = drmCommandWriteRead(drm->fd, DRM_TEGRA_CHANNEL_UNMAP,
				  &unmap_args, sizeof(unmap_args));

	DRMLISTDELINIT(&mapping->bo_list);

	free(mapping);

	return err;
}

int drm_tegra_channel_deinit_v3(struct drm_tegra_channel *channel)
{
	struct drm_tegra_channel_close args;
	struct drm_tegra *drm;
	int err;

	if (!channel)
		return -EINVAL;

	if (channel->v3.channel_ctx == 0)
		return 0;

	drm = channel->drm;

	memset(&args, 0, sizeof(args));
	args.channel_ctx = channel->v3.channel_ctx;

	err = drmCommandWriteRead(drm->fd, DRM_TEGRA_CHANNEL_CLOSE, &args,
				  sizeof(args));
	if (err < 0)
		return err;

	while (!DRMLISTEMPTY(&channel->v3.mapping_list)) {
		struct drm_tegra_bo_mapping_v3 *mapping;

		mapping = DRMLISTENTRY(struct drm_tegra_bo_mapping_v3,
				       channel->v3.mapping_list.next,
				       ch_list);

		err = drm_tegra_channel_mapping_unref_v3(drm, mapping);
		if (err < 0)
			VDBG_DRM(drm, "UNMAP failed err %d strerror(%s) mapping_id=%u channel_ctx=%u\n",
				 err, strerror(-err), mapping->id, mapping->channel_ctx);
	}

	return 0;
}

int drm_tegra_channel_close_v3(struct drm_tegra_channel *channel)
{
	drm_tegra_channel_deinit_v3(channel);
	free(channel);

	return 0;
}

int drm_tegra_job_new_v3(struct drm_tegra_job_v3 **jobp,
			 struct drm_tegra_channel *channel,
			 unsigned int num_buffers_expected,
			 unsigned int num_words_expected,
			 unsigned int num_cmds_expected)
{
	struct drm_tegra_job_v3 *job;
	void *buf_table;
	void *start;
	void *cmds;
	int err;

	if (!jobp || !channel)
		return -EINVAL;

	job = calloc(1, sizeof(*job));
	if (!job)
		return -ENOMEM;

	if (num_words_expected < 64)
		num_words_expected = 64;

	err = posix_memalign(&start, 64,
			     sizeof(*job->start) * num_words_expected);
	if (err)
		goto err_free_job;

	if (num_buffers_expected < 8)
		num_buffers_expected = 8;

	err = posix_memalign(&buf_table, 64,
			     sizeof(*job->buf_table) * num_buffers_expected);
	if (err)
		goto err_free_words;

	if (num_cmds_expected < 8)
		num_cmds_expected = 8;

	err = posix_memalign(&cmds, 64, sizeof(*job->cmds) * num_cmds_expected);
	if (err)
		goto err_free_words;

	job->num_buffers_max = num_buffers_expected;
	job->num_cmds_max = num_cmds_expected;
	job->num_words = num_words_expected;
	job->buf_table = buf_table;
	job->gather_start = start;
	job->drm = channel->drm;
	job->channel = channel;
	job->start = start;
	job->cmds = cmds;
	job->ptr = start;

	*jobp = job;

	return 0;

err_free_words:
	free(start);
err_free_job:
	free(job);

	return err;
}

int drm_tegra_job_reset_v3(struct drm_tegra_job_v3 *job)
{
	if (!job)
		return -EINVAL;

	job->sp_incrs = 0;
	job->num_cmds = 0;
	job->num_buffers = 0;
	job->ptr = job->start;
	job->gather_start = job->start;

	return 0;
}

int drm_tegra_job_resize_v3(struct drm_tegra_job_v3 *job,
			    unsigned int num_words,
			    unsigned int num_buffers,
			    unsigned int num_cmds,
			    bool reallocate)
{
	void *new_buf_table, *new_start, *new_cmds;
	unsigned int offset, gather_offset;
	size_t size;
	int err;

	if (!job)
		return -EINVAL;

	if (num_words != job->num_words) {
		offset = (unsigned int)(job->ptr - job->start);
		gather_offset = (unsigned int)(job->gather_start - job->start);

		err = posix_memalign(&new_start, 64,
				     sizeof(*job->start) * num_words);
		if (err)
			return err;

		if (reallocate) {
			if (num_words < job->num_words)
				size = sizeof(*job->start) * num_words;
			else
				size = sizeof(*job->start) * job->num_words;

			memcpy(new_start, job->start, size);
		}

		free(job->start);

		job->num_words = num_words;
		job->gather_start = new_start;
		job->gather_start += gather_offset;
		job->start = new_start;
		job->ptr = new_start;
		job->ptr += offset;
	}

	if (num_buffers != job->num_buffers_max) {
		err = posix_memalign(&new_buf_table, 64,
				     sizeof(*job->buf_table) * num_buffers);
		if (err)
			return err;

		if (reallocate) {
			if (num_buffers < job->num_buffers_max)
				size = sizeof(*job->buf_table) * num_buffers;
			else
				size = sizeof(*job->buf_table) * job->num_buffers_max;

			memcpy(new_buf_table, job->buf_table, size);
		}

		free(job->buf_table);

		job->buf_table = new_buf_table;
		job->num_buffers_max = num_buffers;
	}

	if (num_cmds != job->num_cmds_max) {
		err = posix_memalign(&new_cmds, 64,
				     sizeof(*job->cmds) * num_cmds);
		if (err)
			return err;

		if (reallocate) {
			if (num_cmds < job->num_cmds_max)
				size = sizeof(*job->cmds) * num_cmds;
			else
				size = sizeof(*job->cmds) * job->num_cmds_max;

			memcpy(new_cmds, job->cmds, size);
		}

		free(job->cmds);

		job->cmds = new_cmds;
		job->num_cmds_max = num_cmds;
	}

	return 0;
}

int drm_tegra_job_free_v3(struct drm_tegra_job_v3 *job)
{
	if (!job)
		return -EINVAL;

	free(job->buf_table);
	free(job->start);
	free(job->cmds);
	free(job);

	return 0;
}

static int drm_tegra_bo_map_io_v3(struct drm_tegra_bo_mapping_v3 **pmapping,
				  struct drm_tegra_channel *channel,
				  struct drm_tegra_bo *bo)
{
	struct drm_tegra_bo_mapping_v3 *mapping;
	struct drm_tegra_channel_map map_args;
	int err;

	DRMLISTFOREACHENTRY(mapping, &bo->mapping_list_v3, bo_list) {
		if (mapping->channel_ctx == channel->v3.channel_ctx) {
			atomic_inc(&mapping->ref);
			*pmapping = mapping;
			return 0;
		}
	}

	mapping = calloc(1, sizeof(*mapping));

	memset(&map_args, 0, sizeof(map_args));
	map_args.channel_ctx = channel->v3.channel_ctx;
	map_args.handle = bo->handle;
	map_args.flags = DRM_TEGRA_CHANNEL_MAP_READWRITE;

	err = drmCommandWriteRead(channel->drm->fd, DRM_TEGRA_CHANNEL_MAP,
				  &map_args, sizeof(map_args));
	if (err) {
		free(mapping);
		return err;
	}

	mapping->id = map_args.mapping_id;
	mapping->channel_ctx = channel->v3.channel_ctx;
	DRMLISTADDTAIL(&mapping->bo_list, &bo->mapping_list_v3);
	DRMLISTADDTAIL(&mapping->ch_list, &channel->v3.mapping_list);
	atomic_set(&mapping->ref, 1);

	*pmapping = mapping;

	return 0;
}

int drm_tegra_job_push_reloc_v3(struct drm_tegra_job_v3 *job,
				struct drm_tegra_bo *target,
				unsigned long offset,
				uint32_t drm_buf_table_flags)
{
	struct drm_tegra_bo_mapping_v3 *mapping = NULL, *mapping_itr;
	struct drm_tegra_submit_buf buf;
	int err;

	if (!job)
		return -EINVAL;

	DRMLISTFOREACHENTRY(mapping_itr, &target->mapping_list_v3, bo_list) {
		if (mapping_itr->channel_ctx == job->channel->v3.channel_ctx) {
			mapping = mapping_itr;
			break;
		}
	}

	if (!mapping) {
		err = drm_tegra_bo_map_io_v3(&mapping, job->channel, target);
		if (err)
			return err;
	}

	if (job->num_buffers == job->num_buffers_max) {
		err = drm_tegra_job_resize_v3(job, job->num_words,
					      job->num_buffers_max * 2,
					      job->num_cmds_max,
					      true);
		if (err)
			return err;
	}

	memset(&buf, 0, sizeof(buf));
	buf.flags = drm_buf_table_flags;
	buf.mapping_id = mapping->id;
	buf.reloc.target_offset = target->offset + offset;
	buf.reloc.gather_offset_words = (unsigned long)(job->ptr - job->start);

	if (buf.reloc.gather_offset_words == job->num_words) {
		err = drm_tegra_job_resize_v3(job, job->num_words + 256,
					      job->num_buffers_max,
					      job->num_cmds_max,
					      true);
		if (err)
			return err;
	}

	job->buf_table[job->num_buffers++] = buf;
	*job->ptr++ = 0xdeadbeef;

	return 0;
}

static int drm_tegra_job_push_gather_v3(struct drm_tegra_job_v3 *job)
{
	struct drm_tegra_submit_cmd cmd;
	int err;

	if (job->gather_start == job->ptr)
		return 0;

	if (job->num_cmds == job->num_cmds_max) {
		err = drm_tegra_job_resize_v3(job, job->num_words,
					      job->num_buffers_max,
					      job->num_cmds_max * 2,
					      true);
		if (err)
			return err;
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.type = DRM_TEGRA_SUBMIT_CMD_GATHER_UPTR;
	cmd.gather_uptr.words = (unsigned long)(job->ptr - job->gather_start);

	job->gather_start = job->ptr;
	job->cmds[job->num_cmds++] = cmd;

	return 0;
}

int drm_tegra_job_push_wait_v3(struct drm_tegra_job_v3 *job,
			       uint32_t threshold)
{
	struct drm_tegra_submit_cmd cmd;
	int err;

	err = drm_tegra_job_push_gather_v3(job);
	if (err)
		return err;

	if (job->num_cmds == job->num_cmds_max) {
		err = drm_tegra_job_resize_v3(job, job->num_words,
					      job->num_buffers_max,
					      job->num_cmds_max * 2,
					      true);
		if (err)
			return err;
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.type = DRM_TEGRA_SUBMIT_CMD_WAIT_SYNCPT_RELATIVE;
	cmd.wait_syncpt.id = job->channel->v3.sp_id;
	cmd.wait_syncpt.threshold = threshold;

	job->cmds[job->num_cmds++] = cmd;

	return 0;
}

#define HOST1X_OPCODE_NONINCR(offset, count) \
	((0x2 << 28) | (((offset) & 0xfff) << 16) | ((count) & 0xffff))

int drm_tegra_job_push_syncpt_incr_v3(struct drm_tegra_job_v3 *job,
				      enum drm_tegra_syncpt_cond cond)
{
	*job->ptr++ = HOST1X_OPCODE_NONINCR(0x0, 0x1);
	*job->ptr++ = cond << 8 | job->channel->v3.sp_id;

	job->sp_incrs++;

	return 0;
}

int drm_tegra_job_submit_v3(struct drm_tegra_job_v3 *job,
			    struct drm_tegra_fence **pfence)
{
	struct drm_tegra_channel_submit args;
	int err;

	if (!job)
		return -EINVAL;

	err = drm_tegra_job_push_gather_v3(job);
	if (err) {
		fprintf(stderr, "push_gather failed %d\n", err);
		return err;
	}

	memset(&args, 0, sizeof(args));
	args.channel_ctx		= job->channel->v3.channel_ctx;
	args.num_bufs			= job->num_buffers;
	args.num_cmds			= job->num_cmds;
	args.gather_data_words		= (uint32_t)(job->ptr - job->start);
	args.bufs_ptr			= (uintptr_t)job->buf_table;
	args.cmds_ptr			= (uintptr_t)job->cmds;
	args.gather_data_ptr		= (uintptr_t)job->start;
	args.syncpt_incr.id		= job->channel->v3.sp_id;
	args.syncpt_incr.num_incrs	= job->sp_incrs;

	if (pfence) {
		err = drm_tegra_job_create_fence_v3(job, pfence);
		if (err)
			return err;

		args.syncobj_out = (*pfence)->syncobj;
	}

	err = drmCommandWriteRead(job->drm->fd, DRM_TEGRA_CHANNEL_SUBMIT,
				  &args, sizeof(args));
	if (err) {
		fprintf(stderr, "CHANNEL_SUBMIT failed errno=%d\n", errno);
		if (pfence)
			drm_tegra_fence_free_v3(*pfence);
		return err;
	}

	return 0;
}
