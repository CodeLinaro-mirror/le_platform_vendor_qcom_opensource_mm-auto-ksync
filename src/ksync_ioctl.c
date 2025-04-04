// SPDX-License-Ideksync_ntifier: GPL-2.0-only
/*
 * Copyright (c) 2008-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "ksync_timeline.h"
#include "ksync_source.h"

#define KSYNC_IOCTL_FUNC(_cmd, _func) \
	[_IOC_NR((_cmd))] = \
		{ .cmd = (_cmd), .func = (_func) }

struct ksync_ioctl {
	u32 cmd;
	long (*func)(struct ksync_priv *dev_priv,
			unsigned long cmd, void *kdata);
};

static const struct ksync_ioctl ksync_ioctl_func_table[] = {
	KSYNC_IOCTL_FUNC(IOCTL_KSYNC_SOURCE_CREATE,
		ksync_ioctl_source_create),
	KSYNC_IOCTL_FUNC(IOCTL_KSYNC_SOURCE_DESTROY,
		ksync_ioctl_source_destroy),
	KSYNC_IOCTL_FUNC(IOCTL_KSYNC_SOURCE_CREATE_FENCE,
		ksync_ioctl_source_create_fence),
	KSYNC_IOCTL_FUNC(IOCTL_KSYNC_SOURCE_SIGNAL_FENCE,
		ksync_ioctl_source_signal_fence),

	KSYNC_IOCTL_FUNC(KSYNC_IOCTL_TIMELINE_CREATE,
		ksync_ioctl_timeline_create),
	KSYNC_IOCTL_FUNC(KSYNC_IOCTL_TIMELINE_DESTROY,
		ksync_ioctl_timeline_destroy),
	KSYNC_IOCTL_FUNC(KSYNC_IOCTL_FENCE_CREATE,
		ksync_ioctl_fence_create),
	KSYNC_IOCTL_FUNC(KSYNC_IOCTL_FENCE_SIGNAL,
		ksync_ioctl_fence_signal),
	KSYNC_IOCTL_FUNC(KSYNC_IOCTL_FORWARD,
		ksync_ioctl_forward),
	KSYNC_IOCTL_FUNC(KSYNC_IOCTL_TIMELINE_SIGNAL,
		ksync_ioctl_timeline_signal),
	KSYNC_IOCTL_FUNC(KSYNC_IOCTL_TIMELINE_QUERY,
		ksync_ioctl_timeline_query),
	KSYNC_IOCTL_FUNC(KSYNC_IOCTL_TIMELINE_WAIT,
		ksync_ioctl_timeline_wait),
};

long ksync_ioctl_copy_in(u32 kernel_cmd, u32 user_cmd,
		unsigned long arg, unsigned char *ptr)
{
	u32 usize = _IOC_SIZE(user_cmd);
	u32 ksize = _IOC_SIZE(kernel_cmd);
	u32 copy = ksize < usize ? ksize : usize;

	if ((kernel_cmd & IOC_IN) && (user_cmd & IOC_IN)) {
		if (copy > 0 && copy_from_user(ptr, (void __user *) arg, copy))
			return -EFAULT;
	}

	return 0;
}

long ksync_ioctl_copy_out(u32 kernel_cmd, u32 user_cmd,
		unsigned long arg, unsigned char *ptr)
{
	u32 usize = _IOC_SIZE(user_cmd);
	u32 ksize = _IOC_SIZE(kernel_cmd);
	u32 copy = ksize < usize ? ksize : usize;

	if ((kernel_cmd & IOC_OUT) && (user_cmd & IOC_OUT) &&
			kernel_cmd != KSYNC_IOCTL_TIMELINE_QUERY) {
		if (copy > 0 && copy_to_user((void __user *) arg, ptr, copy))
			return -EFAULT;
	}

	return 0;
}

long ksync_ioctl(struct file *filep,
	u32 cmd, unsigned long arg)
{
	struct ksync_priv *dev_priv = filep->private_data;
	const struct ksync_ioctl *cmds = ksync_ioctl_func_table;
	s32 len = ARRAY_SIZE(ksync_ioctl_func_table);
	u32 nr = _IOC_NR(cmd);
	unsigned char kdata[128] = { 0 };
	long ret;

	if (nr >= len || cmds[nr].func == NULL)
		return -ENOIOCTLCMD;

	if (_IOC_SIZE(cmds[nr].cmd) > sizeof(kdata)) {
		dev_err_ratelimited(dev_priv->dev->dev,
			"data too big for ioctl 0x%08x: %d/%zu\n",
			cmd, _IOC_SIZE(cmds[nr].cmd), sizeof(kdata));
		return -EINVAL;
	}

	if (_IOC_SIZE(cmds[nr].cmd)) {
		ret = ksync_ioctl_copy_in(cmds[nr].cmd, cmd, arg, kdata);
		if (ret)
			return ret;
	}

	ret = cmds[nr].func(dev_priv, cmd, kdata);

	if (ret == 0 && _IOC_SIZE(cmds[nr].cmd))
		ret = ksync_ioctl_copy_out(cmds[nr].cmd, cmd, arg, kdata);

	return ret;
}

long ksync_compat_ioctl(struct file *filep,
	u32 cmd, unsigned long arg)
{
	return ksync_ioctl(filep, cmd, arg);
}
