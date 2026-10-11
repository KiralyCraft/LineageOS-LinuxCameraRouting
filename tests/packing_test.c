#include "../include/packing.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void)
{
	uint8_t _luma[8 * 6];
	uint8_t _chroma[8 * 3];
	uint8_t _destination[64];
	for (uint32_t _y = 0; _y < 6; ++_y)
	{
		for (uint32_t _x = 0; _x < 8; ++_x)
		{
			_luma[_y * 8 + _x] = _y * 10 + _x;
		}
	}
	for (uint32_t _y = 0; _y < 3; ++_y)
	{
		for (uint32_t _x = 0; _x < 4; ++_x)
		{
			_chroma[_y * 8 + _x * 2] = 80 + _y * 10 + _x;
			_chroma[_y * 8 + _x * 2 + 1] = 160 + _y * 10 + _x;
		}
	}
	packing_t _planes[3] = {
		{_luma, sizeof(_luma), 8, 1},
		{_chroma, sizeof(_chroma) - 1, 8, 2},
		{_chroma + 1, sizeof(_chroma) - 1, 8, 2},
	};
	memset(_destination, 0xa5, sizeof(_destination));
	assert(packing_i420(_destination, sizeof(_destination), _planes, 2, 2, 4, 4) == 0);
	for (uint32_t _y = 0; _y < 4; ++_y)
	{
		for (uint32_t _x = 0; _x < 4; ++_x)
		{
			assert(_destination[_y * 4 + _x] == (_y + 2) * 10 + _x + 2);
		}
	}
	assert(_destination[16] == 91 && _destination[19] == 102);
	assert(_destination[20] == 171 && _destination[23] == 182);
	assert(_destination[24] == 0xa5);
	_planes[2].length = 1;
	memset(_destination, 0xa5, sizeof(_destination));
	assert(packing_i420(_destination, sizeof(_destination), _planes, 2, 2, 4, 4) == -1);
	for (uint32_t _i = 0; _i < sizeof(_destination); ++_i)
	{
		assert(_destination[_i] == 0xa5);
	}
	assert(packing_i420(_destination, 2, _planes, 0, 0, 4, 4) == -1);
	assert(packing_i420(_destination, sizeof(_destination), _planes, 1, 0, 4, 4) == -1);
	puts("packing: patterned crop, chroma alias, padding, bounds, and atomic rejection PASS");
	return 0;
}
