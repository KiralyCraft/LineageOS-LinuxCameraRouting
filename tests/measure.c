#define _GNU_SOURCE
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint64_t _measure_now(void)
{
	struct timespec _now;
	clock_gettime(CLOCK_MONOTONIC, &_now);
	return (uint64_t)_now.tv_sec * 1000000000ULL + _now.tv_nsec;
}

static int32_t _measure_compare(const void *__left, const void *__right)
{
	double _left = *(const double *)__left;
	double _right = *(const double *)__right;
	if (_left < _right)
		return -1;
	if (_left > _right)
		return 1;
	return 0;
}

int main(int __argc, char **__argv)
{
	if (__argc != 5)
	{
		fputs("Usage: camera-measure /dev/videoN width height fps\nCaptures 300 frames; saves only timing statistics, no images.\n", stderr);
		return 2;
	}
	uint32_t _width = strtoul(__argv[2], NULL, 10);
	uint32_t _height = strtoul(__argv[3], NULL, 10);
	uint32_t _fps = strtoul(__argv[4], NULL, 10);
	int32_t _video = open(__argv[1], O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (_video < 0)
	{
		perror("open");
		return 1;
	}
	struct v4l2_format _format = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .fmt.pix = {.width = _width, .height = _height, .pixelformat = V4L2_PIX_FMT_YUV420}};
	struct v4l2_streamparm _parm = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .parm.capture.timeperframe = {1, _fps}};
	if (ioctl(_video, VIDIOC_S_FMT, &_format) || _format.fmt.pix.width != _width || _format.fmt.pix.height != _height || ioctl(_video, VIDIOC_S_PARM, &_parm) || _parm.parm.capture.timeperframe.denominator != _fps || _parm.parm.capture.timeperframe.numerator != 1)
	{
		fputs("Requested mode unavailable\n", stderr);
		close(_video);
		return 1;
	}
	struct v4l2_requestbuffers _request = {.type = _format.type, .memory = V4L2_MEMORY_MMAP, .count = 4};
	if (ioctl(_video, VIDIOC_REQBUFS, &_request) || _request.count > 8)
	{
		perror("REQBUFS");
		close(_video);
		return 1;
	}
	void *_maps[8] = {0};
	size_t _sizes[8] = {0};
	int32_t _result = 1;
	for (uint32_t _i = 0; _i < _request.count; ++_i)
	{
		struct v4l2_buffer _buffer = {.type = _format.type, .memory = V4L2_MEMORY_MMAP, .index = _i};
		if (ioctl(_video, VIDIOC_QUERYBUF, &_buffer))
			goto _cleanup;
		_sizes[_i] = _buffer.length;
		_maps[_i] = mmap(NULL, _buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, _video, _buffer.m.offset);
		if (_maps[_i] == MAP_FAILED || ioctl(_video, VIDIOC_QBUF, &_buffer))
			goto _cleanup;
	}
	if (ioctl(_video, VIDIOC_STREAMON, &_format.type))
		goto _cleanup;
	double _ages[300];
	double _intervals[299];
	uint64_t _first = 0;
	uint64_t _previous = 0;
	uint64_t _last = 0;
	uint32_t _frames = 0;
	uint32_t _errors = 0;
	uint64_t _deadline = _measure_now() + 40000000000ULL;
	while (_frames < 300 && _measure_now() < _deadline)
	{
		struct pollfd _poll = {_video, POLLIN, 0};
		if (poll(&_poll, 1, 10000) <= 0 || (_poll.revents & (POLLERR | POLLHUP | POLLNVAL)))
			break;
		struct v4l2_buffer _buffer = {.type = _format.type, .memory = V4L2_MEMORY_MMAP};
		if (ioctl(_video, VIDIOC_DQBUF, &_buffer))
			break;
		_last = _measure_now();
		uint64_t _timestamp = (uint64_t)_buffer.timestamp.tv_sec * 1000000000ULL + (uint64_t)_buffer.timestamp.tv_usec * 1000;
		if ((_buffer.flags & V4L2_BUF_FLAG_ERROR) || _timestamp > _last || _buffer.bytesused != _width * _height * 3 / 2)
			_errors++;
		_ages[_frames] = (_last - _timestamp) / 1000000.0;
		if (!_frames)
			_first = _last;
		else
			_intervals[_frames - 1] = (_last - _previous) / 1000000.0;
		_previous = _last;
		_frames++;
		if (ioctl(_video, VIDIOC_QBUF, &_buffer))
			break;
	}
	ioctl(_video, VIDIOC_STREAMOFF, &_format.type);
	if (_frames > 1)
	{
		qsort(_ages, _frames, sizeof(double), _measure_compare);
		qsort(_intervals, _frames - 1, sizeof(double), _measure_compare);
		printf("{\"width\":%u,\"height\":%u,\"requested_fps\":%u,\"frames\":%u,\"errors\":%u,\"observed_fps\":%.3f,\"age_p50_ms\":%.3f,\"age_p95_ms\":%.3f,\"interval_p95_ms\":%.3f}\n", _width, _height, _fps, _frames, _errors, (_frames - 1) * 1e9 / (_last - _first), _ages[_frames / 2], _ages[(_frames - 1) * 95 / 100], _intervals[(_frames - 2) * 95 / 100]);
	}
	if (_frames == 300 && !_errors)
		_result = 0;
_cleanup:
	for (uint32_t _i = 0; _i < _request.count; ++_i)
	{
		if (_maps[_i] && _maps[_i] != MAP_FAILED)
			munmap(_maps[_i], _sizes[_i]);
	}
	close(_video);
	return _result;
}
