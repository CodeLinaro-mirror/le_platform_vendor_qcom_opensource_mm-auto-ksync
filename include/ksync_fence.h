/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2022, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __KSYNC_FENCE_H__
#define __KSYNC_FENCE_H__

#include "ksync.h"

/**
 * struct ksync_fence - A struct containing a fence and other data
 *				associated with it
 * @fence: The fence struct
 * @sync_file: Pointer to the sync file
 * @parent: Pointer to the hgsl ksync timeline this fence is on
 * @link: List of fences on the same timeline
 * @seqno: Context timestamp that this fence is associated with
 */
struct ksync_fence {
	struct dma_fence base;
	struct sync_file *sync_file;
	struct ksync_timeline *timeline;
	struct list_head link;
	struct rb_node rb_node;
	u64 seqno;
	u32 flag;
};

struct ksync_fence *to_ksync_fence(struct dma_fence *base);

void ksync_forward(struct ksync_timeline *timeline,
				u64 seqno, s32 force);
long ksync_query(struct ksync_priv *dev_priv,
				s32 id,
				u64 *seqno);
long ksync_wait_multiple(struct ksync_priv *dev_priv,
				struct ksync_timeline_wait *param);

#endif /* __KSYNC_FENCE_H__ */
