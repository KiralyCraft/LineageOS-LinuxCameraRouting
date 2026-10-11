#define _GNU_SOURCE
#include "../include/packing.h"
#include "../include/metrics.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static uint8_t *_packing_equivalence_guard(size_t __size, void **__mapping, size_t *__length)
{
	size_t _page = sysconf(_SC_PAGESIZE);
	*__length = ((__size + _page - 1) / _page + 2) * _page;
	*__mapping = mmap(NULL, *__length, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	assert(*__mapping != MAP_FAILED);
	assert(mprotect((uint8_t *)*__mapping + _page, *__length - 2 * _page, PROT_READ | PROT_WRITE) == 0);
	return (uint8_t *)*__mapping + *__length - _page - __size;
}

static void _packing_equivalence_case(uint32_t __width, uint32_t __height, uint32_t __crop, uint32_t __padding, uint32_t __layout)
{
	uint32_t _fullWidth = __width + __crop;
	uint32_t _fullHeight = __height + __crop;
	uint32_t _lumaStride = _fullWidth + __padding;
	uint32_t _pixel = 1;
	if (__layout == 1 || __layout == 2)
	{
		_pixel = 2;
	}
	else if (__layout == 3)
	{
		_pixel = 3;
	}
	uint32_t _chromaStride = _fullWidth / 2 * _pixel + __padding;
	size_t _lumaSize = (size_t)(_fullHeight - 1) * _lumaStride + _fullWidth;
	size_t _chromaSize = (size_t)(_fullHeight / 2 - 1) * _chromaStride + (size_t)(_fullWidth / 2 - 1) * _pixel + 1;
	if (_pixel == 2)
	{
		_chromaSize++;
	}
	void *_maps[4];
	size_t _lengths[4];
	uint8_t *_luma = _packing_equivalence_guard(_lumaSize, &_maps[0], &_lengths[0]);
	uint8_t *_chroma = _packing_equivalence_guard(_chromaSize, &_maps[1], &_lengths[1]);
	uint8_t *_separate = _packing_equivalence_guard(_chromaSize, &_maps[2], &_lengths[2]);
	size_t _outputSize = (size_t)__width * __height * 3 / 2;
	uint8_t *_output = _packing_equivalence_guard(_outputSize, &_maps[3], &_lengths[3]);
	uint8_t *_reference = malloc(_outputSize);
	assert(_reference);
	for (size_t _i = 0; _i < _lumaSize; ++_i)
	{
		_luma[_i] = (_i * 17 + _i / 19 + 7) & 255;
	}
	for (size_t _i = 0; _i < _chromaSize; ++_i)
	{
		_chroma[_i] = (_i * 29 + _i / 13 + 53) & 255;
		_separate[_i] = (_i * 37 + _i / 7 + 101) & 255;
	}
	packing_t _planes[3] = {{_luma, _lumaSize, _lumaStride, 1}, {_chroma, _chromaSize, _chromaStride, _pixel}, {_separate, _chromaSize, _chromaStride, _pixel}};
	if (_pixel == 2)
	{
		_planes[1].length = _chromaSize - 1;
		_planes[2].length = _chromaSize - 1;
		if (__layout == 1)
		{
			_planes[2].data = _chroma + 1;
		}
		else
		{
			_planes[1].data = _chroma + 1;
			_planes[2].data = _chroma;
		}
	}
	memset(_output, 0xa5, _outputSize);
	assert(packing_i420_reference(_reference, _outputSize, _planes, __crop, __crop, __width, __height) == 0);
	assert(packing_i420(_output, _outputSize, _planes, __crop, __crop, __width, __height) == 0);
	assert(memcmp(_output, _reference, _outputSize) == 0);
	/* Independent sample-coordinate expectation, not only fast/reference agreement. */
	size_t _offset = 0;
	for (uint32_t _plane = 0; _plane < 3; ++_plane)
	{
		uint32_t _scale = 1;
		if (_plane)
		{
			_scale = 2;
		}
		for (uint32_t _y = 0; _y < __height / _scale; ++_y)
		{
			for (uint32_t _x = 0; _x < __width / _scale; ++_x)
			{
				assert(_output[_offset++] == _planes[_plane].data[(size_t)(_y + __crop / _scale) * _planes[_plane].row_stride + (size_t)(_x + __crop / _scale) * _planes[_plane].pixel_stride]);
			}
		}
	}
	/* Invalid last plane must reject before even the luma destination is touched. */
	_planes[2].length = 1;
	memset(_output, 0x5a, _outputSize);
	assert(packing_i420(_output, _outputSize, _planes, __crop, __crop, __width, __height) == -1);
	for (size_t _i = 0; _i < _outputSize; ++_i)
	{
		assert(_output[_i] == 0x5a);
	}
	free(_reference);
	for (uint32_t _i = 0; _i < 4; ++_i)
	{
		munmap(_maps[_i], _lengths[_i]);
	}
}

int main(void)
{
	const uint32_t _widths[] = {2, 14, 30, 32, 34, 62, 64, 66, 126, 128, 130, 1280, 1920, 3840};
	uint32_t _cases = 0;
	for (size_t _w = 0; _w < sizeof(_widths) / sizeof(_widths[0]); ++_w)
	{
		for (uint32_t _layout = 0; _layout < 4; ++_layout)
		{
			for (uint32_t _crop = 0; _crop <= 2; _crop += 2)
			{
				for (uint32_t _padding = 0; _padding <= 7; _padding += 7)
				{
					_packing_equivalence_case(_widths[_w], 6, _crop, _padding, _layout);
					_cases++;
				}
			}
		}
	}
	metrics_t _metrics = {0};
	assert(metrics_p95_upper_ns(&_metrics) == 0);
	for (uint32_t _i = 0; _i < 95; ++_i)
	{
		metrics_record(&_metrics, 1700);
	}
	for (uint32_t _i = 0; _i < 5; ++_i)
	{
		metrics_record(&_metrics, 9000);
	}
	assert(_metrics.count == 100 && _metrics.maximum_ns == 9000 && metrics_p95_upper_ns(&_metrics) == 2000);
	printf("PASS %u guarded packing cases: NV12/NV21, planar, generic stride, crops, tails, independent exact pixels; timing histogram\n", _cases);
	return 0;
}
