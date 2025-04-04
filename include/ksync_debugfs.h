/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */
#ifndef __KSYNC_DEBUGFS_H__
#define __KSYNC_DEBUGFS_H__

#include <linux/device.h>
#include <linux/platform_device.h>

#ifdef CONFIG_DEBUG_FS
int ksync_debugfs_client_init(struct ksync_priv *priv);
void ksync_debugfs_client_release(struct ksync_priv *priv);

void ksync_debugfs_init(struct platform_device *pdev);
void ksync_debugfs_release(struct platform_device *pdev);
#else
static int ksync_debugfs_client_init(struct ksync_priv *priv)
{
	return 0;
}

void ksync_debugfs_client_release(struct ksync_priv *priv)
{

}

void ksync_debugfs_init(struct platform_device *pdev)
{

}
void ksync_debugfs_release(struct platform_device *pdev)
{

}
#endif

#endif  /* __KSYNC_DEBUGFS_H__ */

