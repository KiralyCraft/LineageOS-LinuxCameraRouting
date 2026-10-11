// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/capability.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/dma-buf.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>
#include "camera_uapi.h"

#include "camera_kernel.h"

static DEFINE_MUTEX(_camera_control_lock);
static DEFINE_SPINLOCK(_camera_admission_lock);
static DECLARE_WAIT_QUEUE_HEAD(_camera_events);
static atomic64_t _camera_event_serial = ATOMIC64_INIT(1);
static camera_t *_camera_devices[CAMERA_COUNT];
static camera_t *_camera_active;
static bool _camera_owner;

typedef struct
{
	u64 observed;
} camera_control_t;

static void _camera_notify(void)
{
	atomic64_inc(&_camera_event_serial);
	wake_up_interruptible(&_camera_events);
}

static u32 _camera_size(const camera_t *__camera)
{
	return __camera->mode.width * __camera->mode.height * 3 / 2;
}

static void _camera_release_admission(camera_t *__camera)
{
	unsigned long _flags;
	spin_lock_irqsave(&_camera_admission_lock, _flags);
	if (_camera_active == __camera)
	{
		_camera_active = NULL;
	}
	spin_unlock_irqrestore(&_camera_admission_lock, _flags);
}

static void _camera_return_buffers(camera_t *__camera)
{
	unsigned long _flags;
	spin_lock_irqsave(&__camera->pending_lock, _flags);
	while (!list_empty(&__camera->pending))
	{
		camera_buffer_t *_buffer = list_first_entry(&__camera->pending, camera_buffer_t, pending);
		list_del_init(&_buffer->pending);
		vb2_buffer_done(&_buffer->buffer.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&__camera->pending_lock, _flags);
	if (__camera->claimed && __camera->claimed_index < __camera->queue.num_buffers)
	{
		struct vb2_buffer *_buffer = __camera->queue.bufs[__camera->claimed_index];
		if (_buffer->state == VB2_BUF_STATE_ACTIVE)
		{
			vb2_buffer_done(_buffer, VB2_BUF_STATE_ERROR);
		}
	}
	__camera->claimed = false;
}

static int _camera_queue_setup(struct vb2_queue *__queue, unsigned int *__count, unsigned int *__planes, unsigned int __sizes[], struct device *__devices[])
{
	camera_t *_camera = vb2_get_drv_priv(__queue);
	if (__queue->num_buffers >= CAMERA_BUFFERS)
	{
		return -ENOBUFS;
	}
	*__count = clamp_t(unsigned int, *__count, 1, CAMERA_BUFFERS - __queue->num_buffers);
	if (*__planes)
	{
		if (*__planes != 1 || __sizes[0] < _camera_size(_camera))
		{
			return -EINVAL;
		}
		return 0;
	}
	*__planes = 1;
	__sizes[0] = _camera_size(_camera);
	return 0;
}

static int _camera_buffer_prepare(struct vb2_buffer *__buffer)
{
	camera_t *_camera = vb2_get_drv_priv(__buffer->vb2_queue);
	if (vb2_plane_size(__buffer, 0) < _camera_size(_camera))
	{
		return -EINVAL;
	}
	vb2_set_plane_payload(__buffer, 0, _camera_size(_camera));
	return 0;
}

static void _camera_buffer_queue(struct vb2_buffer *__buffer)
{
	camera_t *_camera = vb2_get_drv_priv(__buffer->vb2_queue);
	camera_buffer_t *_buffer = container_of(to_vb2_v4l2_buffer(__buffer), camera_buffer_t, buffer);
	unsigned long _flags;
	spin_lock_irqsave(&_camera->pending_lock, _flags);
	list_add_tail(&_buffer->pending, &_camera->pending);
	spin_unlock_irqrestore(&_camera->pending_lock, _flags);
	_camera_notify();
}

static int _camera_start(struct vb2_queue *__queue, unsigned int __count)
{
	camera_t *_camera = vb2_get_drv_priv(__queue);
	int _result = 0;
	unsigned long _flags;
	spin_lock_irqsave(&_camera_admission_lock, _flags);
	if (!_camera->connected)
	{
		_result = -ENODEV;
	}
	else if (_camera_active || _camera->stopping)
	{
		_result = -EBUSY;
	}
	else
	{
		_camera_active = _camera;
	}
	spin_unlock_irqrestore(&_camera_admission_lock, _flags);
	if (_result)
	{
		spin_lock_irqsave(&_camera->pending_lock, _flags);
		while (!list_empty(&_camera->pending))
		{
			camera_buffer_t *_buffer = list_first_entry(&_camera->pending, camera_buffer_t, pending);
			list_del_init(&_buffer->pending);
			vb2_buffer_done(&_buffer->buffer.vb2_buf, VB2_BUF_STATE_QUEUED);
		}
		spin_unlock_irqrestore(&_camera->pending_lock, _flags);
		return _result;
	}
	_camera->generation++;
	_camera->running = true;
	_camera->error = 0;
	_camera->sequence = 0;
	_camera_notify();
	return 0;
}

static void _camera_stop(struct vb2_queue *__queue)
{
	camera_t *_camera = vb2_get_drv_priv(__queue);
	_camera->running = false;
	_camera->stopping = _camera->connected;
	_camera->generation++;
	_camera_return_buffers(_camera);
	/* Exported mappings pin their allocations. A fresh STREAMON is blocked
	 * until the producer has stopped and closed those mappings/descriptors. */
	if (!_camera->connected)
	{
		_camera_release_admission(_camera);
	}
	_camera_notify();
}

static const struct vb2_ops _camera_queue_ops = {
	.queue_setup = _camera_queue_setup,
	.buf_prepare = _camera_buffer_prepare,
	.buf_queue = _camera_buffer_queue,
	.start_streaming = _camera_start,
	.stop_streaming = _camera_stop,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
};

static const struct v4l2_file_operations _camera_file_ops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
};

