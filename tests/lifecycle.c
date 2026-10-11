#define _GNU_SOURCE
#include "../include/camera_uapi.h"
#include "../include/device.h"
#include <linux/videodev2.h>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static volatile sig_atomic_t _lifecycle_stop;

static void _lifecycle_signal(int32_t __signal)
{
	(void)__signal;
	_lifecycle_stop = 1;
}

static void _lifecycle_check(int32_t __condition, const char *__operation)
{
	if (!__condition)
	{
		fprintf(stderr, "FAIL %s: errno=%d %s\n", __operation, errno, strerror(errno));
		exit(1);
	}
}

static uint64_t _lifecycle_time(void)
{
	struct timespec _time;
	clock_gettime(CLOCK_MONOTONIC, &_time);
	return (uint64_t)_time.tv_sec * 1000000000ULL + _time.tv_nsec;
}

int main(int __argc, char **__argv)
{
	int32_t _control = open("/dev/linux_camera", O_RDWR | O_CLOEXEC);
	_lifecycle_check(_control >= 0, "open exclusive producer");
	int32_t _duplicate = open("/dev/linux_camera", O_RDWR | O_CLOEXEC);
	_lifecycle_check(_duplicate < 0 && errno == EBUSY, "second producer rejected");
	struct camera_register _camera = {.version = CAMERA_VERSION, .size = sizeof(_camera), .name = "Linux Camera Synthetic", .identifier = "synthetic", .mode_count = 1, .color_profile = CAMERA_COLOR_BT601_625_FULL, .modes = {{64, 48, 30, 0}}};
	struct camera_register _invalid = _camera;
	_invalid.version = 1;
	_lifecycle_check(ioctl(_control, CAMERA_REGISTER, &_invalid) < 0 && errno == EINVAL, "old producer contract rejected");
	_invalid = _camera;
	_invalid.color_profile = 0;
	_lifecycle_check(ioctl(_control, CAMERA_REGISTER, &_invalid) < 0 && errno == EINVAL, "unknown color profile rejected");
	_lifecycle_check(ioctl(_control, CAMERA_REGISTER, &_camera) == 0, "register camera");
	_lifecycle_check(ioctl(_control, CAMERA_REGISTER, &_camera) < 0 && errno == EEXIST, "duplicate endpoint rejected");
	char _path[64];
	snprintf(_path, sizeof(_path), "/dev/video%d", _camera.video_number);
	int32_t _consumer = device_open(_path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	_lifecycle_check(_consumer >= 0, "open consumer");
	if (__argc == 2 && strcmp(__argv[1], "--serve") == 0)
	{
		close(_consumer);
		signal(SIGTERM, _lifecycle_signal);
		signal(SIGINT, _lifecycle_signal);
		printf("%s\n", _path);
		fflush(stdout);
		while (!_lifecycle_stop)
			pause();
		close(_control);
		return 0;
	}
	const uint32_t _formatCommands[] = {VIDIOC_G_FMT, VIDIOC_TRY_FMT, VIDIOC_S_FMT};
	for (size_t _i = 0; _i < sizeof(_formatCommands) / sizeof(_formatCommands[0]); ++_i)
	{
		struct v4l2_format _format = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .fmt.pix = {.width = 64, .height = 48, .pixelformat = V4L2_PIX_FMT_YUV420, .priv = V4L2_PIX_FMT_PRIV_MAGIC}};
		_lifecycle_check(ioctl(_consumer, _formatCommands[_i], &_format) == 0, "query and negotiate full-range color");
		_lifecycle_check(_format.fmt.pix.colorspace == V4L2_COLORSPACE_470_SYSTEM_BG && _format.fmt.pix.ycbcr_enc == V4L2_YCBCR_ENC_601 && _format.fmt.pix.quantization == V4L2_QUANTIZATION_FULL_RANGE && _format.fmt.pix.xfer_func == V4L2_XFER_FUNC_709, "JFIF metadata preserved");
	}
	struct camera_register _limited = _camera;
	strcpy(_limited.identifier, "synthetic-limited");
	_limited.color_profile = CAMERA_COLOR_BT709_LIMITED;
	_lifecycle_check(ioctl(_control, CAMERA_REGISTER, &_limited) == 0, "register limited-range profile");
	char _limitedPath[64];
	snprintf(_limitedPath, sizeof(_limitedPath), "/dev/video%d", _limited.video_number);
	int32_t _limitedConsumer = device_open(_limitedPath, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	_lifecycle_check(_limitedConsumer >= 0, "open limited-range profile");
	struct v4l2_format _limitedFormat = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
	_lifecycle_check(ioctl(_limitedConsumer, VIDIOC_G_FMT, &_limitedFormat) == 0 && _limitedFormat.fmt.pix.colorspace == V4L2_COLORSPACE_REC709 && _limitedFormat.fmt.pix.ycbcr_enc == V4L2_YCBCR_ENC_709 && _limitedFormat.fmt.pix.quantization == V4L2_QUANTIZATION_LIM_RANGE && _limitedFormat.fmt.pix.xfer_func == V4L2_XFER_FUNC_709, "BT709 metadata preserved");
	close(_limitedConsumer);
	struct v4l2_requestbuffers _request = {.count = 3, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP};
	_lifecycle_check(ioctl(_consumer, VIDIOC_REQBUFS, &_request) == 0 && _request.count == 3, "allocate consumer buffers");
	int32_t _second = open(_path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	struct v4l2_requestbuffers _otherRequest = _request;
	_lifecycle_check(ioctl(_second, VIDIOC_REQBUFS, &_otherRequest) < 0 && errno == EBUSY, "second capture owner rejected");
	close(_second);
	uint8_t *_consumerMaps[3];
	size_t _lengths[3];
	for (uint32_t _i = 0; _i < 3; ++_i)
	{
		struct v4l2_buffer _buffer = {.index = _i, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP};
		_lifecycle_check(ioctl(_consumer, VIDIOC_QUERYBUF, &_buffer) == 0, "query buffer");
		_lengths[_i] = _buffer.length;
		_consumerMaps[_i] = mmap(NULL, _buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, _consumer, _buffer.m.offset);
		_lifecycle_check(_consumerMaps[_i] != MAP_FAILED, "mmap consumer");
		_lifecycle_check(ioctl(_consumer, VIDIOC_QBUF, &_buffer) == 0, "queue buffer");
	}
	uint32_t _type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	_lifecycle_check(ioctl(_consumer, VIDIOC_STREAMON, &_type) == 0, "stream on");
	struct camera_state _state = {.index = _camera.index};
	_lifecycle_check(ioctl(_control, CAMERA_QUERY, &_state) == 0 && _state.running, "on-demand event");
	int32_t _producerDescriptors[3];
	uint8_t *_producerMaps[3];
	struct camera_buffer _last = {0};
	for (uint32_t _frame = 0; _frame < 3; ++_frame)
	{
		struct camera_buffer _buffer = {.camera = _camera.index, .generation = _state.generation};
		_lifecycle_check(ioctl(_control, CAMERA_ACQUIRE, &_buffer) == 0, "claim queued destination");
		_lifecycle_check(ioctl(_control, CAMERA_EXPORT, &_buffer) == 0, "export producer mapping");
		_producerDescriptors[_buffer.index] = _buffer.fd;
		_producerMaps[_buffer.index] = mmap(NULL, _lengths[_buffer.index], PROT_READ | PROT_WRITE, MAP_SHARED, _buffer.fd, 0);
		_lifecycle_check(_producerMaps[_buffer.index] != MAP_FAILED, "mmap exported allocation");
		struct dma_buf_sync _sync = {.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE};
		_lifecycle_check(ioctl(_buffer.fd, DMA_BUF_IOCTL_SYNC, &_sync) == 0, "begin cpu access");
		for (uint32_t _p = 0; _p < _buffer.bytes; ++_p)
		{
			_producerMaps[_buffer.index][_p] = (_frame * 53 + _p * 7) & 255;
		}
		_sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE;
		_lifecycle_check(ioctl(_buffer.fd, DMA_BUF_IOCTL_SYNC, &_sync) == 0, "end cpu access");
		_buffer.timestamp_ns = _lifecycle_time();
		_lifecycle_check(ioctl(_control, CAMERA_COMPLETE, &_buffer) == 0, "publish completion");
		struct v4l2_buffer _capture = {.type = _type, .memory = V4L2_MEMORY_MMAP};
		_lifecycle_check(ioctl(_consumer, VIDIOC_DQBUF, &_capture) == 0 && _capture.index == _buffer.index && _capture.sequence == _frame, "dequeue published frame");
		for (uint32_t _p = 0; _p < _buffer.bytes; ++_p)
		{
			_lifecycle_check(_consumerMaps[_buffer.index][_p] == ((_frame * 53 + _p * 7) & 255), "pattern exact visibility");
		}
		/* Deliberately hold every dequeued frame: the producer may not reclaim it. */
		_last = _buffer;
	}
	struct camera_buffer _empty = {.camera = _camera.index, .generation = _state.generation};
	_lifecycle_check(ioctl(_control, CAMERA_ACQUIRE, &_empty) < 0 && errno == EAGAIN, "held buffers excluded");
	for (uint32_t _i = 0; _i < 3; ++_i)
	{
		for (uint32_t _p = 0; _p < 64 * 48 * 3 / 2; ++_p)
		{
			_lifecycle_check(_consumerMaps[_i][_p] == ((_i * 53 + _p * 7) & 255), "held pattern never overwritten");
		}
	}
	struct v4l2_buffer _requeue = {.index = 0, .type = _type, .memory = V4L2_MEMORY_MMAP};
	_lifecycle_check(ioctl(_consumer, VIDIOC_QBUF, &_requeue) == 0, "requeue one");
	_lifecycle_check(ioctl(_control, CAMERA_ACQUIRE, &_empty) == 0 && _empty.index == 0, "only returned buffer reused");
	_lifecycle_check(ioctl(_consumer, VIDIOC_STREAMOFF, &_type) == 0, "stop during producer ownership");
	_empty.timestamp_ns = _lifecycle_time();
	_lifecycle_check(ioctl(_control, CAMERA_COMPLETE, &_empty) < 0 && errno == ESTALE, "late producer completion rejected");
	_lifecycle_check(ioctl(_control, CAMERA_ACK_STOP, &_state) < 0 && errno == ESTALE, "old retirement generation rejected");
	_lifecycle_check(ioctl(_consumer, VIDIOC_QBUF, &_requeue) == 0, "queue while retiring");
	_lifecycle_check(ioctl(_consumer, VIDIOC_STREAMON, &_type) < 0 && errno == EBUSY, "restart blocked until producer retirement");
	for (uint32_t _i = 0; _i < 3; ++_i)
	{
		munmap(_producerMaps[_i], _lengths[_i]);
		close(_producerDescriptors[_i]);
	}
	_lifecycle_check(ioctl(_control, CAMERA_QUERY, &_state) == 0 && !_state.running, "stop event");
	_lifecycle_check(ioctl(_control, CAMERA_ACK_STOP, &_state) == 0, "retirement acknowledged");
	_lifecycle_check(ioctl(_consumer, VIDIOC_STREAMON, &_type) == 0, "restart after retirement");
	_lifecycle_check(ioctl(_control, CAMERA_COMPLETE, &_last) < 0 && errno == ESTALE, "previous frame invalid after restart");
	close(_control);
	struct v4l2_buffer _dead = {.type = _type, .memory = V4L2_MEMORY_MMAP};
	_lifecycle_check(ioctl(_consumer, VIDIOC_DQBUF, &_dead) < 0, "producer death errors consumer");
	for (uint32_t _i = 0; _i < 3; ++_i)
	{
		munmap(_consumerMaps[_i], _lengths[_i]);
	}
	close(_consumer);
	puts("PASS synthetic: color profiles, old ABI and duplicate rejection, exact patterns, DMA-BUF sharing, exclusive ownership, held buffers, stale epochs, cancellation, restart, producer death");
	return 0;
}
