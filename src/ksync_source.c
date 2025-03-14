// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2019, 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "ksync_source.h"
#include "ksync_trace.h"

static const struct dma_fence_ops ksync_source_fence_ops;
long ksync_ioctl_source_create(struct ksync_priv *dev_priv,
					unsigned long cmd, void *kdata)
{
	struct ksync_source *syncsource = NULL;
	struct ksync_source_create *param = kdata;
	int ret = -EINVAL;
	int id = 0;

	if (!ksync_process_priv_get(dev_priv))
		return ret;

	syncsource = kzalloc(sizeof(*syncsource), GFP_KERNEL);
	if (syncsource == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	kref_init(&syncsource->refcount);
	snprintf(syncsource->name, sizeof(syncsource->name),
		"ksync-source-pid-%d", current->group_leader->pid);
	syncsource->dev_priv = dev_priv;
	INIT_LIST_HEAD(&syncsource->source_list_head);
	spin_lock_init(&syncsource->lock);

	idr_preload(GFP_KERNEL);
	spin_lock(&dev_priv->source_lock);
	id = idr_alloc_cyclic(&dev_priv->source_idr,
			syncsource, 1, 0, GFP_NOWAIT);
	if (id > 0) {
		syncsource->id = id;
		param->id = id;
		ret = 0;
	} else {
		ret = id;
	}

	spin_unlock(&dev_priv->source_lock);
	idr_preload_end();

out:
	if (ret) {
		ksync_process_priv_put(dev_priv);
		kfree(syncsource);
	}

	return ret;
}

static struct ksync_source *
ksync_source_get(struct ksync_priv *dev_priv, int id)
{
	int result = 0;
	struct ksync_source *syncsource = NULL;

	spin_lock(&dev_priv->source_lock);

	syncsource = idr_find(&dev_priv->source_idr, id);
	if (syncsource)
		result = kref_get_unless_zero(&syncsource->refcount);

	spin_unlock(&dev_priv->source_lock);

	return result ? syncsource : NULL;
}

static void ksync_source_destroy(struct kref *kref)
{
	struct ksync_source *syncsource = container_of(kref,
						struct ksync_source,
						refcount);

	struct ksync_priv *dev_priv = syncsource->dev_priv;

	/* Done with process dev_priv. Release the refcount */
	ksync_process_priv_put(dev_priv);

	kfree(syncsource);
}

void ksync_source_put(struct ksync_source *syncsource)
{
	if (syncsource)
		kref_put(&syncsource->refcount, ksync_source_destroy);
}

static void ksync_source_cleanup(struct ksync_priv *dev_priv,
				struct ksync_source *syncsource)
{
	struct ksync_source_fence *fence, *next;
	unsigned long flags;

	/* Signal all fences to release any callbacks */
	spin_lock_irqsave(&syncsource->lock, flags);

	list_for_each_entry_safe(fence, next,
				&syncsource->source_list_head,
				source_link) {
		dma_fence_signal_locked(&fence->base);
		list_del_init(&fence->source_link);
	}

	spin_unlock_irqrestore(&syncsource->lock, flags);

	/* put reference from syncsource creation */
	ksync_source_put(syncsource);
}

long ksync_ioctl_source_destroy(struct ksync_priv *dev_priv,
					unsigned long cmd, void *kdata)
{
	struct ksync_source_destroy *param = kdata;
	struct ksync_source *syncsource = NULL;

	spin_lock(&dev_priv->source_lock);
	syncsource = idr_find(&dev_priv->source_idr, param->id);

	if (syncsource == NULL) {
		spin_unlock(&dev_priv->source_lock);
		return -EINVAL;
	}

	if (syncsource->id != 0) {
		idr_remove(&dev_priv->source_idr, syncsource->id);
		syncsource->id = 0;
	}
	spin_unlock(&dev_priv->source_lock);

	ksync_source_cleanup(dev_priv, syncsource);
	return 0;
}

long ksync_ioctl_source_create_fence(struct ksync_priv *dev_priv,
					unsigned long cmd, void *kdata)
{
	struct ksync_source_create_fence *param = kdata;
	struct ksync_source *syncsource = NULL;
	int ret = -EINVAL;
	struct ksync_source_fence *fence = NULL;
	struct sync_file *sync_file = NULL;
	int fd = -1;
	unsigned long flags;

	/*
	 * Take a refcount that is released when the fence is released
	 * (or if fence can't be added to the syncsource).
	 */
	syncsource = ksync_source_get(dev_priv,
					param->id);
	if (syncsource == NULL)
		goto out;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (fence == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	fence->parent = syncsource;

	/* Use a new fence context for each fence */
	dma_fence_init(&fence->base, &ksync_source_fence_ops,
		&syncsource->lock, dma_fence_context_alloc(1), 1);

	sync_file = sync_file_create(&fence->base);

	if (sync_file == NULL) {
		dev_err(dev_priv->dev->dev,
			     "Create sync_file failed\n");
		ret = -ENOMEM;
		goto out;
	}

	fd = get_unused_fd_flags(0);
	if (fd < 0) {
		ret = -EBADF;
		goto out;
	}
	ret = 0;

	fd_install(fd, sync_file->file);

	param->fence_fd = fd;

	spin_lock_irqsave(&syncsource->lock, flags);
	list_add_tail(&fence->source_link,
			&syncsource->source_list_head);
	spin_unlock_irqrestore(&syncsource->lock, flags);

	trace_ksync_source_fence_create(fence);
out:
	/*
	 * We're transferring ownership of the fence to the sync file.
	 * The sync file takes an extra refcount when it is created, so put
	 * our refcount.
	 */
	if (sync_file)
		dma_fence_put(&fence->base);

	if (ret) {
		if (sync_file)
			fput(sync_file->file);
		else if (fence)
			dma_fence_put(&fence->base);
		else
			ksync_source_put(syncsource);
	}

	return ret;
}

static int ksync_source_signal(struct ksync_source *syncsource,
					struct dma_fence *base)
{
	struct ksync_source_fence *fence, *next;
	int ret = -EINVAL;
	unsigned long flags;

	spin_lock_irqsave(&syncsource->lock, flags);

	list_for_each_entry_safe(fence, next,
				&syncsource->source_list_head,
				source_link) {
		if (base == &fence->base) {
			dma_fence_signal_locked(base);
			list_del_init(&fence->source_link);

			ret = 0;
			break;
		}
	}

	spin_unlock_irqrestore(&syncsource->lock, flags);

	trace_ksync_source_fence_signal(fence);
	return ret;
}

long ksync_ioctl_source_signal_fence(struct ksync_priv *dev_priv,
					unsigned long cmd, void *kdata)
{
	int ret = -EINVAL;
	struct ksync_source_signal_fence *param = kdata;
	struct ksync_source *syncsource = NULL;
	struct dma_fence *base = NULL;

	syncsource = ksync_source_get(dev_priv,
					param->id);
	if (syncsource == NULL)
		goto out;

	base = sync_file_get_fence(param->fence_fd);
	if (base == NULL) {
		ret = -EBADF;
		goto out;
	}

	ret = ksync_source_signal(syncsource, base);
out:
	if (base)
		dma_fence_put(base);
	if (syncsource)
		ksync_source_put(syncsource);
	return ret;
}

static void ksync_source_fence_release(struct dma_fence *base)
{
	struct ksync_source_fence *fence =
			(struct ksync_source_fence *)base;

	/* Signal if it's not signaled yet */
	ksync_source_signal(fence->parent, base);

	trace_ksync_source_fence_release(fence);
	/* Release the refcount on the syncsource */
	ksync_source_put(fence->parent);

	dma_fence_free(base);
}

void ksync_source_process_release(
		struct ksync_priv *dev_priv)
{
	struct ksync_source *syncsource;
	int next = 0;

	while (1) {
		spin_lock(&dev_priv->source_lock);
		syncsource = idr_get_next(&dev_priv->source_idr, &next);

		if (syncsource == NULL) {
			spin_unlock(&dev_priv->source_lock);
			break;
		}

		if (syncsource->id != 0) {
			idr_remove(&dev_priv->source_idr, syncsource->id);
			syncsource->id = 0;
		}
		spin_unlock(&dev_priv->source_lock);

		ksync_source_cleanup(dev_priv, syncsource);
		next = next + 1;
	}
}

static const char *ksync_source_get_timeline_name(struct dma_fence *base)
{
	struct ksync_source_fence *fence =
			(struct ksync_source_fence *)base;
	struct ksync_source *syncsource = fence->parent;

	return syncsource->name;
}

static bool ksync_source_enable_signaling(struct dma_fence *base)
{
	return true;
}

static const char *ksync_source_driver_name(struct dma_fence *base)
{
	return "ksync-syncsource-timeline";
}

static void ksync_source_fence_value_str(struct dma_fence *base,
						char *str, int size)
{
	/*
	 * Each fence is independent of the others on the same timeline.
	 * We use a different context for each of them.
	 */
	snprintf(str, size, "%llu", base->context);
}

static const struct dma_fence_ops ksync_source_fence_ops = {
	.get_driver_name = ksync_source_driver_name,
	.get_timeline_name = ksync_source_get_timeline_name,
	.enable_signaling = ksync_source_enable_signaling,
	.wait = dma_fence_default_wait,
	.release = ksync_source_fence_release,

	.fence_value_str = ksync_source_fence_value_str,
};