static void _camera_video_release(struct video_device *__video)
{
	camera_t *_camera = container_of(__video, camera_t, video);
	v4l2_device_unregister(&_camera->device);
	mutex_destroy(&_camera->mutex);
	kfree(_camera);
}

static int _camera_register(struct camera_register *__description)
{
	if (__description->version != CAMERA_VERSION || __description->size != sizeof(*__description) || __description->reserved || !__description->mode_count || __description->mode_count > CAMERA_MODES || !memchr(__description->name, 0, CAMERA_NAME) || !memchr(__description->identifier, 0, CAMERA_NAME))
	{
		return -EINVAL;
	}
	for (u32 _i = 0; _i < __description->mode_count; ++_i)
	{
		struct camera_mode *_mode = &__description->modes[_i];
		if (!_mode->width || !_mode->height || _mode->width > 3840 || _mode->height > 2160 || (_mode->width & 1) || (_mode->height & 1) || !_mode->fps || _mode->fps > 60 || _mode->reserved)
		{
			return -EINVAL;
		}
	}
	u32 _index;
	for (_index = 0; _index < CAMERA_COUNT; ++_index)
	{
		if (!_camera_devices[_index])
		{
			break;
		}
	}
	if (_index == CAMERA_COUNT)
	{
		return -ENOSPC;
	}
	camera_t *_camera = kzalloc(sizeof(*_camera), GFP_KERNEL);
	if (!_camera)
	{
		return -ENOMEM;
	}
	__description->index = _index;
	_camera->description = *__description;
	_camera->mode = __description->modes[0];
	_camera->connected = true;
	mutex_init(&_camera->mutex);
	spin_lock_init(&_camera->pending_lock);
	INIT_LIST_HEAD(&_camera->pending);
	strscpy(_camera->device.name, "linux-camera", sizeof(_camera->device.name));
	int _result = v4l2_device_register(NULL, &_camera->device);
	if (_result)
	{
		kfree(_camera);
		return _result;
	}
	_camera->queue.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	_camera->queue.io_modes = VB2_MMAP;
	_camera->queue.drv_priv = _camera;
	_camera->queue.buf_struct_size = sizeof(camera_buffer_t);
	_camera->queue.ops = &_camera_queue_ops;
	_camera->queue.mem_ops = &vb2_vmalloc_memops;
	_camera->queue.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	_camera->queue.lock = &_camera->mutex;
	_camera->queue.min_buffers_needed = 1;
	_result = vb2_queue_init(&_camera->queue);
	if (_result)
	{
		v4l2_device_unregister(&_camera->device);
		kfree(_camera);
		return _result;
	}
	strscpy(_camera->video.name, __description->name, sizeof(_camera->video.name));
	_camera->video.v4l2_dev = &_camera->device;
	_camera->video.fops = &_camera_file_ops;
	_camera->video.ioctl_ops = &video_ioctl_ops;
	_camera->video.release = _camera_video_release;
	_camera->video.lock = &_camera->mutex;
	_camera->video.queue = &_camera->queue;
	_camera->video.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	video_set_drvdata(&_camera->video, _camera);
	_result = video_register_device(&_camera->video, VFL_TYPE_VIDEO, -1);
	if (_result)
	{
		_camera_video_release(&_camera->video);
		return _result;
	}
	__description->video_number = _camera->video.num;
	_camera->description.video_number = _camera->video.num;
	_camera_devices[_index] = _camera;
	return 0;
}

