#define _GNU_SOURCE
#include "include/camera_uapi.h"
#include "include/packing.h"
#include <android/native_window_jni.h>
#include <android/data_space.h>
#include <android/hardware_buffer.h>
#include <android/log.h>
#include <media/NdkImageReader.h>
#include <media/NdkImage.h>
#include <linux/dma-buf.h>
#include <jni.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <poll.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

typedef struct
{
	int32_t descriptor;
	uint8_t *address;
	size_t size;
} capture_mapping_t;

typedef struct
{
	JavaVM *vm;
	jobject service;
	jmethodID request;
	pthread_t worker;
	pthread_mutex_t mutex;
	int32_t broker;
	int32_t control;
	int32_t wake;
	int32_t quit;
	int32_t started;
	uint32_t count;
	uint64_t observed[CAMERA_COUNT];
	struct camera_state current;
	AImageReader *reader;
	capture_mapping_t mappings[CAMERA_BUFFERS];
	uint64_t frames;
	uint64_t drops;
	uint64_t copy_ns;
	uint64_t max_copy_ns;
	uint64_t max_age_ns;
} capture_t;

static void _capture_throw(JNIEnv *__env, const char *__message)
{
	jclass _exception = (*__env)->FindClass(__env, "java/io/IOException");
	if (_exception)
	{
		(*__env)->ThrowNew(__env, _exception, __message);
	}
}

static uint64_t _capture_clock(clockid_t __clock)
{
	struct timespec _time;
	clock_gettime(__clock, &_time);
	return (uint64_t)_time.tv_sec * 1000000000ULL + _time.tv_nsec;
}

static void _capture_fail(capture_t *__capture, int32_t __error)
{
	struct camera_state _state = __capture->current;
	_state.error = __error;
	ioctl(__capture->control, CAMERA_FAIL, &_state);
	__android_log_print(ANDROID_LOG_ERROR, "LinuxCamera", "capture failed camera=%u generation=%llu error=%d", _state.index, (unsigned long long)_state.generation, __error);
}

static int32_t _capture_map(capture_t *__capture, struct camera_buffer *__buffer)
{
	if (__buffer->index >= CAMERA_BUFFERS)
	{
		return -1;
	}
	capture_mapping_t *_mapping = &__capture->mappings[__buffer->index];
	if (_mapping->address)
	{
		return 0;
	}
	if (ioctl(__capture->control, CAMERA_EXPORT, __buffer))
	{
		return -1;
	}
	_mapping->descriptor = __buffer->fd;
	_mapping->size = __buffer->bytes;
	_mapping->address = mmap(NULL, _mapping->size, PROT_READ | PROT_WRITE, MAP_SHARED, _mapping->descriptor, 0);
	if (_mapping->address == MAP_FAILED)
	{
		_mapping->address = NULL;
		close(_mapping->descriptor);
		_mapping->descriptor = -1;
		return -1;
	}
	return 0;
}

