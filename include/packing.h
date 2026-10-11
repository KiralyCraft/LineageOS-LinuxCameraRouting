#ifndef PACKING_H
#define PACKING_H
#include <stdint.h>
#include <stddef.h>

typedef struct
{
	const uint8_t *data;
	size_t length;
	uint32_t row_stride;
	uint32_t pixel_stride;
} packing_t;

int32_t packing_i420(uint8_t *__destination, size_t __capacity, const packing_t __planes[3], uint32_t __left, uint32_t __top, uint32_t __width, uint32_t __height);
/* Retained implementation for exact-output checks and live A/B measurement. */
int32_t packing_i420_reference(uint8_t *__destination, size_t __capacity, const packing_t __planes[3], uint32_t __left, uint32_t __top, uint32_t __width, uint32_t __height);
#endif