static int _camera_control_open(struct inode *__inode, struct file *__file)
{
	if (!capable(CAP_SYS_ADMIN))
	{
		return -EPERM;
	}
	mutex_lock(&_camera_control_lock);
	if (_camera_owner)
	{
		mutex_unlock(&_camera_control_lock);
		return -EBUSY;
	}
	camera_control_t *_control = kzalloc(sizeof(*_control), GFP_KERNEL);
	if (!_control)
	{
		mutex_unlock(&_camera_control_lock);
		return -ENOMEM;
	}
	_camera_owner = true;
	__file->private_data = _control;
	mutex_unlock(&_camera_control_lock);
	return 0;
}

static int _camera_control_release(struct inode *__inode, struct file *__file)
{
	mutex_lock(&_camera_control_lock);
	for (u32 _i = 0; _i < CAMERA_COUNT; ++_i)
	{
		camera_t *_camera = _camera_devices[_i];
		if (!_camera)
		{
			continue;
		}
		mutex_lock(&_camera->mutex);
		_camera->connected = false;
		_camera->running = false;
		_camera->generation++;
		vb2_queue_error(&_camera->queue);
		_camera_return_buffers(_camera);
		_camera_release_admission(_camera);
		mutex_unlock(&_camera->mutex);
		_camera_devices[_i] = NULL;
		video_unregister_device(&_camera->video);
	}
	_camera_owner = false;
	kfree(__file->private_data);
	mutex_unlock(&_camera_control_lock);
	_camera_notify();
	return 0;
}

