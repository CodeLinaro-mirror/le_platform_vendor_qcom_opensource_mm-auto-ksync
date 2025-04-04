/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note
 * Copyright (c) 2020-2022, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __UAPI_KSYNC_H__
#define __UAPI_KSYNC_H__

#include <linux/types.h>
#include <linux/ioctl.h>

enum ksync_wait_type {
	WAIT_ALL = 0x01,
	WAIT_ANY = 0x02,
};

#define KSYNC_32BITS_TIMELINE (0)
#define KSYNC_64BITS_TIMELINE (1)

/**
 * struct ksync_source_create - Argument to IOCTL_KSYNC_SOURCE_CREATE
 * @id: returned id for the syncsource that was created.
 *
 * This ioctl creates a userspace sync timeline.
 */

struct ksync_source_create {
	__u32 id;
	__u32 __pad[3]; /* private: reserved for future use */
};
#define IOCTL_KSYNC_SOURCE_CREATE \
	_IOWR('S', 0x10, struct ksync_source_create)

/**
 * struct ksync_source_destroy - Argument to IOCTL_KSYNC_SOURCE_DESTROY
 * @id: syncsource id to destroy
 *
 * This ioctl creates a userspace sync timeline.
 */

struct ksync_source_destroy {
	__u32 id;
	__u32 __pad[3]; /* private: reserved for future use */
};
#define IOCTL_KSYNC_SOURCE_DESTROY \
	_IOW('S', 0x11, struct ksync_source_destroy)

/**
 * struct ksync_source_create_fence - Argument to
 *     IOCTL_KSYNC_SOURCE_CREATE_FENCE
 * @id: syncsource id
 * @fence_fd: returned sync_fence fd
 *
 * Create a fence that may be signaled by userspace by calling
 * IOCTL_KSYNC_SOURCE_CREATE_FENCE. There are no order dependencies between
 * these fences.
 */
struct ksync_source_create_fence {
	__u32 id;
	__s32 fence_fd;
	__u32 __pad[4]; /* private: reserved for future use */
};
#define IOCTL_KSYNC_SOURCE_CREATE_FENCE \
	_IOWR('S', 0x12, struct ksync_source_create_fence)

/**
 * struct ksync_source_signal_fence - Argument to
 *     IOCTL_KSYNC_SOURCE_SIGNAL_FENCE
 * @id: syncsource id
 * @fence_fd: sync_fence fd
 *
 * Signal a fence that may be signaled by userspace by calling
 * IOCTL_KSYNC_SOURCE_SIGNAL_FENCE. There are no order dependencies between
 * these fences.
 */
struct ksync_source_signal_fence {
	__u32 id;
	__s32 fence_fd;
	__u32 __pad[4]; /* private: reserved for future use */
};
#define IOCTL_KSYNC_SOURCE_SIGNAL_FENCE \
	_IOW('S', 0x13, struct ksync_source_signal_fence)

/**
 * struct ksync_timeline_create - create a ksync timeline with 64 or 32bits timestamp
 * @seqno: The initial timestamp value.
 * @id: The timeline id created
 * @flag: flag to create timeline
 */
struct ksync_timeline_create {
	__u64 seqno;
	__s32 id;
	__s32 flag;
	__u32 __pad[4]; /* private: reserved for future use */
};
#define KSYNC_IOCTL_TIMELINE_CREATE \
	_IOWR('S', 0x21, struct ksync_timeline_create)
#define KSYNC_IOCTL_TIMELINE_DESTROY \
	_IOW('S', 0x22, __s32)

/**
 * struct ksync_fence_create - wait a ksync fence
 * @id: The timeline id for current fence
 * @seqno: The user timestamp attached to the fence
 * @fd: File descriptor of the new created fence
 */
struct ksync_fence_create {
	__s32 id;
	__u64 seqno;
	__s32 fd;
	__u32 __pad[4]; /* private: reserved for future use */
};
#define KSYNC_IOCTL_FENCE_CREATE \
	_IOWR('S', 0x23, struct ksync_fence_create)

/**
 * struct ksync_signal_fence - signal an ksync fence
 * @id: The timeline id for current fence
 * @seqno: The signaled seqno
 */
struct ksync_signal_fence {
	__s32 id;
	__u64 seqno;
	__u32 __pad[4]; /* private: reserved for future use */
};
#define KSYNC_IOCTL_FENCE_SIGNAL \
	_IOW('S', 0x24, struct ksync_signal_fence)

/**
 * struct ksync_forward - forward ksync timeline
 * @id: The timeline id for current fence
 * @seqno: The seqno to forward
 * @force: Force to forward
 */
struct ksync_forward {
	__s32 id;
	__u64 seqno;
	__s32 force;
	__u32 __pad[4]; /* private: reserved for future use */
};
#define KSYNC_IOCTL_FORWARD \
	_IOW('S', 0x25, struct ksync_forward)

/**
 * struct ksync_timeline_signal - signal multiple timelines
 * @timelines: Address of an array of &struct hgsl_timeline_val entries
 * @count: Number of entries in @timeline
 * @timelines_size: Size of each entry in @timelines
 */
struct ksync_timeline_signal {
	__u64 timelines;
	__u32 count;
	__u32 timelines_size;
	__u32 __pad[4]; /* private: reserved for future use */
};
#define KSYNC_IOCTL_TIMELINE_SIGNAL \
	_IOW('S', 0x26, struct ksync_timeline_signal)
/**
 * struct ksync_timeline_data - A container to store a timeline/sequence number
 * pair.
 * @seqno: The seqno of the fence
 * @id: The timeline id
 *
 * A container to store a timeline/timepoint pair used by the query, signal and wait
 * ioctls.
 */
struct ksync_timeline_data {
	__u64 seqno;
	__s32 id;
	__u32 padding;
	__u32 __pad[4]; /* private: reserved for future use */
};
/**
 * struct ksync_timeline_query - query multiple timelines
 * @timelines: Address of an array of &struct ksync_timeline_val entries
 * @count: Number of entries in @timeline
 * @timelines_size: Size of each entry in @timelines
 */
struct ksync_timeline_query {
	__u64 timelines;
	__u32 count;
	__u32 timelines_size;
	__u32 __pad[4]; /* private: reserved for future use */
};
#define KSYNC_IOCTL_TIMELINE_QUERY \
	_IOWR('S', 0x27, struct ksync_timeline_query)

/**
 * struct ksync_timeline_wait - wait multiple timelines
 * @timeout_nanosec: Number of nanoseconds to wait for the signal
 * @timelines: Address of an array of &struct ksync_timeline_val entries
 * @count: Number of entries in @timeline
 * @timelines_size: Size of each entry in @timelines
 * @flags: One of WAIT_ALL or WAIT_ANY
 * @padding: padding for 64 bit compatibility
 */
struct ksync_timeline_wait {
	__u64 timeout_nanosec;
	__u64 timelines;
	__u32 count;
	__u32 timelines_size;
	__u32 flags;
	__u32 padding;
	__u32 __pad[4]; /* private: reserved for future use */
};
#define KSYNC_IOCTL_TIMELINE_WAIT \
	_IOW('S', 0x28, struct ksync_timeline_wait)

#endif /* __UAPI_KSYNC_H__ */
