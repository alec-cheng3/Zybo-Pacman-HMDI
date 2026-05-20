#include "profiler.h"

//stores the current cycle count from PMU into start
void profiler_start(profiler_s *profiler){
	XTime_GetTime(&(profiler->start));
}

//stores the current cycle count from PMU into end
//also calculates elapsed microseconds (elapsed_us)
void profiler_end(profiler_s *profiler){
	XTime_GetTime(&(profiler->end));
	profiler->time_diff = profiler->end - profiler->start;
	profiler->elapsed_us = (u32)(PROFILER_US_PER_COUNT * profiler->time_diff);
}