static void _capture_image(void *__context, AImageReader *__reader)
{
	capture_t *_capture = __context;
	pthread_mutex_lock(&_capture->mutex);
	if (_capture->reader != __reader)
	{
		pthread_mutex_unlock(&_capture->mutex);
		return;
	}
	AImage *_image = NULL;
	if (AImageReader_acquireLatestImage(__reader, &_image) != AMEDIA_OK)
	{
		pthread_mutex_unlock(&_capture->mutex);
		return;
	}
	struct camera_buffer _buffer = {
		.camera = _capture->current.index,
		.generation = _capture->current.generation,
	};
	if (ioctl(_capture->control, CAMERA_ACQUIRE, &_buffer))
	{
		_capture->drops++;
		AImage_delete(_image);
		pthread_mutex_unlock(&_capture->mutex);
		return;
	}
	int32_t _result = _capture_map(_capture, &_buffer);
	packing_t _planes[3] = {0};
	AImageCropRect _crop;
	int64_t _timestamp = 0;
	int32_t _dataSpace = 0;
	if (AImage_getDataSpace(_image, &_dataSpace) != AMEDIA_OK || _dataSpace != ADATASPACE_JFIF)
	{
		__android_log_print(ANDROID_LOG_ERROR, "LinuxCamera", "Unexpected raw dataspace %d; expected BT601-625 full range", _dataSpace);
		_result = -1;
	}
	if (AImage_getCropRect(_image, &_crop) != AMEDIA_OK || AImage_getTimestamp(_image, &_timestamp) != AMEDIA_OK)
	{
		_result = -1;
	}
	for (int32_t _i = 0; _i < 3 && !_result; ++_i)
	{
		uint8_t *_data;
		int32_t _length;
		int32_t _row;
		int32_t _pixel;
		if (AImage_getPlaneData(_image, _i, &_data, &_length) != AMEDIA_OK || AImage_getPlaneRowStride(_image, _i, &_row) != AMEDIA_OK || AImage_getPlanePixelStride(_image, _i, &_pixel) != AMEDIA_OK || _length <= 0 || _row <= 0 || _pixel <= 0)
		{
			_result = -1;
			break;
		}
		_planes[_i] = (packing_t){_data, (size_t)_length, (uint32_t)_row, (uint32_t)_pixel};
	}
	uint64_t _begin = _capture_clock(CLOCK_MONOTONIC);
	if (!_result && (_crop.right - _crop.left != (int32_t)_capture->current.mode.width || _crop.bottom - _crop.top != (int32_t)_capture->current.mode.height))
	{
		_result = -1;
	}
	if (!_result)
	{
		struct dma_buf_sync _sync = {.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE};
		capture_mapping_t *_mapping = &_capture->mappings[_buffer.index];
		_result = ioctl(_mapping->descriptor, DMA_BUF_IOCTL_SYNC, &_sync);
		if (!_result)
		{
			_result = packing_i420(_mapping->address, _mapping->size, _planes, _crop.left, _crop.top, _capture->current.mode.width, _capture->current.mode.height);
			_sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE;
			if (ioctl(_mapping->descriptor, DMA_BUF_IOCTL_SYNC, &_sync))
			{
				_result = -1;
			}
		}
	}
	uint64_t _end = _capture_clock(CLOCK_MONOTONIC);
	uint64_t _boot = _capture_clock(CLOCK_BOOTTIME);
	/* This device reports SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME (boottime).
	 * Re-sample the offset each frame so suspend cannot age the conversion. */
	if (_timestamp <= 0 || (uint64_t)_timestamp > _boot || _boot - (uint64_t)_timestamp > _end)
	{
		_result = -1;
	}
	else
	{
		_buffer.timestamp_ns = _end - (_boot - _timestamp);
	}
	if (!_result && ioctl(_capture->control, CAMERA_COMPLETE, &_buffer) == 0)
	{
		_capture->frames++;
		_capture->copy_ns += _end - _begin;
		if (_end - _begin > _capture->max_copy_ns)
		{
			_capture->max_copy_ns = _end - _begin;
		}
		if (_end - _buffer.timestamp_ns > _capture->max_age_ns)
		{
			_capture->max_age_ns = _end - _buffer.timestamp_ns;
		}
	}
	else if (_result)
	{
		_capture_fail(_capture, EIO);
	}
	AImage_delete(_image);
	pthread_mutex_unlock(&_capture->mutex);
}

static void *_capture_worker(void *__argument)
{
	capture_t *_capture = __argument;
	JNIEnv *_env = NULL;
	if ((*_capture->vm)->AttachCurrentThread(_capture->vm, &_env, NULL))
	{
		return NULL;
	}
	while (!__atomic_load_n(&_capture->quit, __ATOMIC_ACQUIRE))
	{
		uint64_t _serial;
		if (ioctl(_capture->control, CAMERA_EVENTS, &_serial))
		{
			break;
		}
		for (uint32_t _i = 0; _i < _capture->count; ++_i)
		{
			struct camera_state _state = {.index = _i};
			if (ioctl(_capture->control, CAMERA_QUERY, &_state))
			{
				__atomic_store_n(&_capture->quit, 1, __ATOMIC_RELEASE);
				break;
			}
			if (_capture->observed[_i] != _state.generation)
			{
				_capture->observed[_i] = _state.generation;
				(*_env)->CallVoidMethod(_env, _capture->service, _capture->request, (jint)_i, (jlong)_state.generation, (jint)_state.mode.width, (jint)_state.mode.height, (jint)_state.mode.fps, (jboolean)_state.running);
				if ((*_env)->ExceptionCheck(_env))
				{
					(*_env)->ExceptionClear(_env);
					__atomic_store_n(&_capture->quit, 1, __ATOMIC_RELEASE);
				}
			}
		}
		if (__atomic_load_n(&_capture->quit, __ATOMIC_ACQUIRE))
		{
			break;
		}
		struct pollfd _poll[3] = {{_capture->control, POLLIN, 0}, {_capture->wake, POLLIN, 0}, {_capture->broker, POLLIN, 0}};
		if (poll(_poll, 3, -1) < 0 && errno != EINTR)
		{
			break;
		}
		if (_poll[1].revents || _poll[2].revents)
		{
			break;
		}
	}
	(*_env)->CallVoidMethod(_env, _capture->service, _capture->request, (jint)-1, (jlong)0, (jint)0, (jint)0, (jint)0, (jboolean)0);
	(*_capture->vm)->DetachCurrentThread(_capture->vm);
	return NULL;
}

