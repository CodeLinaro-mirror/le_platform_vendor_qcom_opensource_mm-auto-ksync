// SPDX-License-Ideksync_ntifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/jiffies.h>

#include "ksync_fence.h"
#include "ksync_timeline.h"
#include "ksync_trace.h"

#define INFINITE_TIMEOUT          (~(0ULL))

static const struct dma_fence_ops ksync_fence_ops;
struct ksync_fence *ksync_alloc_fence(
				struct ksync_timeline *timeline,
				u64 seqno, u32 flag)
{
	struct ksync_fence *fence;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return ERR_PTR(-ENOMEM);

	fence->timeline = ksync_timeline_get(timeline);
	if (!fence->timeline) {
		kfree(fence);
		return ERR_PTR(-ENOENT);
	}

	fence->seqno = seqno;
	fence->flag = flag;
	dma_fence_init(&fence->base, &ksync_fence_ops,
		&timeline->lock, timeline->ksync_ctx, seqno);
	INIT_LIST_HEAD(&fence->link);
	spin_lock_irq(&timeline->lock);
	if (!dma_fence_is_signaled_locked(&fence->base))
		ksync_timeline_add_fence(timeline, &fence, flag);

	trace_ksync_timeline_fence_alloc(fence);

	spin_unlock_irq(&timeline->lock);

	return fence;
}

struct ksync_fence *to_ksync_fence(struct dma_fence *base)
{
	struct ksync_fence *fence = NULL;

	if (!base)
		goto out;

	fence = container_of(base, struct ksync_fence, base);
	if (!fence)
		pr_warn("[%s:%d] fence is NULL dma_fence:%p\n",
				__func__, __LINE__, base);

out:
	return fence;
}

void ksync_forward(struct ksync_timeline *timeline,
			u64 seqno, s32 force)
{
	struct ksync_fence *cur, *next;
	struct list_head signalled;

	INIT_LIST_HEAD(&signalled);
	spin_lock_irq(&timeline->lock);

	if (!force) {
		if (ksync_seqno_is_later(timeline->signaled_seqno,
				seqno, timeline->is64))
			goto unlock; /* already signaled */

		timeline->signaled_seqno = seqno;
	}

	spin_lock(&timeline->fence_lock);
	list_for_each_entry_safe(cur, next, &timeline->fence_list, link) {
		if ((force ||
			ksync_seqno_is_later(seqno, cur->seqno, timeline->is64)) &&
			dma_fence_get_rcu(&cur->base)) {
			list_move_tail(&cur->link, &signalled);
			rb_erase(&cur->rb_node, &timeline->rb_tree);

			/* update signaled_seqno for force forward */
			if (force)
				timeline->signaled_seqno = cur->seqno;
		}
	}
	spin_unlock(&timeline->fence_lock);

	list_for_each_entry_safe(cur, next, &signalled, link) {
		dma_fence_signal_locked(&cur->base);
		trace_ksync_timeline_fence_signal(cur);
		dma_fence_put(&cur->base);
	}

	trace_ksync_timeline_signal(timeline);
unlock:
	spin_unlock_irq(&timeline->lock);
}

long ksync_query(struct ksync_priv *dev_priv,
			s32 id,
			u64 *seqno)
{
	struct ksync_timeline *timeline;
	long ret = 0;

	timeline = ksync_timeline_get_by_id(dev_priv, id);
	if (!timeline) {
		ret = -EINVAL;
		goto out;
	}

	*seqno = timeline->signaled_seqno;
	ksync_timeline_put(timeline);

out:
	return ret;
}

