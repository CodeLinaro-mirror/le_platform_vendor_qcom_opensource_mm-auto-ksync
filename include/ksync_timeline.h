/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2022, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __KSYNC_TIMELINE_H__
#define __KSYNC_TIMELINE_H__

#include "ksync_fence.h"

#define KSYNC_TIMELINE_NAME_LEN (64)

/**
 * struct ksync_timeline - A ksync timeline
 * @kref: Refcount to keep the struct alive
 * @lock: Spinlock to protect this timeline
 * @fence_lock: Spinlock to protect fence_list
 * @name: String to describe this timeline
 * @id: timeline id
 * @ksync_ctx: Used by the fence driver to identify fences belonging to
 *		   this context
 * @free_node: Node of the released timeline
 * @fence_list: List head for all fences on this timeline
 * @rb_tree: rb tree of the fences
 * @dev_priv: per-process private data
 * @signaled_seqno: Last timestamp when signaling fences
 * @flag: reserved for fulture use
 * @is64: 32bit or 64bit seqno
 */
struct ksync_timeline {
	struct kref kref;
	spinlock_t lock;
	spinlock_t fence_lock;
	char name[KSYNC_TIMELINE_NAME_LEN];
	s32 id;
	u64 ksync_ctx;
	struct list_head free_node;
	struct list_head fence_list;
	struct rb_root rb_tree;
	struct ksync_priv *dev_priv;
	u64 signaled_seqno;
	u32 flag;
	bool is64;
};

struct ksync_timeline *ksync_timeline_get(
			struct ksync_timeline *timeline);
struct ksync_timeline *ksync_timeline_get_by_id(
			struct ksync_priv *dev_priv, s32 id);
void ksync_timeline_put(struct ksync_timeline *timeline);
void ksync_timeline_value_str(struct dma_fence *base,
			char *str, s32 size);
long ksync_ioctl_timeline_create(struct ksync_priv *dev_priv,
			unsigned long cmd, void *kdata);
long ksync_ioctl_timeline_destroy(struct ksync_priv *dev_priv,
			unsigned long cmd, void *kdata);
long ksync_ioctl_fence_create(struct ksync_priv *dev_priv,
			unsigned long cmd, void *kdata);
long ksync_ioctl_fence_signal(struct ksync_priv *dev_priv,
			unsigned long cmd, void *kdata);
long ksync_ioctl_forward(struct ksync_priv *dev_priv,
			unsigned long cmd, void *kdata);
long ksync_ioctl_timeline_signal(struct ksync_priv *dev_priv,
			unsigned long cmd, void *kdata);
long ksync_ioctl_timeline_query(struct ksync_priv *dev_priv,
			unsigned long cmd, void *kdata);
long ksync_ioctl_timeline_wait(struct ksync_priv *dev_priv,
			unsigned long cmd, void *kdata);
void ksync_timeline_add_fence(struct ksync_timeline *timeline,
			struct ksync_fence **out_fence, u32 flag);

void ksync_timeline_process_release(
			struct ksync_priv *dev_priv);

#endif /* __KSYNC_TIMELINE_H__ */