JNIEXPORT jlong JNICALL Java_dev_kiraly_linuxcamera_NativeCamera_connect(JNIEnv *__env, jclass __class, jobject __service)
{
	(void)__class;
	capture_t *_capture = calloc(1, sizeof(*_capture));
	if (!_capture)
	{
		_capture_throw(__env, "Camera allocation failed");
		return 0;
	}
	const char *_stage = "create broker socket";
	_capture->broker = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
	_capture->control = -1;
	_capture->wake = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	struct sockaddr_un _address = {.sun_family = AF_UNIX};
	memcpy(_address.sun_path + 1, CAMERA_SOCKET, sizeof(CAMERA_SOCKET));
	if (_capture->broker < 0 || _capture->wake < 0)
	{
		goto _failure;
	}
	struct timeval _timeout = {.tv_sec = 5};
	setsockopt(_capture->broker, SOL_SOCKET, SO_RCVTIMEO, &_timeout, sizeof(_timeout));
	_stage = "connect to camera broker";
	if (connect(_capture->broker, (struct sockaddr *)&_address, offsetof(struct sockaddr_un, sun_path) + sizeof(CAMERA_SOCKET)))
	{
		goto _failure;
	}
	struct ucred _peer;
	socklen_t _peerSize = sizeof(_peer);
	_stage = "verify camera broker";
	if (getsockopt(_capture->broker, SOL_SOCKET, SO_PEERCRED, &_peer, &_peerSize))
	{
		goto _failure;
	}
	if (_peer.uid != 0)
	{
		errno = EACCES;
		goto _failure;
	}
	_stage = "receive camera device descriptor (check broker and SELinux policy)";
	char _marker;
	char _ancillary[CMSG_SPACE(sizeof(int32_t))] = {0};
	struct iovec _vector = {&_marker, 1};
	struct msghdr _message = {0};
	_message.msg_iov = &_vector;
	_message.msg_iovlen = 1;
	_message.msg_control = _ancillary;
	_message.msg_controllen = sizeof(_ancillary);
	ssize_t _received = recvmsg(_capture->broker, &_message, MSG_CMSG_CLOEXEC);
	if (_received != 1)
	{
		if (_received == 0)
		{
			errno = ECONNRESET;
		}
		goto _failure;
	}
	struct cmsghdr *_header = CMSG_FIRSTHDR(&_message);
	if (!_header || _header->cmsg_level != SOL_SOCKET || _header->cmsg_type != SCM_RIGHTS || _header->cmsg_len != CMSG_LEN(sizeof(int32_t)))
	{
		errno = EPROTO;
		goto _failure;
	}
	memcpy(&_capture->control, CMSG_DATA(_header), sizeof(int32_t));
	if (_marker != 'F' || (_message.msg_flags & (MSG_CTRUNC | MSG_TRUNC)))
	{
		errno = EPROTO;
		goto _failure;
	}
	pthread_mutex_init(&_capture->mutex, NULL);
	(*__env)->GetJavaVM(__env, &_capture->vm);
	_capture->service = (*__env)->NewGlobalRef(__env, __service);
	jclass _serviceClass = (*__env)->GetObjectClass(__env, __service);
	_capture->request = (*__env)->GetMethodID(__env, _serviceClass, "onRequest", "(IJIIIZ)V");
	for (uint32_t _i = 0; _i < CAMERA_BUFFERS; ++_i)
	{
		_capture->mappings[_i].descriptor = -1;
	}
	return (jlong)(intptr_t)_capture;
_failure:;
	int32_t _error = errno;
	char _failureMessage[256];
	snprintf(_failureMessage, sizeof(_failureMessage), "%s: %s", _stage, strerror(_error));
	close(_capture->broker);
	close(_capture->control);
	close(_capture->wake);
	free(_capture);
	_capture_throw(__env, _failureMessage);
	return 0;
}

