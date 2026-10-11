#include "include/packing.h"
#include <string.h>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

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

static int32_t _packing_check(uint8_t *__destination, size_t __capacity, const packing_t __planes[3], uint32_t __left, uint32_t __top, uint32_t __width, uint32_t __height)
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
	return 0;
}

int32_t packing_i420_reference(uint8_t *__destination, size_t __capacity, const packing_t __planes[3], uint32_t __left, uint32_t __top, uint32_t __width, uint32_t __height)
{
	if (_packing_check(__destination, __capacity, __planes, __left, __top, __width, __height))
	{
		return -1;
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

static void _packing_plane(uint8_t *__destination, const packing_t *__plane, uint32_t __left, uint32_t __top, uint32_t __width, uint32_t __height)
{
	const uint8_t *_row = __plane->data + (size_t)__top * __plane->row_stride + (size_t)__left * __plane->pixel_stride;
	if (__plane->pixel_stride == 1 && __plane->row_stride == __width)
	{
		memcpy(__destination, _row, (size_t)__width * __height);
		return;
	}
	for (uint32_t _y = 0; _y < __height; ++_y)
	{
		if (__plane->pixel_stride == 1)
		{
			memcpy(__destination, _row, __width);
		}
		else
		{
			for (uint32_t _x = 0; _x < __width; ++_x)
			{
				__destination[_x] = _row[(size_t)_x * __plane->pixel_stride];
			}
		}
		__destination += __width;
		if (_y + 1 < __height)
		{
			_row += __plane->row_stride;
		}
	}
}

static void _packing_split(uint8_t *__first, uint8_t *__second, const uint8_t *__source, uint32_t __width)
{
	uint32_t _x = 0;
#if defined(__aarch64__)
	/* Exactly 16 complete pairs per load. The scalar tail never reads padding. */
	for (; _x + 16 <= __width; _x += 16)
	{
		uint8x16x2_t _pair = vld2q_u8(__source + (size_t)_x * 2);
		vst1q_u8(__first + _x, _pair.val[0]);
		vst1q_u8(__second + _x, _pair.val[1]);
	}
#endif
	for (; _x < __width; ++_x)
	{
		__first[_x] = __source[(size_t)_x * 2];
		__second[_x] = __source[(size_t)_x * 2 + 1];
	}
}

int32_t packing_i420(uint8_t *__destination, size_t __capacity, const packing_t __planes[3], uint32_t __left, uint32_t __top, uint32_t __width, uint32_t __height)
{
	if (_packing_check(__destination, __capacity, __planes, __left, __top, __width, __height))
	{
		return -1;
	}
	_packing_plane(__destination, &__planes[0], __left, __top, __width, __height);
	uint32_t _width = __width / 2;
	uint32_t _height = __height / 2;
	uint8_t *_u = __destination + (size_t)__width * __height;
	uint8_t *_v = _u + (size_t)_width * _height;
	uintptr_t _uAddress = (uintptr_t)__planes[1].data;
	uintptr_t _vAddress = (uintptr_t)__planes[2].data;
	if (__planes[1].pixel_stride == 2 && __planes[2].pixel_stride == 2 && __planes[1].row_stride == __planes[2].row_stride && __planes[1].row_stride >= __left + __width && (_uAddress + 1 == _vAddress || _vAddress + 1 == _uAddress))
	{
		const uint8_t *_source;
		uint8_t *_first;
		uint8_t *_second;
		if (_uAddress < _vAddress)
		{
			_source = __planes[1].data;
			_first = _u;
			_second = _v;
		}
		else
		{
			_source = __planes[2].data;
			_first = _v;
			_second = _u;
		}
		_source += (size_t)(__top / 2) * __planes[1].row_stride + __left;
		for (uint32_t _y = 0; _y < _height; ++_y)
		{
			_packing_split(_first, _second, _source, _width);
			_first += _width;
			_second += _width;
			if (_y + 1 < _height)
			{
				_source += __planes[1].row_stride;
			}
		}
	}
	else
	{
		_packing_plane(_u, &__planes[1], __left / 2, __top / 2, _width, _height);
		_packing_plane(_v, &__planes[2], __left / 2, __top / 2, _width, _height);
	}
	return 0;
}
