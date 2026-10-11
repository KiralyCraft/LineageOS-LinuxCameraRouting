// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include "camera_kernel.h"
static int _video_querycap(struct file *__file, void *__context, struct v4l2_capability *__caps)
{
	camera_t *_camera = video_drvdata(__file);
	strscpy(__caps->driver, "linux-camera", sizeof(__caps->driver));
	strscpy(__caps->card, _camera->description.name, sizeof(__caps->card));
	snprintf(__caps->bus_info, sizeof(__caps->bus_info), "platform:linux-camera-%u", _camera->description.index);
	return 0;
}

static int _video_enum_format(struct file *__file, void *__context, struct v4l2_fmtdesc *__format)
{
	if (__format->index)
	{
		return -EINVAL;
	}
	__format->pixelformat = V4L2_PIX_FMT_YUV420;
	return 0;
}

static void _video_fill_format(const struct camera_mode *__mode, u32 __profile, struct v4l2_format *__format)
{
	memset(&__format->fmt.pix, 0, sizeof(__format->fmt.pix));
	__format->fmt.pix.width = __mode->width;
	__format->fmt.pix.height = __mode->height;
	__format->fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
	__format->fmt.pix.field = V4L2_FIELD_NONE;
	__format->fmt.pix.bytesperline = __mode->width;
	__format->fmt.pix.sizeimage = __mode->width * __mode->height * 3 / 2;
	if (__profile == CAMERA_COLOR_BT601_625_FULL)
	{
		/* Android JFIF: BT.601-625 primaries, full range, SMPTE 170M transfer.
		 * SMPTE 170M and Rec.709 use the same transfer function. */
		__format->fmt.pix.colorspace = V4L2_COLORSPACE_470_SYSTEM_BG;
		__format->fmt.pix.ycbcr_enc = V4L2_YCBCR_ENC_601;
		__format->fmt.pix.quantization = V4L2_QUANTIZATION_FULL_RANGE;
	}
	else
	{
		__format->fmt.pix.colorspace = V4L2_COLORSPACE_REC709;
		__format->fmt.pix.ycbcr_enc = V4L2_YCBCR_ENC_709;
		__format->fmt.pix.quantization = V4L2_QUANTIZATION_LIM_RANGE;
	}
	__format->fmt.pix.xfer_func = V4L2_XFER_FUNC_709;
}

static u32 _video_nearest(camera_t *__camera, u32 __width, u32 __height)
{
	u32 _selected = 0;
	u64 _distance = U64_MAX;
	for (u32 _i = 0; _i < __camera->description.mode_count; ++_i)
	{
		struct camera_mode *_mode = &__camera->description.modes[_i];
		s64 _dx = (s64)_mode->width - __width;
		s64 _dy = (s64)_mode->height - __height;
		u64 _next = _dx * _dx + _dy * _dy;
		if (_next < _distance)
		{
			_distance = _next;
			_selected = _i;
		}
	}
	return _selected;
}

static int _video_get_format(struct file *__file, void *__context, struct v4l2_format *__format)
{
	camera_t *_camera = video_drvdata(__file);
	_video_fill_format(&_camera->mode, _camera->description.color_profile, __format);
	return 0;
}

static int _video_try_format(struct file *__file, void *__context, struct v4l2_format *__format)
{
	camera_t *_camera = video_drvdata(__file);
	u32 _selected = _video_nearest(_camera, min(__format->fmt.pix.width, 8192U), min(__format->fmt.pix.height, 8192U));
	_video_fill_format(&_camera->description.modes[_selected], _camera->description.color_profile, __format);
	return 0;
}

static int _video_set_format(struct file *__file, void *__context, struct v4l2_format *__format)
{
	camera_t *_camera = video_drvdata(__file);
	if (vb2_is_busy(&_camera->queue) || _camera->stopping)
	{
		return -EBUSY;
	}
	_video_try_format(__file, __context, __format);
	_camera->mode = _camera->description.modes[_video_nearest(_camera, __format->fmt.pix.width, __format->fmt.pix.height)];
	return 0;
}

