/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM ksync

#if !defined(_TRACE_KSYNC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_KSYNC_H

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE ksync_trace

#include <linux/tracepoint.h>

struct ksync_timeline;
struct ksync_fence;
struct ksync_source_fence;

DECLARE_EVENT_CLASS(ksync_timeline,

	TP_PROTO(struct ksync_timeline *timeline),

	TP_ARGS(timeline),

	TP_STRUCT__entry(
		__string(name, timeline->name)
		__field(u32, id)
		__field(bool, is64)
		__field(u64, seqno)
		__field(unsigned int, refcnt)
	),

	TP_fast_assign(
		__assign_str(name, timeline->name);
		__entry->id = timeline->id;
		__entry->is64 = timeline->is64;
		__entry->seqno = timeline->signaled_seqno;
		__entry->refcnt = kref_read(&timeline->kref);
	),

	TP_printk("name=%s id=%u%s signaled seqno=%llu refcnt=%u",
		__get_str(name), __entry->id,
		__entry->is64 ? " 64bit":"",
		__entry->seqno, __entry->refcnt)
);

DEFINE_EVENT(ksync_timeline, ksync_timeline_alloc,

	TP_PROTO(struct ksync_timeline *timeline),

	TP_ARGS(timeline)
);

DEFINE_EVENT(ksync_timeline, ksync_timeline_destroy,

	TP_PROTO(struct ksync_timeline *timeline),

	TP_ARGS(timeline)
);

DEFINE_EVENT(ksync_timeline, ksync_timeline_release,

	TP_PROTO(struct ksync_timeline *timeline),

	TP_ARGS(timeline)
);

DEFINE_EVENT(ksync_timeline, ksync_timeline_signal,

	TP_PROTO(struct ksync_timeline *timeline),

	TP_ARGS(timeline)
);

DECLARE_EVENT_CLASS(ksync_fence,

	TP_PROTO(struct ksync_fence *fence),

	TP_ARGS(fence),

	TP_STRUCT__entry(
		__string(name, fence->timeline->name)
		__field(u32, id)
		__field(long, fence)
		__field(u64, seqno)
		__field(unsigned int, refcnt)
	),

	TP_fast_assign(
		__assign_str(name, fence->timeline->name);
		__entry->id = fence->timeline->id;
		__entry->fence = (long)(void *)fence;
		__entry->seqno = fence->seqno;
		__entry->refcnt = kref_read(&fence->base.refcount);
	),

	TP_printk("timeline=%s id=%u fence=0x%lx seqno=%llu refcnt=%u",
		  __get_str(name), __entry->id, __entry->fence,
		  __entry->seqno, __entry->refcnt)
);

DEFINE_EVENT(ksync_fence, ksync_timeline_fence_alloc,

	TP_PROTO(struct ksync_fence *fence),

	TP_ARGS(fence)
);

DEFINE_EVENT(ksync_fence, ksync_timeline_fence_signal,

	TP_PROTO(struct ksync_fence *fence),

	TP_ARGS(fence)
);

DEFINE_EVENT(ksync_fence, ksync_timeline_fence_release,

	TP_PROTO(struct ksync_fence *fence),

	TP_ARGS(fence)
);

DECLARE_EVENT_CLASS(ksync_source_fence,

	TP_PROTO(struct ksync_source_fence *fence),

	TP_ARGS(fence),

	TP_STRUCT__entry(
		__string(name, fence->parent->name)
		__field(u32, id)
		__field(unsigned int, refcnt)
	),

	TP_fast_assign(
		__assign_str(name, fence->parent->name);
		__entry->id = fence->parent->id;
		__entry->refcnt = kref_read(&fence->base.refcount);
	),

	TP_printk("timeline=%s id=%u refcnt=%u",
		__get_str(name), __entry->id,
		__entry->refcnt)
);

DEFINE_EVENT(ksync_source_fence, ksync_source_fence_create,

	TP_PROTO(struct ksync_source_fence *fence),

	TP_ARGS(fence)
);

DEFINE_EVENT(ksync_source_fence, ksync_source_fence_signal,

	TP_PROTO(struct ksync_source_fence *fence),

	TP_ARGS(fence)
);

DEFINE_EVENT(ksync_source_fence, ksync_source_fence_release,

	TP_PROTO(struct ksync_source_fence *fence),

	TP_ARGS(fence)
);

TRACE_EVENT(ksync_timeline_wait,
	TP_PROTO(
		u32 flags,
		s64 tv_sec,
		s64 tv_nsec
	),
	TP_ARGS(
		flags,
		tv_sec,
		tv_nsec
	),
	TP_STRUCT__entry(
		__field(u32, flags)
		__field(s64, tv_sec)
		__field(s64, tv_nsec)
	),
	TP_fast_assign(
		__entry->flags = flags;
		__entry->tv_sec = tv_sec;
		__entry->tv_nsec = tv_nsec;
	),
	TP_printk("flags=0x%x tv_sec=%llu tv_nsec=%llu",
		__entry->flags, __entry->tv_sec, __entry->tv_nsec

	)
);

#endif /* _TRACE_KSYNC_H */

 #include <trace/define_trace.h>
