// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "ksync_timeline.h"
#include "ksync_trace.h"

static void ksync_timeline_release(struct kref *kref)
{
	struct ksync_timeline *timeline = container_of(kref,
					struct ksync_timeline,
					kref);
	struct ksync_priv *dev_priv = timeline->dev_priv;
	struct qcom_ksync *ksync = dev_priv->dev;

	trace_ksync_timeline_release(timeline);
	ksync_process_priv_put(dev_priv);
	spin_lock(&ksync->timeline_lock);
	idr_remove(&ksync->timeline_idr, timeline->id);
	timeline->id = 0;
	spin_unlock(&ksync->timeline_lock);

	kfree(timeline);
}

struct ksync_timeline *ksync_timeline_get(
		struct ksync_timeline *timeline)
{
	if (timeline && !kref_get_unless_zero(&timeline->kref))
		return NULL;

	return timeline;
}

struct ksync_timeline *ksync_timeline_get_by_id(
		struct ksync_priv *dev_priv, s32 id)
{
	struct qcom_ksync *ksync = dev_priv->dev;
	struct ksync_timeline *timeline;
	s32 ret = 0;

	spin_lock(&ksync->timeline_lock);
	timeline = idr_find(&ksync->timeline_idr, id);
	if (timeline)
		ret = kref_get_unless_zero(&timeline->kref);
	spin_unlock(&ksync->timeline_lock);

	return ret ? timeline : NULL;
}

void ksync_timeline_put(struct ksync_timeline *timeline)
{
	if (timeline)
		kref_put(&timeline->kref, ksync_timeline_release);
}

static struct ksync_timeline *ksync_timeline_alloc(
		struct ksync_priv *dev_priv,
		u32 flag, u64 seqno)
{
	struct qcom_ksync *ksync = dev_priv->dev;
	struct ksync_timeline *timeline = NULL;
	s32 id;

	timeline = kzalloc(sizeof(*timeline), GFP_KERNEL);
	if (!timeline)
		return ERR_PTR(-ENOMEM);

	idr_preload(GFP_KERNEL);
	spin_lock(&ksync->timeline_lock);
	id = idr_alloc_cyclic(&ksync->timeline_idr, NULL, 1, 0, GFP_NOWAIT);
	spin_unlock(&ksync->timeline_lock);
	idr_preload_end();

	if (id < 0) {
		kfree(timeline);
		return ERR_PTR(id);
	}

	timeline->ksync_ctx = dma_fence_context_alloc(1);
	timeline->id = id;
	timeline->rb_tree = RB_ROOT;
	INIT_LIST_HEAD(&timeline->fence_list);
	timeline->dev_priv = dev_priv;
	timeline->flag = flag;
	timeline->signaled_seqno = seqno;
	timeline->is64 = ((flag & KSYNC_64BITS_TIMELINE) != 0);
	snprintf((char *) timeline->name, sizeof(timeline->name),
		"ksync-timeline-%llu", timeline->ksync_ctx);

	spin_lock_init(&timeline->lock);
	spin_lock_init(&timeline->fence_lock);

	kref_init(&timeline->kref);
	trace_ksync_timeline_alloc(timeline);
	return timeline;
}

void ksync_timeline_add_fence(struct ksync_timeline *timeline,
		struct ksync_fence **out_fence, u32 flag)
{
	unsigned long flags;
	struct rb_node **p = &timeline->rb_tree.rb_node;
	struct rb_node *parent = NULL;
	struct ksync_fence *fence = *out_fence;

	spin_lock_irqsave(&timeline->fence_lock, flags);
	while (*p) {
		struct ksync_fence *node;

		parent = *p;
		node = rb_entry(parent, typeof(*node), rb_node);
		if (fence->seqno == node->seqno) {
			if (dma_fence_get_rcu(&node->base)) {
				ksync_timeline_put(timeline);
				kfree(fence);
				*out_fence = node;
				pr_debug("fence seqno %llu exists\n", node->seqno);
				goto unlock;
			}
			p = &parent->rb_left;
		} else if (ksync_seqno_is_later(fence->seqno,
				node->seqno, timeline->is64)) {
			p = &parent->rb_right;
		} else {
			p = &parent->rb_left;
		}
	}
	rb_link_node(&fence->rb_node, parent, p);
	rb_insert_color(&fence->rb_node, &timeline->rb_tree);

	parent = rb_next(&fence->rb_node);
	list_add_tail(&fence->link,
			parent ?
			&rb_entry(parent, typeof(*fence), rb_node)->link :
			&timeline->fence_list);

unlock:
	spin_unlock_irqrestore(&timeline->fence_lock, flags);
}

static long ksync_timeline_destruct(struct ksync_priv *dev_priv,
		struct ksync_timeline *timeline)
{
	struct ksync_fence *cur, *next;
	unsigned long flags;
	struct list_head destructed;

	if (!timeline)
		return -EINVAL;

	INIT_LIST_HEAD(&destructed);

	spin_lock_irqsave(&timeline->fence_lock, flags);
	list_for_each_entry_safe(cur, next,
				&timeline->fence_list, link) {
		if (!dma_fence_get_rcu(&cur->base))
			list_del_init(&cur->link);
		rb_erase(&cur->rb_node, &timeline->rb_tree);
	}
	list_replace_init(&timeline->fence_list, &destructed);
	spin_unlock_irqrestore(&timeline->fence_lock, flags);

