#define _GNU_SOURCE
#include "include/device.h"
#include <sys/inotify.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

static int64_t _device_milliseconds(void)
{
	struct timespec _now;
	clock_gettime(CLOCK_MONOTONIC, &_now);
	return (int64_t)_now.tv_sec * 1000 + _now.tv_nsec / 1000000;
}

int32_t device_open(const char *__path, int32_t __flags)
{
	/* Android ueventd creates nodes asynchronously after video_register_device.
	 * Watch before opening so node arrival cannot be missed. The deadline only
	 * bounds failed provisioning; it does not pace capture. */
	int32_t _events = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
	if (_events < 0)
	{
		return -1;
	}
	if (inotify_add_watch(_events, "/dev", IN_CREATE | IN_MOVED_TO) < 0)
	{
		close(_events);
		return -1;
	}
	int64_t _deadline = _device_milliseconds() + 3000;
	int32_t _result;
	while ((_result = open(__path, __flags)) < 0 && errno == ENOENT)
	{
		int64_t _remaining = _deadline - _device_milliseconds();
		if (_remaining <= 0)
		{
			errno = ETIMEDOUT;
			break;
		}
		struct pollfd _poll = {_events, POLLIN, 0};
		int32_t _ready = poll(&_poll, 1, (int32_t)_remaining);
		if (_ready < 0 && errno == EINTR)
		{
			continue;
		}
		if (_ready <= 0)
		{
			errno = ETIMEDOUT;
			break;
		}
		char _buffer[4096];
		while (read(_events, _buffer, sizeof(_buffer)) > 0)
		{
		}
	}
	int32_t _error = errno;
	close(_events);
	errno = _error;
	return _result;
}
