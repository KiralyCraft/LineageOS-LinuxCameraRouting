#include "include/metrics.h"

void metrics_record(metrics_t *__metrics, uint64_t __duration)
{
	__metrics->count++;
	__metrics->total_ns += __duration;
	if (__duration > __metrics->maximum_ns)
	{
		__metrics->maximum_ns = __duration;
	}
	uint32_t _bucket = 0;
	while (_bucket + 1 < METRICS_BUCKETS && __duration > (1000ULL << _bucket))
	{
		_bucket++;
	}
	__metrics->buckets[_bucket]++;
}

uint64_t metrics_p95_upper_ns(const metrics_t *__metrics)
{
	if (!__metrics->count)
	{
		return 0;
	}
	uint64_t _threshold = __metrics->count - __metrics->count / 20;
	uint64_t _count = 0;
	for (uint32_t _bucket = 0; _bucket < METRICS_BUCKETS; ++_bucket)
	{
		_count += __metrics->buckets[_bucket];
		if (_count >= _threshold)
		{
			uint64_t _upper = 1000ULL << _bucket;
			if (_upper > __metrics->maximum_ns || _bucket + 1 == METRICS_BUCKETS)
			{
				return __metrics->maximum_ns;
			}
			return _upper;
		}
	}
	return __metrics->maximum_ns;
}
