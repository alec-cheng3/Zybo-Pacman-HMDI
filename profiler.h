#ifndef PROFILER_H
#define PROFILER_H

#include "xtime_l.h"

#define PROFILER_US_PER_COUNT (1000000.0 / COUNTS_PER_SECOND)

typedef struct {
	XTime start;
	XTime end;
	XTime time_diff;
	u32 elapsed_us;
} profiler_s;

void profiler_start(profiler_s*);
void profiler_end(profiler_s*);

#endif //PROFILER_H