static long _camera_control_ioctl(struct file *__file, unsigned int __command, unsigned long __argument)
{
	void __user *_argument = (void __user *)__argument;
	struct camera_state _state = {0};
	struct camera_buffer _buffer = {0};
	int _result = 0;
	mutex_lock(&_camera_control_lock);
	if (__command == CAMERA_EVENTS)
	{
		camera_control_t *_control = __file->private_data;
		u64 _serial = atomic64_read(&_camera_event_serial);
		WRITE_ONCE(_control->observed, _serial);
		if (copy_to_user(_argument, &_serial, sizeof(_serial)))
		{
			_result = -EFAULT;
		}
		goto _unlock;
	}
	if (__command == CAMERA_REGISTER)
	{
		struct camera_register *_description = memdup_user(_argument, sizeof(*_description));
		if (IS_ERR(_description))
		{
			_result = PTR_ERR(_description);
		}
		else
		{
			_result = _camera_register(_description);
			if (!_result && copy_to_user(_argument, _description, sizeof(*_description)))
			{
				_result = -EFAULT;
			}
			kfree(_description);
		}
		goto _unlock;
	}
	bool _buffer_command = __command == CAMERA_ACQUIRE || __command == CAMERA_COMPLETE || __command == CAMERA_EXPORT;
	if (_buffer_command)
	{
		if (copy_from_user(&_buffer, _argument, sizeof(_buffer)))
		{
			_result = -EFAULT;
			goto _unlock;
		}
		_state.index = _buffer.camera;
	}
	else if (__command == CAMERA_QUERY || __command == CAMERA_ACK_STOP || __command == CAMERA_FAIL)
	{
		if (copy_from_user(&_state, _argument, sizeof(_state)))
		{
			_result = -EFAULT;
			goto _unlock;
		}
	}
	else
	{
		_result = -ENOTTY;
		goto _unlock;
	}
	if (_state.index >= CAMERA_COUNT || !_camera_devices[_state.index])
	{
		_result = -ENODEV;
		goto _unlock;
	}
	camera_t *_camera = _camera_devices[_state.index];
	mutex_lock(&_camera->mutex);
	if (__command == CAMERA_QUERY)
	{
		_state.running = _camera->running;
		_state.generation = _camera->generation;
		_state.mode = _camera->mode;
		_state.completed = _camera->completed;
		_state.dropped = _camera->dropped;
		_state.error = _camera->error;
		_state.reserved = 0;
		if (copy_to_user(_argument, &_state, sizeof(_state)))
		{
			_result = -EFAULT;
		}
	}
	else if (__command == CAMERA_ACK_STOP)
	{
		if (_camera->running || _state.generation != _camera->generation)
		{
			_result = -ESTALE;
		}
		else
		{
			_camera->stopping = false;
			_camera_release_admission(_camera);
		}
	}
	else if (__command == CAMERA_FAIL)
	{
		if (_state.generation != _camera->generation)
		{
			_result = -ESTALE;
		}
		else
		{
			_camera->error = _state.error;
			_camera->running = false;
			_camera->stopping = true;
			_camera->generation++;
			vb2_queue_error(&_camera->queue);
			_camera_return_buffers(_camera);
			_camera_notify();
		}
	}
	else if (!_camera->running || _buffer.generation != _camera->generation)
	{
		_result = -ESTALE;
	}
	else if (__command == CAMERA_ACQUIRE)
	{
		unsigned long _flags;
		spin_lock_irqsave(&_camera->pending_lock, _flags);
		if (_camera->claimed || list_empty(&_camera->pending))
		{
			_camera->dropped++;
			_result = -EAGAIN;
		}
		else
		{
			camera_buffer_t *_next = list_first_entry(&_camera->pending, camera_buffer_t, pending);
			list_del_init(&_next->pending);
			_camera->claimed_index = _next->buffer.vb2_buf.index;
			_camera->claimed = true;
			_buffer.index = _camera->claimed_index;
			_buffer.token = ++_camera->token;
			_buffer.bytes = _camera_size(_camera);
			_buffer.fd = -1;
		}
		spin_unlock_irqrestore(&_camera->pending_lock, _flags);
		if (!_result && copy_to_user(_argument, &_buffer, sizeof(_buffer)))
		{
			_result = -EFAULT;
		}
	}
	else if (!_camera->claimed || _buffer.index != _camera->claimed_index || _buffer.token != _camera->token)
	{
		_result = -EINVAL;
	}
	else if (__command == CAMERA_EXPORT)
	{
		struct vb2_buffer *_vb = _camera->queue.bufs[_buffer.index];
		struct dma_buf *_dma = _camera->queue.mem_ops->get_dmabuf(_vb, _vb->planes[0].mem_priv, O_RDWR);
		if (IS_ERR_OR_NULL(_dma))
		{
			_result = -ENOMEM;
		}
		else
		{
			_buffer.fd = get_unused_fd_flags(O_CLOEXEC);
			if (_buffer.fd < 0)
			{
				_result = _buffer.fd;
				dma_buf_put(_dma);
			}
			else if (copy_to_user(_argument, &_buffer, sizeof(_buffer)))
			{
				put_unused_fd(_buffer.fd);
				dma_buf_put(_dma);
				_result = -EFAULT;
			}
			else
			{
				fd_install(_buffer.fd, _dma->file);
			}
		}
	}
	else if (_buffer.bytes != _camera_size(_camera) || !_buffer.timestamp_ns)
	{
		_result = -EINVAL;
	}
	else
	{
		struct vb2_buffer *_vb = _camera->queue.bufs[_buffer.index];
		struct vb2_v4l2_buffer *_video_buffer = to_vb2_v4l2_buffer(_vb);
		_vb->timestamp = _buffer.timestamp_ns;
		_video_buffer->sequence = _camera->sequence++;
		_video_buffer->field = V4L2_FIELD_NONE;
		_camera->claimed = false;
		_camera->completed++;
		vb2_buffer_done(_vb, VB2_BUF_STATE_DONE);
	}
	mutex_unlock(&_camera->mutex);
_unlock:
	mutex_unlock(&_camera_control_lock);
	return _result;
}

static __poll_t _camera_control_poll(struct file *__file, struct poll_table_struct *__wait)
{
	camera_control_t *_control = __file->private_data;
	poll_wait(__file, &_camera_events, __wait);
	if (READ_ONCE(_control->observed) != atomic64_read(&_camera_event_serial))
	{
		return EPOLLIN | EPOLLRDNORM;
	}
	return 0;
}

static const struct file_operations _camera_control_ops = {
	.owner = THIS_MODULE,
	.open = _camera_control_open,
	.release = _camera_control_release,
	.unlocked_ioctl = _camera_control_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_ptr_ioctl,
#endif
	.poll = _camera_control_poll,
	.llseek = no_llseek,
};

static struct miscdevice _camera_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "linux_camera",
	.fops = &_camera_control_ops,
	.mode = 0600,
};

static int __init _camera_init(void)
{
	return misc_register(&_camera_misc);
}

static void __exit _camera_exit(void)
{
	misc_deregister(&_camera_misc);
}

module_init(_camera_init);
module_exit(_camera_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Android camera frames in owned V4L2 capture buffers");
MODULE_VERSION("0.1.0");
