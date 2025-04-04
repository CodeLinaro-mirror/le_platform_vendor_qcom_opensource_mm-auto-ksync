// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2022, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/async.h>

#include "ksync.h"
#include "ksync_timeline.h"
#include "ksync_source.h"
#include "ksync_debugfs.h"

/* Instantiate tracepoints */
#define CREATE_TRACE_POINTS
#include "ksync_trace.h"

static struct ksync_priv *ksync_process_priv_open(
	struct qcom_ksync *ksync)
{
	struct ksync_priv *dev_priv;
	struct pid *cur_pid = get_task_pid(
				current->group_leader, PIDTYPE_PID);

	list_for_each_entry(dev_priv, &ksync->process_list, node) {
		if (dev_priv->pid == cur_pid) {
			if (!ksync_process_priv_get(dev_priv))
				dev_priv = ERR_PTR(-EEXIST);
			else {
				mutex_lock(&dev_priv->lock);
				dev_priv->fd_count++;
				mutex_unlock(&dev_priv->lock);
			}

			put_pid(cur_pid);
			return dev_priv;
		}
	}

	dev_priv = ksync_zalloc(sizeof(*dev_priv));
	if (!dev_priv) {
		put_pid(cur_pid);
		return ERR_PTR(-ENOMEM);
	}

	mutex_init(&dev_priv->lock);
	kref_init(&dev_priv->refcount);
	dev_priv->fd_count = 1;
	dev_priv->pid = cur_pid;
	dev_priv->dev = ksync;
	idr_init(&dev_priv->source_idr);

	list_add(&dev_priv->node, &ksync->process_list);
	return dev_priv;
}

static s32 ksync_open(struct inode *inodep, struct file *filep)
{
	struct ksync_priv *dev_priv;
	struct qcom_ksync *ksync = container_of(inodep->i_cdev,
					struct qcom_ksync, cdev);
	int i = 0;

	mutex_lock(&ksync->mutex);
	dev_priv = ksync_process_priv_open(ksync);
	/*
	 * If we get error and error is -EEXIST that means previous process
	 * private destroy is triggered but didn't complete. Retry creating
	 * process private after sometime to allow previous destroy to complete.
	 */
	for (; (PTR_ERR_OR_ZERO(dev_priv) == -EEXIST) && (i < 50); i++) {
		usleep_range(10, 100);
		dev_priv = ksync_process_priv_open(ksync);
	}

	if (IS_ERR(dev_priv)) {
		mutex_unlock(&ksync->mutex);
		return PTR_ERR(dev_priv);
	}

	ksync_debugfs_client_init(dev_priv);
	filep->private_data = dev_priv;
	mutex_unlock(&ksync->mutex);

	return 0;
}

static void ksync_destroy_process_priv(struct kref *kref)
{
	struct ksync_priv *dev_priv = container_of(kref,
			struct ksync_priv, refcount);
	struct qcom_ksync *ksync = dev_priv->dev;

	mutex_lock(&ksync->mutex);
	list_del(&dev_priv->node);

	ksync_debugfs_client_release(dev_priv);
	mutex_unlock(&ksync->mutex);

	put_pid(dev_priv->pid);
	idr_destroy(&dev_priv->source_idr);
	ksync_free(dev_priv);
}

void ksync_process_priv_put(struct ksync_priv *dev_priv)
{
	if (dev_priv)
		kref_put(&dev_priv->refcount, ksync_destroy_process_priv);
}

static s32 ksync_release(struct inode *inodep, struct file *filep)
{
	struct ksync_priv *dev_priv = filep->private_data;

	mutex_lock(&dev_priv->lock);
	if (--dev_priv->fd_count > 0) {
		mutex_unlock(&dev_priv->lock);
		ksync_process_priv_put(dev_priv);
		return 0;
	}
	ksync_source_process_release(dev_priv);
	ksync_timeline_process_release(dev_priv);
	mutex_unlock(&dev_priv->lock);

	ksync_process_priv_put(dev_priv);
	return 0;
}

static const struct file_operations ksync_fops = {
	.owner = THIS_MODULE,
	.open = ksync_open,
	.release = ksync_release,
	.unlocked_ioctl = ksync_ioctl,
	.compat_ioctl = ksync_compat_ioctl
};

static s32 qcom_ksync_register(struct platform_device *pdev,
	struct qcom_ksync *ksync)
{
	s32 ret;

