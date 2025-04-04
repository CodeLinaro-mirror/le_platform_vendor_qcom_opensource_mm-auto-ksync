// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */
#include <linux/debugfs.h>

#include "ksync.h"
#include "ksync_timeline.h"
#include "ksync_source.h"
#include "ksync_fence.h"
#include "ksync_debugfs.h"

static LIST_HEAD(sync_file_list_head);
static DEFINE_SPINLOCK(sync_file_list_lock);
void ksync_file_debug_add(struct sync_file *sync_file)
{
	unsigned long flags;

	spin_lock_irqsave(&sync_file_list_lock, flags);
	list_add_tail(&sync_file->sync_file_list, &sync_file_list_head);
	spin_unlock_irqrestore(&sync_file_list_lock, flags);
}

void ksync_file_debug_remove(struct sync_file *sync_file)
{
	unsigned long flags;

	spin_lock_irqsave(&sync_file_list_lock, flags);
	list_del(&sync_file->sync_file_list);
	spin_unlock_irqrestore(&sync_file_list_lock, flags);
}

static const char *ksync_status_str(int status)
{
	if (status < 0)
		return "error";

	if (status > 0)
		return "signaled";

	return "active";
}

static void ksync_print_fence(struct seq_file *s,
			     struct dma_fence *base, bool show)
{
	struct ksync_timeline *timeline =
		((struct ksync_fence *)base)->timeline;
	int status;

	status = dma_fence_get_status_locked(base);

	seq_printf(s, "  %s%sfence %s",
		   show ? timeline->name : "",
		   show ? "_" : "",
		   ksync_status_str(status));

	if (test_bit(DMA_FENCE_FLAG_TIMESTAMP_BIT, &base->flags)) {
		struct timespec64 ts64 =
			ktime_to_timespec64(base->timestamp);

		seq_printf(s, "@%lld.%09ld", (s64)ts64.tv_sec, ts64.tv_nsec);
	}

	if (base->ops->timeline_value_str &&
		base->ops->fence_value_str) {
		char value[64];
		bool success;

		base->ops->fence_value_str(base, value, sizeof(value));
		success = strlen(value);

		if (success) {
			seq_printf(s, ": %s", value);

			base->ops->timeline_value_str(base, value,
						       sizeof(value));

			if (strlen(value))
				seq_printf(s, " / %s", value);
		}
	}

	seq_putc(s, '\n');
}

static void ksync_print_timeline(
	struct seq_file *s,
	struct ksync_timeline *timeline)
{
	struct ksync_fence *cur;

	seq_printf(s, "%s: %llu refcnt: %d\n", timeline->name,
			timeline->signaled_seqno, kref_read(&timeline->kref));

	spin_lock_irq(&timeline->lock);
	list_for_each_entry(cur, &timeline->fence_list, link) {
		if (!dma_fence_get_rcu(&cur->base))
			continue;
		ksync_print_fence(s, &cur->base, true);
		dma_fence_put(&cur->base);
	}
	spin_unlock_irq(&timeline->lock);
}

static void ksync_print_source(
	struct seq_file *s,
	struct ksync_source *syncsource)
{
	struct ksync_source_fence *cur;

	seq_printf(s, "%s\n", syncsource->name);

	spin_lock_irq(&syncsource->lock);
	list_for_each_entry(cur,
			&syncsource->source_list_head,
			source_link) {
		if (!dma_fence_get_rcu(&cur->base))
			continue;
		ksync_print_fence(s, &cur->base, true);
		dma_fence_put(&cur->base);
	}
	spin_unlock_irq(&syncsource->lock);
}

static void ksync_print_sync_file(struct seq_file *s,
				  struct sync_file *sync_file)
{
	char buf[128];
	int i;

	seq_printf(s, "[%p] %s: %s\n", sync_file,
		   sync_file_get_name(sync_file, buf, sizeof(buf)),
		   ksync_status_str(dma_fence_get_status(sync_file->fence)));

	if (dma_fence_is_array(sync_file->fence)) {
		struct dma_fence_array *array = to_dma_fence_array(sync_file->fence);

		for (i = 0; i < array->num_fences; ++i)
			ksync_print_fence(s, array->fences[i], true);
	} else {
		ksync_print_fence(s, sync_file->fence, true);
	}
}

static int ksync_client_info_show(struct seq_file *s, void *unused)
{
	struct ksync_priv *dev_priv = s->private;
	struct qcom_ksync *ksync = dev_priv->dev;
	struct ksync_timeline *cur;
	struct ksync_source *cur_src;
	u32 idr;

	seq_puts(s, "timelines:\n--------------\n");

	spin_lock(&ksync->timeline_lock);
	idr_for_each_entry(&ksync->timeline_idr, cur, idr) {
		if (cur->dev_priv == dev_priv) {
			ksync_print_timeline(s, cur);
			seq_putc(s, '\n');
		}
	}
	spin_unlock(&ksync->timeline_lock);

	spin_lock(&dev_priv->source_lock);
	idr_for_each_entry(&dev_priv->source_idr, cur_src, idr) {
		ksync_print_source(s, cur_src);
		seq_putc(s, '\n');
	}
	spin_unlock(&dev_priv->source_lock);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ksync_client_info);

int ksync_debugfs_client_init(struct ksync_priv *priv)
{
	struct qcom_ksync *ksync = priv->dev;
	unsigned char name[16];
	struct dentry *ret;

	snprintf(name, sizeof(name), "%d", pid_nr(priv->pid));
	ret = debugfs_create_dir(name,
				ksync->clients_debugfs);
	if (IS_ERR(ret)) {
		pr_warn("Create debugfs proc node failed.\n");
		return PTR_ERR(ret);
	}

	priv->debugfs_client = ret;
	priv->debugfs_mem = debugfs_create_file("info", 0444,
			priv->debugfs_client,
			priv,
			&ksync_client_info_fops);

	return 0;
}

void ksync_debugfs_client_release(struct ksync_priv *priv)
{
	debugfs_remove_recursive(priv->debugfs_client);
}

void ksync_debugfs_init(struct platform_device *pdev)
{
	struct qcom_ksync *ksync_dev = platform_get_drvdata(pdev);
	struct dentry *root;

	root = debugfs_create_dir("ksync", NULL);

	ksync_dev->debugfs = root;
	ksync_dev->clients_debugfs = debugfs_create_dir("clients", root);
}

void ksync_debugfs_release(struct platform_device *pdev)
{
	struct qcom_ksync *ksync = platform_get_drvdata(pdev);

	debugfs_remove_recursive(ksync->debugfs);
}