JNIEXPORT jint JNICALL Java_dev_kiraly_linuxcamera_NativeCamera_registerCamera(JNIEnv *__env, jclass __class, jlong __handle, jstring __name, jstring __identifier, jintArray __modes)
{
	(void)__class;
	capture_t *_capture = (capture_t *)(intptr_t)__handle;
	struct camera_register _description = {.version = CAMERA_VERSION, .size = sizeof(_description), .color_profile = CAMERA_COLOR_BT601_625_FULL};
	jsize _length = (*__env)->GetArrayLength(__env, __modes);
	if (_length <= 0 || _length % 3 || _length / 3 > CAMERA_MODES || _capture->started)
	{
		_capture_throw(__env, "Invalid camera modes");
		return -1;
	}
	const char *_name = (*__env)->GetStringUTFChars(__env, __name, NULL);
	const char *_identifier = (*__env)->GetStringUTFChars(__env, __identifier, NULL);
	if (!_name || !_identifier)
	{
		return -1;
	}
	snprintf(_description.name, sizeof(_description.name), "%s", _name);
	snprintf(_description.identifier, sizeof(_description.identifier), "%s", _identifier);
	(*__env)->ReleaseStringUTFChars(__env, __name, _name);
	(*__env)->ReleaseStringUTFChars(__env, __identifier, _identifier);
	jint _modes[CAMERA_MODES * 3];
	(*__env)->GetIntArrayRegion(__env, __modes, 0, _length, _modes);
	_description.mode_count = _length / 3;
	for (uint32_t _i = 0; _i < _description.mode_count; ++_i)
	{
		_description.modes[_i] = (struct camera_mode){_modes[_i * 3], _modes[_i * 3 + 1], _modes[_i * 3 + 2], 0};
	}
	if (ioctl(_capture->control, CAMERA_REGISTER, &_description))
	{
		int32_t _error = errno;
		char _message[192];
		snprintf(_message, sizeof(_message), "Register camera %s: %s", _description.identifier, strerror(_error));
		_capture_throw(__env, _message);
		return -1;
	}
	char _reply = 0;
	if (send(_capture->broker, &_description, sizeof(_description), MSG_NOSIGNAL) != sizeof(_description) || recv(_capture->broker, &_reply, 1, 0) != 1)
	{
		_capture_throw(__env, "Camera registered, but broker communication failed");
		return -1;
	}
	if (_reply != 'K')
	{
		_capture_throw(__env, "Camera registered, but broker could not publish its Linux device");
		return -1;
	}
	_capture->count++;
	return _description.index;
}

JNIEXPORT void JNICALL Java_dev_kiraly_linuxcamera_NativeCamera_listen(JNIEnv *__env, jclass __class, jlong __handle)
{
	(void)__class;
	capture_t *_capture = (capture_t *)(intptr_t)__handle;
	if (pthread_create(&_capture->worker, NULL, _capture_worker, _capture))
	{
		_capture_throw(__env, "Camera worker failed");
		return;
	}
	_capture->started = 1;
}

JNIEXPORT jobject JNICALL Java_dev_kiraly_linuxcamera_NativeCamera_surface(JNIEnv *__env, jclass __class, jlong __handle, jint __index, jlong __generation, jint __width, jint __height)
{
	(void)__class;
	capture_t *_capture = (capture_t *)(intptr_t)__handle;
	pthread_mutex_lock(&_capture->mutex);
	struct camera_state _state = {.index = __index};
	if (_capture->reader || ioctl(_capture->control, CAMERA_QUERY, &_state) || _state.generation != (uint64_t)__generation || !_state.running || _state.mode.width != (uint32_t)__width || _state.mode.height != (uint32_t)__height || AImageReader_newWithDataSpace(__width, __height, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, 3, AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420, ADATASPACE_JFIF, &_capture->reader) != AMEDIA_OK)
	{
		pthread_mutex_unlock(&_capture->mutex);
		_capture_throw(__env, "Camera stream was cancelled or image reader failed");
		return NULL;
	}
	_capture->current = _state;
	AImageReader_ImageListener _listener = {_capture, _capture_image};
	AImageReader_setImageListener(_capture->reader, &_listener);
	ANativeWindow *_window = NULL;
	AImageReader_getWindow(_capture->reader, &_window);
	jobject _surface = ANativeWindow_toSurface(__env, _window);
	pthread_mutex_unlock(&_capture->mutex);
	return _surface;
}