	ret = alloc_chrdev_region(&ksync->device_no, 0,
						KSYNC_DEV_NUM,
						KSYNC_DEVICE_NAME);
	if (ret < 0) {
		pr_err("alloc_chrdev_region failed %d\n", ret);
		return ret;
	}

	ksync->driver_class = class_create(KSYNC_DEVICE_NAME);
	if (IS_ERR(ksync->driver_class)) {
		ret = -ENOMEM;
		pr_err("class_create failed %d\n", ret);
		goto exit_unreg_chrdev_region;
	}

	ksync->class_dev = device_create(
					ksync->driver_class,
					NULL,
					ksync->device_no,
					ksync, KSYNC_DEVICE_NAME);

	if (IS_ERR(ksync->class_dev)) {
		pr_err("class_device_create failed %d\n", ret);
		ret = -ENOMEM;
		goto exit_destroy_class;
	}

	cdev_init(&ksync->cdev, &ksync_fops);

	ksync->cdev.owner = THIS_MODULE;

	ret = cdev_add(&ksync->cdev,
					MKDEV(MAJOR(ksync->device_no), 0),
					1);
	if (ret < 0) {
		pr_err("cdev_add failed %d\n", ret);
		goto exit_destroy_device;
	}

	return 0;

exit_destroy_device:
	device_destroy(ksync->driver_class, ksync->device_no);
exit_destroy_class:
	class_destroy(ksync->driver_class);
exit_unreg_chrdev_region:
	unregister_chrdev_region(ksync->device_no, 1);
	return ret;
}

static void qcom_ksync_deregister(struct platform_device *pdev)
{
	struct qcom_ksync *ksync = platform_get_drvdata(pdev);

	cdev_del(&ksync->cdev);
	device_destroy(ksync->driver_class, ksync->device_no);
	class_destroy(ksync->driver_class);
	unregister_chrdev_region(ksync->device_no, KSYNC_DEV_NUM);
}

static s32 qcom_ksync_probe(struct platform_device *pdev)
{
	struct qcom_ksync *ksync;
	s32 ret;

	ksync = devm_kzalloc(&pdev->dev, sizeof(*ksync), GFP_KERNEL);
	if (!ksync)
		return -ENOMEM;

	ksync->dev = &pdev->dev;

	ret = qcom_ksync_register(pdev, ksync);
	if (ret < 0) {
		pr_err("qcom_ksync_register failed, ret %d\n",
			ret);
		return ret;
	}

	INIT_LIST_HEAD(&ksync->process_list);

	idr_init(&ksync->timeline_idr);
	spin_lock_init(&ksync->timeline_lock);

	platform_set_drvdata(pdev, ksync);
	ksync_debugfs_init(pdev);

	return 0;
}

static s32 qcom_ksync_remove(struct platform_device *pdev)
{
	struct qcom_ksync *ksync = platform_get_drvdata(pdev);

	ksync_debugfs_release(pdev);

	idr_destroy(&ksync->timeline_idr);
	qcom_ksync_deregister(pdev);
	platform_device_unregister(pdev);
	return 0;
}

static struct platform_driver qcom_ksync_driver = {
	.probe = qcom_ksync_probe,
	.remove = qcom_ksync_remove,
	.driver  = {
		.name  = "ksync",
	},
};

static s32 __init ksync_init(void)
{
	s32 err;
	static struct platform_device *ksync_pdev;

	ksync_pdev = platform_device_alloc("ksync", -1);
	if (!ksync_pdev) {
		pr_err("Failed to allocate ksync device\n");
		err = -EINVAL;
		goto out;
	}

	err = platform_device_add(ksync_pdev);
	if (err != 0) {
		pr_err("Failed to register ksync device: %d\n", err);
		platform_device_put(ksync_pdev);
		goto out;
	}

	err = platform_driver_register(&qcom_ksync_driver);
	if (err) {
		platform_device_unregister(ksync_pdev);
		pr_err("Failed to register ksync driver: %d\n", err);
	}

out:
	return err;
}

static void __init _ksync_init(void *data, async_cookie_t cookie)
{
	ksync_init();
}

static int __init ksync_init_async(void)
{
	async_schedule(_ksync_init, NULL);
	return 0;
}

static void __exit ksync_exit(void)
{
	platform_driver_unregister(&qcom_ksync_driver);
}

subsys_initcall(ksync_init_async);
module_exit(ksync_exit);

MODULE_DESCRIPTION("QTI Kernel sync driver");
MODULE_LICENSE("GPL v2");
