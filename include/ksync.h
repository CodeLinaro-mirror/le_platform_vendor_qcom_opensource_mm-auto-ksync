/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2022, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __KSYNC_H__
#define __KSYNC_H__

#include <linux/unistd.h>
#include <linux/cdev.h>
#include <linux/dma-buf.h>
#include <linux/dma-fence.h>
#include <linux/spinlock.h>
#include <linux/sync_file.h>
#include <uapi/linux/msm_ksync.h>
#include <linux/file.h>
#include <linux/list.h>
#include <linux/kref.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/delay.h>

#define KSYNC_DEVICE_NAME  "ksync"

#define KSYNC_DEV_NUM  (1)
#define SEQNO64_WINDOW (0x8000000000000000LL)
#define SEQNO32_WINDOW (0x80000000)

struct qcom_ksync {
	struct device *dev;

	/* character device info */
	struct cdev cdev;
	dev_t device_no;
	struct class *driver_class;
	struct device *class_dev;

	struct mutex mutex;
	struct list_head process_list;

	struct idr timeline_idr;
	spinlock_t timeline_lock;

	/* Debug nodes */
	struct dentry *debugfs;
	struct dentry *clients_debugfs;
};

struct ksync_priv {
	struct qcom_ksync *dev;
	struct list_head node;
	struct mutex lock;
	struct kref refcount;
	int fd_count; /* Counter for the number of FDs for this process */
	struct pid *pid;

	struct idr source_idr;
	spinlock_t source_lock;

	/* debugfs */
	struct dentry *debugfs_client;
	struct dentry *debugfs_mem;
	struct dentry *debugfs_memtype;
};

long ksync_ioctl(struct file *filep,
			u32 cmd, unsigned long arg);
long ksync_compat_ioctl(struct file *filep,
			u32 cmd, unsigned long arg);

/**
 * ksync_seqno_is_later - return if f1 is chronologically later than f2
 * @f1: the first fence's seqno
 * @f2: the second fence's seqno from the same context
 *
 * Returns true if f1 is chronologically later than f2. Both fences must be
 * from the same context, since a seqno is not common across contexts.
 */
static inline bool ksync_seqno_is_later(
	u64 f1, u64 f2, bool is64)
{
	if (is64)
		return (f1 - f2) < SEQNO64_WINDOW;

	return (lower_32_bits(f1) - lower_32_bits(f2)) < SEQNO32_WINDOW;
}

static inline void *ksync_zalloc(size_t size)
{
	if (size <= PAGE_SIZE)
		return kzalloc(size, GFP_KERNEL);

	return vzalloc(size);
}

static inline void ksync_free(void *ptr)
{
	if (ptr != NULL) {
		if (is_vmalloc_addr(ptr))
			vfree(ptr);
		else
			kfree(ptr);
	}
}

/**
 * ksync_process_priv_get() - increment the refcount on a
 * kgsl_process_private struct
 * @process: Pointer to the KGSL process_private
 *
 * Returns 0 if the structure is invalid and a reference count could not be
 * obtained, nonzero otherwise.
 */
static inline int ksync_process_priv_get(struct ksync_priv *dev_priv)
{
	if (dev_priv != NULL)
		return kref_get_unless_zero(&dev_priv->refcount);
	return 0;
}

void ksync_process_priv_put(struct ksync_priv *dev_priv);

#endif /* __KSYNC_H__ */