static int _video_enum_size(struct file *__file, void *__context, struct v4l2_frmsizeenum *__size)
{
	camera_t *_camera = video_drvdata(__file);
	if (__size->pixel_format != V4L2_PIX_FMT_YUV420)
	{
		return -EINVAL;
	}
	u32 _ordinal = 0;
	for (u32 _i = 0; _i < _camera->description.mode_count; ++_i)
	{
		struct camera_mode *_mode = &_camera->description.modes[_i];
		bool _duplicate = false;
		for (u32 _j = 0; _j < _i; ++_j)
		{
			struct camera_mode *_prior = &_camera->description.modes[_j];
			if (_prior->width == _mode->width && _prior->height == _mode->height)
			{
				_duplicate = true;
			}
		}
		if (!_duplicate && _ordinal++ == __size->index)
		{
			__size->type = V4L2_FRMSIZE_TYPE_DISCRETE;
			__size->discrete.width = _mode->width;
			__size->discrete.height = _mode->height;
			return 0;
		}
	}
	return -EINVAL;
}

static int _video_enum_interval(struct file *__file, void *__context, struct v4l2_frmivalenum *__interval)
{
	camera_t *_camera = video_drvdata(__file);
	if (__interval->pixel_format != V4L2_PIX_FMT_YUV420)
	{
		return -EINVAL;
	}
	u32 _ordinal = 0;
	for (u32 _i = 0; _i < _camera->description.mode_count; ++_i)
	{
		struct camera_mode *_mode = &_camera->description.modes[_i];
		if (_mode->width == __interval->width && _mode->height == __interval->height && _ordinal++ == __interval->index)
		{
			__interval->type = V4L2_FRMIVAL_TYPE_DISCRETE;
			__interval->discrete.numerator = 1;
			__interval->discrete.denominator = _mode->fps;
			return 0;
		}
	}
	return -EINVAL;
}

static int _video_get_parm(struct file *__file, void *__context, struct v4l2_streamparm *__parm)
{
	camera_t *_camera = video_drvdata(__file);
	if (__parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
	{
		return -EINVAL;
	}
	memset(&__parm->parm.capture, 0, sizeof(__parm->parm.capture));
	__parm->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	__parm->parm.capture.timeperframe.numerator = 1;
	__parm->parm.capture.timeperframe.denominator = _camera->mode.fps;
	return 0;
}

static int _video_set_parm(struct file *__file, void *__context, struct v4l2_streamparm *__parm)
{
	camera_t *_camera = video_drvdata(__file);
	if (__parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
	{
		return -EINVAL;
	}
	if (vb2_is_streaming(&_camera->queue) || _camera->stopping)
	{
		return -EBUSY;
	}
	u32 _numerator = __parm->parm.capture.timeperframe.numerator;
	u32 _fps = 30;
	if (_numerator)
	{
		_fps = __parm->parm.capture.timeperframe.denominator / _numerator;
	}
	u64 _distance = U64_MAX;
	for (u32 _i = 0; _i < _camera->description.mode_count; ++_i)
	{
		struct camera_mode *_mode = &_camera->description.modes[_i];
		if (_mode->width == _camera->mode.width && _mode->height == _camera->mode.height)
		{
			u64 _delta = abs((s64)_mode->fps - _fps);
			if (_delta < _distance)
			{
				_distance = _delta;
				_camera->mode.fps = _mode->fps;
			}
		}
	}
	return _video_get_parm(__file, __context, __parm);
}

static int _video_enum_input(struct file *__file, void *__context, struct v4l2_input *__input)
{
	if (__input->index)
	{
		return -EINVAL;
	}
	strscpy(__input->name, "Android Camera", sizeof(__input->name));
	__input->type = V4L2_INPUT_TYPE_CAMERA;
	return 0;
}

static int _video_get_input(struct file *__file, void *__context, unsigned int *__input)
{
	*__input = 0;
	return 0;
}

static int _video_set_input(struct file *__file, void *__context, unsigned int __input)
{
	if (__input)
	{
		return -EINVAL;
	}
	return 0;
}

const struct v4l2_ioctl_ops video_ioctl_ops = {
	.vidioc_querycap = _video_querycap,
	.vidioc_enum_fmt_vid_cap = _video_enum_format,
	.vidioc_g_fmt_vid_cap = _video_get_format,
	.vidioc_s_fmt_vid_cap = _video_set_format,
	.vidioc_try_fmt_vid_cap = _video_try_format,
	.vidioc_enum_framesizes = _video_enum_size,
	.vidioc_enum_frameintervals = _video_enum_interval,
	.vidioc_g_parm = _video_get_parm,
	.vidioc_s_parm = _video_set_parm,
	.vidioc_enum_input = _video_enum_input,
	.vidioc_g_input = _video_get_input,
	.vidioc_s_input = _video_set_input,
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
};