	spin_lock_irq(&timeline->lock);
	list_for_each_entry_safe(cur, next, &destructed, link) {
		dma_fence_set_error(&cur->base, -ENOENT);
		dma_fence_signal_locked(&cur->base);
		dma_fence_put(&cur->base);
	}
	spin_unlock_irq(&timeline->lock);

	trace_ksync_timeline_destroy(timeline);
	ksync_timeline_put(timeline);
	return 0;
}

static long ksync_timeline_destroy(
		struct ksync_priv *dev_priv, s32 id)
{
	struct qcom_ksync *ksync = dev_priv->dev;
	struct ksync_timeline *timeline;

	spin_lock(&ksync->timeline_lock);
	timeline = idr_find(&ksync->timeline_idr, id);
	spin_unlock(&ksync->timeline_lock);

	if (timeline == NULL)
		return -EINVAL;

	return ksync_timeline_destruct(dev_priv, timeline);
}

void ksync_timeline_value_str(struct dma_fence *base,
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

	snprintf(str, size, "Last retired seqno:%llu",
			timeline->signaled_seqno);
	ksync_timeline_put(timeline);
out:
	return;
}

long ksync_ioctl_timeline_create(
		struct ksync_priv *dev_priv,
		unsigned long cmd, void *kdata)
{
	struct qcom_ksync *ksync = dev_priv->dev;
	struct ksync_timeline_create *param = kdata;
	struct ksync_timeline *timeline;

	if (!ksync_process_priv_get(dev_priv))
		return -EINVAL;

	timeline = ksync_timeline_alloc(dev_priv,
					param->flag, param->seqno);
	if (IS_ERR(timeline)) {
		ksync_process_priv_put(dev_priv);
		return PTR_ERR(timeline);
	}

	/* Commit the timeline to timeline idr */
	spin_lock(&ksync->timeline_lock);
	idr_replace(&ksync->timeline_idr, timeline, timeline->id);
	param->id = timeline->id;
	spin_unlock(&ksync->timeline_lock);

	return 0;
}

long ksync_ioctl_timeline_destroy(
		struct ksync_priv *dev_priv,
		unsigned long cmd, void *kdata)
{
	s32 *param = kdata;

	return ksync_timeline_destroy(dev_priv, *param);
}

long ksync_ioctl_timeline_signal(
		struct ksync_priv *dev_priv,
		unsigned long cmd, void *kdata)
{
	struct ksync_timeline_signal *param = kdata;
	u64 addr;
	u32 i;

	if (!param->timelines_size)
		param->timelines_size = sizeof(struct ksync_timeline_data);

	addr = param->timelines;
	for (i = 0; i < param->count; i++) {
		struct ksync_timeline *timeline;
		struct ksync_timeline_data data;

		if (copy_struct_from_user(&data, sizeof(data),
				u64_to_user_ptr(addr),
				param->timelines_size))
			return -EFAULT;

		if (data.padding)
			return -EINVAL;

		timeline = ksync_timeline_get_by_id(dev_priv, data.id);
		if (!timeline)
			return -ENODEV;

		ksync_forward(timeline, data.seqno, false);

		ksync_timeline_put(timeline);

		addr += param->timelines_size;
	}

	return 0;
}

long ksync_ioctl_timeline_query(
		struct ksync_priv *dev_priv,
		unsigned long cmd, void *kdata)
{
	struct ksync_timeline_query *param = kdata;
	struct ksync_timeline_data data;
	u64 addr;
	u32 i;
	long ret = 0;

	if (!param->timelines_size)
		param->timelines_size = sizeof(struct ksync_timeline_data);

	addr = param->timelines;
	for (i = 0; i < param->count; i++) {
		if (copy_struct_from_user(&data, sizeof(data),
				u64_to_user_ptr(addr),
				param->timelines_size)) {
			ret = -EFAULT;
			goto out;
		}

		if (data.padding) {
			ret = -EINVAL;
			goto out;
		}

		ret = ksync_query(dev_priv, data.id, &data.seqno);
		if (ret)
			goto out;

		if (copy_to_user(u64_to_user_ptr(addr),
				&data, sizeof(data))) {
			ret = -EFAULT;
			goto out;
		}
		addr += param->timelines_size;
	}

out:
	return ret;
}

long ksync_ioctl_timeline_wait(
		struct ksync_priv *dev_priv,
		unsigned long cmd, void *kdata)
{
	return ksync_wait_multiple(dev_priv, kdata);
}

void ksync_timeline_process_release(
		struct ksync_priv *dev_priv)
{
	struct qcom_ksync *ksync = dev_priv->dev;
	struct ksync_timeline *cur, *next;
	u32 idr;
	LIST_HEAD(released);

	spin_lock(&ksync->timeline_lock);
	idr_for_each_entry(&ksync->timeline_idr, cur, idr) {
		if (cur->dev_priv == dev_priv)
			list_add(&cur->free_node, &released);
	}
	spin_unlock(&ksync->timeline_lock);

	list_for_each_entry_safe(cur, next, &released, free_node) {
		list_del(&cur->free_node);
		ksync_timeline_destruct(dev_priv, cur);
	}
}
