/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef CAMERA_KERNEL_H
#define CAMERA_KERNEL_H
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include "camera_uapi.h"
typedef struct
{
	struct vb2_v4l2_buffer buffer;
	struct list_head pending;
} camera_buffer_t;

typedef struct
{
	struct v4l2_device device;
	struct video_device video;
	struct vb2_queue queue;
	struct mutex mutex;
	wait_queue_head_t retirement;
	spinlock_t pending_lock;
	struct list_head pending;
	struct camera_register description;
	struct camera_mode mode;
	u64 generation;
	u64 token;
	u64 completed;
	u64 dropped;
	u32 claimed_index;
	u32 sequence;
	bool claimed;
	bool running;
	bool stopping;
	bool connected;
	s32 error;
} camera_t;

extern const struct v4l2_ioctl_ops video_ioctl_ops;
#endif
