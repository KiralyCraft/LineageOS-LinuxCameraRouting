#define _GNU_SOURCE
#include "include/camera_uapi.h"
#include "include/device.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <linux/videodev2.h>
#include <unistd.h>

static volatile sig_atomic_t _broker_stop;

static void _broker_signal(int32_t __signal)
{
	(void)__signal;
	_broker_stop = 1;
}

static uint32_t _broker_helper_uid(void)
{
	FILE *_file = fopen("/data/system/packages.list", "re");
	if (!_file)
	{
		return UINT32_MAX;
	}
	char _line[2048];
	uint32_t _result = UINT32_MAX;
	while (fgets(_line, sizeof(_line), _file))
	{
		char _name[256];
		uint32_t _uid;
		if (sscanf(_line, "%255s %" SCNu32, _name, &_uid) == 2 && strcmp(_name, "dev.kiraly.linuxcamera") == 0)
		{
			_result = _uid;
			break;
		}
	}
	fclose(_file);
	return _result;
}

static int32_t _broker_handoff(int32_t __socket)
{
	int32_t _control = open("/dev/linux_camera", O_RDWR | O_CLOEXEC);
	if (_control < 0)
	{
		return -1;
	}
	char _marker = 'F';
	struct iovec _vector = {&_marker, 1};
	union
	{
		struct cmsghdr alignment;
		char bytes[CMSG_SPACE(sizeof(int32_t))];
	} _ancillary = {0};
	struct msghdr _message = {0};
	_message.msg_iov = &_vector;
	_message.msg_iovlen = 1;
	_message.msg_control = _ancillary.bytes;
	_message.msg_controllen = sizeof(_ancillary.bytes);
	struct cmsghdr *_header = CMSG_FIRSTHDR(&_message);
	_header->cmsg_level = SOL_SOCKET;
	_header->cmsg_type = SCM_RIGHTS;
	_header->cmsg_len = CMSG_LEN(sizeof(_control));
	memcpy(CMSG_DATA(_header), &_control, sizeof(_control));
	int32_t _result = sendmsg(__socket, &_message, MSG_NOSIGNAL);
	close(_control);
	if (_result != 1)
	{
		return -1;
	}
	return 0;
}

static int32_t _broker_publish(const struct camera_register *__camera)
{
	if (__camera->version != CAMERA_VERSION || __camera->size != sizeof(*__camera) || __camera->video_number < 0 || __camera->video_number > 255 || !memchr(__camera->identifier, 0, CAMERA_NAME))
	{
		return -1;
	}
	for (size_t _i = 0; __camera->identifier[_i]; ++_i)
	{
		char _c = __camera->identifier[_i];
		if (!((_c >= 'a' && _c <= 'z') || (_c >= '0' && _c <= '9') || _c == '-'))
		{
			return -1;
		}
	}
	if (!__camera->identifier[0])
	{
		return -1;
	}
	char _path[96];
	snprintf(_path, sizeof(_path), "/dev/video%d", __camera->video_number);
	int32_t _video = device_open(_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
	struct v4l2_capability _caps = {0};
	if (_video < 0 || ioctl(_video, VIDIOC_QUERYCAP, &_caps) < 0 || strcmp((const char *)_caps.driver, "linux-camera"))
	{
		if (_video >= 0)
		{
			close(_video);
		}
		return -1;
	}
	int32_t _result = fchown(_video, 4000, 4000);
	_result |= fchmod(_video, 0660);
	close(_video);
	if (_result)
	{
		return -1;
	}
	char _alias[128];
	snprintf(_alias, sizeof(_alias), "/dev/linux-camera/%s", __camera->identifier);
	unlink(_alias);
	if (symlink(_path, _alias))
	{
		return -1;
	}
	fprintf(stderr, "camera ready: %s -> %s\n", __camera->identifier, _path);
	return 0;
}

int main(void)
{
	if (geteuid() != 0)
	{
		fputs("Run the camera broker as Android root.\n", stderr);
		return 1;
	}
	umask(0077);
	mkdir("/dev/linux-camera", 0755);
	chmod("/dev/linux-camera", 0755);
	signal(SIGTERM, _broker_signal);
	signal(SIGINT, _broker_signal);
	int32_t _listener = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	struct sockaddr_un _address = {.sun_family = AF_UNIX};
	memcpy(_address.sun_path + 1, CAMERA_SOCKET, sizeof(CAMERA_SOCKET));
	if (_listener < 0 || bind(_listener, (struct sockaddr *)&_address, offsetof(struct sockaddr_un, sun_path) + sizeof(CAMERA_SOCKET)) || listen(_listener, 4))
	{
		perror("camera broker listen");
		return 1;
	}
	int32_t _helper = -1;
	while (!_broker_stop)
	{
		struct pollfd _poll[2] = {{_listener, POLLIN, 0}, {_helper, POLLIN, 0}};
		if (poll(_poll, 2, -1) < 0)
		{
			if (errno == EINTR)
			{
				continue;
			}
			break;
		}
		if (_poll[0].revents & POLLIN)
		{
			int32_t _peer = accept4(_listener, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
			struct ucred _credentials = {0};
			socklen_t _length = sizeof(_credentials);
			if (_peer >= 0)
			{
				if (_helper < 0 && getsockopt(_peer, SOL_SOCKET, SO_PEERCRED, &_credentials, &_length) == 0 && _credentials.uid == _broker_helper_uid() && _broker_handoff(_peer) == 0)
				{
					_helper = _peer;
				}
				else
				{
					close(_peer);
				}
			}
		}
		if (_helper >= 0 && _poll[1].revents)
		{
			struct camera_register _registration;
			ssize_t _received = recv(_helper, &_registration, sizeof(_registration), MSG_DONTWAIT | MSG_TRUNC);
			char _reply = 'K';
			if (_received != sizeof(_registration) || _broker_publish(&_registration))
			{
				_reply = 'E';
			}
			if (_received <= 0 || send(_helper, &_reply, 1, MSG_NOSIGNAL) != 1 || _reply != 'K')
			{
				close(_helper);
				_helper = -1;
			}
		}
	}
	if (_helper >= 0)
	{
		close(_helper);
	}
	close(_listener);
	return 0;
}