JNIEXPORT void JNICALL Java_dev_kiraly_linuxcamera_NativeCamera_retire(JNIEnv *__env, jclass __class, jlong __handle, jint __index, jlong __generation)
{
	(void)__env;
	(void)__class;
	capture_t *_capture = (capture_t *)(intptr_t)__handle;
	pthread_mutex_lock(&_capture->mutex);
	if (_capture->current.index != (uint32_t)__index)
	{
		struct camera_state _state = {.index = __index, .generation = __generation};
		ioctl(_capture->control, CAMERA_ACK_STOP, &_state);
		pthread_mutex_unlock(&_capture->mutex);
		return;
	}
	AImageReader *_reader = _capture->reader;
	_capture->reader = NULL;
	pthread_mutex_unlock(&_capture->mutex);
	if (_reader)
	{
		AImageReader_delete(_reader);
	}
	pthread_mutex_lock(&_capture->mutex);
	for (uint32_t _i = 0; _i < CAMERA_BUFFERS; ++_i)
	{
		capture_mapping_t *_mapping = &_capture->mappings[_i];
		if (_mapping->address)
		{
			munmap(_mapping->address, _mapping->size);
			close(_mapping->descriptor);
			*_mapping = (capture_mapping_t){.descriptor = -1};
		}
	}
	struct camera_state _state = {.index = __index, .generation = __generation};
	ioctl(_capture->control, CAMERA_ACK_STOP, &_state);
	pthread_mutex_unlock(&_capture->mutex);
}

JNIEXPORT void JNICALL Java_dev_kiraly_linuxcamera_NativeCamera_fail(JNIEnv *__env, jclass __class, jlong __handle, jint __index, jlong __generation)
{
	(void)__env;
	(void)__class;
	capture_t *_capture = (capture_t *)(intptr_t)__handle;
	struct camera_state _state = {.index = __index, .generation = __generation, .error = EIO};
	ioctl(_capture->control, CAMERA_FAIL, &_state);
}

JNIEXPORT jstring JNICALL Java_dev_kiraly_linuxcamera_NativeCamera_statistics(JNIEnv *__env, jclass __class, jlong __handle)
{
	(void)__class;
	capture_t *_capture = (capture_t *)(intptr_t)__handle;
	char _status[256];
	pthread_mutex_lock(&_capture->mutex);
	double _mean = 0;
	if (_capture->frames)
	{
		_mean = _capture->copy_ns / (double)_capture->frames / 1000000.0;
	}
	snprintf(_status, sizeof(_status), "Frames: %llu; drops: %llu; copy mean/max: %.2f/%.2f ms; max delivery age: %.2f ms", (unsigned long long)_capture->frames, (unsigned long long)_capture->drops, _mean, _capture->max_copy_ns / 1000000.0, _capture->max_age_ns / 1000000.0);
	pthread_mutex_unlock(&_capture->mutex);
	return (*__env)->NewStringUTF(__env, _status);
}

JNIEXPORT void JNICALL Java_dev_kiraly_linuxcamera_NativeCamera_disconnect(JNIEnv *__env, jclass __class, jlong __handle)
{
	(void)__class;
	capture_t *_capture = (capture_t *)(intptr_t)__handle;
	__atomic_store_n(&_capture->quit, 1, __ATOMIC_RELEASE);
	uint64_t _wake = 1;
	write(_capture->wake, &_wake, sizeof(_wake));
	if (_capture->started)
	{
		pthread_join(_capture->worker, NULL);
	}
	Java_dev_kiraly_linuxcamera_NativeCamera_retire(__env, __class, __handle, _capture->current.index, _capture->current.generation);
	close(_capture->control);
	close(_capture->broker);
	close(_capture->wake);
	(*__env)->DeleteGlobalRef(__env, _capture->service);
	pthread_mutex_destroy(&_capture->mutex);
	free(_capture);
}