static struct dma_fence *ksync_fence_array_create(
		struct ksync_priv *dev_priv, u64 timelines,
		u32 count, u64 usize, bool any)
{
	void __user *uptr = u64_to_user_ptr(timelines);
	struct dma_fence_array *array;
	struct dma_fence **fences;
	struct ksync_fence *fence = NULL;
	struct dma_fence *out_fence = NULL;
	struct ksync_timeline_data data;
	struct ksync_timeline *timeline = NULL;
	s32 i, ret = 0;

	if (!count || count > INT_MAX) {
		out_fence = ERR_PTR(-EINVAL);
		goto out;
	}

	fences = kcalloc(count, sizeof(*fences),
		GFP_KERNEL | __GFP_NORETRY | __GFP_NOWARN | __GFP_ZERO);
	if (!fences) {
		out_fence = ERR_PTR(-ENOMEM);
		goto out;
	}

	for (i = 0; i < count; i++) {
		if (copy_struct_from_user(&data, sizeof(data), uptr, usize)) {
			ret = -EFAULT;
			goto err;
		}

		if (data.padding) {
			ret = -EINVAL;
			goto err;
		}

		timeline = ksync_timeline_get_by_id(dev_priv, data.id);
		if (!timeline) {
			ret = -EINVAL;
			goto out;
		}

		fence = ksync_alloc_fence(timeline,
					data.seqno, 0);
		ksync_timeline_put(timeline);
		if (IS_ERR_OR_NULL(fence)) {
			ret = -ENOMEM;
			goto out;
		}

		fences[i] = &fence->base;
		uptr += usize;
	}

	/* No need for a fence array for only one fence */
	if (count == 1) {
		out_fence = fences[0];
		kfree(fences);
		goto out;
	}

	array = dma_fence_array_create(count, fences,
			dma_fence_context_alloc(1), 0, any);
	if (array)
		out_fence = &array->base;
	else
		ret = -ENOMEM;

err:
	if (ret) {
		for (i = 0; i < count; i++) {
			if (!IS_ERR_OR_NULL(fences[i]))
				dma_fence_put(fences[i]);
		}
		kfree(fences);
		out_fence = ERR_PTR(ret);
	}
out:
	return out_fence;
}

long ksync_wait_multiple(struct ksync_priv *dev_priv,
				struct ksync_timeline_wait *param)
{
	struct dma_fence *base;
	struct timespec64 timespec;
	unsigned long timeout;
	long ret;

	if (param->flags != WAIT_ANY &&
			param->flags != WAIT_ALL) {
		pr_err("Invalid ksync wait flags:0x%x", param->flags);
		ret = -EINVAL;
		goto out;
	}

	if (param->padding) {
		pr_err("Invalid ksync wait padding:0x%x", param->padding);
		ret = -EINVAL;
		goto out;
	}

	if (!param->timelines_size)
		param->timelines_size = sizeof(struct ksync_timeline_data);

	base = ksync_fence_array_create(dev_priv, param->timelines,
				param->count, param->timelines_size,
				(param->flags == WAIT_ANY));
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		goto out;
	}

	if (param->timeout_nanosec == INFINITE_TIMEOUT)
		timeout = MAX_SCHEDULE_TIMEOUT;
	else {
		timespec.tv_sec = param->timeout_nanosec / NSEC_PER_SEC;
		timespec.tv_nsec = param->timeout_nanosec % NSEC_PER_SEC;
		timeout = timespec64_to_jiffies(&timespec);
	}

	trace_ksync_timeline_wait(param->flags, timespec.tv_sec, timespec.tv_nsec);

	if (!timeout)
		ret = dma_fence_is_signaled(base) ? 0 : -EBUSY;
	else {
		ret = dma_fence_wait_timeout(base, true, timeout);
		if (!ret)
			ret = -ETIMEDOUT;
		else if (ret > 0)
			ret = 0;
		else if (ret == -ERESTARTSYS)
			ret = -EINTR;
	}

	dma_fence_put(base);

out:
	return ret;
}


static const char *ksync_get_driver_name(struct dma_fence *base)
{
	return "ksync";
}

static const char *ksync_get_timeline_name(struct dma_fence *base)
{
	struct ksync_fence *fence = to_ksync_fence(base);
	struct ksync_timeline *timeline = NULL;

	if (!fence)
		goto out;

	timeline = fence->timeline;
out:
	return (timeline == NULL) ? "null" : timeline->name;
}

static bool ksync_is_signaled(struct dma_fence *base)
{
	struct ksync_fence *fence = to_ksync_fence(base);
	struct ksync_timeline *timeline = NULL;
	bool ret = false;

	if (!fence)
		goto out;

	timeline = ksync_timeline_get(fence->timeline);
	if (!timeline) {
		pr_warn("timeline is NULL fence:%p seqno:%llu\n",
			fence, fence->seqno);
		goto out;
	}

	ret = ksync_seqno_is_later(
				timeline->signaled_seqno,
				fence->seqno, timeline->is64);
	ksync_timeline_put(timeline);
out:
	return ret;
}

