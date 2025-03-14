# SPDX-License-Identifier: GPL-2.0-only

ifeq ($(CONFIG_SYNC_FILE), y)
	CONFIG_QCOM_KSYNC = m

	LINUXINCLUDE += -I$(KSYNC_PATH)/include \
			-I$(KSYNC_PATH)/include/uapi \
			-I$(KSYNC_PATH)/ \
			-I$(KERNEL_SRC)/include \
			-I$(KERNEL_SRC)/include/uapi/linux

	ccflags-y += -DCONFIG_QCOM_KSYNC=1 \
				-Iinclude/linux
endif

obj-$(CONFIG_QCOM_KSYNC) += qcom_ksync.o

qcom_ksync-y += src/ksync.o \
		src/ksync_ioctl.o \
		src/ksync_fence.o \
		src/ksync_timeline.o \
		src/ksync_source.o

qcom_ksync-$(CONFIG_DEBUG_FS) += src/ksync_debugfs.o
