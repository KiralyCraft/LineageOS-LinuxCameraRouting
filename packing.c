#include "include/packing.h"
#include <string.h>

static int32_t _packing_validate(const packing_t *__plane, uint32_t __left, uint32_t __top, uint32_t __width, uint32_t __height)
{
	if (!__plane->data || !__plane->row_stride || !__plane->pixel_stride || !__width || !__height)
	{
		return -1;
	}
	uint64_t _rowEnd = ((uint64_t)__left + __width - 1) * __plane->pixel_stride + 1;
	uint64_t _end = ((uint64_t)__top + __height - 1) * __plane->row_stride + _rowEnd;
	if (_rowEnd > __plane->row_stride || _end > __plane->length)
	{
		return -1;
	}
	return 0;
}

int32_t packing_i420(uint8_t *__destination, size_t __capacity, const packing_t __planes[3], uint32_t __left, uint32_t __top, uint32_t __width, uint32_t __height)
{
	if (!__destination || !__planes || !__width || !__height || __width > 3840 || __height > 2160 || ((__left | __top | __width | __height) & 1) || __capacity < (uint64_t)__width * __height * 3 / 2)
	{
		return -1;
	}
	/* Validate every plane before touching the destination, including a last
	 * row with no trailing padding and aliased interleaved U/V sources. */
	for (uint32_t _plane = 0; _plane < 3; ++_plane)
	{
		uint32_t _scale = 1;
		if (_plane)
		{
			_scale = 2;
		}
		if (_packing_validate(&__planes[_plane], __left / _scale, __top / _scale, __width / _scale, __height / _scale))
		{
			return -1;
		}
	}
	for (uint32_t _plane = 0; _plane < 3; ++_plane)
	{
		uint32_t _scale = 1;
		if (_plane)
		{
			_scale = 2;
		}
		uint32_t _width = __width / _scale;
		uint32_t _height = __height / _scale;
		const packing_t *_source = &__planes[_plane];
		const uint8_t *_row = _source->data + (size_t)(__top / _scale) * _source->row_stride + (size_t)(__left / _scale) * _source->pixel_stride;
		for (uint32_t _y = 0; _y < _height; ++_y)
		{
			if (_source->pixel_stride == 1)
			{
				memcpy(__destination, _row, _width);
			}
			else
			{
				for (uint32_t _x = 0; _x < _width; ++_x)
				{
					__destination[_x] = _row[(size_t)_x * _source->pixel_stride];
				}
			}
			__destination += _width;
			if (_y + 1 < _height)
			{
				_row += _source->row_stride;
			}
		}
	}
	return 0;
}
