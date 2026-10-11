#ifndef CAMERA_UAPI_H
#define CAMERA_UAPI_H
#include <linux/types.h>
#include <linux/ioctl.h>

#define CAMERA_VERSION 1
#define CAMERA_COUNT 8
#define CAMERA_MODES 32
#define CAMERA_BUFFERS 8
#define CAMERA_NAME 64
#define CAMERA_SOCKET "linux-camera-broker-v1"

struct camera_mode
{
	__u32 width;
	__u32 height;
	__u32 fps;
	__u32 reserved;
};

struct camera_register
{
	__u32 version;
	__u32 size;
	char name[CAMERA_NAME];
	char identifier[CAMERA_NAME];
	__u32 mode_count;
	__u32 index;
	__s32 video_number;
	__u32 reserved;
	struct camera_mode modes[CAMERA_MODES];
};

/* QUERY reports desired state. A stopped generation requires ACK_STOP before
 * its buffers can be used again. Only one hardware stream is admitted in v1. */
struct camera_state
{
	__u32 index;
	__u32 running;
	__u64 generation;
	struct camera_mode mode;
	__u64 completed;
	__u64 dropped;
	__s32 error;
	__u32 reserved;
};

struct camera_buffer
{
	__u32 camera;
	__u32 index;
	__u64 generation;
	__u64 token;
	__u64 timestamp_ns;
	__u32 bytes;
	__s32 fd;
};

#define CAMERA_REGISTER _IOWR('K', 0x40, struct camera_register)
#define CAMERA_QUERY _IOWR('K', 0x41, struct camera_state)
#define CAMERA_ACQUIRE _IOWR('K', 0x42, struct camera_buffer)
#define CAMERA_COMPLETE _IOW('K', 0x43, struct camera_buffer)
#define CAMERA_ACK_STOP _IOW('K', 0x44, struct camera_state)
#define CAMERA_FAIL _IOW('K', 0x45, struct camera_state)
#define CAMERA_EXPORT _IOWR('K', 0x46, struct camera_buffer)
#define CAMERA_EVENTS _IOR('K', 0x47, __u64)
#endif