static bool ksync_enable_signaling(struct dma_fence *base)
{
	return !ksync_is_signaled(base);
}

static void ksync_fence_release(struct dma_fence *base)
{
	unsigned long flags;
	struct ksync_fence *cur, *next, *fence = to_ksync_fence(base);
	struct ksync_timeline *timeline = NULL;

	if (!fence || !(fence->timeline))
		goto out;

	trace_ksync_timeline_fence_release(fence);
	timeline = fence->timeline;
	spin_lock_irqsave(&timeline->fence_lock, flags);
	/* If the fence is still on the active list, remove it */
	list_for_each_entry_safe(cur, next,
			&timeline->fence_list, link) {
		if (fence == cur) {
			list_del_init(&fence->link);
			rb_erase(&fence->rb_node, &timeline->rb_tree);
			break;
		}
	}
	spin_unlock_irqrestore(&timeline->fence_lock, flags);

	dma_fence_signal_locked(base);
	ksync_timeline_put(timeline);

out:
	dma_fence_free(base);
	return;
}

static void ksync_fence_value_str(struct dma_fence *base,
				char *str, s32 size)
{
	struct ksync_fence *fence = to_ksync_fence(base);
	struct ksync_timeline *timeline = NULL;

	if (!fence)
		goto out;

	timeline = ksync_timeline_get(fence->timeline);
	if (!timeline) {
		pr_warn("[%s:%d] timeline is NULL fence:%p seqno:%llu\n",
			__func__, __LINE__, fence, fence->seqno);
		goto out;
	}

	snprintf(str, size, "%llu-%llu", base->context, fence->seqno);
	ksync_timeline_put(timeline);
out:
	return;
}

static const struct dma_fence_ops ksync_fence_ops = {
	.get_driver_name = ksync_get_driver_name,
	.get_timeline_name = ksync_get_timeline_name,
	.enable_signaling = ksync_enable_signaling,
	.signaled = ksync_is_signaled,
	.release = ksync_fence_release,
	.fence_value_str = ksync_fence_value_str,
	.timeline_value_str = ksync_timeline_value_str,
};

long ksync_ioctl_fence_create(struct ksync_priv *dev_priv,
					unsigned long cmd, void *kdata)
{
	struct ksync_fence_create *param = kdata;
	struct ksync_timeline *timeline = ksync_timeline_get_by_id(
							dev_priv,
							param->id);
	struct ksync_fence *fence = NULL;
	struct sync_file *sync_file = NULL;
	s32 ret = 0;
	s32 fd = -1;

	if (!timeline)
		return -EINVAL;

	fence = ksync_alloc_fence(timeline,
						param->seqno, 0);
	if (IS_ERR_OR_NULL(fence)) {
		ksync_timeline_put(timeline);
		return PTR_ERR(fence);
	}

	fd = get_unused_fd_flags(0);
	if (fd < 0) {
		ret = fd;
		goto out;
	}

	sync_file = sync_file_create(&fence->base);
	if (sync_file) {
		fd_install(fd, sync_file->file);
		param->fd = fd;
	} else {
		put_unused_fd(fd);
		ret = -ENOMEM;
	}

out:
	dma_fence_put(&fence->base);
	ksync_timeline_put(timeline);

	return ret;
}

long ksync_ioctl_fence_signal(struct ksync_priv *dev_priv,
				unsigned long cmd, void *kdata)
{
	struct ksync_signal_fence *param = kdata;
	struct ksync_timeline *timeline;

	timeline = ksync_timeline_get_by_id(dev_priv, param->id);
	if (!timeline)
		return -ENODEV;

	ksync_forward(timeline, param->seqno, false);
	ksync_timeline_put(timeline);

	return 0;
}

long ksync_ioctl_forward(struct ksync_priv *dev_priv,
				unsigned long cmd, void *kdata)
{
	struct ksync_forward *param = kdata;
	struct ksync_timeline *timeline;

	timeline = ksync_timeline_get_by_id(dev_priv, param->id);
	if (!timeline)
		return -ENODEV;

	ksync_forward(timeline, param->seqno, param->force);

	ksync_timeline_put(timeline);

	return 0;
}
