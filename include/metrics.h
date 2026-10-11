#ifndef METRICS_H
#define METRICS_H
#include <stdint.h>
#define METRICS_BUCKETS 24

typedef struct
{
	uint64_t count;
	uint64_t total_ns;
	uint64_t maximum_ns;
	uint64_t buckets[METRICS_BUCKETS];
} metrics_t;

void metrics_record(metrics_t *__metrics, uint64_t __duration);
uint64_t metrics_p95_upper_ns(const metrics_t *__metrics);
#endif
