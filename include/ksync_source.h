/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2022, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __KSYNC_SOURCE_H__
#define __KSYNC_SOURCE_H__

#include "ksync.h"

struct ksync_source {
	struct kref refcount;
	char name[32];
	int id;
	struct ksync_priv *dev_priv;
	struct list_head source_list_head;
	spinlock_t lock;
};

struct ksync_source_fence {
	struct dma_fence base;
	struct ksync_source *parent;
	struct list_head source_link;
};

long ksync_ioctl_source_create(struct ksync_priv *dev_priv,
		unsigned long cmd, void *data);
void ksync_source_put(struct ksync_source *syncsource);
long ksync_ioctl_source_destroy(struct ksync_priv *dev_priv,
		unsigned long cmd, void *data);
long ksync_ioctl_source_create_fence(struct ksync_priv *dev_priv,
		unsigned long cmd, void *data);
long ksync_ioctl_source_signal_fence(struct ksync_priv *dev_priv,
		unsigned long cmd, void *data);
void ksync_source_process_release(
		struct ksync_priv *dev_priv);

#endif /* __KSYNC_SOURCE_H__ */