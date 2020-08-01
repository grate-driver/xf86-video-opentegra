
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <xf86drm.h>

#include "private.h"

int drm_tegra_job_new_v2(struct drm_tegra_job_v2 **jobp,
			 struct drm_tegra *drm,
			 unsigned int num_bos_expected,
			 unsigned int num_words_expected)
{
	struct drm_tegra_job_v2 *job;
	void *bo_table;
	void *start;
	int err;

	if (!jobp || !drm)
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

	if (num_bos_expected < 8)
		num_bos_expected = 8;

	if (num_bos_expected > DRM_TEGRA_BO_TABLE_MAX_ENTRIES_NUM)
		goto err_free_words;

	err = posix_memalign(&bo_table, 64,
			     sizeof(*job->bo_table) * num_bos_expected);
	if (err)
		goto err_free_words;

	job->num_bos_max = num_bos_expected;
	job->num_words = num_words_expected;
	job->bo_table = bo_table;
	job->start = start;
	job->ptr = start;
	job->drm = drm;

	*jobp = job;

	return 0;

err_free_words:
	free(start);
err_free_job:
	free(job);

	return err;
}

int drm_tegra_job_resize_v2(struct drm_tegra_job_v2 *job,
			    unsigned int num_words,
			    unsigned int num_bos,
			    bool reallocate)
{
	unsigned int offset;
	void *new_bo_table;
	void *new_start;
	size_t size;
	int err;

	if (!job)
		return -EINVAL;

	if (num_words != job->num_words) {
		offset = (unsigned int)(job->ptr - job->start);

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
		job->start = new_start;
		job->ptr = new_start;
		job->ptr += offset;
	}

	if (num_bos != job->num_bos_max) {
		if (num_bos > DRM_TEGRA_BO_TABLE_MAX_ENTRIES_NUM)
			return -EINVAL;

		err = posix_memalign(&new_bo_table, 64,
				     sizeof(*job->bo_table) * num_bos);
		if (err)
			return err;

		if (reallocate) {
			if (num_bos < job->num_bos_max)
				size = sizeof(*job->bo_table) * num_bos;
			else
				size = sizeof(*job->bo_table) * job->num_bos_max;

			memcpy(new_bo_table, job->bo_table, size);
		}

		free(job->bo_table);

		job->bo_table = new_bo_table;
		job->num_bos_max = num_bos;
	}

	return 0;
}

int drm_tegra_job_reset_v2(struct drm_tegra_job_v2 *job)
{
	if (!job)
		return -EINVAL;

	job->num_bos = 0;
	job->ptr = job->start;

	return 0;
}

int drm_tegra_job_free_v2(struct drm_tegra_job_v2 *job)
{
	if (!job)
		return -EINVAL;

	free(job->bo_table);
	free(job->start);
	free(job);

	return 0;
}

int drm_tegra_job_push_reloc_v2(struct drm_tegra_job_v2 *job,
				struct drm_tegra_bo *target,
				unsigned long offset,
				uint32_t drm_bo_table_flags)
{
	struct drm_tegra_cmdstream_reloc reloc;
	unsigned int i;
	int err;

	if (!job)
		return -EINVAL;

	for (i = 0; i < job->num_bos; i++) {
		if (job->bo_table[i].handle == target->handle) {
			if (drm_bo_table_flags & DRM_TEGRA_BO_TABLE_WRITE)
				job->bo_table[i].flags |= DRM_TEGRA_BO_TABLE_WRITE;

			if (!(drm_bo_table_flags & DRM_TEGRA_BO_TABLE_EXPLICIT_FENCE))
				job->bo_table[i].flags &= ~DRM_TEGRA_BO_TABLE_EXPLICIT_FENCE;

			break;
		}
	}

	if (i == job->num_bos) {
		if (job->num_bos == job->num_bos_max) {
			err = drm_tegra_job_resize_v2(
				job, job->num_words, job->num_bos_max + 8, true);
			if (err)
				return err;
		}

		job->bo_table[i].handle = target->handle;
		job->bo_table[i].flags = drm_bo_table_flags;
		job->num_bos++;
	}

	reloc.bo_index = i;
	reloc.bo_offset = target->offset + offset;

	offset = (unsigned long)(job->ptr - job->start);

	if (offset == job->num_words) {
		err = drm_tegra_job_resize_v2(
			job, job->num_words + 256, job->num_bos_max, true);
		if (err)
			return err;
	}

	*job->ptr++ = reloc.u_data;

	return 0;
}

int drm_tegra_job_submit_v2(struct drm_tegra_job_v2 *job,
			    uint32_t syncobj_handle_in,
			    uint32_t syncobj_handle_out,
			    uint64_t pipes_mask)
{
	struct drm_tegra_submit_v2 args;

	if (!job)
		return -EINVAL;

	args.in_fence			= syncobj_handle_in;
	args.out_fence			= syncobj_handle_out;
	args.cmdstream_ptr		= (uintptr_t)job->start;
	args.bo_table_ptr		= (uintptr_t)job->bo_table;
	args.num_cmdstream_words	= (uint32_t)(job->ptr - job->start);
	args.num_bos			= job->num_bos;
	args.pipes			= pipes_mask;
	args.uapi_ver			= 0;
	args.flags			= 0;

	return drmCommandWriteRead(job->drm->fd, DRM_TEGRA_SUBMIT_V2,
				   &args, sizeof(args));
}
