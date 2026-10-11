#define _GNU_SOURCE
#include "../include/packing.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static uint64_t _packing_benchmark_time(clockid_t __clock)
{
	struct timespec _time;
	clock_gettime(__clock, &_time);
	return (uint64_t)_time.tv_sec * 1000000000ULL + _time.tv_nsec;
}

int main(void)
{
	const uint32_t _sizes[][2] = {{1280, 720}, {1920, 1080}, {3840, 2160}};
	for (uint32_t _s = 0; _s < 3; ++_s)
	{
		uint32_t _width = _sizes[_s][0];
		uint32_t _height = _sizes[_s][1];
		size_t _lumaSize = (size_t)_width * _height;
		size_t _size = _lumaSize * 3 / 2;
		uint8_t *_sources = malloc(_size * 4);
		uint8_t *_destinations = malloc(_size * 4);
		if (!_sources || !_destinations)
		{
			return 1;
		}
		for (size_t _i = 0; _i < _size * 4; ++_i)
		{
			_sources[_i] = (_i * 17 + _i / 13) & 255;
		}
		memset(_destinations, 0, _size * 4);
		uint64_t _wall[2] = {0};
		uint64_t _cpu[2] = {0};
		for (uint32_t _iteration = 0; _iteration < 68; ++_iteration)
		{
			uint8_t *_source = _sources + (_iteration % 4) * _size;
			uint8_t *_destination = _destinations + (_iteration % 4) * _size;
			packing_t _planes[3] = {{_source, _lumaSize, _width, 1}, {_source + _lumaSize, _lumaSize / 2 - 1, _width, 2}, {_source + _lumaSize + 1, _lumaSize / 2 - 1, _width, 2}};
			for (uint32_t _order = 0; _order < 2; ++_order)
			{
				uint32_t _mode = (_iteration + _order) % 2;
				uint64_t _start = _packing_benchmark_time(CLOCK_MONOTONIC);
				uint64_t _cpuStart = _packing_benchmark_time(CLOCK_PROCESS_CPUTIME_ID);
				int32_t _result;
				if (_mode)
				{
					_result = packing_i420(_destination, _size, _planes, 0, 0, _width, _height);
				}
				else
				{
					_result = packing_i420_reference(_destination, _size, _planes, 0, 0, _width, _height);
				}
				uint64_t _cpuEnd = _packing_benchmark_time(CLOCK_PROCESS_CPUTIME_ID);
				uint64_t _end = _packing_benchmark_time(CLOCK_MONOTONIC);
				if (_result)
				{
					return 1;
				}
				if (_iteration >= 4)
				{
					_wall[_mode] += _end - _start;
					_cpu[_mode] += _cpuEnd - _cpuStart;
				}
			}
		}
		printf("{\"width\":%u,\"height\":%u,\"samples_each\":64,\"reference_ms\":%.4f,\"fast_ms\":%.4f,\"reference_cpu_ms\":%.4f,\"fast_cpu_ms\":%.4f}\n", _width, _height, _wall[0] / 64e6, _wall[1] / 64e6, _cpu[0] / 64e6, _cpu[1] / 64e6);
		free(_sources);
		free(_destinations);
	}
	return 0;
}
